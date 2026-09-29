// Enemy Su-57-class fighter: point-mass flight with lift-limited 9 g turns, thrust/drag energy (2 x AL-41F-1 class), terrain
// avoidance (won't follow into the canyon floor), AI (parked -> takeoff roll -> climb -> intercept by GCI/radar/visual -> IR missile
// or gun attack -> RTB), damage (25 mm hits), wreck fall. Procedural Su-57-like mesh until the CC-BY model is available.
#pragma once

#include "CoreMinimal.h"
#include "MissionCombat.h"
#include "RadarSite.h"
#include "EnemyFighter.generated.h"

class UProceduralMeshComponent;
class AOsrJetPawn;

UENUM()
enum class EOsrFighterState : uint8 { Parked, Takeoff, Climb, Intercept, Attack, Search, Rtb, Wreck };

UCLASS()
class SILENTRIDGEUE_API AOsrEnemyFighter : public AOsrDamageable
{
	GENERATED_BODY()
public:
	AOsrEnemyFighter();
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector PosG() const override { return P; }
	virtual FVector VelG() const override { return V; }
	virtual void OnKilled(AActor* By) override;
	virtual void ApplyHit(const FVector& Hp, double Damage, EOsrDamage Kind, AActor* By, int32 Part) override;

	void Park(const FVector& At, double Yaw);
	void Scramble(const FVector& RunwayStart, const FVector& RunwayDir, double Delay);
	void OnChaff(const AActor* Target);
	bool RwrFor(const AActor* Receiver, FOsrRwrContact& Out) const;
	void ResetFighter();

	EOsrFighterState State = EOsrFighterState::Parked;
	FVector P = FVector::ZeroVector, V = FVector::ZeroVector;
	FVector Up = FVector(0, 1, 0);
	FString Callsign = TEXT("BANDIT");
	int32 Aams = 2;
	int32 GunRounds = 150;
	double Ab = 0.0;
	static const TCHAR* StateName(EOsrFighterState S);

private:
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Body;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Flame;
	FVector ParkP = FVector::ZeroVector;
	double ParkYaw = 0.0;
	FVector RwyStart = FVector::ZeroVector, RwyDir = FVector(1, 0, 0);
	double StartDelay = 0.0;
	double StateT = 0.0, Now = 0.0, LastShot = -99.0, GunAcc = 0.0, RadarBlind = 0.0, TrailAcc = 0.0;
	TWeakObjectPtr<AOsrJetPawn> Target;
	FVector LastKnown = FVector::ZeroVector;
	double LastKnownT = -99.0;
	bool bRadarTrack = false;
	int32 SmokeTrail = 0;
	void BuildMesh();
	void Fly(const FVector& DesiredDir, double DesiredSpeed, double Dt, double MaxG = 9.0);
	bool Sense(AOsrJetPawn*& Out);
	void Attack(AOsrJetPawn* J, double Dt);
	void UpdateTransform();
};
