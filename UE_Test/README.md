# UE 5.7 feasibility test (2026-09-27)

Content-only UE 5.7.4 project `SilentRidgeUE` (not in git, ~600 MB) with the same 80x80 km heightfield as the Godot v1 (Nanite static-mesh tiles), Poly Haven materials, the F-35C, conifers, SkyAtmosphere + VolumetricCloud, Lumen, VSM, TSR, and a 65 s auto-playing canyon flythrough. Built on the Mac with editor Python (`scripts/ue_*.py`), cooked and packaged on the HP (`scripts/hp_package.sh`), measured with `scripts/hp_run_ue.sh` + `scripts/analyze_run.py`.

HP (RTX 3050 Laptop 4 GB, 8 GB RAM), 1080p, TSR 67 %, canyon segment, warm shader cache:

| preset | avg fps | 1% low | GPU ms | VRAM peak | notes |
|---|---|---|---|---|---|
| Medium | 53.2 | 47.2 | 18.3 | 2.9 GB | smooth |
| High | 43.6 | 20.3 | 22.0 | 3.4 GB | |
| Epic | 34.8 | 26.5 | 28.1 | 3.9 GB | on-screen "video memory exhausted (~200 MB over budget)" |
| Epic, textures/view distance/foliage at High | 37.4 | 33.7 | 26.3 | 3.85 GB | still over budget |
| Epic cold (first run, shader/PSO compile) | 8.9 | 6.3 | 111 | 3.8 GB | one-time |

Godot v1 for reference: 95 fps avg, VRAM 1.3 GB — but visibly much worse (no GI, no volumetric clouds, flat materials).

Finding: the texture streaming pool holds ~1000 MB while the scene only wants ~100 MB of streamed mips; lowering the pool at startup made it worse (thrash) — the over-budget memory is non-streaming/large test textures (terrain masks), to be fixed at asset level, together with baked indirect lighting (fixed time of day) replacing Lumen.
