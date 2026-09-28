// Terrain v2 kernels (built as a shared lib, called via ctypes from tv2.py): noise, priority flood, stream-power erosion with uplift + external inflow, MFD flow, thermal/talus, lighting bakes, preview heightfield raymarcher.
// Noise/flood/stream-power/thermal/bake code carried over from game/tools/terrain/kernels.c (v1 generator).
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NTHREADS 16

static const int DX8[8] = {1, 1, 0, -1, -1, -1, 0, 1};
static const int DY8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const float DL8[8] = {1.f, 1.41421356f, 1.f, 1.41421356f, 1.f, 1.41421356f, 1.f, 1.41421356f};

static inline int mirror_i(int i, int n) {
	int p = 2 * (n - 1);
	int m = i % p;
	if (m < 0) m += p;
	return m <= n - 1 ? m : p - m;
}

/* ------------------------------------------------------------------ */
/* thread helper                                                       */
/* ------------------------------------------------------------------ */
typedef void (*row_fn)(void *ctx, int y0, int y1);
typedef struct { row_fn fn; void *ctx; int y0, y1; } Job;
static void *job_run(void *p) { Job *j = (Job *)p; j->fn(j->ctx, j->y0, j->y1); return NULL; }
static void parallel_rows(row_fn fn, void *ctx, int rows) {
	pthread_t th[NTHREADS];
	Job jobs[NTHREADS];
	int per = (rows + NTHREADS - 1) / NTHREADS;
	for (int t = 0; t < NTHREADS; t++) {
		jobs[t].fn = fn; jobs[t].ctx = ctx;
		jobs[t].y0 = t * per; jobs[t].y1 = (t + 1) * per > rows ? rows : (t + 1) * per;
		pthread_create(&th[t], NULL, job_run, &jobs[t]);
	}
	for (int t = 0; t < NTHREADS; t++) pthread_join(th[t], NULL);
}

/* ------------------------------------------------------------------ */
/* gradient noise                                                      */
/* ------------------------------------------------------------------ */
static inline uint32_t hash2(int x, int y, uint32_t seed) {
	uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ seed * 0xcb1ab31fu;
	h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
	return h;
}
static inline float grad(int ix, int iy, float fx, float fy, uint32_t seed) {
	uint32_t h = hash2(ix, iy, seed);
	float a = (h & 0xffff) * (6.28318530718f / 65536.f);
	return cosf(a) * fx + sinf(a) * fy;
}
static inline float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
static float gnoise(float x, float y, uint32_t seed) {
	int ix = (int)floorf(x), iy = (int)floorf(y);
	float fx = x - ix, fy = y - iy;
	float u = fade(fx), v = fade(fy);
	float a = grad(ix, iy, fx, fy, seed), b = grad(ix + 1, iy, fx - 1, fy, seed);
	float c = grad(ix, iy + 1, fx, fy - 1, seed), d = grad(ix + 1, iy + 1, fx - 1, fy - 1, seed);
	return 1.41421356f * (a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v);
}

typedef struct {
	float *out; int w, h; double x0, z0, dx; double scale; int octaves; float lac, gain; uint32_t seed; int mode;
	float warp; double warp_scale;
} NoiseCtx;

// mode 0 = fBm in [-1,1]; mode 1 = ridged multifractal in [0,1]; mode 2 = billow in [0,1]
static float fbm_eval(NoiseCtx *c, double wx, double wz) {
	double x = wx / c->scale, y = wz / c->scale;
	if (c->warp > 0.f) {
		double qx = wx / c->warp_scale, qy = wz / c->warp_scale;
		float w1 = 0.f, w2 = 0.f, a = 1.f, f = 1.f, n = 0.f;
		for (int o = 0; o < 4; o++) {
			w1 += a * gnoise((float)(qx * f), (float)(qy * f), c->seed + 101 + o);
			w2 += a * gnoise((float)(qx * f + 5.2), (float)(qy * f + 1.3), c->seed + 211 + o);
			n += a; a *= 0.5f; f *= 2.0f;
		}
		x += c->warp * w1 / n; y += c->warp * w2 / n;
	}
	float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f, prev = 1.f;
	for (int o = 0; o < c->octaves; o++) {
		float n = gnoise((float)(x * freq), (float)(y * freq), c->seed + o * 7919);
		if (c->mode == 1) {
			float r = 1.f - fabsf(n);
			r = r * r;
			sum += r * amp * prev;
			prev = fminf(1.f, r * 1.8f);
		} else if (c->mode == 2) {
			sum += fabsf(n) * amp;
		} else {
			sum += n * amp;
		}
		norm += amp; amp *= c->gain; freq *= c->lac;
	}
	return sum / norm;
}
static void noise_rows(void *p, int y0, int y1) {
	NoiseCtx *c = (NoiseCtx *)p;
	for (int y = y0; y < y1; y++)
		for (int x = 0; x < c->w; x++)
			c->out[(long)y * c->w + x] = fbm_eval(c, c->x0 + x * c->dx, c->z0 + y * c->dx);
}
void fbm(float *out, int w, int h, double x0, double z0, double dx, double scale, int octaves, float lac, float gain,
         uint32_t seed, int mode, float warp, double warp_scale) {
	NoiseCtx c = {out, w, h, x0, z0, dx, scale, octaves, lac, gain, seed, mode, warp, warp_scale};
	parallel_rows(noise_rows, &c, h);
}

/* ------------------------------------------------------------------ */
/* min-heap                                                            */
/* ------------------------------------------------------------------ */
typedef struct { float k; int i; } HItem;
typedef struct { HItem *a; long n, cap; } Heap;
static void hpush(Heap *hp, float k, int i) {
	if (hp->n >= hp->cap) { hp->cap = hp->cap * 2 + 1024; hp->a = realloc(hp->a, hp->cap * sizeof(HItem)); }
	long c = hp->n++;
	while (c > 0) {
		long p = (c - 1) >> 1;
		if (hp->a[p].k <= k) break;
		hp->a[c] = hp->a[p]; c = p;
	}
	hp->a[c].k = k; hp->a[c].i = i;
}
static HItem hpop(Heap *hp) {
	HItem top = hp->a[0];
	HItem last = hp->a[--hp->n];
	long c = 0;
	for (;;) {
		long l = 2 * c + 1;
		if (l >= hp->n) break;
		long r = l + 1, m = (r < hp->n && hp->a[r].k < hp->a[l].k) ? r : l;
		if (hp->a[m].k >= last.k) break;
		hp->a[c] = hp->a[m]; c = m;
	}
	if (hp->n > 0) hp->a[c] = last;
	return top;
}

// Priority-flood + epsilon (Barnes 2014). outlet[i]!=0 marks fixed base-level cells. Fills h in place, writes ascending order.
static void priority_flood(float *h, const uint8_t *outlet, int w, int hh, int *order, uint8_t *closed, float eps) {
	long n = (long)w * hh;
	memset(closed, 0, n);
	Heap hp = {malloc(sizeof(HItem) * 1024 * 64), 0, 1024 * 64};
	for (long i = 0; i < n; i++)
		if (outlet[i]) { closed[i] = 1; hpush(&hp, h[i], (int)i); }
	long k = 0;
	while (hp.n > 0) {
		HItem c = hpop(&hp);
		order[k++] = c.i;
		int cx = c.i % w, cy = c.i / w;
		for (int d = 0; d < 8; d++) {
			int nx = cx + DX8[d], ny = cy + DY8[d];
			if (nx < 0 || ny < 0 || nx >= w || ny >= hh) continue;
			long ni = (long)ny * w + nx;
			if (closed[ni]) continue;
			closed[ni] = 1;
			float minh = h[c.i] + eps * DL8[d];
			if (h[ni] < minh) h[ni] = minh;
			hpush(&hp, h[ni], (int)ni);
		}
	}
	free(hp.a);
}

static uint32_t g_rec_seed = 0;
static float g_jitter = 1.2f;    // stochastic steepest descent: over many steps approximates multiple-flow-direction routing
static int g_nofill = 0;         // 1: route on a filled copy but never flatten the real surface (keeps overdeepened basins)
void set_flow(float jitter, int nofill) { g_jitter = jitter; g_nofill = nofill; }   // changes every stream-power step: randomised steepest descent breaks D8 straight-line artefacts
static void d8_receivers(const float *h, const uint8_t *outlet, int w, int hh, int *rec, float *rdist) {
	long n = (long)w * hh;
	for (long i = 0; i < n; i++) {
		rec[i] = (int)i; rdist[i] = 1.f;
		if (outlet[i]) continue;
		int x = i % w, y = i / w;
		float best = 0.f;
		for (int d = 0; d < 8; d++) {
			int nx = x + DX8[d], ny = y + DY8[d];
			if (nx < 0 || ny < 0 || nx >= w || ny >= hh) continue;
			long ni = (long)ny * w + nx;
			float jit = g_rec_seed ? 1.f + g_jitter * (hash2((int)i, d, g_rec_seed) & 0xffff) / 65535.f : 1.f;
			float s = (h[i] - h[ni]) / DL8[d] * jit;
			if (s > best) { best = s; rec[i] = (int)ni; rdist[i] = DL8[d]; }
		}
	}
}

typedef struct { float *h, *tmp; const float *talus; int w, hh; float cell; float rate; float *dep; } ThermCtx;
static void therm_rows(void *p, int y0, int y1) {
	ThermCtx *c = (ThermCtx *)p;
	int w = c->w, hh = c->hh;
	for (int y = y0; y < y1; y++)
		for (int x = 0; x < w; x++) {
			long i = (long)y * w + x;
			float hi = c->h[i];
			float out = 0.f, in = 0.f;
			// outflow of this cell
			float maxd = 0.f, sumd = 0.f;
			for (int d = 0; d < 8; d++) {
				int nx = x + DX8[d], ny = y + DY8[d];
				if (nx < 0 || ny < 0 || nx >= w || ny >= hh) continue;
				float dd = hi - c->h[(long)ny * w + nx] - c->talus[i] * DL8[d] * c->cell;
				if (dd > 0.f) { sumd += dd; if (dd > maxd) maxd = dd; }
			}
			if (sumd > 0.f) out = c->rate * maxd * 0.5f;
			// inflow from neighbours
			for (int d = 0; d < 8; d++) {
				int nx = x + DX8[d], ny = y + DY8[d];
				if (nx < 0 || ny < 0 || nx >= w || ny >= hh) continue;
				long j = (long)ny * w + nx;
				float hj = c->h[j];
				float dji = hj - hi - c->talus[j] * DL8[d] * c->cell;
				if (dji <= 0.f) continue;
				float jmax = 0.f, jsum = 0.f;
				for (int e = 0; e < 8; e++) {
					int mx = nx + DX8[e], my = ny + DY8[e];
					if (mx < 0 || my < 0 || mx >= w || my >= hh) continue;
					float de = hj - c->h[(long)my * w + mx] - c->talus[j] * DL8[e] * c->cell;
					if (de > 0.f) { jsum += de; if (de > jmax) jmax = de; }
				}
				if (jsum > 1e-6f) in += c->rate * jmax * 0.5f * fminf(dji / jsum, 1.f);
			}
			c->tmp[i] = hi - out + in;
			if (c->dep) c->dep[i] += in;
		}
}
// Thermal erosion: material slides down where the slope exceeds talus[i] (tan of the angle of repose). Race-free gather form.
void thermal(float *h, const float *talus, int w, int hh, float cell, int iters, float rate, float *dep) {
	float *tmp = malloc(sizeof(float) * (long)w * hh);
	ThermCtx c = {h, tmp, talus, w, hh, cell, rate, dep};
	for (int it = 0; it < iters; it++) {
		parallel_rows(therm_rows, &c, hh);
		memcpy(h, tmp, sizeof(float) * (long)w * hh);
	}
	free(tmp);
}

// Stream-power fluvial erosion with uplift (Braun & Willett 2013 implicit scheme, n = 1).
void stream_power(float *h, const float *uplift, const float *kmul, const uint8_t *outlet, const float *talus, int w, int hh,
                  float cell, int iters, float dt, float K, float m, int therm_iters, float *area_out) {
	long n = (long)w * hh;
	int *order = malloc(sizeof(int) * n), *rec = malloc(sizeof(int) * n);
	float *rd = malloc(sizeof(float) * n), *A = malloc(sizeof(float) * n);
	uint8_t *closed = malloc(n);
	float ca = cell * cell;
	for (int it = 0; it < iters; it++) {
		for (long i = 0; i < n; i++) if (!outlet[i]) h[i] += uplift[i] * dt;
		priority_flood(h, outlet, w, hh, order, closed, 1e-3f);
		d8_receivers(h, outlet, w, hh, rec, rd);
		for (long i = 0; i < n; i++) A[i] = ca;
		for (long k = n - 1; k >= 0; k--) { int i = order[k]; if (rec[i] != i) A[rec[i]] += A[i]; }
		for (long k = 0; k < n; k++) {
			int i = order[k];
			if (outlet[i] || rec[i] == i) continue;
			float F = K * kmul[i] * dt * powf(A[i], m) / (rd[i] * cell);
			float nh = (h[i] + F * h[rec[i]]) / (1.f + F);
			if (nh < h[rec[i]]) nh = h[rec[i]] + 1e-3f;
			h[i] = nh;
		}
		if (therm_iters > 0) thermal(h, talus, w, hh, cell, therm_iters, 0.5f, NULL);
	}
	if (area_out) memcpy(area_out, A, sizeof(float) * n);
	free(order); free(rec); free(rd); free(A); free(closed);
}

// D8 drainage area on a pit-filled copy (for glacial carving / masks).
void drainage_area(const float *h_in, const uint8_t *outlet, int w, int hh, float cell, float *A) {
	long n = (long)w * hh;
	float *h = malloc(sizeof(float) * n); memcpy(h, h_in, sizeof(float) * n);
	int *order = malloc(sizeof(int) * n), *rec = malloc(sizeof(int) * n);
	float *rd = malloc(sizeof(float) * n); uint8_t *closed = malloc(n);
	priority_flood(h, outlet, w, hh, order, closed, 1e-3f);
	d8_receivers(h, outlet, w, hh, rec, rd);
	for (long i = 0; i < n; i++) A[i] = cell * cell;
	for (long k = n - 1; k >= 0; k--) { int i = order[k]; if (rec[i] != i) A[rec[i]] += A[i]; }
	free(h); free(order); free(rec); free(rd); free(closed);
}

// Multiple-flow-direction accumulation (Freeman 1991) on a pit-filled copy — smooth wetness / river mask.
void mfd_area(const float *h_in, const uint8_t *outlet, int w, int hh, float cell, float p, float *A) {
	long n = (long)w * hh;
	float *h = malloc(sizeof(float) * n); memcpy(h, h_in, sizeof(float) * n);
	int *order = malloc(sizeof(int) * n); uint8_t *closed = malloc(n);
	priority_flood(h, outlet, w, hh, order, closed, 1e-3f);
	for (long i = 0; i < n; i++) A[i] = cell * cell;
	for (long k = n - 1; k >= 0; k--) {
		int i = order[k];
		if (outlet[i]) continue;
		int x = i % w, y = i / w;
		float wsum = 0.f, ws[8];
		for (int d = 0; d < 8; d++) {
			ws[d] = 0.f;
			int nx = x + DX8[d], ny = y + DY8[d];
			if (nx < 0 || ny < 0 || nx >= w || ny >= hh) continue;
			float s = (h[i] - h[(long)ny * w + nx]) / DL8[d];
			if (s > 0.f) { ws[d] = powf(s, p); wsum += ws[d]; }
		}
		if (wsum <= 0.f) continue;
		for (int d = 0; d < 8; d++)
			if (ws[d] > 0.f) A[(long)(y + DY8[d]) * w + x + DX8[d]] += A[i] * ws[d] / wsum;
	}
	free(h); free(order); free(closed);
}

/* ------------------------------------------------------------------ */
/* droplet hydraulic erosion (after Beyer 2015)                        */
/* ------------------------------------------------------------------ */
typedef struct {
	float *h; int w, hh; float cell; long drops; uint32_t seed; int radius;
	float inertia, capacity, min_slope, deposit, erode, evaporate, gravity; int max_steps;
	float *dep; const float *hard; int *bi; float *bw; int bn;
} DropCtx;
/* ------------------------------------------------------------------ */
/* lighting bakes on a mirrored heightfield                            */
/* ------------------------------------------------------------------ */
typedef struct { const float *h; int w, hh; float cell; float *out; int dirs; float maxdist; float sx, sz, selev, pen; } BakeCtx;

static inline float hs(const BakeCtx *c, float fx, float fy) {
	int x = (int)floorf(fx), y = (int)floorf(fy);
	float u = fx - x, v = fy - y;
	int x0 = mirror_i(x, c->w), x1 = mirror_i(x + 1, c->w), y0 = mirror_i(y, c->hh), y1 = mirror_i(y + 1, c->hh);
	float a = c->h[(long)y0 * c->w + x0], b = c->h[(long)y0 * c->w + x1];
	float d = c->h[(long)y1 * c->w + x0], e = c->h[(long)y1 * c->w + x1];
	return (a * (1 - u) + b * u) * (1 - v) + (d * (1 - u) + e * u) * v;
}
static void ao_rows(void *p, int y0, int y1) {
	BakeCtx *c = (BakeCtx *)p;
	for (int y = y0; y < y1; y++)
		for (int x = 0; x < c->w; x++) {
			float h0 = fmaxf(c->h[(long)y * c->w + x], 0.f) + 1.f;
			float vis = 0.f;
			for (int d = 0; d < c->dirs; d++) {
				float a = (d + 0.5f * ((x + y) & 1)) * 6.2831853f / c->dirs;
				float ddx = cosf(a), ddy = sinf(a);
				float best = 0.f, t = 1.f;
				while (t * c->cell < c->maxdist) {
					float hv = hs(c, x + ddx * t, y + ddy * t);
					float s = (hv - h0) / (t * c->cell);
					if (s > best) best = s;
					t = t < 4.f ? t + 1.f : t * 1.12f;
				}
				float sa = best / sqrtf(1.f + best * best); // sin of horizon elevation
				vis += 1.f - sa * sa;
			}
			c->out[(long)y * c->w + x] = vis / c->dirs;
		}
}
void bake_ao(const float *h, int w, int hh, float cell, int dirs, float maxdist, float *out) {
	BakeCtx c = {h, w, hh, cell, out, dirs, maxdist, 0, 0, 0, 0};
	parallel_rows(ao_rows, &c, hh);
}
static void sh_rows(void *p, int y0, int y1) {
	BakeCtx *c = (BakeCtx *)p;
	float tanel = tanf(c->selev);
	for (int y = y0; y < y1; y++)
		for (int x = 0; x < c->w; x++) {
			float h0 = fmaxf(c->h[(long)y * c->w + x], 0.f) + 0.5f;
			float best = -10.f, t = 1.f;
			while (t * c->cell < c->maxdist) {
				float hv = hs(c, x + c->sx * t, y + c->sz * t);
				float s = (hv - h0) / (t * c->cell);
				if (s > best) best = s;
				if (best > tanel + 1.f) break;
				t = t < 8.f ? t + 1.f : t * 1.04f;
			}
			float ang = atanf(best);
			float lit = (c->selev - ang) / c->pen + 0.5f;
			c->out[(long)y * c->w + x] = lit < 0.f ? 0.f : (lit > 1.f ? 1.f : lit);
		}
}
// Sun visibility: sx/sz = horizontal unit direction towards the sun in grid space, selev = sun elevation (rad), pen = penumbra (rad).
void bake_shadow(const float *h, int w, int hh, float cell, float sx, float sz, float selev, float pen, float maxdist, float *out) {
	BakeCtx c = {h, w, hh, cell, out, 0, maxdist, sx, sz, selev, pen};
	parallel_rows(sh_rows, &c, hh);
}

/* ------------------------------------------------------------------ */
/* v2 additions                                                        */
/* ------------------------------------------------------------------ */

static float g_amax = 1e30f;   // optional cap on drainage area in the erosion law (fine passes: gullies only)
void set_amax(float a) { g_amax = a; }

// Stream power (n = 1, implicit) with per-cell uplift (m per step), erodibility multiplier, optional external inflow area
// (m^2 entering at a cell from outside the grid, e.g. rivers crossing the border of a refined region), hillslope talus limit.
void stream_power2(float *h, const float *uplift, const float *kmul, const uint8_t *outlet, const float *talus, const float *ext,
                   int w, int hh, float cell, int iters, float dt, float K, float m, int therm_iters, float therm_rate, float *area_out) {
	long n = (long)w * hh;
	int *order = malloc(sizeof(int) * n), *rec = malloc(sizeof(int) * n);
	float *rd = malloc(sizeof(float) * n), *A = malloc(sizeof(float) * n);
	uint8_t *closed = malloc(n);
	float ca = cell * cell;
	float *fillb = g_nofill ? malloc(sizeof(float) * n) : NULL;
	for (int it = 0; it < iters; it++) {
		if (uplift) for (long i = 0; i < n; i++) if (!outlet[i]) h[i] += uplift[i] * dt;
		float *hr = h;
		if (g_nofill) { memcpy(fillb, h, sizeof(float) * n); hr = fillb; }
		priority_flood(hr, outlet, w, hh, order, closed, 1e-3f);
		g_rec_seed = 977u + (uint32_t)it * 7919u;
		d8_receivers(hr, outlet, w, hh, rec, rd);
		g_rec_seed = 0;
		for (long i = 0; i < n; i++) A[i] = ca + (ext ? ext[i] : 0.f);
		for (long k = n - 1; k >= 0; k--) { int i = order[k]; if (rec[i] != i) A[rec[i]] += A[i]; }
		for (long k = 0; k < n; k++) {
			int i = order[k];
			if (outlet[i] || rec[i] == i) continue;
			float F = K * (kmul ? kmul[i] : 1.f) * dt * powf(fminf(A[i], g_amax), m) / (rd[i] * cell);
			if (g_nofill && h[i] <= h[rec[i]]) continue;          // inside a depression: no incision, no filling
			float nh = (h[i] + F * h[rec[i]]) / (1.f + F);
			if (nh < h[rec[i]]) nh = h[rec[i]] + 1e-3f;
			h[i] = nh;
		}
		if (therm_iters > 0 && talus) thermal(h, talus, w, hh, cell, therm_iters, therm_rate, NULL);
	}
	if (fillb) free(fillb);
	if (area_out) memcpy(area_out, A, sizeof(float) * n);
	free(order); free(rec); free(rd); free(A); free(closed);
}

// D8 receivers + ascending order on a pit-filled copy (for flow-path tracing and accumulations in Python).
void receivers(const float *h_in, const uint8_t *outlet, int w, int hh, int *rec, int *order, float *filled) {
	long n = (long)w * hh;
	float *h = filled; memcpy(h, h_in, sizeof(float) * n);
	float *rd = malloc(sizeof(float) * n); uint8_t *closed = malloc(n);
	priority_flood(h, outlet, w, hh, order, closed, 1e-3f);
	d8_receivers(h, outlet, w, hh, rec, rd);
	free(rd); free(closed);
}

/* ------------------------------------------------------------------ */
/* preview raymarcher: two-level heightfield (global + refined core)   */
/* ------------------------------------------------------------------ */
typedef struct {
	const float *hg; int gw, gh; float gcell, gx0, gz0; const float *cg;
	const float *hc; int cw, ch; float ccell, cx0, cz0; const float *cc;
	float cam[3], fwd[3], rgt[3], up[3]; float tanfov; int W, H; float *out;
	float sun[3], fogL, haze[3], skyz[3], skyh[3], deep[3];
} RCtx;

static inline float bil(const float *a, int w, int h, float fx, float fy, int stride, int ch) {
	if (fx < 0) fx = 0; if (fy < 0) fy = 0; if (fx > w - 1.001f) fx = w - 1.001f; if (fy > h - 1.001f) fy = h - 1.001f;
	int x = (int)fx, y = (int)fy; float u = fx - x, v = fy - y;
	const float *p = a + ((long)y * w + x) * stride + ch;
	float a0 = p[0], a1 = p[stride], a2 = p[(long)w * stride], a3 = p[(long)w * stride + stride];
	return (a0 * (1 - u) + a1 * u) * (1 - v) + (a2 * (1 - u) + a3 * u) * v;
}
static inline int in_core(const RCtx *c, float x, float z) {
	if (!c->hc) return 0;
	float fx = (x - c->cx0) / c->ccell, fy = (z - c->cz0) / c->ccell;
	return fx > 1 && fy > 1 && fx < c->cw - 2 && fy < c->ch - 2;
}
static inline float hat(const RCtx *c, float x, float z, float *cell) {
	if (in_core(c, x, z)) { *cell = c->ccell; return bil(c->hc, c->cw, c->ch, (x - c->cx0) / c->ccell, (z - c->cz0) / c->ccell, 1, 0); }
	*cell = c->gcell;
	float fx = (x - c->gx0) / c->gcell, fy = (z - c->gz0) / c->gcell;
	if (fx < 0 || fy < 0 || fx > c->gw - 1 || fy > c->gh - 1) return -300.f;
	return bil(c->hg, c->gw, c->gh, fx, fy, 1, 0);
}
static inline void colat(const RCtx *c, float x, float z, float *rgb) {
	for (int k = 0; k < 3; k++) {
		if (in_core(c, x, z)) rgb[k] = bil(c->cc, c->cw, c->ch, (x - c->cx0) / c->ccell, (z - c->cz0) / c->ccell, 3, k);
		else rgb[k] = bil(c->cg, c->gw, c->gh, (x - c->gx0) / c->gcell, (z - c->gz0) / c->gcell, 3, k);
	}
}
static void sky(const RCtx *c, const float *d, float *rgb) {
	float t = d[1] < 0 ? 0 : d[1];
	float g = powf(1.f - t, 4.f);
	float sd = d[0] * c->sun[0] + d[1] * c->sun[1] + d[2] * c->sun[2];
	float glow = powf(fmaxf(sd, 0.f), 64.f) * 0.6f + powf(fmaxf(sd, 0.f), 6.f) * 0.15f;
	for (int k = 0; k < 3; k++) rgb[k] = c->skyz[k] * (1 - g) + c->skyh[k] * g + glow;
}
static void render_rows(void *p, int y0, int y1) {
	RCtx *c = (RCtx *)p;
	for (int py = y0; py < y1; py++)
		for (int px = 0; px < c->W; px++) {
			float sx = (2.f * (px + 0.5f) / c->W - 1.f) * c->tanfov * c->W / c->H, sy = (1.f - 2.f * (py + 0.5f) / c->H) * c->tanfov;
			float d[3]; float L = 0;
			for (int k = 0; k < 3; k++) { d[k] = c->fwd[k] + c->rgt[k] * sx + c->up[k] * sy; L += d[k] * d[k]; }
			L = sqrtf(L); for (int k = 0; k < 3; k++) d[k] /= L;
			float t = 1.f, hit = -1.f, cell, prevt = 0.f;
			float tw = d[1] < 0 ? -c->cam[1] / d[1] : 1e9f;
			while (t < 120000.f) {
				float x = c->cam[0] + d[0] * t, y = c->cam[1] + d[1] * t, z = c->cam[2] + d[2] * t;
				float hh = hat(c, x, z, &cell);
				if (y < hh) {
					float a = prevt, b = t;
					for (int it = 0; it < 10; it++) {
						float m = 0.5f * (a + b);
						float hm = hat(c, c->cam[0] + d[0] * m, c->cam[2] + d[2] * m, &cell);
						if (c->cam[1] + d[1] * m < hm) b = m; else a = m;
					}
					hit = 0.5f * (a + b); break;
				}
				if (y < -50.f && d[1] < 0) break;
				prevt = t;
				float st = (y - hh) * 0.45f;
				float mn = cell * 0.4f + t * 0.0008f;
				t += st > mn ? (st < 400.f ? st : 400.f) : mn;
			}
			float rgb[3];
			float tt;
			if (hit > 0 && hit < tw) {
				tt = hit;
				colat(c, c->cam[0] + d[0] * hit, c->cam[2] + d[2] * hit, rgb);
			} else if (tw < 1e8f) {
				tt = tw;
				float x = c->cam[0] + d[0] * tw, z = c->cam[2] + d[2] * tw;
				float bed = hat(c, x, z, &cell);
				float r[3] = {d[0], -d[1], d[2]}, s[3]; sky(c, r, s);
				float cosi = -d[1]; float fr = 0.02f + 0.98f * powf(1.f - cosi, 5.f);
				float sh = expf(fminf(bed, 0.f) / 6.f);
				float bc[3]; colat(c, x, z, bc);
				for (int k = 0; k < 3; k++) {
					float body = c->deep[k] * (1 - sh) + bc[k] * 0.6f * sh;
					rgb[k] = body * (1 - fr) + s[k] * fr;
				}
			} else {
				sky(c, d, rgb);
				tt = -1;
			}
			if (tt > 0) {
				float yavg = fmaxf(c->cam[1] + d[1] * tt * 0.5f, 0.f);
				float f = 1.f - expf(-tt / (c->fogL * expf(yavg / 2500.f)));
				for (int k = 0; k < 3; k++) rgb[k] = rgb[k] * (1 - f) + c->haze[k] * f;
			}
			float *o = c->out + ((long)py * c->W + px) * 3;
			o[0] = rgb[0]; o[1] = rgb[1]; o[2] = rgb[2];
		}
}
void render(const float *hg, int gw, int gh, float gcell, float gx0, float gz0, const float *cg,
            const float *hc, int cw, int ch, float ccell, float cx0, float cz0, const float *cc,
            const float *cam, const float *fwd, const float *rgt, const float *up, float tanfov, int W, int H,
            const float *sun, float fogL, const float *haze, const float *skyz, const float *skyh, const float *deep, float *out) {
	RCtx c;
	c.hg = hg; c.gw = gw; c.gh = gh; c.gcell = gcell; c.gx0 = gx0; c.gz0 = gz0; c.cg = cg;
	c.hc = hc; c.cw = cw; c.ch = ch; c.ccell = ccell; c.cx0 = cx0; c.cz0 = cz0; c.cc = cc;
	for (int k = 0; k < 3; k++) { c.cam[k] = cam[k]; c.fwd[k] = fwd[k]; c.rgt[k] = rgt[k]; c.up[k] = up[k]; c.sun[k] = sun[k];
		c.haze[k] = haze[k]; c.skyz[k] = skyz[k]; c.skyh[k] = skyh[k]; c.deep[k] = deep[k]; }
	c.tanfov = tanfov; c.W = W; c.H = H; c.out = out; c.fogL = fogL;
	parallel_rows(render_rows, &c, H);
}

typedef struct { float *out; const float *xs, *zs; long n; double scale; int octaves; float lac, gain; uint32_t seed; int mode; } PtsCtx;
static void pts_rows(void *p, int r0, int r1) {
	PtsCtx *c = (PtsCtx *)p;
	NoiseCtx nc = {NULL, 0, 0, 0, 0, 0, c->scale, c->octaves, c->lac, c->gain, c->seed, c->mode, 0.f, 1.0};
	long per = (c->n + NTHREADS - 1) / NTHREADS;
	for (int t = r0; t < r1; t++)
		for (long i = t * per; i < (t + 1) * per && i < c->n; i++)
			c->out[i] = fbm_eval(&nc, c->xs[i], c->zs[i]);
}
// fBm / ridged noise evaluated at arbitrary coordinates (e.g. canyon-aligned s/d space).
void fbm_points(float *out, const float *xs, const float *zs, long n, double scale, int octaves, float lac, float gain,
                uint32_t seed, int mode) {
	PtsCtx c = {out, xs, zs, n, scale, octaves, lac, gain, seed, mode};
	parallel_rows(pts_rows, &c, NTHREADS);
}

// Ice flux routed down D8 receivers (order = ascending elevation): a glacier keeps flowing while its accumulated mass balance is positive.
void ice_flux(const int *order, const int *rec, const float *Q, long n, float cell, float *out) {
	double *fl = calloc(n, sizeof(double));
	for (long k = n - 1; k >= 0; k--) {
		int i = order[k];
		// signed: ablation of cells below the ELA eats into the glacier they drain into -> the terminus sits where the
		// catchment-integrated mass balance reaches zero
		double qi = Q[i];
		if (fl[i] > 0 && qi < 0) {   // ablation along a glacier tongue acts over its full width, not one cell
			double H = 55.0 * pow(fl[i] / 2.5e5, 0.3); if (H < 55) H = 55; if (H > 650) H = 650;
			double wid = (2.0 * (2.4 * H + 60.0)) / cell; if (wid < 1) wid = 1;
			qi *= wid;
		}
		double q = fl[i] + qi;
		if (rec[i] != i && (q > 0 || fl[i] > 0)) fl[rec[i]] += q;
		out[i] = q > 0 ? (float)q : 0.f;
	}
	free(fl);
}
// Make a bed profile descend monotonically downstream along receivers (only inside mask): bed[i] >= bed[rec[i]] + step.
void monotone_bed(const int *order, const int *rec, const uint8_t *mask, float *bed, long n, float step) {
	for (long k = 0; k < n; k++) {
		int i = order[k], r = rec[i];
		if (!mask[i] || r == i || !mask[r]) continue;
		if (bed[i] < bed[r] + step) bed[i] = bed[r] + step;
	}
}
// Along-flow distance to the outlet (sea) for every cell, following D8 receivers (order ascending from the outlets).
void dist_downstream(const int *order, const int *rec, long n, int w, float cell, float *out) {
	for (long k = 0; k < n; k++) {
		int i = order[k], r = rec[i];
		if (r == i) { out[i] = 0.f; continue; }
		int dx = i % w - r % w, dy = i / w - r / w;
		out[i] = out[r] + cell * ((dx != 0 && dy != 0) ? 1.41421356f : 1.f);
	}
}
// Weighted downstream accumulation along D8 receivers with a per-cell carry factor (e.g. avalanche runout dies on flats).
void accum(const int *order, const int *rec, const float *src, const float *carry, long n, float *out) {
	for (long i = 0; i < n; i++) out[i] = src[i];
	for (long k = n - 1; k >= 0; k--) { int i = order[k]; if (rec[i] != i) out[rec[i]] += out[i] * carry[i]; }
}
