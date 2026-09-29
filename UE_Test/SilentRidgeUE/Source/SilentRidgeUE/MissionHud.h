// Mission/combat HUD pages drawn by AOsrHud (small hooks there): objective line, radio subtitles, briefing card, success/fail
// screens with stats (any pawn); for jets: weapons status, LGB release cues + target designator box, RWR scope, missile warnings,
// hostile markers, and the full-screen targeting-pod page (TGP symbology, laser, zoom, track, range, TTI).
#pragma once

#include "CoreMinimal.h"

class UCanvas;
class APawn;
class AOsrJetPawn;

struct SILENTRIDGEUE_API FOsrMissionHud
{
	/** True while the jet's targeting pod is the full-screen view (the flight HUD is replaced by the TGP page). */
	static bool TgpFullscreen(const AOsrJetPawn* Jet);
	/** Objective, radio, briefing, success/fail (drawn for any pawn). */
	static void DrawOverlay(UCanvas* Canvas, APawn* Pawn);
	/** Weapons, cues, RWR, warnings, TGP page. */
	static void DrawJet(UCanvas* Canvas, const AOsrJetPawn* Jet);
};
