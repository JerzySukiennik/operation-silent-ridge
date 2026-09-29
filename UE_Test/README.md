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

## Stage 2 — C++ project + flight (2026-09-27)

C++ module `Source/SilentRidgeUE`: 1:1 port of the Godot F-35C flight model (120 Hz), v1 gamepad layout via runtime Enhanced Input, chase camera with speed cues + motion blur, HMD HUD, procedural VFX, jet audio (same 12 recordings, credits in `game/assets/CREDITS.md`), crash/respawn, pause menu, `-autotest` canyon autopilot, cinematic colour grade (`-nograde` to disable). HP packaging compiles the Game target only (editor targets need the .NET Framework SDK, not installed on the HP). Desktop build: `SilentRidge_UE_Stage2\PLAY.cmd`.

Note: runs before stage 2 were DPI-unaware on the HP (125 % Windows scaling) and rendered at 1536x864, not 1080p; `bAllowHighDPIInGameMode=True` fixes it. Stage-2 numbers are true 1920x1080.

Epic, 1920x1080, TSR 67 %, warm, autotest canyon without screenshots: 43.5 fps avg, 1% low 37.7, GPU 22.5 ms, VRAM 2.4 GB, RAM 2.2 GB (cloud shadow map 1024 / 48 samples, TSR history 100 %).

### Flight numbers: Godot vs UE (`UnrealEditor-Cmd <uproject> -run=FlightNumbers`)

| test | Godot 4.6 (GDScript) | UE 5.7 (C++) |
|---|---|---|
| level trim 250 kt TAS, sea level | AoA 5.7°  thr 32% | AoA 5.7°  thr 32% |
| level trim 350 kt TAS, sea level | AoA 2.9°  thr 36% | AoA 2.9°  thr 36% |
| level trim 450 kt TAS, sea level | AoA 1.7°  thr 45% | AoA 1.7°  thr 45% |
| level trim M 0.6, 15000 ft | AoA 3.9°  thr 46% | AoA 3.9°  thr 46% |
| level trim M 0.8, 15000 ft | AoA 2.0°  thr 53% | AoA 2.0°  thr 53% |
| level trim M 0.8, 30000 ft | AoA 3.8°  thr 63% | AoA 3.8°  thr 63% |
| approach, gear down, 135 KTAS, 20 % fuel | AoA 12.1°  thr 45%  17888 kg | AoA 12.1°  thr 45%  17888 kg |
| sustained turn, M 0.8, 15,000 ft, max AB, 50 % fuel | 5.11 g  10.7°/s  (M 0.81) | 5.11 g  10.7°/s  (M 0.81) |
| full aft stick, M 0.8, 15,000 ft | max 7.37 g, 37°/s, AoA 21° | max 7.37 g, 37°/s, AoA 21° |
| full aft stick, M 0.9, sea level | max 7.52 g, 21°/s, AoA 8° | max 7.52 g, 21°/s, AoA 8° |
| full aft stick, 330 kt, sea level | max 7.01 g, 45°/s, AoA 50° | max 7.01 g, 45°/s, AoA 50° |
| full forward stick, M 0.8, 15,000 ft | min -3.01 g | min -3.01 g |
| roll rate, 400 kt, sea level, full stick | 200°/s, 360° in 1.88 s | 200°/s, 360° in 1.88 s |
| top speed, max AB, sea level | M 1.06  700 KCAS  702 KTAS | M 1.06  700 KCAS  702 KTAS |
| top speed, max AB, 40,000 ft | M 1.57  516 KCAS  900 KTAS | M 1.57  516 KCAS  900 KTAS |
| top speed, MIL, 328 ft | M 0.96  634 KCAS | M 0.96  634 KCAS |
| top speed, MIL, 35000 ft | M 1.02  357 KCAS | M 1.02  357 KCAS |
| accel M 0.8 → 1.2, 30,000 ft, max AB, 50 % fuel | 57 s | 57 s |
| accel 250 → 500 / 600 KCAS, sea level, max AB | 14.7 s / 21.0 s | 14.7 s / 21.0 s |
| max rate of climb, sea level (Ps at 1 g) | 48056 ft/min AB @ 560 kt, 24089 MIL | 48056 ft/min AB @ 560 kt, 24089 MIL |
| 7.5 g turn from 450 KCAS, sea level, MIL | 453 → 443 → 418 KCAS (0/3/6 s) | 453 → 443 → 418 KCAS (0/3/6 s) |
| zoom 600 KCAS → 150 KCAS, 60° climb, MIL | +22006 ft | +22006 ft |
| idle + full aft stick 40 s, 15,000 ft | AoA max 50.4°, min 76 KCAS | AoA max 50.4°, min 76 KCAS |
| vertical zoom to zero speed, idle, neutral | min 3 m/s → recovers at 198 kt, pitch -79° | min 3 m/s → recovers at 198 kt, pitch -79° |
| time to 90° bank, full stick, 350 kt | 0.54 s | 0.54 s |
| 5 g pull-up step, 400 kt: 63 % / 90 % / overshoot | 0.28 s / 0.50 s / +0.00 g | 0.28 s / 0.50 s / +0.00 g |
| full right pedal 3 s, 250 kt | β -6.5°, heading +10.3°, bank 0.4° | β -6.5°, heading +10.3°, bank 0.4° |
| throttle detent: MIL stop / re-press+hold → AB / LT | 1.00 / 1.39 / 0.55 | 1.00 / 1.39 / 0.55 |
| stick shaping: 10 % / 50 % pitch stick | 0.046 / 0.294 of full command | 0.046 / 0.294 of full command |
| determinism (two runs, 20 s random input) | identical | identical |
| fuel flow, sea level, MIL / max AB | 3.8 / 11.0 kg/s | 3.8 / 11.0 kg/s |
| full-fuel AB endurance at SL | 13.5 min | 13.5 min |

32/32 rows identical to display precision

## World v2 — procedural mountains, fjord and ocean (2026-09-28)

Jurek's verdict on the stage 1/2 map ("mega unrealistic and weird": smooth vertically striped canyon walls, a comb of parallel
coastal ridges, flat valley floors, smeared cliff textures, odd snow, flat water) led to a new offline generator,
`scripts/terrain_v2/` (Python + C kernels via ctypes, seeded, ~25 min end to end on the Mac). Pre-v2 content is gone from the
project (old tiles, masks, flythrough sequence); stage 2 stays playable from its packaged desktop build.

**Generator** (`gen_world.py` 80 x 80 km, `gen_core.py` 40 x 30 km playable core):
1. Tectonics at 1024^2 / 78 m: irregular coastline (warped, bays + headlands), offshore skerries, broad range uplift with
   NNW en-echelon structural grain, lithology (plutons, soft belts) as erodibility contrasts, a strike-slip fault zone.
2. Stream-power fluvial erosion with uplift (Braun & Willett implicit scheme, m = 0.5, n = 1) + talus limit, 320 steps to a
   quasi steady state, refined at 2048^2. The trunk canyon is an **antecedent river**: its longitudinal profile is a base-level
   boundary condition during these fluvial stages only (meandering 7 km / 3 km wavelengths); the valley walls, spurs and the
   dendritic tributary network are produced by the erosion model. Flow direction is stochastic per step (breaks D8 grid lines).
3. Glaciers at 4096^2 / 19.5 m: ice routed down the drainage with a signed mass balance (ELA ~1500 m, lower on north/lee faces)
   so termini sit where the catchment balance reaches zero; troughs carved to a parabolic U profile scaled with ice flux
   (tributaries shallower -> hanging valleys, heads -> cirques); the lower courses of big rivers are overdeepened below sea
   level with a mouth sill (fjords); landlocked below-sea pockets become lakes.
4. Post-glacial fluvial incision (gullies only, trunk rivers capped so they cannot saw a slot; no flood-fill flattening),
   1-cell de-hatching, rocky detail on islands/coastal hills.
5. Core at 4.88 m: dipping strata with random hard beds that pinch out (irregular cliff bands, not contour lines), two joint
   sets, warped ridged meso relief (buttresses/couloirs 60-400 m) on steep ground, crest roughness, full-res gully erosion,
   talus with per-bed repose angles (soil slopes smooth up to ~40 deg, hard rock to ~70 deg, scree cones), floodplains with
   shallow channels, colluvial aprons and hummocky floors.
6. Canyon check (`export_layout.py`): 40.4 km route = 9.8 km fjord + 30.6 km canyon, floor width at +15 m median 135 m,
   width at 70 m AGL ~540 m, walls median ~640 m (p90 ~910 m), centreline min turn radius 860 m; carrier 47.7 km from the target.

**Engine side** (`ue_10_world_v2.py`, `hlsl/terrain_v2.hlsl`, `hlsl/ocean_*.hlsl`):
- Kept **Nanite static-mesh tiles** instead of a Landscape: a 2.5 km lattice with per-cell resolution (4.88 m within ~3 km of
  the route, 9.77 m in the rest of the core, 19.5 m around, 39 m far; 337 tiles, 55 M triangles), crack-free edges, and a
  C++ lookup (`OsrTerrain`, format OSR2, 50 MB) that stores the same quantised, edge-fixed samples and triangle split as the
  meshes, so crash detection / radar altitude match the render exactly. A single Landscape cannot vary resolution, and
  4.88 m over the whole core would cost far more than the corridor-only detail; VSM caching also favours Nanite. (No
  Landscape build was measured on the HP - see known issues.)
- `M_TerrainV2`: masks (snow, forest, wet, scree / beach, meadow, avalanche paths, hardness / strata phase, sky visibility)
  at 9.77 m in the core and 19.5 m elsewhere; full 3-projection triplanar rock at 3 scales with per-scale UV rotation (no
  fall-line streaks), strata colour bands from the same bed geometry as the carved ledges, snow on ledges from the detail
  normal, canopy colour only where no real firs are drawn, turquoise glacial rivers/lakes, baked sky visibility + bounce.
- Rocks: 6 CC0 Poly Haven scans (Nanite, 22 k instances) laid onto cliffs along the terrain normal, boulders on scree and
  floors, sea-cliff pieces on the coast; re-tinted to the terrain rock and snow-dusted from the same mask (`M_RockScan`).
- Firs (stage 1 mesh): 537 k within 1.7 km of the route, below an aspect-dependent treeline, off cliffs/scree/avalanche
  paths/rivers; needles darkened to real conifer albedo (`MI_FirNeedlesDark`).
- Ocean: custom instead of the Water plugin (a Water plugin ocean needs water zones/render targets and its own mesh system -
  more VRAM and less control on a 4 GB GPU). A sinh-spaced grid (3 m quads at the centre, 100 km reach) follows the camera
  (`AOsrGameMode::FollowOcean`); 8 incommensurate Gerstner swells from the SW (`hlsl/ocean_waves.inc`, shared by vertex and
  pixel shader) modulated by a km-scale wind/slick field; 4 detail normal layers at 4.3/17.9/71/293 m with different
  rotations/drift and distance fades (no visible tiling, far water = smooth swell + sky reflection); roughness grows with
  distance (broad glitter path); depth colour from the baked bathymetry; swell exposure mask (calm fjords, surf on open
  coasts); whitecaps only on steep crests in windy patches; breaking bands + surf foam from a shore-distance field.
- Lighting bake (`masks_ue.py`) redone on the new heightfield (sky visibility + one bounce), Lumen stays off.
- Volumetric cloud shadow map found to cost 8.5 ms on the RTX 3050 (the stage-2 cvars were caps above the active values):
  now 256^2 x 12 samples over a 60 km extent = 1.4 ms.

Epic, true 1920x1080, TSR 67 %, warm cache, `-autotest -noshots` (canyon autopilot 45 s at ~70 m AGL + overview + crash):

| | avg fps (canyon) | 1% low | GPU ms | VRAM peak | RAM WS peak | over-budget msg |
|---|---|---|---|---|---|---|
| stage 2 (old map) | 43.5 | 37.7 | 22.5 | 2.4 GB | 2.2 GB | no |
| world v2, before the cloud-shadow fix | 40.5 | 35.6 | 24.3 | 2.57 GB | 2.33 GB | no |
| **world v2 (final)** | **54.0** (autotest 53.4) | **45.3** (autotest 45.8) | **18.2** | **2.57 GB** | **2.33 GB** | no |

Autotest: 0 false crashes over the canyon run (radar altitude 300-500 ft), deliberate crash detected, respawn OK. Packaged
size 1.67 GB. Desktop build: `SilentRidge_UE_Map2\PLAY.cmd`; comparison: `Niepotrzebne/compare_map2/map_before_after.jpg`.

Known issues / next: rivers are still drawn from a flow mask on the terrain (no separate river surface), the D8 river line
can show a stair-step edge from low altitude; floodplain floors are a bit uniform (no braided channels / moraines yet); rock
scans are re-tinted Namaqualand/coastal scans (fine at speed, not true gneiss); no Landscape A/B was built on the HP -
the tile lattice was kept for variable resolution + exact collision; the stage-1 fir is the only tree species.

## Fast iteration (2026-09-29)

One command from a change on the Mac to a launch-tested build on the HP desktop, in a stable folder
`C:\Users\jurek\Desktop\SilentRidge\PLAY.cmd` (the older `SilentRidge_UE_*` folders are left as they are):

    UE_Test/scripts/ship.sh                  # sync -> build what's needed -> deploy -> launch test, prints phase times
    UE_Test/scripts/ship.sh --no-launch      # skip the ~20 s LoadMap test
    UE_Test/scripts/ship.sh --mode cook      # force a mode: cpp | stage | cook | deploy
    UE_Test/scripts/ship.sh --rehash-remote  # rebuild the HP manifest from its files (first run, or after editing files on the HP)
    UE_Test/scripts/quickrun.sh [tag] [game args]   # run the desktop build (default "-autotest"; "-autotest -noshots" for perf),
                                                    # pulls log/CSV/screenshots to Niepotrzebne/runs/<tag>, prints REPORT + perf

How it works:
- **Incremental sync** (`scripts/hp_sync.py`, SSH/SCP only - no SMB share, no firewall changes): MD5 manifest of
  `Content/`, `Config/`, `Source/` (+ the Game-only `.uproject`) on both sides; only new/changed files are tarred and sent,
  removed ones deleted, the new manifest stored on the HP. Local hashes are cached by size+mtime (0.2 s for 844 files).
- **Build mode from what changed** (`scripts/hp/build.cmd`, run as a battery-safe S4U scheduled task):
  - `cpp` (only `Source/`): UBT compile of the Game target, the new exe is copied into the staged build - no cook, no pak.
  - `stage` (`Config/` touched, no content): compile + restage/repak with `-skipcook` (Config lives in the pak).
  - `cook` (`Content/` touched): compile + **iterative cook** (`-iterate` = legacy iterative: keeps unchanged packages;
    a change of global cook settings still triggers a full recook automatically) + stage + pak/iostore.
  - no `-archive` copy: the staged build (`proj\SilentRidgeUE\Saved\StagedBuilds\Windows`) is robocopied (changed files
    only) into `Desktop\SilentRidge`, then `PLAY.cmd` is started in Jurek's session, LoadMap awaited, window closed, `Saved` wiped.
- The tool upload runs in parallel with the Mac-side hashing; everything holds the shared HP lock.

Measured on the HP (Wi-Fi LAN), change on the Mac -> launch-tested build on the desktop:

| change | before (`hp_package.sh` + `hp_deploy_desktop.sh`) | after (`ship.sh`) | after, `--no-launch` |
|---|---|---|---|
| C++ (one .cpp) | 350 s (tar 23 + transfer ~100 + BuildCookRun 186 + deploy/launch 34) | **67 s** (sync 5, UBT 41, deploy 2, launch 19) | ~48 s |
| config (one .ini value) | 253 s | **58 s** (sync 3, repak 32, deploy 2, launch 20) | 27 s |
| content (3 materials + 16 instances, 44 MB) | 267 s (full recook of 900 packages) | **101 s** (sync 6, cook 72: kept 544 / recooked 356, launch 21) | ~80 s |
| nothing changed | - | 28 s (sync 9 incl. first remote rehash, launch 17) | - |

Limitations:
- `cpp` mode swaps only the exe: fine while cooked content does not serialize our classes (true today - the game mode comes
  from config, the pawn is spawned). If a UCLASS/UPROPERTY layout used by assets changes, run `ship.sh --mode cook`.
- `stage` mode assumes the ini change does not alter cooked data; renderer/shader-format settings need `--mode cook`.
- Big content changes are bound by the Wi-Fi transfer (~15 MB/s) and the cook (e.g. regenerating all terrain tiles:
  ~600 MB, estimated 3-5 min). Content generation itself still runs in the Mac editor (`ue_run.sh ...`), not counted above.
- The HP manifest assumes nobody edits `C:\Users\jurek\osr_ue\proj` by hand (else `--rehash-remote`). If the HP changes
  network, fix `HostName` in `~/.ssh/config` (ship.sh fails fast with "HP unreachable").
- The old full pipeline (`hp_package.sh`, `hp_deploy_desktop.sh`, `hp_run_ue.sh`) still works for clean full builds.
