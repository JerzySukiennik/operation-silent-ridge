# Operation Silent Ridge — architecture contract (v1)

Read `DESIGN.md` for the game. This file is the contract between modules. Each module is
built by a separate agent in parallel, then integrated by the main agent. **Stay inside your
owned paths; talk to other modules only through the interfaces below.**

Engine: Godot **4.6.2** (`/Applications/Godot.app/Contents/MacOS/Godot`), Forward+, Jolt.
Language: GDScript, typed. Code, comments, commits in English. Each file starts with a
one-line header comment describing the file; no other comments except where a number
needs its source/meaning (e.g. `# F135 max AB thrust, P&W 2024: 43,000 lbf`).

## v1 scope

F-35C flying (semi-sim, gamepad, 3rd-person chase cam, HMD-style HUD) over an ocean and
fictional snowy mountains with a canyon, 1–4 players online via public room list, join in
progress. Air start near the carrier position (the carrier itself is v2). Crash → respawn.

## Hard rules

- **Never open a Godot window on the Mac.** Mac = `--headless` only (import, parse check,
  logic tests). Anything visual or performance-related runs on the HP laptop.
- **HP laptop** (Windows 11, Ryzen 7 5800H, RTX 3050 Laptop **4 GB VRAM**, 8 GB RAM with only
  ~3.5 GB free): run builds ONLY via `Niepotrzebne/hp-run.sh` (it holds a global lock so
  runs from different agents never overlap; it waits for the lock up to 20 min):
  `Niepotrzebne/hp-run.sh <tag> <build_dir> <out_dir> <timeout_s> <user args...>`
  The build dir must contain `OperationSilentRidge.exe` (+ the webrtc dll). The exe gets
  `--shots=<dir>` automatically; write screenshots and logs there. Print lines starting with
  `REPORT ` for anything you want to read back (they are grepped from the log).
  Keep runs short (≤ 90 s) — other agents are waiting for the lock.
- **Performance target on HP: never below 30 fps, aim ≥ 45 fps average** at 1920×1080 output
  with FSR 2 at 0.67 render scale (set in project.godot). Budget: terrain ≤ 8 ms GPU,
  ocean ≤ 3 ms, sky/fog/lighting ≤ 4 ms, aircraft + VFX ≤ 3 ms. Process RAM ≤ 2.5 GB,
  VRAM ≤ 3.2 GB. Measure, don't guess.
- **Work in a private copy** to avoid clobbering each other's `.godot` import cache:
  `rsync -a --delete --exclude .godot "<project>/game/" "<your scratch>/game/"`, then import
  and export from there (`--headless --import`, then
  `--headless --export-release "Windows Desktop" <your scratch>/build/OperationSilentRidge.exe`).
  Write your real deliverables into the shared project under your owned paths only.
  If another module's files are broken/incomplete in your copy, replace them in *your copy*
  with a stub — never edit them in the shared project.
- Per-module test scenes: `scenes/test/<module>_*.tscn`, launched with
  `--scene=res://scenes/test/<module>_x.tscn` (the boot router in `scripts/main.gd` handles it).
  Boot args are available as `Engine.get_meta("boot_args")` (Dictionary, e.g. `{"shots": "C:\\..."}`).
- Assets: only CC0 / CC-BY (credit in `game/assets/CREDITS.md`, append a line per asset:
  file → author, source URL, license). No NC/ND, no ripped game assets. Keep source files
  (high-res originals) in `assets/**/src/` — excluded from export.
- Units: metres, seconds, kg, radians internally. Godot axes: +Y up, -Z forward.
- Don't touch `project.godot` except: the **world** module owns the `[rendering]` section,
  and each module may tell the main agent in its final report what else it needs.

## World layout (world module decides the exact numbers and documents them here)

Map 80 × 80 km, origin at map centre, sea level y = 0. x, z ∈ [-40 000, 40 000]; +x east,
+z south (north = -z). Beyond the map edge the heightfield is mirrored (terrain continues).
All numbers below are generated constants in `scripts/world/world_layout.gd` (seed 1729,
`tools/terrain/gen_terrain.py`); read them from there, not from this table.

| item | value |
|---|---|
| Heightfield | 8192² samples, 9.77 m spacing, R16 (0.08 m steps), range -1024…4096 m; land peaks 3 530 m, p99 3 080 m, median land 1 570 m |
| Coast | ragged fjord coast, west-most land at x ≈ -6.6 km (median over z), from -17.5 km (headland at z ≈ +8…+11 km) to -0.6 km (deep bays); open ocean everywhere west of x = -17.5 km |
| Sun | elevation 40°, from azimuth 200° (SSW); `WorldLayout.TO_SUN` = (-0.262, 0.643, 0.720) |
| Snow / trees | continuous snow cover above ~520 m (±250 m noise, less on steep/convex rock), forest up to ~1 550 m |
| Carrier | (-31 000, 0, 9 500), heading 250°; 45.1 km from the target, ~13.5 km west of the fjord mouth |
| Spawns | 4 air starts at 600–645 m, 1.5 km east of the carrier, echelon, nose towards the canyon mouth |
| Canyon mouth | (-17 500, 0, 9 800) — the run starts as an 8.5 km sea-level fjord between cliffs, first dry floor at (-9 724, 4, 8 315) |
| Canyon run | 34.2 km centreline (25.7 km over land), floor 64–234 m wide, walls 400–850 m of 52–72° rock then ~42° slopes; floor climbs from sea level to ~880 m; 6 main bends |
| Canyon end | (9 493, ~970, -1 838), opening into the target valley |
| Target valley | flat glacial trough centred at `TARGET` = (12 400, 880, -2 700), 6.4 × 2.2 km, axis (0.83, -0.56), ringed by 2 000–3 000 m peaks |

`canyon_path()` returns the centreline every 200 m with y = terrain floor (0 over the fjord);
the flight corridor (floor width) is kept free of trees and never rises above the floor profile.

## Modules, owners, interfaces

### main (main agent)
Owns: `project.godot`, `scripts/main.gd`, `scenes/main.tscn`, `scenes/game.tscn`,
`scripts/game.gd`, `scripts/test/autotest.gd`, `export_presets.cfg`, `docs/`,
`scenes/aircraft/f35c_visual.tscn` + `assets/models/` (real Sketchfab F-35C, after v1 integration).
The real model already exists: `scenes/aircraft/f35c_visual_real.tscn` (script `scripts/aircraft_visual/f35c_visual.gd`,
`set_gear(0.0 up … 1.0 down)`, same Marker3D contract, ~190k tris, gear animation baked). At integration it replaces the placeholder.
`game.gd` instantiates World, spawns the local Aircraft, spawns/removes remote Aircraft on
NetSync signals, calls `world.set_focus()` each frame, owns the HUD instance.

### world — `scripts/world/`, `scenes/world/`, `shaders/world/`, `assets/world/`, `tools/terrain/`
`scenes/world/world.tscn`, root script `class_name World extends Node3D`:
```gdscript
const SEA_LEVEL := 0.0
func height_at(x: float, z: float) -> float        # terrain height (m), ≤ 0.5 m from rendered surface; negative under sea
func surface_at(x: float, z: float) -> float       # max(height_at, SEA_LEVEL) (ocean waves ignored)
func normal_at(x: float, z: float) -> Vector3
func spawn_points() -> Array[Transform3D]          # ≥ 4 air-start transforms near the carrier, ~600 m, facing the coast
func carrier_transform() -> Transform3D            # placeholder, used by v2
func target_position() -> Vector3                  # target valley point, used by v3
func canyon_path() -> PackedVector3Array           # canyon centreline (for autotest autopilot + future AI)
func set_focus(world_pos: Vector3) -> void         # camera/player position each frame (LOD/streaming)
```
Owns WorldEnvironment, sun DirectionalLight3D, sky, fog, ocean, terrain, vegetation.
Clear bright day, sun ~35–45° high. Must also export `height_at` bit-exactly usable from
headless Mac tests (no GPU readback).

### flight — `scripts/flight/`, `scenes/aircraft/f35c.tscn`, `scripts/ui/hud*.gd`, `scenes/ui/hud.tscn`, `shaders/vfx/`, `scenes/test/flight_*`
- `Controls` autoload (`scripts/flight/controls.gd`): registers input actions in code
  (gamepad first, keyboard fallback for debugging), exposes a per-frame `ControlInput`
  (pitch, roll, yaw, throttle, afterburner, look...) and `Controls.override` for autotest autopilot.
- `class_name F35FlightModel extends RefCounted` — pure simulation, no SceneTree access,
  deterministic, testable headless. State: position, orientation (Basis/Quaternion),
  velocity, angular velocity, throttle, fuel, mass.
- `scenes/aircraft/f35c.tscn`, root `class_name Aircraft extends Node3D`:
```gdscript
var is_local := true
var peer_id := 1
var pilot_name := "Pilot"
var world: Node   # has height_at/surface_at; may be null in tests (then sea level only)
signal crashed(position: Vector3)
func get_net_state() -> Dictionary     # {p: Vector3, q: Quaternion, v: Vector3, w: Vector3, thr: float, ab: bool, gear: bool, alive: bool}
func apply_remote_state(state: Dictionary, sent_time: float) -> void  # for remote copies; interpolation handled in NetSync or here (flight decides, documents)
func respawn(at: Transform3D) -> void
func telemetry() -> Dictionary         # ias_kt, mach, alt_ft, radar_alt_ft, aoa_deg, g, heading_deg, throttle, ab, fuel_kg, vs_fpm
```
  Child `Visual` = instance of `res://scenes/aircraft/f35c_visual.tscn`. Visual contract:
  root Node3D, nose towards -Z, +Y up, origin at CG, length ≈ 15.7 m, span ≈ 13.1 m.
  Marker3D children: `Nozzle` (exhaust exit, facing +Z), `WingtipL`, `WingtipR`,
  `Canopy`. The flight module ships a **placeholder** visual (clean procedural F-35-like
  silhouette); the main agent replaces it with the real model keeping the same markers.
- Chase camera (3rd person, right-stick orbit, speed-dependent FOV, G/buffet shake).
- HUD overlay styled after F-35 HMD symbology (green, thin lines): flight path marker,
  speed (kt) + Mach, altitude (ft) + radar altitude below 5 000 ft AGL, heading tape,
  G, AoA, throttle/AB, fuel, teammate name tags (via `hud.set_teammates(Array[Dictionary])`).
- VFX: afterburner plume, heat haze, wingtip vortex trails at high G/AoA, vapour at high G
  and transonic, crash explosion.

### net — `scripts/net/`, `scenes/ui/menu.tscn`, `scripts/ui/menu*.gd`, `scenes/test/net_*`
Rewrite of Haystack's proven transport (reference copies in `Niepotrzebne/haystack_*`,
`ntfy_stream.gd` already copied) with a **public room list instead of room codes**.
- `Net` autoload (`scripts/net/net.gd`): `host(pilot_name)`, `join(room_id, pilot_name)`,
  `leave()`, `start_browsing()`/`stop_browsing()`, signals `rooms_updated(rooms: Array)`
  (each `{id, host, players, max, mission, version, age_s}`), `state_changed`,
  `peer_joined(id)`, `peer_left(id)`; `is_online()`, `is_host()`, `my_id()`, `names`.
- `NetSync` node (`scripts/net/net_sync.gd`), added by `game.gd`:
  `register_local(aircraft)`, signals `player_spawned(id, name)`, `player_left(id)`,
  `remote_state(id, state: Dictionary, sent_time: float)`; sends local aircraft state
  ~30 Hz (unreliable ordered), host relays; join in progress works (late joiner receives
  everyone). Include a clock offset estimate so `sent_time` is in the receiver's clock.
- Menu (`scenes/ui/menu.tscn`): title over a dark background, pilot name, Host, room list
  (auto-refresh, join by selecting a row), Settings placeholder, Quit. Fully gamepad
  navigable. On success → `get_tree().change_scene_to_file("res://scenes/game.tscn")`.
- Rooms are always public (DESIGN): playing solo = hosting a room others may join.
  Offline fallback if ntfy is unreachable: play solo offline.

### audio — `assets/audio/`, `scripts/audio/`, `scenes/test/audio_*`
Real recordings only (DVIDS/US Navy public domain, Freesound CC0/CC-BY, Wikimedia Commons),
no synthesis. v1: F-35/F135 engine external loops (idle, military, afterburner) with
crossfade by throttle, doppler + distance for other players' jets, wind/airframe noise by
IAS, afterburner light-up, sonic boom (heard from outside), crash explosion. 3rd-person
listener. Deliver `class_name JetAudio extends Node3D` with
`setup(is_local: bool)` and `update(telemetry: Dictionary, delta: float)`; the flight
module/main agent attaches it to Aircraft.
