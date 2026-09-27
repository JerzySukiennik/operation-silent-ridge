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

## Stage 1 — world in budget, Epic look (2026-09-27)

Lumen off for the static world; fixed-sun lighting baked offline (`scripts/bake_lighting.py`: 24-direction horizon march on the heightfield → cosine-weighted sky visibility + one-bounce terrain irradiance, 2048² `T_bake`) and applied in `M_Terrain` (AO + emissive bounce) and the fir materials (AO); SSR + sky-light reflections; no mesh distance fields; Nanite streaming pool 384 MB, texture pool 600 MB; PSO precaching; Epic @ 67 % TSR as first-launch defaults (`Config/DefaultGameUserSettings.ini`, `DefaultScalability.ini`). Pre-stage snapshot: `Niepotrzebne/snapshots/stage0_feasibility/`.

| Epic, 1080p, TSR 67 %, warm | avg fps (canyon) | 1% low | GPU ms | VRAM peak | RAM WS | over-budget msg |
|---|---|---|---|---|---|---|
| before (Lumen) | 34.8 | 26.5 | 28.1 | 3.93 GB | 3.19 GB | yes (~200 MB) |
| stage 1 (baked) | 47.8 | 43.4 | 20.5 | 2.26 GB | 2.06 GB | no |
| stage 1, first run after a re-cook (new PSOs) | 47.8 (all 48.3) | 34.6 (whole run) | 20.5 | 2.36 GB | 2.28 GB | no |

Packaged build on the HP desktop: `SilentRidge_UE_Epic\PLAY - Epic.cmd`; side-by-side: `Niepotrzebne/compare_stage1/`.
