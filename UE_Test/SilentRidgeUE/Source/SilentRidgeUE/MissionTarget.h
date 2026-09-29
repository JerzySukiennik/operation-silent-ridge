// The target: an underground plant in the target valley — surface complex (power hall with stacks, admin, barracks, tanks,
// fence, roads), two hardened tunnel portals with blast doors, and the ventilation shaft on a levelled terrace on the spur.
// A bomb into the shaft opening destroys the plant (secondary explosions from the shaft and out of both portals); any other hit
// is recorded as "no kill" with feedback.
#pragma once

#include "CoreMinimal.h"
#include "MissionCombat.h"
#include "MissionTarget.generated.h"

class UProceduralMeshComponent;

UCLASS()
class SILENTRIDGEUE_API AOsrMissionTarget : public AOsrDamageable
{
	GENERATED_BODY()
public:
	AOsrMissionTarget();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector PosG() const override { return Origin; }
	virtual void ApplyHit(const FVector& P, double Damage, EOsrDamage Kind, AActor* By, int32 Part) override;

	static AOsrMissionTarget* Find(const UObject* WorldContext);

	/** Godot-frame shaft geometry. */
	FVector VentCentre = FVector::ZeroVector;   // centre of the opening at collar-top height
	double VentHalf = 2.0;
	bool bDestroyed = false;

	/** Called for a bomb that reached the ground/structure at P moving along Dir; returns true when it went into the shaft. */
	bool BombArrived(const FVector& From, const FVector& To, FVector& Impact, FString& What);
	/** Point-track snap: nearest trackable feature (vent, portals, buildings) within Radius of P. */
	bool SnapTrack(const FVector& P, double Radius, FVector& Out, FString& Name) const;
	void ResetTarget();

private:
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Pmc;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Doors;
	FVector Origin = FVector::ZeroVector;
	double PadTop = 0.0;
	struct FTrackPt { FVector P; FString Name; };
	TArray<FTrackPt> TrackPts;
	TArray<FVector> PortalMouths;       // Godot, centre of the door opening
	TArray<FVector> PortalOut;          // outward direction
	TArray<FVector> Stacks;             // stack tops (smoke)
	double DestroyT = -1.0;
	int32 SecondaryStep = 0;
	double SmokeAcc = 0.0;
	int32 ShaftTrail = 0;
	void Build();
	FVector L(const FVector& WorldG) const { return WorldG - Origin; }
};
