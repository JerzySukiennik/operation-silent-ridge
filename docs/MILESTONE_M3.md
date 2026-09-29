# Milestone M3 — carrier ops + the main mission (UE 5.7)

Requested by Jurek on 2026-09-29: "the main mission, the carrier, launching and landing on it",
**all combat systems now** (target + laser + bombs, radar + SAMs, enemy fighters) and **walking on
deck now** (3rd person). Solo first; multiplayer is the next milestone, so keep gameplay state
server-authoritative-friendly (no singletons that assume one player; put state on GameState/actors).
Reference numbers and procedures: `docs/REFERENCES.md` (§1 F-35C, §3 carrier approach/Delta Flight
Path, §4 Nimitz deck/launch/recovery, §5 threats/radar horizon, §9 Top Gun mission). Design: `docs/DESIGN.md`.

## The loop the player must be able to play end-to-end

1. **Spawn on foot** (3rd person, UE template Manny/Quinn with its locomotion set, from
   `Templates/TemplateResources/High/Characters` + `.../Mannequin`) at the island door of a
   **Nimitz-class carrier sailing ~25 kt into the wind** with gentle roll/pitch. Walk the flight deck
   (and hangar deck + elevators — elevators may come in a follow-up if needed, say so).
   The character stands on the moving deck correctly (based movement on a moving platform).
2. Walk to **your F-35C** parked near a catapult (marker), **climb in** (interact button, short
   ladder/fade), camera switches to the chase cam.
3. **Launch procedure** with NPC deck crew (yellow shirt taxi director signals, catapult crew,
   shooter): taxi onto the catapult (director hand signals as animations or at least clear poses),
   hook-up, jet blast deflector raises behind you, run up to MIL, **salute** (button), shooter
   signal → catapult stroke ~2 s to ~150 kt → you fly. Keep it skippable after first time? (no — full
   procedure, Jurek chose "pełna procedura").
4. **Mission** (briefing text on the pause/HUD at start): fly the canyon to the target valley.
   **Radar** sites see you by line of sight over the real terrain (C++ `OsrTerrain`), with a radar-horizon
   check (4.12·(√h_radar+√h_target) km); detection → **RWR** tones/symbols → **SAM launches**
   (long-range S-300/400 class near the valley, short-range Pantsir/Tor at the target), missiles with
   proportional navigation, **flares** (IR) and **chaff** (radar) that can defeat them, damage/kill.
5. **Target**: underground plant in the target valley with a **ventilation shaft**. **Laser
   designation** (solo: you lase it yourself through a targeting-pod view; design is pair-based for
   co-op later — build the API so another player can hold the laser) + **laser-guided bombs** (fixed
   loadout: 2 LGBs + 25 mm GAU-22/A gun pod, 220 rounds). A bomb into the shaft = target destroyed
   (big secondary explosions); a hit elsewhere = no kill.
6. **Egress**: after the strike, **enemy fighters** (Su-57 class pair) scramble from a small enemy
   airbase near the valley; basic but believable AI (pursuit, missile/gun threat); player has the
   gun and can evade/outrun/terrain-mask.
7. **Return and trap**: carrier approach with **IFLOLS meatball**, **LSO radio calls** (TTS or text +
   radio beep for now), **Delta Flight Path / Magic Carpet** approach mode, tailhook, **4 arresting
   wires** (3-wire target), bolter/wave-off handling, **LSO grade** (OK/Fair/No grade/Cut + wire #).
   **Trap = mission success** screen with stats (time, detections, SAMs evaded, grade).
8. **Death**: crash or shot down → respawn on foot on the carrier deck (ejection/CSAR is a later milestone).

## F-35C model needs
The current model is an F-35A converted to a C wing; for carrier ops add a **tailhook** (animated,
Y/hook button) and ideally the **twin nose wheel + launch bar**; the gear animation exists in the
Godot GLB (`game/assets/models/f35c/f35c.glb`, baked `set_gear` animation) — bring gear up/down
animation into UE too (currently a static mesh).

## Assets
- Carrier: Sketchfab CC-BY Nimitz candidates in `docs/REFERENCES.md` §8 (e.g. 402k-tri CVN-68 by
  MirzaArrafiERV_45, 98k by GrantLarcenie). Downloads via Blender MCP (Sketchfab key was set in the
  Blender addon on 2026-09-26; if it's missing, stop and ask via the main agent — never enter
  credentials). **Render any human-made asset and show it before building on it** (the main agent
  shows Jurek). Credits in `UE_Test/CREDITS.md`.
- Enemy: Su-57 (CC-BY, andertan), Pantsir (CC-BY) etc. from §8; SAM/radar sites may be kit-bashed.
- Characters: UE template mannequins (Epic content, allowed in UE projects).
- Audio: real recordings where possible (catapult, deck ambience, arresting wire, RWR tones from real
  cockpit footage on DVIDS if usable), radio voices TTS with radio filter; credit everything.

## Hard requirements
- HP budget unchanged: Epic, true 1080p, ≥ 40 fps avg, 1% low ≥ 30, no VRAM over-budget message —
  measure on the HP with `quickrun.sh`, including on the carrier deck and during a SAM engagement.
- Pad (DS4 via `OsrPadInput.cpp`, XInput, keyboard) for everything, incl. on-foot and targeting.
- Update `CONTROLS.txt` and the in-game help.
- Autotest extended: `-autotest` still flies the canyon; add `-autotest=carrier` (catapult launch +
  auto-approach + trap) and `-autotest=strike` (auto-lase + bomb the shaft) for regression.

## Two workstreams (parallel agents) — file ownership
- **Carrier & deck** (agent "carrier"): carrier actor/motion, deck/hangar/elevators, on-foot character
  + moving-platform movement, deck crew NPCs, catapult + launch procedure, arresting gear + LSO +
  IFLOLS + DFP approach mode + grading, tailhook/gear on the F-35C, spawn on deck.
  Owns `Source/.../Carrier*`, `Deck*`, `OsrCharacter*`, `Lso*`, `Content/Carrier/**`, `Content/Characters/**`.
- **Mission & combat** (agent "mission"): mission state machine + briefing + success/fail screens,
  target plant + vent, targeting pod + laser + LGBs + gun, radar LOS + RWR + SAMs + flares/chaff,
  enemy airbase + fighters AI, damage model. Owns `Source/.../Mission*`, `Weapon*`, `Radar*`, `Sam*`,
  `Enemy*`, `Content/Mission/**`, `Content/Enemy/**`.
- **Shared files** (`OsrGameMode.*`, `OsrJetPawn.*`, `OsrHud.*`, `OsrControls.*`, configs): edit only
  what you need, in small focused hunks, and re-read the file right before editing (the other agent
  may have changed it). Integration contract: the carrier agent exposes events on the carrier/jet
  (`OnCatapultLaunched`, `OnTrap(Grade, Wire)`, `OnBolter`, `OnSpawnedOnDeck`), the mission agent
  drives the mission state from them.
- **Locks**: HP `/private/tmp/claude-501/osr-hp.lock` (ship.sh/quickrun.sh use it); Mac editor
  `/private/tmp/claude-501/osr-editor.lock` — only ONE `UnrealEditor(-Cmd)` process on the project at a
  time (mkdir-lock around every headless editor run; release after). Commit only your own paths
  (`git add <paths>`, never `git add -A`).
