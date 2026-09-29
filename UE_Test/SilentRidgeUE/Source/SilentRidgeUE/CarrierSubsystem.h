// World subsystem for the carrier (no game-mode edits needed): spawns the carrier, starts the player on foot at the island door with
// the F-35C parked by catapult 1, switches pawns (climb in / climb out / respawn on deck after a crash) and runs -autotest=carrier.
// Plain -autotest (canyon) keeps the old airborne start; -nocarrier disables everything.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CarrierSubsystem.generated.h"

class ACarrierActor;
class AOsrJetPawn;
class AOsrCharacter;
class UCarrierJetOps;

UCLASS()
class SILENTRIDGEUE_API UCarrierSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCarrierSubsystem, STATGROUP_Tickables); }

	ACarrierActor* GetCarrier() const { return Carrier; }
	AOsrJetPawn* PlayerJet() const { return Jet; }
	UCarrierJetOps* PlayerOps() const { return Ops; }
	AOsrCharacter* Walker() const { return Foot; }
	bool IsDeckMode() const { return bDeckMode; }

	void EnterJet();
	void ExitJet();
	void RespawnOnDeck();
	double ExitHold = 0.0;

private:
	UPROPERTY() TObjectPtr<ACarrierActor> Carrier;
	UPROPERTY() TObjectPtr<AOsrJetPawn> Jet;
	UPROPERTY() TObjectPtr<UCarrierJetOps> Ops;
	UPROPERTY() TObjectPtr<AOsrCharacter> Foot;
	bool bDeckMode = false;
	bool bSetupDone = false;
	bool bAutotest = false;
	double SwitchT = -1.0;
	int32 SwitchTo = 0;   // 1 = into the jet, 2 = out of the jet
	AOsrCharacter* SpawnWalker(const FVector& LocalM, double YawDeg);
	void SetupDeck();
	void Fade(double From, double To, double Time);
	void DoSwitch();

	// -autotest=carrier
	FString Phase;
	double T = 0.0, Total = 0.0, LatPrev = 0.0;
	int32 Shots = 0, Approaches = 0;
	FString TrapText;
	TArray<float> FramesDeck, FramesApproach;
	bool bShot[20] = {};
	void Autotest(double Dt);
	void Shot(const FString& Name, int32 Slot);
	void AutoTaxi();
	void AutoApproach(double Dt);
	static FString Perf(const TArray<float>& F);
};
