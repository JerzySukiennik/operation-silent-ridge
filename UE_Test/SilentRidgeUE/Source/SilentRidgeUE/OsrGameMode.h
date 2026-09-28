// Game mode: spawns the F-35C pawn + HUD, removes the stage-1 flythrough sequence (kept with -flythrough), and runs the -autotest canyon autopilot (port of autotest.gd).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "OsrGameMode.generated.h"

class AOsrJetPawn;

UCLASS()
class SILENTRIDGEUE_API AOsrGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AOsrGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	bool bFlythrough = false;
	TWeakObjectPtr<AActor> Ocean;
	bool bOceanSearched = false;
	void FollowOcean();
	bool bAutotest = false;
	// autotest
	TArray<FVector> Path;
	int32 PathI = 0;
	FString Phase = TEXT("spawn");
	double T = 0.0, Total = 0.0, ShotT = 0.0, CanyonTime = 45.0;
	int32 Shots = 0, Hitches = 0, CrashSeen = 0, RespawnBase = 0;
	bool bRespawnedSeen = false;
	TArray<float> Frames;
	void AutotestStep(AOsrJetPawn* Jet, double Dt);
	void StartCanyon(AOsrJetPawn* Jet);
	FVector2D CanyonPilot(AOsrJetPawn* Jet);
	void Shot(const FString& Name);
	void ReportPerf(AOsrJetPawn* Jet);
};
