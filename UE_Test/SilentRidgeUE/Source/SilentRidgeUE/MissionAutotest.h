// -autotest=strike: auto-fly the last 10 km of the canyon, pop up, point-track + lase the vent shaft, release in range, guide,
// REPORT hit/miss, then egress low under SAM fire with countermeasures while the Su-57s scramble; screenshots of the TGP view,
// the impact, a SAM launch + flares and the fighters; perf per segment (target area, SAM engagement, all). Variants:
// -autotest=strike_miss (designates 40 m off the shaft: checks the "no kill" path).
#pragma once

#include "CoreMinimal.h"

class AOsrMissionState;
class AOsrJetPawn;
class ACameraActor;

class FOsrStrikeAutotest
{
public:
	void Step(AOsrMissionState* Ms, double Dt);

private:
	FString Phase = TEXT("spawn");
	double T = 0.0, Total = 0.0;
	TArray<FVector> Path;
	int32 PathI = 0;
	bool bMiss = false;
	bool bReleased = false, bImpact = false;
	double ImpactT = -1.0;
	int32 ShotsTaken = 0;
	TSet<FString> Shots;
	TArray<float> FramesAll, FramesTarget, FramesSam;
	double CmAcc = 0.0;
	double TurnSign = -1.0;
	TWeakObjectPtr<ACameraActor> Spectator;
	double SpectatorUntil = -1.0;
	void LoadPath();
	void Shot(const FString& Name, bool bOnce = true);
	void Spectate(AOsrJetPawn* Jet, const FVector& FromG, const FVector& AtG, double Seconds);
	FVector2D Pilot(AOsrJetPawn* Jet, const FVector& HeadingDir, double Gamma, double Speed);
	FVector2D CanyonPilot(AOsrJetPawn* Jet);
	void Report(const TCHAR* Seg, TArray<float>& F) const;
};
