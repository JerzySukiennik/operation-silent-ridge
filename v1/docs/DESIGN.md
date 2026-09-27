# Operation Silent Ridge — design contract

Co-op (1–4) carrier strike game. Players walk the deck of a Nimitz-class carrier, climb
into F-35Cs, launch off the catapult, fly a snowy canyon below enemy radar, laser-bomb
the ventilation shaft of an underground plant, escape SAMs and Su-57s, and trap back on
the carrier. Mission structure inspired by the final strike in *Top Gun: Maverick* —
**no names, callsigns, music, logos or likenesses from the film**.

Decided in a `/pytania` session on 2026-09-26. Real-world reference dossier with sources:
[`REFERENCES.md`](REFERENCES.md).

## Tech

| topic | decision |
|---|---|
| Engine | **Godot 4.6**, Forward+, Jolt Physics |
| Target | Windows, laptop HP (Ryzen 7 5800H, RTX 3050 4 GB, 8 GB RAM) — **stable 30+ fps**, ~720p internal → 1080p via FSR 2 |
| Testing | windowed tests only on HP (`hp-test.sh` pattern from Haystack/Timberline); Mac headless only |
| Distribution | Windows zip on GitHub Releases + download button on gzowo.fun |
| Multiplayer | 1–4 players, host-authoritative, WebRTC (webrtc-native) + ntfy.sh signalling. **Public room list only** (no private/code rooms). **Join in progress** → spawn on the carrier deck. No bots. |
| Controls | gamepad only |
| Camera | **3rd person everywhere** — flying and on foot. HUD overlay styled after F-35 HMD symbology |
| Workflow | stages; every stage = autotested build on HP → Jurek plays → feedback → next version. Builder/critic loop vs. reference screenshots on graphics stages |

## Flight

- **Semi-sim**: AoA, energy, G-loads, stall; fly-by-wire enforces limits like the real jet.
- **F-35C** only (carrier variant). Real names for equipment (F-35C, Su-57, S-400, Pantsir).
- Numbers: see REFERENCES §1 (P&W F135 figures: 43,000 lbf AB / 28,000 dry, +7.5 g, 50° AoA).

## Carrier

- **Nimitz class**, sailing ~25 kt into the wind, gentle roll/pitch, wake.
- Walkable: flight deck + hangar deck + elevators (F-35 rides the elevator up).
- Players walk around on foot (3rd person), climb into the jet, can climb out after landing.
- **Full launch procedure** with NPC deck crew (yellow director signals, hook-up, JBD raises,
  full power, salute button, shooter → cat stroke ~2 s).
- **Landing**: IFLOLS "meatball" + LSO radio calls + Magic Carpet / Delta Flight Path.
  LSO grade + wire number. **Trap = mission success.**
- Jet blast physically shoves people on deck; full friendly fire.

## Mission 1 (the one to perfect first)

1. Deck → launch → ~3–5 min transit (compressed scale, map ~80×80 km, carrier 40–60 km offshore).
2. Low-level canyon ingress in **fully fictional** snowy mountains (Cascades-inspired look).
   **Simulated radar** — line-of-sight detection, terrain masking; RWR tones → SAM launches.
   **No time limit.**
3. Target: underground plant, ventilation shaft. **Mandatory pair laser mechanic** — one jet
   designates, the other drops. Solo = designate and drop yourself.
4. Pop-up, bomb, egress under SAM fire, then Su-57/Su-35 fighters from the enemy airbase.
5. Return and trap.

- **Weapons (fixed mission loadout)**: laser-guided bombs (GBU-12 / penetrator class) +
  25 mm GAU-22/A gun pod. No air-to-air missiles — vs fighters it's the gun or running.
- **Enemy defenses**: long-range SAM (S-300/400 type), short-range SAM (Pantsir/Tor),
  fighters + enemy airbase. Real equipment, unnamed country.
- **Self-defence**: RWR with real tones, flares + chaff, targets on HUD, no radar modes.
- **Friendly fire**: full (collisions and weapons).

## Shot down

- Eject → parachute → **survival on the ground** in enemy territory (3rd person).
- Enemy infantry patrols + vehicles hunt the downed pilot.
- Downed pilot has a **handheld laser designator** (JTAC role) to mark targets for teammates.
- Rescue: walk to an extraction zone, **AI MH-60 Seahawk (CSAR)** flies in from the carrier
  while teammates in F-35s provide cover. Back on deck → take a new jet.
- Everyone down → mission continues, CSAR for everyone; failure only if everyone dies on the ground.

## Look & feel

- Clear bright day, snowy mountains (as in the film).
- Visual priorities: **natural human movement** (no "animated" look), **natural snow and
  nature**, **ocean + carrier**, **F-35 and flight effects** (vapour cones, afterburner, wingtip trails).
- Humans: **Mixamo mocap + procedural foot IK + blending** (torso lean, breathing).
  Realistic characters, faces hidden (helmets, visors, balaclavas).
- 3D assets: **Sketchfab CC-BY** (F-35C, Nimitz, Su-57, Pantsir…) — every model rendered and
  shown to Jurek before use — then Blender MCP for LODs, fixes and animated control surfaces,
  gear, hook. Credits in `game/assets/CREDITS.md`.
- Audio: real recordings only (DVIDS public domain, Freesound CC0), radio voices = TTS with a
  radio filter. Music only in menu, on catapult launch and after the trap.
- Tutorial: optional qualification sorties (cat launch, canyon run, laser drop, trap).

## Stages

1. **v1** — F-35C flying (semi-sim, gamepad, 3rd person, HUD) over ocean + fictional snowy
   canyon terrain, multiplayer from day one (see each other fly), public room list.
2. Carrier: Nimitz, walking on deck/hangar/elevators, deck crew, catapult launch, trap + LSO.
3. Laser designation + bombs + target plant.
4. Radar / terrain masking / SAMs / RWR / flares & chaff.
5. Enemy airbase + fighters + gun.
6. Ejection, ground survival, patrols, handheld laser, CSAR helicopter.
7. Tutorial sorties, audio pass, polish.
