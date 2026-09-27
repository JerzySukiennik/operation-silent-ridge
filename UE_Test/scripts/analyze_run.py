# Summarises a HP run: UE CSV profile (fps, 1% low like the Godot autotest, GPU ms, hitches), runner RAM samples and nvidia-smi VRAM.
import csv, sys, os, glob, statistics as st
d = sys.argv[1]
out = {}
csvs = glob.glob(os.path.join(d, "*.csv")); csvs = [c for c in csvs if not c.endswith(("mem.csv", "nvsmi.csv"))]
if csvs:
    rows = list(csv.reader(open(csvs[0], encoding="utf-8", errors="ignore")))
    head = rows[0]; data = []
    col = {n: i for i, n in enumerate(head)}
    for r in rows[1:]:
        if len(r) < len(head):
            continue
        r = r[:len(head)]
        try:
            float(r[col["FrameTime"]])
        except ValueError:
            continue
        data.append(r)
    ft = [float(r[col["FrameTime"]]) for r in data]
    gpu = [float(r[col["GPUTime"]]) for r in data] if "GPUTime" in col else []
    t = 0; segs = []
    for f in ft:
        t += f / 1000.0; segs.append(t)
    def stats(lo, hi, name):
        idx = [i for i, s in enumerate(segs) if lo <= s < hi]
        if not idx: return
        f = sorted((ft[i] for i in idx), reverse=True); k = max(1, len(f) // 100)
        g = [gpu[i] for i in idx] if gpu else [0]
        out[name] = dict(frames=len(idx), avg_fps=round(len(idx) / (sum(ft[i] for i in idx) / 1000), 1), low1_fps=round(1000 / (sum(f[:k]) / k), 1),
                         min_fps=round(1000 / f[0], 1), p50_ms=round(st.median(ft[i] for i in idx), 2), p99_ms=round(f[k - 1], 2),
                         gpu_ms_avg=round(st.mean(g), 2), gpu_ms_p50=round(st.median(g), 2), hitches_50ms=sum(1 for x in f if x > 50), hitches_100ms=sum(1 for x in f if x > 100))
    stats(0, 1e9, "all")
    stats(0, 8, "spawn_0-8s")
    stats(8, 58, "canyon_8-58s")
    stats(58, 1e9, "overview_58s+")
    stats(5, 1e9, "after_5s")
    out["capture_s"] = round(segs[-1], 1)
    out["first_10_worst_ms"] = [round(x, 0) for x in sorted(ft[: int(len(ft) * 0.1) or 1], reverse=True)[:10]]
    for c in ["GameThreadTime", "RenderThreadTime", "RHIThreadTime"]:
        if c in col:
            out[c + "_p50"] = round(st.median(float(r[col[c]]) for r in data), 2)
    memcols = [c for c in head if "Memory" in c or "PhysicalUsed" in c]
    out["csv_memcols"] = {c: round(max(float(r[col[c]]) for r in data), 1) for c in memcols[:6]}
m = os.path.join(d, "mem.csv")
if os.path.exists(m):
    ws = [int(l.split(",")[1]) for l in open(m) if l.strip()]; pv = [int(l.split(",")[2]) for l in open(m) if l.strip()]
    out["ram_ws_peak_mb"] = max(ws); out["ram_ws_median_mb"] = int(st.median(ws)); out["ram_private_peak_mb"] = max(pv)
n = os.path.join(d, "nvsmi.csv")
if os.path.exists(n):
    rows = list(csv.reader(open(n)))[1:]
    used = [int(r[1].strip().split()[0]) for r in rows if len(r) > 2]
    out["vram_used_peak_mb"] = max(used); out["vram_used_median_mb"] = int(st.median(used))
    out["gpu_util_median"] = st.median(int(r[2].strip().split()[0]) for r in rows if len(r) > 2)
r = os.path.join(d, "runner.txt")
if os.path.exists(r):
    out["runner"] = [l.strip()[13:] for l in open(r) if "baseline" in l or "loadmap" in l or "peak" in l]
import json; print(json.dumps(out, indent=1))
