// Guided missiles: S-400-class 9M96 (command + active radar terminal), Pantsir 57E6 (radio-command dart), R-74-class IR air-to-air.
// Point-mass flight with motor burn/coast and quadratic drag, proportional navigation (N = 4) with a g limit, seeker FOV and terrain
// line of sight, decoy logic (flares vs IR, chaff vs radar), proximity fuze, smoke trail + motor flame.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SamMissile.generated.h"

class UProceduralMeshComponent;
class AOsrRadarSite;

UENUM()
enum class EOsrMissileKind : uint8 { S400_9M96, Pantsir_57E6, Tor_9M331, AAM_IR };

UCLASS()
class SILENTRIDGEUE_API AOsrMissile : public AActor
{
	GENERATED_BODY()
public:
	AOsrMissile();
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	void Launch(EOsrMissileKind InKind, const FVector& P, const FVector& Dir, double Speed, AActor* InTarget, AActor* InShooter, AOsrRadarSite* InSite);

	EOsrMissileKind Kind = EOsrMissileKind::S400_9M96;
	FVector P = FVector::ZeroVector, V = FVector::ZeroVector;   // Godot frame
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<AActor> Shooter;
	TWeakObjectPtr<AOsrRadarSite> Site;
	bool bAlive = false;
	bool bIr() const { return Kind == EOsrMissileKind::AAM_IR; }
	double Age = 0.0;
	double ClosestM = 1e9;
	bool bDecoyed = false;
	FVector DecoyPos = FVector::ZeroVector;
	int32 DecoyId = 0;
	bool bWarned = false;            // for the player's MAWS/RWR ("new launch")
	static TArray<AOsrMissile*> HomingOn(UWorld* W, const AActor* Receiver);

private:
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Body;
	// performance
	double Burn = 6.0, Accel = 170.0, DragK = 3e-5, MaxG = 20.0, Fuse = 12.0, MaxTime = 60.0, SeekerFov = 1.0, Lethal = 9.0, BoostOnly = 0.0;
	double PrevDist = 1e9;
	int32 Trail = 0;
	double PuffAcc = 0.0;
	TSet<int32> SeenDecoys;
	FVector LastAim = FVector::ZeroVector;
	bool bGuided = true;
	void BuildBody(double Len, double Rad);
	void Detonate(bool bKill, bool bTerrain);
	bool AimPoint(FVector& Aim);
	void CheckDecoys();
};
