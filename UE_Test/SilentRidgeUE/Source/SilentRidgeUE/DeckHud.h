// Carrier HUD layer, drawn by AOsrHud: on foot (prompts, jet marker, help, pause menu, jet-blast warning) and in the jet
// (launch prompts, HOOK/DFP/APC cues, IFLOLS meatball repeater, LSO radio text, trap/bolter/wave-off debrief, climb-out hold).
#pragma once

#include "CoreMinimal.h"

class AHUD;
class UCanvas;
class APawn;

class SILENTRIDGEUE_API FDeckHud
{
public:
	static void Draw(AHUD* Hud, UCanvas* Canvas, APawn* Pawn);
};
