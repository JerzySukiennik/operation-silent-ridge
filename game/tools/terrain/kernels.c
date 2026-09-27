// Performance-critical kernels for gen_terrain.py (noise, stream-power + droplet + thermal erosion, flow, AO/shadow bakes); built as a shared lib and called via ctypes.
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
			float s = (h[i] - h[ni]) / DL8[d];
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
				in += c->rate * jmax * 0.5f * dji / jsum;
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

static inline void hgrad(const float *h, int w, float px, float py, float *hv, float *gx, float *gy) {
	int x = (int)px, y = (int)py;
	float u = px - x, v = py - y;
	long i = (long)y * w + x;
	float a = h[i], b = h[i + 1], c = h[i + w], d = h[i + w + 1];
	*gx = (b - a) * (1 - v) + (d - c) * v;
	*gy = (c - a) * (1 - u) + (d - b) * u;
	*hv = a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
}
static void drop_rows(void *p, int t0, int t1) {
	DropCtx *c = (DropCtx *)p;
	int w = c->w, hh = c->hh;
	float *h = c->h;
	float inv = 1.f / c->cell; // heights handled in cell units
	for (int t = t0; t < t1; t++) {
		uint32_t rs = c->seed * 747796405u + t * 2891336453u + 1;
		long per = c->drops;
		for (long k = 0; k < per; k++) {
			rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
			float px = 2.f + (rs % 100000) / 100000.f * (w - 5);
			rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
			float py = 2.f + (rs % 100000) / 100000.f * (hh - 5);
			float dx = 0.f, dy = 0.f, speed = 1.f, water = 1.f, sed = 0.f;
			for (int s = 0; s < c->max_steps; s++) {
				int nx = (int)px, ny = (int)py;
				float ox = px - nx, oy = py - ny;
				float hv, gx, gy;
				hgrad(h, w, px, py, &hv, &gx, &gy);
				hv *= inv; gx *= inv; gy *= inv;
				dx = dx * c->inertia - gx * (1 - c->inertia);
				dy = dy * c->inertia - gy * (1 - c->inertia);
				float len = sqrtf(dx * dx + dy * dy);
				if (len < 1e-6f) break;
				dx /= len; dy /= len;
				px += dx; py += dy;
				if (px < 2 || py < 2 || px > w - 3 || py > hh - 3) break;
				float nh, ngx, ngy;
				hgrad(h, w, px, py, &nh, &ngx, &ngy);
				nh *= inv;
				if (nh * c->cell < 0.f) break; // reached the sea
				float dh = nh - hv;
				float cap = fmaxf(-dh * speed * water * c->capacity, c->min_slope);
				long i0 = (long)ny * w + nx;
				if (sed > cap || dh > 0) {
					float amt = dh > 0 ? fminf(dh, sed) : (sed - cap) * c->deposit;
					sed -= amt;
					float a = amt * c->cell;
					h[i0] += a * (1 - ox) * (1 - oy); h[i0 + 1] += a * ox * (1 - oy);
					h[i0 + w] += a * (1 - ox) * oy; h[i0 + w + 1] += a * ox * oy;
					if (c->dep) c->dep[i0] += a;
				} else {
					float hard = c->hard ? c->hard[i0] : 1.f;
					float amt = fminf((cap - sed) * c->erode / hard, -dh);
					for (int b = 0; b < c->bn; b++) {
						int bx = nx + c->bi[2 * b], by = ny + c->bi[2 * b + 1];
						if (bx < 0 || by < 0 || bx >= w || by >= hh) continue;
						long bi = (long)by * w + bx;
						float d = amt * c->bw[b];
						h[bi] -= d * c->cell;
						sed += d;
					}
				}
				speed = sqrtf(fmaxf(0.f, speed * speed + dh * c->gravity * -1.f));
				water *= (1 - c->evaporate);
			}
		}
	}
}
void droplets(float *h, int w, int hh, float cell, long drops, uint32_t seed, int radius, float inertia, float capacity,
              float min_slope, float deposit, float erode, float evaporate, float gravity, int max_steps, float *dep,
              const float *hard) {
	int maxb = (2 * radius + 1) * (2 * radius + 1);
	int *bi = malloc(sizeof(int) * 2 * maxb); float *bw = malloc(sizeof(float) * maxb);
	int bn = 0; float ws = 0.f;
	for (int y = -radius; y <= radius; y++)
		for (int x = -radius; x <= radius; x++) {
			float d = sqrtf((float)(x * x + y * y));
			if (d > radius) continue;
			bi[2 * bn] = x; bi[2 * bn + 1] = y; bw[bn] = 1.f - d / (radius + 0.5f); ws += bw[bn]; bn++;
		}
	for (int b = 0; b < bn; b++) bw[b] /= ws;
	DropCtx c = {h, w, hh, cell, drops, seed, radius, inertia, capacity, min_slope, deposit, erode, evaporate, gravity,
	             max_steps, dep, hard, bi, bw, bn};
	drop_rows(&c, 0, 1); // single-threaded on purpose: deterministic for a given seed
	free(bi); free(bw);
}

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
