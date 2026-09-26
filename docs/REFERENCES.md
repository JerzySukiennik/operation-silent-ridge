# F-35 Carrier Strike Game: Real-World Reference Dossier

Compiled 2026-09-26 for a realistic co-op carrier-strike game (Godot 4.6, target: laptop RTX 3050 4 GB).

**How to read this**
- Every number has a source link. Wikipedia is used as a secondary source, and its specs box cites Lockheed Martin, the SAR and DOT&E.
- Numbers **derived** by me (simple physics or arithmetic from cited figures) are marked **[derived]**.
- Numbers that are **estimates** or come from forums or other non-primary sources are marked **[estimate]** or **[secondary/unverified]**.
- Official sources sometimes disagree. The main case is that Lockheed's 2019 fact sheets use older engine and fuel figures. Where that happens, both values are listed.

---

## 1. F-35 variants

### 1.1 Which variant for a carrier game? **F-35C**

| | F-35A (CTOL) | F-35B (STOVL) | **F-35C (CV / CATOBAR)** |
|---|---|---|---|
| Operator/base | USAF + allies, land runways | USMC, UK, Italy, Japan; amphibious ships (USS *America* LHA-6, USS *Wasp* LHD-1), HMS *Queen Elizabeth* | **US Navy + USMC on Nimitz/Ford supercarriers** |
| Launch/recovery | Runway | Short take-off (ski-jump on UK carriers), vertical landing, "rolling" landing (SRVL) | **Catapult launch + tailhook arrested landing** |
| Tailhook | Emergency single-use hook only | None | **Robust carrier tailhook** |
| Wing | 35 ft, fixed | 35 ft, fixed | **43 ft, larger, folding wingtips** |

**Why the F-35C.** A game that starts on a supercarrier with catapults, wires and the meatball needs the F-35C. It is the only variant built for CATOBAR: bigger folding wing, strong tailhook, twin-wheel nose gear with a launch bar, reinforced gear. It has flown from USS *Carl Vinson* on operational deployments since 2 Aug 2021 (VFA-147, CVW-2) and used its weapons in combat in April 2025 (VFA-97 shooting down Houthi drones). The F-35B belongs on an amphibious ship or HMS *Queen Elizabeth*, which is a different game loop with hover landings and no wires. [Wikipedia F-35 (F-35C variant section, US Navy operators)](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II)

### 1.2 Hard numbers per variant

**Source A:** Lockheed Martin official fact sheets (2019), archived: [F-35A PDF](https://web.archive.org/web/2024id_/https://www.lockheedmartin.com/content/dam/lockheed-martin/aero/documents/F-35/f35A.pdf), [F-35B PDF](https://web.archive.org/web/2024id_/https://www.lockheedmartin.com/content/dam/lockheed-martin/aero/documents/F-35/f35B.pdf), [F-35C PDF](https://web.archive.org/web/2024id_/https://www.lockheedmartin.com/content/dam/lockheed-martin/aero/documents/F-35/f35C.pdf). The same F-35C figures are reproduced at the [US Naval Academy F-35C page](https://www.usna.edu/NavalAviation/FixedWingAircraft/F35C_Lightning_II.php).
**Source W:** Wikipedia "Differences among variants" table and specs box. Its citations are LM specs, the FY2019 SAR and DOT&E. [link](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Specifications_(F-35A))

| Parameter | F-35A | F-35B | F-35C | Src |
|---|---|---|---|---|
| Length | 51.4 ft / 15.7 m | 51.2 ft / 15.6 m | 51.5 ft / 15.7 m | A, W |
| Wingspan | 35 ft / 10.7 m | 35 ft / 10.7 m | **43 ft / 13.1 m** | A, W |
| Height | 14.4 ft (4.39 m) | 14.3 ft (4.36 m) | 14.7 ft (4.48 m) | W |
| Wing area | 460 ft² / 42.7 m² | 460 ft² / 42.7 m² | **668 ft² / 62.1 m²** | A, W |
| Empty weight | 29,300 lb (13,290 kg) | 32,472 lb (14,729 kg) | 34,581 lb (15,686 kg) | W |
| Max take-off weight | 70,000 lb class (A: 65,918 lb per RAAF) | 60,000 lb class | 70,000 lb class | W ([RAAF](https://www.airforce.gov.au/aircraft/f-35a-lightning-ii)) |
| Internal fuel | 18,250 lb / 8,278 kg | 13,100 lb (LM) / 13,500 lb (W) | 19,200 lb / 8,708 kg (LM) / 19,750 lb (W) | A, W |
| Weapons payload | 18,000 lb / 8,160 kg | 15,000 lb / 6,800 kg | 18,000 lb / 8,160 kg | A, W |
| Engine | F135-PW-100 | F135-PW-600 (+ LiftFan) | F135-PW-100 | A |
| Thrust (LM 2019 sheet) | 40,000 lb max (A/B) / 25,000 lb mil | 38,000 lb max / 26,000 mil / 40,500 lb vertical | 40,000 lb max / 25,000 lb mil | A |
| Thrust (P&W current) | **43,000 lbf max class / 28,000 lbf intermediate class** | n/a | **43,000 / 28,000** | [P&W F135-PW-100 product card (2022)](https://app.prattwhitney.com/download/F135-CTOL.pdf) |
| Max speed | Mach 1.6 (high altitude); Mach 1.06 / 700 kt at sea level (W) | Mach 1.6 | Mach 1.6 | A, W |
| Combat radius (internal fuel) | >590 nmi (LM); 669 nmi interdiction / 760 nmi A-A (W) | >450 nmi (LM); 505 nmi (W) | >600 nmi (LM); 670 nmi (W) | A, W |
| Range (internal fuel) | >1,200 nmi | >900 nmi | >1,200 nmi | A |
| Max g | **+9.0** | **+7.0** | **+7.5** | A, W |
| Service ceiling | 50,000 ft | n/a | n/a | W |
| Thrust/weight, full fuel / 50% fuel | 0.87 / 1.07 | 0.90 / 1.04 | 0.75 / 0.91 | W |
| Trimmed AoA capability | 50° | 50° | 50° | W (Design section) |
| Supersonic dash | Mach 1.2 for 150 mi with afterburner (not a supercruiser) | | | W |

**F135 engine physical data** (for modelling the nozzle): length 220 in (5.59 m), inlet diameter 43 in, max diameter 46 in. [P&W product card](https://app.prattwhitney.com/download/F135-CTOL.pdf). P&W 2025 fast facts describe it as "40K+ lbs of thrust" and note more than 1,300 engines delivered. [P&W F135 Fast Facts 2025](https://prd-sc102-cdn.rtx.com/-/media/pw/newsroom/collateral/documents/military-engines/f135-fast-facts.pdf?rev=5a276abd05104503bb04f543ab7fe6d3&hash=753197D3B93A8F2F690F6BD3EBAC0282)

**Game-balance recommendation.** For the flight model, use 43,000 lbf AB / 28,000 lbf mil (P&W's current rating), F-35C empty weight 34,581 lb and 19,200–19,750 lb internal fuel. Mission gross weight comes out around 55–60k lb with an internal load **[derived]**.

### 1.3 Weapons: internal bays, external "beast mode", loadouts

All from the Wikipedia Armament section, which cites Keijsper 2007, the JSF Program brief and GD/Terma: [link](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Armament)

- **Two internal bays, 4 stations total.**
  - Outboard bay station: up to **2,500 lb** (F-35B: 1,500 lb). Carries air-to-ground weapons: JDAM, Paveway, JSOW, WCMD. It can also take multiple small munitions: **up to 4 × GBU-39 SDB or GBU-53/B StormBreaker per station on the A/C** (3 on the B).
  - Inboard bay station: air-to-air, AIM-120 AMRAAM (later AIM-260 JATM).
  - Typical stealth load: **2 × 2,000-lb class bombs + 2 × AIM-120**.
- **Six external wing stations ("beast mode")** for missions that don't need stealth.
  - Wingtip pylons for AIM-9X / ASRAAM, canted outward for RCS.
  - Each wing has a 5,000 lb inboard station and a 2,500 lb middle station (1,500 lb on the B).
  - Example loads: 8 × AIM-120 + 2 × AIM-9X, or 6 × 2,000-lb bombs + 2 × AIM-120 + 2 × AIM-9X.
- **Gun: GAU-22/A, 25 mm, four-barrel rotary.**
  - F-35A: internal, near the left wing root, 182 rounds (Wikipedia's specs box says 180).
  - F-35B/C: no internal gun. They use a **centerline Terma multi-mission pod with 220 rounds**, shaped for low RCS.
- **Countermeasures:** flares, chaff and towed decoys sit in two compartments behind the weapons bays.

**Munitions relevant to the game**

| Weapon | Key numbers | Source |
|---|---|---|
| **GBU-31 JDAM** | GPS/INS kit on 2,000-lb Mk 84 (v1/v2) or **BLU-109 penetrator (v3 USAF / v4 USN)**; "published range up to 15 nmi"; demonstrated CEP ≈ 11 m in early tests | [Wikipedia JDAM](https://en.wikipedia.org/wiki/Joint_Direct_Attack_Munition) |
| **GBU-32 JDAM** | Same kit on 1,000-lb Mk 83 | same |
| **GBU-12 Paveway II** | Laser-guided 500-lb Mk 82 | [Wikipedia Paveway](https://en.wikipedia.org/wiki/Paveway) |
| **GBU-53/B StormBreaker (SDB II)** | 204 lb, 69 in long, tri-mode seeker (mmW radar / semi-active laser / imaging IR) + GPS/INS + datalink; range 60 nmi (40 nmi vs moving targets) | [Wikipedia GBU-53/B](https://en.wikipedia.org/wiki/GBU-53/B_StormBreaker) |
| **BLU-109** | 2,000-lb penetrator bomb body, 550 lb Tritonal, 7 ft 11 in long, 14.6 in dia; designed to penetrate concrete shelters | [Wikipedia BLU-109](https://en.wikipedia.org/wiki/BLU-109_bomb) |
| **GBU-28** | 4,000–5,000 lb class laser-guided bunker buster, 5.82 m long; penetrated >50 m of earth or 5 m of concrete in tests. **Not an F-35 weapon** (F-15E, B-2). Reference only | [Wikipedia GBU-28](https://en.wikipedia.org/wiki/GBU-28) |
| **AIM-120 AMRAAM** | 3.65 m, Mach 4, INS + datalink midcourse + active radar terminal; range 75 km (A/B), 90 km (C), 130–160 km (D) | [Wikipedia AIM-120](https://en.wikipedia.org/wiki/AIM-120_AMRAAM) |
| **AIM-9X Sidewinder** | 188 lb, 9 ft 11 in, imaging IR 128×128 FPA, Mach 2.5+ | [Wikipedia AIM-9](https://en.wikipedia.org/wiki/AIM-9_Sidewinder) |

**Stealth vs beast mode as a game mechanic.** Internal-only loads keep a low RCS. Anything on the wing pylons shows up on enemy radar. Wikipedia and [19FortyFive (Sep 2026)](https://www.19fortyfive.com/2026/09/the-f-35-carries-5700-pounds-of-weapons-inside-and-stays-nearly-invisible-go-beast-mode-and-hang-22000-pounds-on-the-outside-and-enemy-radar-will-find-it-which-is-exactly-what-pilots/) frame beast mode as a choice between payload and detectability. That trade maps cleanly onto a loadout screen: more bombs means bigger detection radii.

---

## 2. Cockpit, HMD, DAS, HOTAS, voice

### Panoramic cockpit display (PCD)
- **One 20 × 8 in (50 × 20 cm) touchscreen** covers the whole panel. It shows flight instruments, stores management, CNI (comms/nav/ID) and integrated caution/warnings, and the pilot can rearrange the layout.
- A **small standby display** sits below it.
- The cockpit has **speech recognition** (Adacel).
- Controls are a **right-hand side-stick + left throttle (HOTAS)**.
- Martin-Baker US16E ejection seat; one-piece tinted canopy hinged at the front.
- Source: [Wikipedia F-35 (Cockpit)](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Cockpit)
- The **Technology Refresh 3 (TR-3)** upgrade brings a new core processor and a new cockpit display (Lot 15+). Same source.

### No HUD: Gen III helmet-mounted display system (HMDS)
- The F-35 has **no head-up display**. All flight and combat symbology is projected on the visor, so it follows the pilot's head. [Wikipedia](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Cockpit)
- **Biocular 40 × 30° field of view** with integrated digital night vision. Made by Collins Elbit Vision Systems (CEVS). The 3,000th helmet was delivered in Feb 2024. [Collins Aerospace F-35 Gen III HMDS](https://www.rtx.com/collinsaerospace/what-we-do/industries/military-and-defense/displays-and-controls/airborne/helmet-mounted-displays/f-35-gen-iii-helmet-mounted-display-system), [Collins datasheet PDF](https://pbrproductions.com/safe2020/client_booths/collins_aerospace/assets/f35geniiihelmetmounteddisplaydatasheet.pdf), [RTX news 3,000th HMDS](https://www.rtx.com/news/news-center/2024/02/26/collins-elbit-vision-systems-delivers-3-000th-f-35-gen-iii-helmet-mounted-display), [Elbit Systems JSF HMDS](https://www.elbitsystems.com/air-space/aircraft-systems/helmet-mounted-display/joint-strike-fighter-f-35)
- Each helmet costs about $400,000. It supports high off-boresight missile cueing. [Wikipedia](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Cockpit)
- **Game implication:** render the "HUD" as **head-locked symbology** (visor layer) plus a few **world-stabilized cues**: velocity vector, horizon line, target boxes. Don't build a fixed glass HUD combiner in the cockpit model. It's accurate, and it's easier to render.

### DAS (AN/AAQ-37 Distributed Aperture System)
- **Six IR sensors** around the airframe give **spherical 360° coverage**. They provide missile launch detection and tracking, aircraft IRST and night imagery on the visor. The pilot can **see through the floor** of the aircraft.
- Northrop Grumman designed it; Raytheon has produced it since 2018.
- Sources: [RTX/Raytheon EODAS](https://www.rtx.com/raytheon/what-we-do/air/eodas), [Wikipedia AN/AAQ-37](https://en.wikipedia.org/wiki/AN/AAQ-37_Distributed_Aperture_System)
- **Game mechanic idea:** a "DAS view" toggle. A grayscale IR post-process shader, with the cockpit mesh hidden or faded, shows the terrain below. It's cheap to render and gives a strong low-level-flying payoff.

### Other mission systems
- **APG-81 AESA radar:** air-to-air, SAR and strike modes; track-while-scan beyond 80 nmi.
- **AN/ASQ-239 Barracuda EW:** 10 RF antennas in the wing and tail edges give an all-aspect RWR. It geolocates threats and can jam.
- **AAQ-40 EOTS:** laser designator, FLIR and IRST under the nose.
- Source: [Wikipedia Sensors and avionics](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Sensors_and_avionics)
- **For the game:** the F-35 can **self-designate with EOTS**, so it doesn't need a buddy-lasing partner (compare the film's F/A-18 plan in section 9).

### HMD symbology images
There are no official high-resolution symbology diagrams in the public domain. Best leads:
- DVIDS photo and video search for "F-35 helmet": https://www.dvidshub.net/search/?q=F-35+helmet+mounted+display
- Collins/Elbit product pages above (marketing renders; copyrighted, reference only)
- Wikimedia Commons: [Category: Lockheed Martin F-35 Lightning II](https://commons.wikimedia.org/wiki/Category:Lockheed_Martin_F-35_Lightning_II) (88 files, 22 subcategories)

### Voice warnings ("Bitching Betty")
- Fighter voice warning systems (VWS) traditionally use a calm female voice ("Pull up… pull up"). [Wikipedia Voice warning system](https://en.wikipedia.org/wiki/Voice_warning_system), [Hush-Kit: F-15 pilot on "Bitchin' Betty"](https://hushkit.net/2018/02/05/f-15-pilot-shares-the-history-of-bitchin-betty/)
- **The F-35's exact VWS phrase list is not publicly documented** (no primary source found).
- **Game recommendation:** record your own generic phrases ("PULL UP", "ALTITUDE", "BINGO FUEL", "MISSILE, MISSILE", "COUNTERMEASURES LOW", "SAM LAUNCH"). Don't sample recordings from films or other games.

---

## 3. Flight-model specifics

| Topic | Fact | Source |
|---|---|---|
| Control laws | Triplex-redundant fly-by-wire, relaxed stability, departure-resistant; trimmed AoA capability **50°** | [Wikipedia Performance](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Performance) |
| g-limits | A +9.0, B +7.0, C +7.5 | LM sheets (section 1) |
| Control surfaces | Leading-edge flaps, flaperons, canted twin rudders, all-moving stabilators; **F-35C adds ailerons on the folding wing sections** | [Wikipedia Design](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Overview) |
| Speedbrake | **No dedicated speedbrake.** Flight-control software decelerates the jet by deflecting control surfaces: rudders toe **in**, plus leading- and trailing-edge flaps. Buffet and decel rates are reported as similar to the F-16's speedbrake | [Code One Magazine "F-35 Flight Tests"](https://www.codeonemagazine.com/article.html?item_id=33) via search summary **[secondary]** |
| Carrier approach (IDLC) | **Integrated Direct Lift Control** moves the trailing-edge flaps up and down to change lift directly. Glideslope response is immediate, with no speed or AoA change. Flaps sit at a nominal **15° TED "half flap"** so they can move both ways. The pilot engages **auto-throttle** and flies glidepath and lineup **with the stick only** | [Defense Media Network: F-35C IDLC](https://www.defensemedianetwork.com/stories/f-35c-integrated-direct-lift-control-how-it-works/) |
| Delta Flight Path (DFP) | F-35C-only mode. It switches the pitch-axis control law from **pitch-rate command to glideslope command**, and the jet holds the glideslope automatically. Trials on USS *George Washington* had **no bolters** and no wave-offs attributed to aircraft performance. The LSO suggested FCLPs could drop from 16–18 to 4–6 and CQ traps from 10 to 6 | [Military.com 2016](https://www.military.com/daily-news/2016/08/17/f-35s-new-landing-technology-may-simplify-carrier-operations.html), [FlightGlobal](https://www.flightglobal.com/us-navy-makes-f-35c-carrier-qualification-push/121511.article) |
| MAGIC CARPET | "Maritime Augmented Guidance with Integrated Controls for Carrier Approach and Recovery Precision Enabling Technologies." The Super Hornet equivalent of DFP, by the same engineer. Direct-lift control on the throttle plus a flight-path command law | [SLDinfo 2016](https://sldinfo.com/2016/08/navair-magic-carpet-innovation-for-the-f-18-fleet/), [AIAA paper PDF](https://virtualsim.nuaa.edu.cn/file/up_document/2021/05/IdeYAqCc4Ki6TWPE.pdf) |
| Approach speed / AoA | F-35C on-speed **AoA 12.3° at ~135–140 KCAS**. Other reports say ~130–135 kt. The spec threshold is 145 kt with 15 kt wind over deck at required landing weight | **[secondary/unverified]**: [f-16.net forum](https://www.f-16.net/forum/viewtopic.php?t=13450), [GlobalSecurity F-35C](https://www.globalsecurity.org/military/systems/aircraft/f-35c.htm) |
| Afterburner heat limits | Prolonged afterburner use damaged horizontal tails on the B/C in flutter tests. The program now imposes a **time limit on high-speed flight for the B and C** | [Wikipedia Testing](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Testing) |
| Vapor cones | Condensation "shock collar" in **transonic** flight (moist air, expansion fans and a terminating shock). It isn't strictly tied to Mach 1, so trigger it roughly at M 0.85–1.05 and when humidity is high, especially at low altitude over the sea | [Wikipedia Vapor cone](https://en.wikipedia.org/wiki/Vapor_cone) |

**Sim design translation (my recommendations, not sourced facts)**
- **"Carrier Approach Mode" (DFP-like):** stick pitch commands glideslope change, auto-throttle holds on-speed AoA, and the pilot only corrects lineup. This is realistic for the F-35C and makes carrier landings learnable in a co-op game.
- **G-limiter:** 7.5 g on the C, plus an AoA limiter near 50° that the pilot can't override. The real FBW is carefree, so a "stall-spin" model isn't necessary.
- **Nozzle:** the F135 has a **fixed-geometry-look axisymmetric low-observable nozzle with serrated petal edges**. In AB, show orange/blue shock diamonds. **[observational: DVIDS photos]**

---

## 4. Aircraft carrier

### 4.1 Recommendation: Nimitz-class, *Theodore Roosevelt* sub-class (e.g., USS *Abraham Lincoln* CVN-72)

**Reasons**
1. **F-35Cs have deployed operationally on Nimitz-class ships:** *Carl Vinson* 2021 onward, *Abraham Lincoln* and *George Washington* for test and CQ. That gives abundant DVIDS imagery of F-35Cs on these decks. [Wikipedia F-35 (US Navy)](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#United_States)
2. ***Top Gun: Maverick* was filmed on USS *Abraham Lincoln* (CVN-72) and USS *Theodore Roosevelt* (CVN-71)**, both Nimitz-class (section 9).
3. **DCS: Supercarrier** models exactly this sub-class, CVN-71 to CVN-75, which gives a ready benchmark (section 7).
4. Free CC-BY Nimitz models exist on Sketchfab (section 8).

**Ford class** (CVN-78) is the "modern tech" alternative: EMALS, AAG, a smaller island moved about 140 ft aft, 3 elevators. Choose it if you want the newest look. Visually the two are near-identical at gameplay distance except for the island.

### 4.2 Dimensions and layout

| | Nimitz class (CVN-68–77) | Gerald R. Ford class (CVN-78+) |
|---|---|---|
| Length | 1,092 ft (332.8 m) overall; 1,040 ft waterline | 1,092–1,106 ft (333–337 m) |
| Flight deck width | 252 ft (76.8 m) | 256 ft (flight deck) |
| Waterline beam | 134 ft | 134 ft |
| Draft | 37 ft nav. / 41 ft limit | 39 ft |
| Displacement | 100,000–104,600 LT full load | ~100,000 LT |
| Speed | 30+ kn | "in excess of 30 kn" |
| Air wing | ~64 aircraft typical (up to 85–90) | 75+ (up to 90) |
| Catapults | **4 steam** (2 bow, 2 waist) | **4 EMALS** |
| Arresting wires | **4** (CVN-68–75); **3** on *Ronald Reagan* & *George H.W. Bush* | **3** (AAG) |
| Aircraft elevators | 4 | **3** (2 starboard forward of island, 1 port) |
| Island | Starboard, mid-ship | Smaller, ~140 ft (42.7 m) further aft & outboard |
| Angled deck | 9° | similar |
| Sortie rate | 120/day sustained, 240 surge | design 160/day, 270 surge |

Sources:
- [Wikipedia Nimitz class](https://en.wikipedia.org/wiki/Nimitz-class_aircraft_carrier) (infobox templates: length/beam/draught/complement/aircraft)
- [Wikipedia Ford class](https://en.wikipedia.org/wiki/Gerald_R._Ford-class_aircraft_carrier)
- [Wikipedia Arresting gear](https://en.wikipedia.org/wiki/Arresting_gear)
- [Wikipedia EMALS](https://en.wikipedia.org/wiki/Electromagnetic_Aircraft_Launch_System) (four catapults on Ford)
- Ford elevators/island: [STRASAM elevator article](https://strasam.org/en/defense/naval-weapons-and-systems/vertical-logistics-on-floating-fortresses-the-elevator-systems-of-nimitz-and-ford-class-aircraft-carriers-4178), [TWZ: why Ford's island is so far back](https://www.twz.com/sea/why-the-ford-class-carriers-island-superstructure-is-located-so-far-back)
- Official Navy fact file (blocks bots, open in a browser): [navy.mil Aircraft Carriers CVN](https://www.navy.mil/Resources/Fact-Files/Display-FactFiles/Article/2169795/aircraft-carriers-cvn/)

**Landing area:** only **120 ft wide**. It has painted "ladder lines", a centerline and a **drop line of lights** hanging off the stern for lineup. [Wikipedia Modern US Navy carrier air operations](https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations)

**Arresting wires (cross-deck pendants)**
- Numbered 1–4 from aft to forward.
- Steel wire rope, 1 to 1-3/8 in diameter, raised on leaf-spring supports.
- Replaced every 125 traps.
- **Pilots aim for the 3-wire (4-wire deck) or the 2-wire (3-wire deck).**
- Source: [Wikipedia Arresting gear](https://en.wikipedia.org/wiki/Arresting_gear)

### 4.3 Catapults: steam vs EMALS
- **Steam:** uses about 1,350 lb of steam per launch, has no feedback control and is 4–6% efficient.
- **EMALS:** a linear induction motor with a **300 ft** stroke. It can take a **100,000 lb aircraft to 130 kt**. Energy is stored in 4 disk alternators (up to 484 MJ) released in 2–3 s, with a **45 s recharge**. The closed loop gives constant tow force. The first EMALS launch from *Ford* was 28 July 2017.
- Source: [Wikipedia EMALS](https://en.wikipedia.org/wiki/Electromagnetic_Aircraft_Launch_System)
- **Launch profile:** 0 → ~150 kt in about 2 s. [Wikipedia carrier air ops](https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations)
  - **[derived]** 77 m/s in 2 s ≈ 38.6 m/s² ≈ **3.9 g average**.
  - **[derived]** EMALS 130 kt over 300 ft ≈ 24.5 m/s² ≈ 2.5 g.
  - Use **3–4 g for 2–2.5 s** with a camera shake that ramps then cuts at the end of the stroke.

### 4.4 Advanced Arresting Gear (Ford)
- General Atomics design. Rotary water "twisters" coupled to an induction motor give finely controlled arresting force.
- It replaces the Mk 7 hydraulic gear on the Nimitz class.
- Source: [Wikipedia AAG](https://en.wikipedia.org/wiki/Advanced_Arresting_Gear)
- **Trap:** stop in about **2 s** from approach speed. [Wikipedia carrier air ops](https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations)
  - **[derived]** ~135 kt → 0 in ~2 s ≈ 3.5 g average.

### 4.5 Jet blast deflector (JBD)
Hydraulically raised, actively cooled panels behind each catapult. They rise **as the aircraft taxis onto the catapult**. [Wikipedia JBD](https://en.wikipedia.org/wiki/Jet_blast_deflector), [carrier air ops](https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations)

### 4.6 Deck crew jersey colors

Source: [Wikipedia template "US aircraft carrier jack colors"](https://en.wikipedia.org/wiki/Template:US_aircraft_carrier_jack_colors), which cites navy.mil "Rainbow wardrobe".

| Jersey | Roles |
|---|---|
| **Yellow** | Aircraft handling officer, catapult & arresting-gear officers ("shooter"), **plane directors** (use lighted yellow wands at night) |
| **Green** | Catapult & arresting-gear crew, visual landing aid electricians, air-wing maintainers, hook runner, photographers, helo LSE |
| **Red** | Ordnance handlers, crash & salvage, EOD, firefighters |
| **Purple** ("grapes") | Aviation fuel |
| **Blue** | Trainee plane handlers, chocks & chains, elevator operators, tractor drivers, messengers/phone talkers |
| **Brown** | Plane captains, air-wing line leading petty officers |
| **White** | QA, squadron plane inspectors, **LSO**, LOX crew, safety observers, medical (with red cross) |

Pants color shows rank: navy-blue trousers for junior sailors, khaki for chiefs and officers. [carrier air ops](https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations)

### 4.7 Launch sequence
Source: [Wikipedia carrier air ops, Launch operations](https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations#Launch_operations)

**Pre-launch timeline:**
- T-45 min: walk-around and man up.
- T-30 min: engines start.
- T-15 min: taxi to the catapults.
- The ship turns into the wind.

**On the catapult:**
1. Taxi onto the cat, wings spread, **JBD rises**. Final checkers inspect; ordnance is armed.
2. **Launch bar** goes into the shuttle and the **holdback** attaches.
3. The catapult is put in **tension**. The pilot gets the signal for **full/military power** and releases the brakes.
4. The pilot checks engines and **wipes out the controls** (full deflection check).
5. The **pilot salutes the shooter** (at night: turns exterior lights on).
6. The checkers give a thumbs up. The **shooter** checks settings and wind, then signals launch (the classic touch-deck-and-point-forward motion **[observational, DVIDS video]**).
7. The operator fires. The holdback breaks, **0 → ~150 kt in about 2 s**.

**After launch (Case I):**
- Clearing turn of about 10°: right off the bow cats, left off the waist cats.
- Straight ahead at **500 ft until 7 nmi**, then an unrestricted climb.
- Case III (night or weather): 30 s launch interval, then a 10-nmi arc.

### 4.8 Recovery sequence
Source: [Wikipedia carrier air ops, Recovery operations](https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations#Recovery_operations)

**Case I (day, VFR):**
1. **Hold:** left-hand circle over the ship, max 5 nmi diameter, minimum altitude 2,000 ft, stacks separated by 1,000 ft.
2. **Initial:** 3 nmi astern at **800 ft**.
3. **Break:** a level 180° turn at 800 ft, descending to **600 ft downwind**. Gear, flaps and hook go down.
4. **"The 180":** abeam the landing area, about 1.1–1.3 nmi from the ship.
5. **"The 90":** at 450 ft.
6. **Wake crossing:** about 370 ft. The pilot acquires the **ball**.
7. Aircraft are spaced **50–60 s apart**.

**Case III (night or weather):**
- Marshal holding stack, 6-min racetrack.
- Descend at 250 kt and 4,000 ft/min, reducing to 2,000 ft/min below 5,000 ft.
- Dirty up at 10 nmi.
- **6 nmi at 1,200 ft and 150 kt**.
- **3–4° glideslope from 3 nmi (~700 ft/min)**.
- **"Call the ball" at 3/4 nmi / ~360–400 ft**; the LSO answers "Roger ball".
- Fallback distance/altitude checkpoints: **1,200 ft @ 3 nmi, 860 ft @ 2 nmi, 460 ft @ 1 nmi, 360 ft at the ball call**.

**Approach aids:**
- **ICLS "bullseye"**, ACLS needles (Mode I coupled to touchdown, Mode IA to 3/4 nmi, Mode II needles).
- **Long-range Laser Lineup System (LLS)**, usable out to 10 nmi.

**Landing:**
- The landing heading is about 9–10° left of the ship's heading because of the angled deck, and the ship keeps moving, so the pilot makes continuous small right corrections.
- **On touchdown, advance to MIL power for about 3 s** in case of a bolter.
- A **bolter** (missing every wire) or a **wave-off** means climbing straight ahead to 1,200 ft into the bolter/wave-off pattern.

**IFLOLS "meatball"** ([Wikipedia Optical landing system](https://en.wikipedia.org/wiki/Optical_landing_system)):
- Horizontal **green datum lights** with a vertical column that shows the **ball** (amber). Ball above datum = high; below = low.
- The ball turns **red when dangerously low**.
- **Flashing red wave-off lights** are a mandatory go-around.
- **Green cut lights** mean "cleared" (a 2–3 s flash in zip-lip ops) or "add power" (longer flashes).
- The LSO works the lights with a hand-held **"pickle"**. LSOs hold the pickle overhead while the deck is fouled.
- IFLOLS has been on every deploying carrier since 2004, with fiber-optic source lights and deck-motion stabilization. **MOVLAS** is the manual backup.

**LSO:** an experienced pilot on the port-side aft platform. [Wikipedia LSO](https://en.wikipedia.org/wiki/Landing_signal_officer)
- **LSO grades for a debrief screen:** OK / Fair / No-grade / Cut / Bolter / Wave-off. Standard terminology, not verified against a primary NATOPS source here. **[secondary]**

**Carrier qualification counts** (for a progression system): Initial CQ is 12 day (10 arrested) + 8 night (6 arrested). Requal is 6 day + 4 night. [carrier air ops](https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations#Carrier_qualifications)

### 4.9 Carrier strike group escorts
Source: [Wikipedia Carrier strike group](https://en.wikipedia.org/wiki/Carrier_strike_group)

**Typical group:** about 7,500 personnel, a carrier, at least 1 cruiser, a DESRON of 2–3 destroyers, and an air wing of 65–70 aircraft. It sometimes adds up to 2 attack submarines and a supply ship.

**Arleigh Burke DDG:**
- 505 ft long (Flight I/II), 66 ft beam, 8,300–9,500 LT, 30+ kn.
- Aegis, air/ASW defense, carries **Tomahawks**.
- [Wikipedia](https://en.wikipedia.org/wiki/Arleigh_Burke-class_destroyer)

**Ticonderoga CG:**
- **Being retired.** 7 were active as of Sept 2025, with all due out by about 2030.
- For a 2026+ setting, use **2–3 Burkes**, one of them a Flight III, instead of a cruiser.
- [Wikipedia](https://en.wikipedia.org/wiki/Ticonderoga-class_cruiser)

---

## 5. Mission realism

### 5.1 SEAD / DEAD
- **SEAD** suppresses enemy air defenses by destruction, jamming or deception. In the first week of a war, SEAD can be up to 30% of sorties.
- **DEAD** means physical destruction of the defenses.
- Classic kit: EA-18G Growler + AGM-88 HARM. F-35 Block 4 adds the AGM-88G AARGM-ER.
- Sources: [Wikipedia SEAD](https://en.wikipedia.org/wiki/Suppression_of_enemy_air_defenses), [Wikipedia F-35 specs box](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II)
- **Co-op role idea:** one player flies SEAD (anti-radiation missiles, jamming) while the others carry bombs.

### 5.2 Threat systems to model

| System | Type | Key engagement numbers | Game role | Source |
|---|---|---|---|---|
| **S-300PMU-2 / S-300V** (SA-20 / SA-12) | Long-range SAM | PMU-2 up to 195 km (48N6E2); PMU 5V55R 75–90 km. Original TVM guidance struggled with targets below ~500 m, which pushed SEAD aircraft to terrain masking | "Umbrella" threat, forces NOE | [Wikipedia S-300](https://en.wikipedia.org/wiki/S-300_missile_system) |
| **S-400** (SA-21) | Long-range SAM | 400 km (40N6E), 250 km (48N6DM), 40 km (9M96). 91N6E/92N6E radars ~340 km; Nebo-M 400 km | Strategic layer, avoid entirely | [Wikipedia S-400](https://en.wikipedia.org/wiki/S-400_missile_system) |
| **Pantsir-S1** (SA-22) | Short-range gun+missile | Continuous zone from **5 m to 15 km altitude, 200 m to 20 km range**; 2 × 2A38M 30 mm, 2,500 rds/min per gun, 4 km gun range, engages down to 0 m AGL; Pantsir-SM 40 km | Point defense of the target, the "last 20 km" | [Wikipedia Pantsir](https://en.wikipedia.org/wiki/Pantsir_missile_system) |
| **Tor-M1/M2** (SA-15) | Short-range SAM | Engagement up to 12 km, altitude 6 m–10 km (M1); detection radar 25 km; 8 missiles | Mobile SHORAD escort | [Wikipedia Tor](https://en.wikipedia.org/wiki/Tor_missile_system) |
| **ZSU-23-4 Shilka** | Radar AAA | 4 × 23 mm, **3,400–4,000 rds/min combined**, 2,000 rounds (30–35 s of fire); Gun Dish (RPK-2) Ku-band radar ~20 km detection; effective ~2,500 m range / 1,500 m altitude (upgrade figure) | Canyon-mouth AAA, punishes low flight | [Wikipedia ZSU-23-4](https://en.wikipedia.org/wiki/ZSU-23-4_Shilka) |
| **9K38 Igla / Igla-S** | MANPADS (IR) | 5.2–6 km range, 3.5 km ceiling, Mach 1.9 peak | Pop-up IR threat, flares | [Wikipedia Igla](https://en.wikipedia.org/wiki/9K38_Igla) |

**Design note (Wikipedia Shilka):** in 1973, Israeli pilots who flew low to avoid SA-6s were shot down by ZSU-23-4s. That is the classic reason low flying must also be dangerous, and it's the core risk/reward loop for this game.

### 5.3 Radar horizon and terrain masking

From the [Wikipedia Radar horizon](https://en.wikipedia.org/wiki/Radar_horizon) article.

**Geometric horizon:** `D = sqrt(2·H·Re)`

**With standard refraction**, using the 4/3 Earth radius (Re_eff ≈ 8,500 km):
```
D_radar(km) ≈ 4.12 · √h_radar(m)
Detection line-of-sight (smooth Earth):  D(km) ≈ 4.12 · (√h_radar + √h_target)   [h in metres]
Shadow condition beyond radar horizon: target hidden if  H_T < (R_T − √(2·H·Re))² / (2·Re)
```

The article's worked example: a radar at 75 ft (23 m) has a horizon of about 12 mi (19 km) with refraction.

**[derived]** Smooth-sea detection ranges for a radar mast at **10 m**:

| Target altitude | LOS range |
|---|---|
| 15 m (≈50 ft) | 29 km |
| 30 m (≈100 ft) | 36 km |
| 150 m (500 ft) | 64 km |
| 1,000 m | 143 km |
| 3,000 m | 239 km |

**Clutter zone:** extends to about 120% of the radar horizon. MTI gives about 35 dB of clutter rejection. **Pulse-Doppler radar with speed rejection has no clutter zone**, so modern radars can see low targets **if line-of-sight exists**. That means **terrain masking (ridges and canyons) is what really hides you**, not just altitude. [Wikipedia Radar horizon](https://en.wikipedia.org/wiki/Radar_horizon), [Wikipedia Nap-of-the-earth](https://en.wikipedia.org/wiki/Nap-of-the-earth)

**Implementation (recommendation):**
- For each SAM radar, raycast from the antenna to the aircraft against the terrain heightfield every N frames.
- Detection probability = f(LOS, range, aircraft RCS for internal vs beast-mode loads, clutter factor when AGL is low and the target is slow).
- This is cheap on an RTX 3050 if done on a coarse heightmap on the CPU.

### 5.4 Hardened bunkers and bunker busters
- **BLU-109 in a GBU-31(V)3/4 JDAM:** a 2,000-lb penetrator, **the realistic F-35 internal bunker weapon**. It's the one to use in-game. [Wikipedia JDAM](https://en.wikipedia.org/wiki/Joint_Direct_Attack_Munition), [BLU-109](https://en.wikipedia.org/wiki/BLU-109_bomb)
- **GBU-28** (4–5k lb, >50 m earth / 5 m concrete in tests) and the **GBU-57 MOP** (30,000 lb, B-2 only) are **not F-35 weapons**. [GBU-28](https://en.wikipedia.org/wiki/GBU-28)
- **Real deep-bunker precedent, Operation Midnight Hammer (22 June 2025):**
  - **More than two dozen Tomahawks** from a submarine hit Isfahan and air defenses.
  - Then **7 B-2s dropped 14 GBU-57 MOPs** on Fordow and Natanz.
  - It was the first combat use of the MOP.
  - Sources: [TWZ](https://www.twz.com/air/b-2-strikes-on-iran-what-we-know-about-operation-midnight-hammer), [USAF Doctrine Paragon PDF](https://www.doctrine.af.mil/Portals/61/Jul-25-Doctrine%20Paragon-Operation%20Midnight%20Hammer.pdf), [CSIS](https://www.csis.org/analysis/what-operation-midnight-hammer-means-future-irans-nuclear-ambitions)
  - A good real-world anchor for "cruise missiles first, then penetrators on the bunker".
- **Game design:** make the bunker invulnerable to non-penetrators. Make it require **a hit on a specific weak point** (vent or door), or 2 sequential penetrator hits, as in the film (section 9).

### 5.5 Enemy fighters (Wikipedia specs; the J-20 figures are Western estimates)

| Aircraft | Length / span | Empty / MTOW | Engines (dry / AB each) | Max speed | Ceiling | g | Gun | Source |
|---|---|---|---|---|---|---|---|---|
| **Su-35S** "Flanker-E" | 21.9 / 15.3 m | 19,000 / 34,500 kg | 2 × AL-41F1S 86.3 / 137.3 kN | Mach 2.25 | 18,000 m | +9 | GSh-30-1, 150 rds | [Wikipedia](https://en.wikipedia.org/wiki/Sukhoi_Su-35) |
| **Su-57** "Felon" | 20.1 / 14.1 m | 18,500 / 34,000 kg | 2 × AL-41F-1 88.3 / 142.2 kN | Mach 2.0 | 18,800 m | +9 | GSh-30-1 | [Wikipedia](https://en.wikipedia.org/wiki/Sukhoi_Su-57) |
| **MiG-29** "Fulcrum" | 17.32 / 11.36 m | 11,000 / 18,000 kg | 2 × RD-33 49.4 / 81.6 kN | Mach 2.3+ | 18,000 m | +9 | GSh-30-1, 100–150 rds | [Wikipedia](https://en.wikipedia.org/wiki/Mikoyan_MiG-29) |
| **J-20** "Mighty Dragon" | 21.2 / 13.01 m | 17,000 / 37,000 kg **[estimate]** | 2 × WS-10C, 142–147 kN AB **[estimate]** | Mach 2.0 | 20,000 m | +9/−3 | none | [Wikipedia](https://en.wikipedia.org/wiki/Chengdu_J-20) |

### 5.6 Countermeasures and RWR
- **F-35:** flares, chaff and towed decoys sit in compartments behind the weapons bays. The **ASQ-239** gives all-aspect RWR and jamming. [Wikipedia F-35](https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II#Armament)
- **RWR behavior** ([Wikipedia RWR](https://en.wikipedia.org/wiki/Radar_warning_receiver)):
  - A circular display, with symbols placed by bearing and threat severity (tracking radars nearer the center).
  - **Audible tones escalate** from search, to track/lock, to **missile guidance/launch**, when the radar changes mode to guide a missile ("more insistent warning tones and flashing, bracketed symbols").
- **Game audio (recommendation):**
  - Distinct low-rate chirp for search radar.
  - Continuous tone for lock.
  - Fast warble plus voice "MISSILE LAUNCH" for a launch, with the direction on the helmet display.
  - Model **IR missiles (Igla, AIM-9-class) as silent** until DAS detects the launch plume. That's realistic, since the RWR can't see IR, and it's why the F-35's DAS matters.

---

## 6. Visual and audio reference library

### 6.1 Licensing baseline
DVIDS usage policy ([dvidshub.net/about/copyright](https://www.dvidshub.net/about/copyright)):
- **US government (DoD) visual information made by employees as official work is generally not copyrighted** in the US.
- There are three conditions:
  1. **No implied endorsement.** Show the disclaimer: *"The appearance of U.S. Department of War (DoW) visual information does not imply or constitute DoW endorsement."* (older wording: "Department of Defense (DoD)").
  2. **Military names, insignia, seals and symbols are trademarks.** Commercial use needs written permission, and commercial users should **obscure distinctive markings (tail and hull numbers, unit insignia, service name)**.
  3. **No waiver of privacy or publicity rights** for identifiable people. Some DVIDS items are third-party copyrighted.

Similar notices: [Navy "Public Use Notice of Limitations"](https://www.nepa.navy.mil/About-NEPA-Website/Media-Resources/Public-Use-Notice-of-Limitations/), [DoD Trademark Licensing Guide PDF](https://www.trademark.marines.mil/Portals/161/Docs/DOD%20Trademark%20Licensing%20Guide-16%20July%202017_1.pdf?ver=QF2mfBCE3qjMYOnigDCkGg%3D%3D)

**Practical rule for textures:** photographic *reference* is fine. For decals, invent squadron names and badges, and don't copy real unit insignia or the Navy seal.

### 6.2 Photo and video sources (reference and texturing)

| What | Where |
|---|---|
| F-35C catapult launches / traps on *Abraham Lincoln* | DVIDS: [F-35C launches from Lincoln](https://www.dvidshub.net/image/6931737/f-35c-launches-lincoln), [Lincoln flight ops 1](https://www.dvidshub.net/image/7557654/lincoln-abraham-conducts-flight-operations), [flight ops 2](https://www.dvidshub.net/image/8145723/abraham-lincoln-conducts-flight-operations), [flight ops 3](https://www.dvidshub.net/image/7086218/abraham-lincoln-conducts-flight-operations) |
| First F-35C arrested landing (USS *Nimitz*, 2014), video | [DVIDS video 948236](https://www.dvidshub.net/video/948236/first-f-35c-arrested-landing-blast-past); [Wikimedia webm](https://commons.wikimedia.org/wiki/File:F-35C_First_Carrier_Landing_1.webm) |
| F-35C integrated air wing test on *Lincoln* (video) | [USNI News 2018](https://news.usni.org/2018/08/27/f-35cs-operating-first-joint-strike-fighter-integrated-air-wing-test-aboard-uss-abraham-lincoln) |
| DVIDS search (all public-domain-ish) | https://www.dvidshub.net/search/?q=F-35C&filter%5Btype%5D=video · https://www.dvidshub.net/search/?q=flight+deck+catapult · https://www.dvidshub.net/search/?q=landing+signal+officer · https://www.dvidshub.net/search/?q=F-35+cockpit |
| Wikimedia Commons | [Lockheed Martin F-35 Lightning II](https://commons.wikimedia.org/wiki/Category:Lockheed_Martin_F-35_Lightning_II) · [USS Nimitz (CVN-68)](https://commons.wikimedia.org/wiki/Category:USS_Nimitz_(CVN-68)) (~2,800 files) · [USS Gerald R. Ford (CVN-78)](https://commons.wikimedia.org/wiki/Category:USS_Gerald_R._Ford_(CVN-78)) · [Optical landing systems](https://commons.wikimedia.org/wiki/Category:Optical_landing_systems) · [Aircraft catapults](https://commons.wikimedia.org/wiki/Category:Aircraft_catapults) · [Pratt & Whitney F135](https://commons.wikimedia.org/wiki/Category:Pratt_%26_Whitney_F135) |
| Deck-crew jersey photos (captioned) | Gallery in [Wikipedia carrier air ops](https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations) (all USN photos) |
| Carrier deck walkaround / "flight deck ops" B-roll | DVIDS search "flight deck operations B-roll": https://www.dvidshub.net/search/?q=flight+deck+operations+b-roll&filter%5Btype%5D=video |
| Lockheed media | [f35.com](https://www.f35.com) and lockheedmartin.com. **Corporate copyright, reference only.** The site blocked automated fetches during this research. |

### 6.3 Audio
- **DVIDS videos have natural sound** (F135 run-ups, cat shots, deck ambience, 1MC announcements). They're public domain with the caveats above, so extract clean segments from B-roll. This is the best free source of *real* F-35C and carrier audio.
- **Freesound:** licenses are CC0, CC-BY or CC-BY-NC; filter by license and avoid NC. Example CC0 jet flyby: [qubodup "Jet Plane Flyby"](https://freesound.org/people/qubodup/sounds/189446/). No verified CC0 F-35-specific recording was found. [Freesound FAQ](https://freesound.org/help/faq/)
- **Do not** rip audio from *Top Gun: Maverick*, DCS or other games (copyright).

### 6.4 Terrain data (for real canyons: see section 9)
- **USGS 3DEP 1 m lidar DEM:** public domain, free, no use restrictions. Cloud-optimized GeoTIFF with good US coverage, including the Cascades. [USGS 3DEP](https://www.usgs.gov/3d-elevation-program/about-3dep-products-services), [data catalog](https://data.usgs.gov/datacatalog/data/USGS:4f34caac-f28f-4ea0-8d82-eafb2b8f9a5d), [OpenTopography mirror](https://portal.opentopography.org/datasetMetadata?otCollectionID=OT.012021.4269.3)

---

## 7. Games and sims to benchmark

| Title | F-35? | Carrier ops | Low-level / feel | Lesson for this game |
|---|---|---|---|---|
| **DCS World** + **DCS: Supercarrier** | **F-35A announced 2025** (2012–2015-era jet, full fidelity). As of the 2026 roadmap part 2 it's "a bit delayed", 2027+ likely. No F-35C. | Supercarrier is the gold standard: Nimitz (CVN-71–75 skins), animated deck crew on all 4 cats, LSO station with PLAT cam, ICLS/ACLS, marshal comms | Full sim; steep learning curve | Copy the *procedure fidelity* (deck crew choreography, LSO view) and simplify the controls. [Supercarrier product page](https://www.digitalcombatsimulator.com/en/shop/modules/supercarrier/), [Simulation Daily 2026 roadmap](https://simulationdaily.com/news/dcs-world-roadmap-2026-part-2/), [Stormbirds F-35 editorial](https://stormbirds.blog/2025/01/26/the-elephant-is-in-the-room-aka-the-dcs-f-35-editorial/) |
| **Ace Combat 7** | F-35C playable | Scripted carrier take-off/landing segments | Arcade; cinematic canyon/trench missions | Spectacle and pacing; AC-style canyon runs directly echo the film. [Ace Combat wiki F-35C](https://acecombat.wiki.gg/wiki/F-35C_Lightning_II) |
| **Project Wingman** | Fictional aircraft | Limited | Arcade, very fast | Speed sensation, particle and contrail readability (no source fetched; general knowledge) |
| **MSFS 2020/2024** + free **Top Gun: Maverick expansion** (25 May 2022) | Stock F/A-18E, fictional Darkstar | **Carrier landing challenge**, low-level canyon challenges | Civil sim physics; photogrammetric terrain | A **licensed** Top Gun tie-in: shows what Paramount considers official product. [Xbox Wire](https://news.xbox.com/en-us/2022/05/26/microsoft-flight-simulator-become-a-top-gun-free-expansion-today/), [flightsimulator.com](https://www.flightsimulator.com/become-a-top-gun-pilot-in-free-expansion-available-today/) |
| **VTOL VR** | **F-45A**, a fictional F-35-like stealth jet | CATOBAR carrier with arresting wires plus an assault carrier; meatball; VTOL auto-land | VR, clickable cockpit, medium fidelity | Fictional-but-recognisable aircraft sidesteps licensing. [VTOL VR wiki F-45A](https://vtolvr.wiki.gg/wiki/F-45A), [Carrier ops](https://vtolvr.wiki.gg/wiki/Carrier_Takeoff_and_Landing) |
| **Nuclear Option** (Shockfront, Early Access) | Fictional aircraft | Carriers incl. an assault carrier (0.30, Mar 2025) | **Most action within ~1 km of the ground**; terrain masking central | **The best low-poly benchmark** for your hardware: sits between DCS and Ace Combat. [Steam](https://store.steampowered.com/app/2168680/Nuclear_Option/), [Reality Remake review](https://www.realityremake.com/articles/nuclear-option-review-a-tactical-flight-sim-you-didnt-see-coming), [Update 0.30](https://steamdb.info/patchnotes/17846791/) |
| **War Thunder** | **No F-35 Lightning II** as of the latest results. Note the June 2025 "Saab F-35" is the Swedish Draken, not a Lightning II. | Some naval aviation | Arcade/realistic/sim modes | Uses Lockheed designs "**under license**", a licensing reminder. [WT news](https://warthunder.com/en/news/9572-development-squadron-vehicles-saab-f-35-red-dragon-en), [WT legal](https://warthunder.com/en/support/legals) |

**Arcade vs sim balance (recommendation):**
- Nuclear Option/VTOL VR-level physics.
- DCS-level carrier *procedure* presented as an optional "realism" setting.
- An F-35C-true DFP approach mode by default.
- Ace Combat-style mission pacing.

---

## 8. Free / permissive 3D assets

**Caveats that apply to every entry:**
1. A CC licence on Sketchfab covers **only the uploader's copyright**. It **doesn't grant trademark or design rights** from Lockheed Martin or Boeing, or rights over DoD insignia (see 6.1 and 9.6).
2. **Avoid models labelled "(War Thunder)"** or similar. Those are ripped game assets, and the uploader can't license them no matter what label is shown.
3. **CC BY-NC(-SA)** means non-commercial only. ShareAlike can also force you to release derivative assets under the same licence, so avoid it for a public game.
4. Sketchfab gives **glTF** downloads for all downloadable models, and Godot 4 imports glTF 2.0 natively. Poly counts below are Sketchfab **faceCount (triangles)** from the [Sketchfab API v3](https://api.sketchfab.com/v3/search?type=models&q=f-35&downloadable=true), queried 2026-09-26. Quality is unverified: inspect before use (see the agent-habits note on looking at human-made assets).
5. **RTX 3050 4 GB budget [estimate]:** a player jet LOD0 of about 30–80k tris with 2k–4k textures; the carrier at 100–300k tris split into LODs and decks; SAM vehicles at 5–30k tris each.

### 8.1 Aircraft

| Model | URL | Licence | Tris | Godot / notes |
|---|---|---|---|---|
| **F-35C** (yichentao1, 2025) | https://sketchfab.com/models/11455dfbee2d43b88bf21fa76f2f2df6 | CC BY | 21,860 | Game-weight budget; the only CC BY **C-model** found. New upload, 0 likes, so inspect first |
| F-35C high-poly (yichentao1) | https://sketchfab.com/models/a484acbc88f74db19bafae6983feba72 | "Free Standard" (Sketchfab Standard licence, **not CC**; read its terms) | 1.47 M | Bake source only |
| **F-35A Lightning II** (shangus930) | https://sketchfab.com/models/a06d6113cfb44a0aa7b8f17106aca9c4 | CC BY | 190,514 | Popular (408 likes); needs decimation / LODs. **A-model:** needs wing and hook changes for a C |
| F-35B Lightning II (shangus930) | https://sketchfab.com/3d-models/f-35b-lightning-ii-fa3e9c175c244bedb457582f08cf8e31 | CC BY (per search listing) | n/a | STOVL reference |
| F-35 Lightning II (CloudHubOmniTeam) | https://sketchfab.com/3d-models/f-35-lightning-ii-a0186eeabcbf4a06ba05b68f665287ba | CC BY (per search listing) | n/a | |
| Low poly F-35 (S1Priv) | https://sketchfab.com/3d-models/low-poly-f-35-lightning-ii-561b5c56fbf94636a465f998e9af1224 | CC BY | low | Distant LOD / AI wingmen |
| F-35 / F-22 / Su-57 / F/A-18F / F-14 "Fighter Jet – Free" series (bohmerang) | e.g. https://sketchfab.com/models/b1ab1c0090e34b0fbfe667e706023e6d | **CC BY-NC-SA**, **avoid** | 32–50k | Very clean, but NC-SA |
| **F/A-18E/F Super Hornet** (andertan) | https://sketchfab.com/models/f71e9fea01e24fea9b1b380161d21d38 | CC BY | 57,856 | Deck-filler aircraft / film-style mission |
| **Su-57 "Felon"** (andertan) | https://sketchfab.com/models/09d546b4355c4fa6882ca46e05069bee | CC BY | 94,064 | Enemy 5th-gen |
| Low poly Su-57 (S1Priv) | https://sketchfab.com/models/b9031bc2e94947b18812ce0eca6b8345 | CC BY | 5,918 | Far LOD |
| **Su-35 Flanker-E** (andertan) | https://sketchfab.com/models/c98cf9b3b3e04017a04732798a31888a | CC BY | 23,565 | Enemy 4++ gen |
| MiG-29 (manilov.ap) | https://sketchfab.com/models/824b341826474fa483dcbc0fa3b15fb0 | CC BY | 52,703 | 2017 upload |

### 8.2 Ships

| Model | URL | Licence | Tris | Notes |
|---|---|---|---|---|
| **USS Nimitz CVN-68** (MirzaArrafiERV_45) | https://sketchfab.com/models/06cf0dba66874934a105b3fe2bfdb0f7 | CC BY | 402,532 | Highest detail found; split and LOD it |
| USS Dwight D. Eisenhower CVN-69 (same author) | https://sketchfab.com/models/f22c344e834f4c3781f676b372a94b2d | CC BY | 7,996 | Very light: distant or ocean LOD |
| USS Nimitz class carrier (GrantLarcenie) | https://sketchfab.com/models/e9f23bab3bd14f34ba9e54ccd082f46d | CC BY | 98,225 | Mid-weight |
| Low-poly USS Nimitz (S1Priv) | https://sketchfab.com/models/acc8e2ab9cff4f6aa41c7f6db5e59abb | CC BY | 73,344 | |
| **Gerald R. Ford carrier** (waelXcm) | https://sketchfab.com/models/562bf516e1494df38d8f222504dc798b | CC BY | 116,381 | 379 likes, if you pick Ford |
| USS Gerald R. Ford (veron66) | https://sketchfab.com/models/abefde1453ae4d78bd9bea70e80bb1b0 | CC BY | 269,706 | |
| **DDG-51 Arleigh Burke, 1:1 low poly** (WTigerTw) | https://sketchfab.com/models/17be09c31c6047e4a5969b68c29eba03 | CC BY | 49,632 | Escort |
| Arleigh Burke Flight IV (Planetrix23) | https://sketchfab.com/models/d26e1b51424c4fb1a87ab8f36f9975d9 | CC BY | 43,836 | Speculative Flight IV |

**Flight deck for gameplay [recommendation]:** build the **deck surface, cats, JBDs, wires and IFLOLS as your own collision/gameplay mesh** to true dimensions (section 4.2). Use the downloaded hull as visual dressing only. Gameplay-critical geometry should never depend on a third-party model's scale.

### 8.3 Ground threats and targets

| Model | URL | Licence | Tris |
|---|---|---|---|
| **96K6 Pantsir-S2** (42manako) | https://sketchfab.com/models/758055e673b543bd9ec3f504c8d60e8b | CC BY | 52,414 |
| Pantsir-S1 (SanderWolf_) | https://sketchfab.com/models/3cb67b0bae10418190e7ce32142231e4 | CC BY | 87,600 |
| Lowpoly ZSU-23-4 (S1Priv) | https://sketchfab.com/models/53bea8003bfc4a80b429d4f1f5d9206c | CC BY | 67,456 |
| ZSU-23-4 Shilka (42manako) | https://sketchfab.com/models/97263f2d813b45c589e1ce83d088ab47 | **CC BY-NC**, avoid | 13,259 |
| S-400 Triumf (42manako) | https://sketchfab.com/models/1a109bdd906149249dce0a18cdfbe708 | **CC BY-NC**, avoid | 27,898 |
| **Real WWII/Cold-War concrete bunkers, photogrammetry** (matousekfoto) | https://sketchfab.com/models/cd3efc123848413fad5e1dcf8817881e · https://sketchfab.com/models/11ae7599f3b640569f7ed4692acabf38 | CC BY | 340–550k (decimate) |
| Military Style Building, animated (TampaJoey) | https://sketchfab.com/models/1671685e1dd94b6e9216ead632f08f6e | CC BY | 16,796 |
| Military Outpost Kit 1.0 (britdawgmasterfunk; title says CC0, API says CC BY, so treat as CC BY) | https://sketchfab.com/models/010dc4f0a73a48e7a53598c6f24fe9cf | CC BY | 229,806 |
| BGM-109 Tomahawk (rickslash) | https://sketchfab.com/models/da71eb1c023b444aba15ff37710e9613 | CC BY | 2,401 |
| **Avoid:** "GBU-31/32/38 JDAM (War Thunder)" and "Pantsir … (War Thunder)" uploads | e.g. https://sketchfab.com/models/a9be1567030747859c3cbb73102630bd | labelled CC BY, but **ripped game assets** | n/a |

### 8.4 Environment, textures, HDRIs, tools

| Source | Licence | Use |
|---|---|---|
| **Poly Haven** https://polyhaven.com | **CC0**: no attribution, commercial OK, redistribution OK ([licence](https://polyhaven.com/license)) | Sky HDRIs, rock/snow/ground PBR textures, some models |
| **USGS 3DEP DEMs** (see 6.4) | Public domain | Real canyon heightmaps |
| **Quixel Megascans via Fab** | Fab **Standard License**. Free for all engines until end of 2024; assets claimed then are yours forever. Since 2025 most are **paid ($0.99+)** with some free. Usable outside Unreal under the Fab Standard License EULA. [CG Channel](https://www.cgchannel.com/2024/10/epic-games-has-made-megascans-free-to-all-but-only-until-the-end-of-2024/), [Fab transition FAQ](https://support.fab.com/s/article/Fab-Transition-FAQs?language=en_US) | Photoreal rocks and cliffs for canyon walls; check each asset's licence |
| **Kenney** https://kenney.nl | CC0 | Stylised; good for **UI icons and prototyping**, not for the realistic look |
| **Freesound** https://freesound.org | CC0 / CC BY / CC BY-NC per sound | SFX (see 6.3) |
| **Terrain3D** (Godot 4 terrain plugin) https://github.com/TokisanGames/Terrain3D | MIT (verified via GitHub API 2026-09-26) | Large clipmap terrain for canyon maps on a 4 GB GPU |

---

## 9. *Top Gun: Maverick* mission breakdown (for mission 1)

### 9.1 Target and defenses (as depicted)
Source: [Wikipedia Top Gun: Maverick, Plot](https://en.wikipedia.org/wiki/Top_Gun:_Maverick); briefing details from the [transcript at Scraps from the Loft](https://scrapsfromtheloft.com/movies/top-gun-maverick-transcript/) (paraphrased).

- **Target:** an unsanctioned **uranium enrichment plant in an underground bunker at the end of a canyon**, in an unnamed country. The only exposed weak point is a **small ventilation hatch, described as under ~3 m wide**, set in the valley between two mountains.
- **Defenses:**
  - **SAMs**, positioned to protect the airspace *above* the canyon.
  - **GPS jamming/spoofing.**
  - **Su-57 fifth-generation fighters** at a nearby airbase.
  - Older **F-14 Tomcats** in reserve.
  - An **Mi-24** gunship appears later.
- **Why not F-35s:** in the film, **GPS spoofing makes an F-35 attack unfeasible** (Wikipedia plot), so the plan uses **two pairs of F/A-18E/F Super Hornets with laser-guided bombs**.
  - **Reality check:** the F-35 has its own laser designator (EOTS) and laser-guided bomb options, so GPS jamming alone wouldn't rule it out.
  - Slate's analysis says the choice avoided depicting classified F-35 tactics and calls the film logic weak: why not B-2s from high altitude, and if Tomahawks can hit the airbase, why not the plant? [Slate, June 2022](https://slate.com/news-and-politics/2022/06/top-gun-maverick-mission-military-analysis.html)
  - **For your F-35 game:** replace "GPS jammed" with **"GPS jammed, so the JDAMs are unreliable. Use laser-guided bombs with EOTS self-designation or buddy-lasing."** That keeps the film's constraint and stays plausible.

### 9.2 Mission phases and parameters (film)

| # | Phase | Parameters stated in the film | Source |
|---|---|---|---|
| 0 | **Carrier launch**: 4 jets (2 two-ship pairs) plus an airborne spare/emergency pilot | Launch from an aircraft carrier | Wikipedia plot |
| 1 | **Tomahawk strike on the enemy airbase**, timed with the jets' ingress to crater runways and delay the fighters | Simultaneous with ingress | Wikipedia plot; transcript |
| 2 | **Low-level canyon ingress** in welded-wing pairs | **Max 100 ft AGL, min 660 kt, time-to-target 2:30** | [ScreenplayHowTo / search summary](https://screenplayhowto.com/screenplay-analysis/top-gun-maverick-2022/), transcript |
| 2a | Cyclone's "relaxed" alternative, later dropped | **4:00 time-to-target, 420 kt, higher-altitude level attack** (avoids the steep pop-up) | same |
| 2b | Maverick's unauthorized demo run | **2:15**, 15 s faster than required | [Screen Rant](https://screenrant.com/top-gun-2-maverick-mission-time-actual-minutes/) |
| 3 | **Pop-up climb and inverted dive** over the ridge onto the vent | Steep climb at very high G (dialogue cites loads beyond the Super Hornet's 7.5 g design limit), then a dive | transcript (paraphrased) |
| 4 | **Laser designation and bombing**: team 1 (Maverick + F/A-18F buddy-lasing) **breaches** the hatch; team 2 (Rooster's pair) delivers the **kill shot** into the opening. In the film, team 2's laser fails and the bomb is guided visually. | 2 × 2 aircraft, sequential hits | Wikipedia plot, [CinemaBlend](https://www.cinemablend.com/movies/i-surprised-learn-top-gun-maverick-final-mission-pretty-realistic) |
| 5 | **Egress under SAM fire**: max-G climb out of the valley exposes the jets to the SAMs; heavy countermeasure use; a wingman runs out of flares | | Wikipedia plot, transcript |
| 6 | **5th-gen pursuit**: Su-57s engage (in the film, after a detour via an F-14 stolen from the damaged airbase) | | Wikipedia plot |
| 7 | **Recovery to the carrier** | | Wikipedia plot |

**[derived] Useful numbers for level design:**
- **Ingress length ≈ 660 kt × 2.5 min ≈ 27.5 nmi ≈ 51 km** (at least, since 660 kt is a minimum speed).
- **Time budget:** at 660 kt the jet covers about 340 m/s. At 100 ft AGL (30 m) that's roughly 0.09 s per metre of altitude margin. A realistic run is **extremely** twitchy, so give players a 150–300 ft band or an assist in normal difficulty.
- **Radar LOS at 30 m altitude to a 10 m mast on flat ground ≈ 36 km** (section 5.3). The canyon walls must break LOS for the whole ingress, which is the design justification for the canyon.
- **On-screen runtime** of canyon-to-target is **over 4 minutes** because of cutaways. [Screen Rant](https://screenrant.com/top-gun-2-maverick-mission-time-actual-minutes/)

### 9.3 Terrain character and real filming locations

**What the terrain looks like:** snowy, forested **alpine valleys**, steep granite walls, frozen or high lakes, and a snowbound airbase with Quonset-style hangars.

| Location | What was shot | Source |
|---|---|---|
| **Cascade Mountains, Washington**, flown from **NAS Whidbey Island** (Oak Harbor, WA); routes scouted by helicopter and L-39 before the F/A-18 filming | **Third-act low-level flying** (snowy canyon runs) | [Wikipedia Filming](https://en.wikipedia.org/wiki/Top_Gun:_Maverick#Filming), [KING5](https://www.king5.com/article/entertainment/television/programs/evening/washingtons-cascade-mountains-are-critical-location-in-top-gun-maverick/281-5e73c9fc-3d29-4958-81ec-40316752e249), [OPB](https://www.opb.org/article/2022/05/29/new-top-gun-sequel-starring-tom-cruise-includes-pacific-nw-scenery-but-we-re-a-rogue-state/) |
| **Rimrock Lake**, west of Yakima, WA (some say **Kachess Lake**) | Low-level formation over a lake | [Atlas of Wonders](https://www.atlasofwonders.com/2022/06/where-was-top-gun-maverick-filmed.html), [Unofficial Networks](https://unofficialnetworks.com/2022/06/13/mountainous-locations-top-gun-maverick/) **[secondary]** |
| Mountains above **Tall Timbers Ranch**, north of Lake Wenatchee, WA | Low passes seen by locals in 2019 | [KW3 blog](https://kw3.com/northcascades/) **[anecdotal]** |
| **Lake Tahoe Airport**, South Lake Tahoe, CA | The **enemy airbase** (hangars built, airstrike damage dressing, non-flying F-14 towed) | [SFGate](https://www.sfgate.com/streaming/article/top-gun-filming-locations-lake-tahoe-17198578.php), [SnowBrains](https://snowbrains.com/parts-of-top-gun-maverick-were-filmed-near-lake-tahoe/), [The Aviationist 2018](https://theaviationist.com/2018/12/18/tom-cruise-with-f-14-tomcat-on-snowy-set-in-tahoe-top-gun-escape-scene-filmed/) |
| **Washoe Meadows State Park**, South Lake Tahoe | Forest scenes after Maverick is shot down | [search summary of SFGate/SnowBrains](https://snowbrains.com/parts-of-top-gun-maverick-were-filmed-near-lake-tahoe/) **[secondary]** |
| **NAS Fallon**, Nevada (real TOPGUN home) | Aerial shots | [Wikipedia](https://en.wikipedia.org/wiki/Top_Gun:_Maverick#Filming) |
| **NAWS China Lake**, CA | Darkstar mock-up; <50 ft / ~450 kt low pass | same |
| Also listed: San Diego / NAS North Island, Lemoore, Chico, Seattle, Patuxent River | Various | same |

**Aerial terrain references to build your canyon:**
- **USGS 3DEP DEM** tiles for the Cascades (around Rimrock Lake / White Pass and the Lake Wenatchee area) and the Lake Tahoe basin. Public domain (section 6.4).
- DVIDS or Wikimedia photos of the **Cascades from NAS Whidbey Island Growlers**: https://www.dvidshub.net/search/?q=Whidbey+Island+Growler+Cascades
- Real low-level canyon culture: **Rainbow Canyon ("Star Wars Canyon")**, Death Valley. It's part of the R-2508 low-level training complex. A Super Hornet crashed there on 31 July 2019, and per a marker updated in 2025 the jets **no longer fly through the canyon**; other reports say flights resumed. [NPS](https://www.nps.gov/deva/learn/news/military-crash-one-year-later.htm), [Wikipedia Rainbow Canyon](https://en.wikipedia.org/wiki/Rainbow_Canyon_(California)), [HMdb marker](https://www.hmdb.org/m.asp?m=194626)

### 9.4 Carriers used in filming
- **USS *Abraham Lincoln* (CVN-72):** a 15-person crew shot flight deck operations in late Aug 2018 (Norfolk-based at the time).
- **USS *Theodore Roosevelt* (CVN-71):** Cruise and the crew were aboard at NAS North Island in mid-Feb 2019.
- Source: [Wikipedia Filming](https://en.wikipedia.org/wiki/Top_Gun:_Maverick#Filming)
- **Both are Nimitz-class, Roosevelt sub-class**, which supports the carrier recommendation in section 4.1.

**Other production facts:**
- The Navy charged **$11,374 per flight hour** for Super Hornets.
- Six IMAX-certified Sony Venice cameras were fitted in the cockpit.
- More than 800 hours of aerial footage were shot.
- Air-to-air platforms: L-39 (350 kt, 3 g), Phenom 300, AS350 helicopter.
- Source: [Wikipedia](https://en.wikipedia.org/wiki/Top_Gun:_Maverick#Filming)

### 9.5 Real-world analogues (to ground the fiction)
- **Operation Midnight Hammer (2025):** Tomahawks first, then B-2s with GBU-57s on the Fordow enrichment site under a mountain. It's the closest real analogue to "underground enrichment plant + cruise-missile opening strike" (section 5.4).
- **Operation Orchard (2007):** Slate cites Israel's Syrian reactor strike, where electronic/cyber attack blinded the air-defense radars. It's a good "SEAD player" mechanic. [Slate](https://slate.com/news-and-politics/2022/06/top-gun-maverick-mission-military-analysis.html)

### 9.6 IP boundaries for a publicly released, fan-inspired game

*Not legal advice. This is a summary of publicly documented facts and standard IP categories. Get a lawyer for anything commercial.*

**Who owns what (documented):**
- ***Top Gun: Maverick*** (screenplay, characters, footage, music, logos) is **Paramount Pictures** copyright and trademark. Paramount actively litigates: *Yonay v. Paramount* was dismissed and affirmed by the 9th Circuit, and it brought counterclaims against a would-be co-author in 2025–26. [Wikipedia Lawsuits](https://en.wikipedia.org/wiki/Top_Gun:_Maverick#Lawsuits), [Chip Law Group](https://www.chiplawgroup.com/top-gun-maverick-copyright-lawsuit-ninth-circuit-affirms-no-infringement-in-yonay-v-paramount/)
  - The **"TOP GUN"** mark has had Paramount registrations. One (Reg. 4743690, toys and sporting goods) is shown as cancelled. [Trademarkia](https://www.trademarkia.com/top-gun-86437724)
  - The Navy's school is also called TOPGUN. **Treat "Top Gun" as off-limits** in the title, store page and logos.
- **Aircraft names and designs:** Lockheed Martin holds trademarks for aircraft names and configurations.
  - Games like **War Thunder use Lockheed aircraft "under license"**. [War Thunder legal](https://warthunder.com/en/support/legals), [gamedev.net discussion](https://gamedev.net/forums/topic/526402-military-plane-nameslikeness-and-licensing/526402/)
  - **VTOL VR and Nuclear Option avoid this with fictional look-alikes** (F-45A etc.).
- **US military insignia, seals and unit badges** are trademark-protected and **may not be used in commerce without permission**. Imagery must not imply endorsement. [DVIDS policy](https://www.dvidshub.net/about/copyright), [DoD trademark guide](https://www.trademark.marines.mil/Portals/161/Docs/DOD%20Trademark%20Licensing%20Guide-16%20July%202017_1.pdf?ver=QF2mfBCE3qjMYOnigDCkGg%3D%3D)
- **Idea vs expression:** the Yonay ruling explicitly relied on the rule that **unprotectable ideas and historical facts** aren't copyrightable. A "low-level canyon strike on a bunker" is a generic tactical idea. [Wikipedia Lawsuits](https://en.wikipedia.org/wiki/Top_Gun:_Maverick#Lawsuits)

**OK to use as inspiration:**
- The generic mission structure: carrier launch → cruise-missile strike on the airbase → low-level canyon ingress under a time limit → pop-up → laser-guided hit on a vent → egress under SAMs → fighter pursuit → trap.
- Real procedures, jersey colors, IFLOLS, and real hardware *types* such as SAM systems.
- Snowy alpine terrain built from **public-domain USGS DEMs** of real places. Geography isn't copyrightable.
- Game-mechanic parameters like "stay under X ft, reach target in Y:ZZ". These are ideas, but **pick your own numbers** rather than 100 ft / 660 kt / 2:30 verbatim, to avoid looking like a direct copy.
- Public-domain DVIDS photos and video as reference or texture sources, with markings removed.

**Do NOT copy:**
- The title "Top Gun", "Maverick", the film logo or poster art.
- **Character names and callsigns**: Maverick, Rooster, Hangman, Phoenix, Bob, Payback, Fanboy, Cyclone, Iceman, Warlock, Goose. Also "Darkstar", "the Hard Deck" bar and its signage.
- **Music**: the score, "Danger Zone", "Hold My Hand", the Top Gun anthem. Any soundalike that's clearly derived.
- **Dialogue lines and the briefing script verbatim**, plus film footage, audio, stills or screenshots.
- **Likenesses** of Tom Cruise or any cast member (right of publicity), and the film's helmet art and patches.
- **Real US Navy squadron insignia** (e.g., VFA-147 Argonauts), the Navy seal, or "U.S. NAVY" text on a commercial product without permission. Invent squadrons.
- **Lockheed names** ("F-35", "Lightning II") in a **commercial** product without a licence. The safest path is a lightly fictionalised jet name and badge ("F/X-35C" style) while keeping the realistic shape. Even the shape (trade dress) can be claimed, which is why VTOL VR's F-45A differs slightly.
- **Ripped assets** from DCS, War Thunder, Ace Combat, MSFS or the film.

---

## Top 10 most useful references

1. **Wikipedia: Modern US Navy carrier air operations.** The full launch and recovery procedure with numbers (Case I/III, altitudes, distances, ball call, bolters, CQ counts): https://en.wikipedia.org/wiki/Modern_United_States_Navy_carrier_air_operations
2. **Lockheed Martin F-35A/B/C fact sheets (2019, archived).** Official per-variant specs: [A](https://web.archive.org/web/2024id_/https://www.lockheedmartin.com/content/dam/lockheed-martin/aero/documents/F-35/f35A.pdf) · [B](https://web.archive.org/web/2024id_/https://www.lockheedmartin.com/content/dam/lockheed-martin/aero/documents/F-35/f35B.pdf) · [C](https://web.archive.org/web/2024id_/https://www.lockheedmartin.com/content/dam/lockheed-martin/aero/documents/F-35/f35C.pdf)
3. **Wikipedia F-35 article, specs and variant table.** Weights, fuel, g-limits, bays, beast mode, cockpit/HMD/DAS: https://en.wikipedia.org/wiki/Lockheed_Martin_F-35_Lightning_II
4. **P&W F135-PW-100 product card.** 43k/28k lbf, engine dimensions: https://app.prattwhitney.com/download/F135-CTOL.pdf
5. **Defense Media Network: F-35C IDLC + Military.com: Delta Flight Path.** How to build the carrier-approach control law: https://www.defensemedianetwork.com/stories/f-35c-integrated-direct-lift-control-how-it-works/ · https://www.military.com/daily-news/2016/08/17/f-35s-new-landing-technology-may-simplify-carrier-operations.html
6. **Wikipedia: Optical landing system + Arresting gear + EMALS.** Meatball logic, wire layout, catapult numbers: https://en.wikipedia.org/wiki/Optical_landing_system · https://en.wikipedia.org/wiki/Arresting_gear · https://en.wikipedia.org/wiki/Electromagnetic_Aircraft_Launch_System
7. **Wikipedia: Radar horizon.** Terrain-masking math for the detection system: https://en.wikipedia.org/wiki/Radar_horizon
8. **DVIDS (+ its copyright policy).** Public-domain F-35C/carrier photo, video and audio with clear usage rules: https://www.dvidshub.net · https://www.dvidshub.net/about/copyright
9. **Sketchfab CC BY models:** F-35C (21.9k tris), USS Nimitz (402k), Su-57, Pantsir-S2, DDG-51. See section 8 for URLs. Plus **Poly Haven (CC0)** and **USGS 3DEP (public domain)** for the environment.
10. **Wikipedia: Top Gun: Maverick (Plot, Filming, Lawsuits).** The mission skeleton, real locations (Cascades/Whidbey, Lake Tahoe) and carriers (CVN-72, CVN-71), plus the IP context: https://en.wikipedia.org/wiki/Top_Gun:_Maverick
