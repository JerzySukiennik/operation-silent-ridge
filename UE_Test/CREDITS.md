# UE project credits (SilentRidgeUE)

Assets used by the Unreal Engine build that are not listed in `game/assets/CREDITS.md` (the stage 1/2 terrain textures,
fir, F-35C and audio credits there still apply). Everything below is CC0 (public domain dedication) from Poly Haven — no
attribution required, listed for provenance. Fetched by `scripts/terrain_v2/fetch_assets.py`.

## World v2 (terrain generator, 2026-09-28)

- Terrain heightfield, masks and lighting bake → procedural, own work (`scripts/terrain_v2/`), no external data.
- Rock scans (Nanite meshes scattered on cliffs, scree and coast; albedo re-tinted in `M_RockScan`):
  - Poly Haven "Namaqualand Cliff 02", https://polyhaven.com/a/namaqualand_cliff_02, CC0.
  - Poly Haven "Rock Face 01", https://polyhaven.com/a/rock_face_01, CC0.
  - Poly Haven "Rock Face 02", https://polyhaven.com/a/rock_face_02, CC0.
  - Poly Haven "Coastal Cliff 02", https://polyhaven.com/a/coastal_cliff_02, CC0.
  - Poly Haven "Boulder 01", https://polyhaven.com/a/boulder_01, CC0.
  - Poly Haven "Rock 09", https://polyhaven.com/a/rock_09, CC0.
- Terrain textures (in addition to the stage 1 set):
  - `T_rockface_*` → Poly Haven "Rock Face 03", https://polyhaven.com/a/rock_face_03, CC0.
  - `T_grav_*` → Poly Haven "Rocks Ground 06", https://polyhaven.com/a/rocks_ground_06, CC0.
  - `T_sand_*` → Poly Haven "Coast Sand 05", https://polyhaven.com/a/coast_sand_05, CC0.
- Ocean grid mesh, ocean/terrain/rock materials → own work (`scripts/terrain_v2/prep_ue_assets.py`, `scripts/hlsl/*_v2.hlsl`).

## M3 carrier (2026-09-29)

- Nimitz-class carrier (hull, flight deck + markings, island, sponsons, hangar, elevators, JBDs, wires, IFLOLS), deck textures,
  helmet and F-35C tailhook → procedural, own work (`scripts/carrier/`), built to REFERENCES.md 4.2 dimensions. The Sketchfab
  CC-BY hull candidates (REFERENCES 8.2) were not used yet (no Sketchfab API key in the Blender MCP addon); the visual hull is
  swappable, gameplay geometry comes from `scripts/carrier/layout.py`.
- F-35C gear animation → split from the project's own F-35C model (`game/assets/models/f35c/f35c.glb`, see `game/assets/CREDITS.md`).
- Characters and locomotion → Unreal Engine template mannequin (Manny) and `ABP_Unarmed` animations from UE 5.7
  `Templates/TemplateResources/High/Characters`, © Epic Games, used under the Unreal Engine EULA (UE projects only).
- LSO / pilot radio voices → synthesized offline with Piper TTS (piper-tts 1.8.0, GPL-3.0 tool, http://github.com/OHF-voice/piper1-gpl;
  the generated audio is not part of the tool) using the voices `en_US-norman-medium` (dataset: LibriVox recordings, public domain; trained from scratch) and
  `en_US-ljspeech-medium` (LJ Speech dataset, public domain), https://huggingface.co/rhasspy/piper-voices; radio filter own work
  (`scripts/carrier/gen_radio.py`).
