# Port to Unreal Engine 5.7 — plan

Decided 2026-09-27 by Jurek after the side-by-side test (`UE_Test/README.md`): the game moves
from Godot 4.6 to **UE 5.7.4**. Visual minimum = **Epic look**. The Godot v1 stays in the repo
(`game/`, `v1/`) as the reference implementation and number source.

## Hard constraints

- Target: HP laptop (RTX 3050 Laptop **4 GB VRAM**, 8 GB RAM) at 1080p, TSR ~67 %.
  **Epic look, ≥ 40 fps avg, 1 % low ≥ 30, no "video memory exhausted"** (UE budget ≈ 3030 MB).
- Fixed time of day (clear day, sun ~40° SSW) → **baked indirect lighting** is allowed
  (Jurek's call, "like Insomniac in Marvel's Spider-Man").
- Editor work on the Mac (UE 5.7.4 + Xcode 26.5), Windows cook/package/test on the HP
  (UE 5.7.4 + VS 2022 Build Tools). Never open editor windows on the Mac unless unavoidable.
- HP runs: shared lock `/private/tmp/claude-501/osr-hp.lock`; scheduled tasks via
  `Register-ScheduledTask` with `-AllowStartIfOnBatteries -DontStopIfGoingOnBatteries`
  (plain `schtasks` tasks don't start on battery and die when unplugged).
- Judge a preset only with a **warm shader/PSO cache** (first run at a new preset compiles).

## Stages

1. **World in budget (Epic look).** Terrain textures/masks made streamable (or RVT), bake
   GI + sky occlusion into terrain textures, Lumen off for the static world (dynamic sun +
   VSM stay: aircraft/tree shadows), PSO precaching. Measure every change on the HP.
2. **C++ project + flight.** Convert to a C++ project (module builds on Mac for the editor and on
   the HP for Win64). Port `F35FlightModel` (same numbers/tests as Godot `flight_numbers.gd`),
   pawn, Enhanced Input gamepad layout (same as v1), chase camera with speed cues, HMD-style
   HUD (C++ `AHUD`/Slate), afterburner/vapour VFX, jet audio (the same 12 recordings).
3. **Multiplayer.** UE replication (listen server, owner-authoritative movement like v1) +
   Epic Online Services for the public room list and NAT traversal (Jurek creates the EOS
   developer account himself).
4. **Continue the design stages** from `DESIGN.md` (carrier, laser + bombs, radar/SAMs,
   fighters, ejection/CSAR, polish).

Each stage ends with a packaged build on the HP desktop that Jurek plays and judges.
