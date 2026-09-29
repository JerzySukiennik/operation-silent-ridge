// Laser-guided bomb (GBU-12 class, 500 lb): drag + gravity, canard lift limited by dynamic pressure with induced drag (real glide
// energy), semi-active laser seeker on the velocity axis (+-17 deg, terrain LOS, laser code), proportional navigation, impact fuze.
// Also the release predictor (LAR) used by the HUD cues and the strike autotest.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeaponBomb.generated.h"

class UProceduralMeshComponent;

struct FOsrBombSim
{
	bool bImpact = false;
	FVector Impact = FVector::ZeroVector;
	double Tof = 0.0;
	double Miss = 1e9;          // horizontal distance from the aim point at impact
	double ImpactAngleDeg = 0.0;
};

UCLASS()
class SILENTRIDGEUE_API AOsrLaserBomb : public AActor
{
	GENERATED_BODY()
public:
	AOsrLaserBomb();
	virtual void Tick(float DeltaSeconds) override;

	static constexpr double MASS = 230.0;
	static constexpr double DRAG_K = 3.9e-5;       // 0.5 rho Cd A / m at sea level (1/m)
	static constexpr double SEEKER_FOV = 0.30;     // rad
	static constexpr double NAV_N = 3.0;

	void Release(const FVector& InP, const FVector& InV, int32 InCode, AActor* InOwner, const FVector& PlannedAim);
	FVector P = FVector::ZeroVector, V = FVector::ZeroVector;
	int32 Code = 1688;
	TWeakObjectPtr<AActor> OwnerJet;
	bool bAlive = false;
	bool bSeeking = false;           // seeker has the spot now
	double Age = 0.0;
	double Tti = 0.0;                // predicted time to impact
	FVector PlannedAim = FVector::ZeroVector;

	/** One guidance/flight step shared with the predictor. Spot = laser spot if seen. */
	static void Step(FVector& Pos, FVector& Vel, const FVector* Spot, double Dt);
	/** Fly a bomb from (P, V) to the ground, guided onto Aim when bGuided (spot assumed visible), unguided otherwise. */
	static FOsrBombSim Simulate(const FVector& InP, const FVector& InV, const FVector& Aim, bool bGuided, double MaxT = 90.0);
	static TArray<AOsrLaserBomb*> InFlight(UWorld* W, const AActor* ForOwner);

private:
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Body;
	void Impact(const FVector& At, bool bShaft, const FString& What);
	double PredictAcc = 0.0;
};
