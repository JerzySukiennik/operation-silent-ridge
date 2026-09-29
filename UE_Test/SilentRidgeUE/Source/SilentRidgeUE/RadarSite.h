// Enemy air defence sites: radar detection by terrain line of sight (OsrTerrain ray march + 4/3-earth curvature), radar horizon,
// range/RCS falloff, Doppler notch and low-level clutter; track quality per target, IADS cueing from the search radar, SAM launch
// logic (S-400-class fire unit, Pantsir, Tor), Pantsir 30 mm guns. Also the RWR picture a player's jet receives from every emitter.
#pragma once

#include "CoreMinimal.h"
#include "MissionCombat.h"
#include "RadarSite.generated.h"

class UProceduralMeshComponent;
class AOsrMissile;

UENUM()
enum class EOsrSiteType : uint8 { SearchRadar, S400, Pantsir, Tor };

UENUM()
enum class EOsrRwr : uint8 { None, Search, Track, Launch };

/** One RWR contact for a given receiver. */
struct FOsrRwrContact
{
	FString Symbol;            // "SR", "S4", "22", "15", "57" (fighter radar), "M" missile
	FVector From = FVector::ZeroVector;
	EOsrRwr State = EOsrRwr::None;
	double Severity = 0.0;     // 0..1 (ring position: closer to the centre = more dangerous)
	bool bNew = false;
};

UCLASS()
class SILENTRIDGEUE_API AOsrRadarSite : public AOsrDamageable
{
	GENERATED_BODY()
public:
	AOsrRadarSite();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector PosG() const override { return Base; }
	virtual void OnKilled(AActor* By) override;

	void Setup(EOsrSiteType InType, const FVector2D& At, double FacingYaw);
	EOsrSiteType Type = EOsrSiteType::SearchRadar;

	// radar
	FVector Base = FVector::ZeroVector;      // ground at the site
	FVector Antenna = FVector::ZeroVector;   // radar phase centre
	double DetectRange = 40000.0;            // vs the F-35 (very low RCS) in the clear
	double ScanPeriod = 0.25;                // detection opportunities
	bool bEmitting = true;
	/** Probability of detection per look (0..1) for a target at P with velocity V; 0 when masked. Why = reason (for debug). */
	double DetectionChance(const FVector& P, const FVector& V, bool& bLos, FString* Why = nullptr) const;

	// per-target track
	struct FTrack { TWeakObjectPtr<AActor> Target; double Q = 0.0; double LockTime = 0.0; double LastSeen = -99.0; bool bLos = false; bool bCounted = false; double LastLaunch = -99.0; int32 InFlight = 0; };
	TArray<FTrack> Tracks;
	const FTrack* TrackOf(const AActor* A) const;
	FTrack* TrackOf(const AActor* A);
	bool IsGuiding(const AActor* Target) const;   // track good enough to command-guide a missile now
	/** Chaff dispensed by Target: may break the track (more likely beaming / low). */
	void OnChaff(const AActor* Target, const FVector& TargetVel);

	// weapons
	int32 Missiles = 0;
	double MaxRange = 30000.0, MinRange = 800.0, MinAlt = 10.0, MaxAlt = 25000.0;
	double LaunchInterval = 8.0;
	int32 MaxInFlight = 1;
	FString Symbol;

	/** RWR contact this site presents to a receiver (none when not illuminating it). */
	bool RwrFor(const AActor* Receiver, FOsrRwrContact& Out) const;
	/** All emitters + missiles homing on Receiver. */
	static void RwrPicture(UWorld* W, const AActor* Receiver, TArray<FOsrRwrContact>& Out);
	/** Engagement markers for the TGP/HUD: all live sites. */
	static TArray<AOsrRadarSite*> All(UWorld* W);

	void ResetSite();

private:
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Pmc;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Rotor;   // rotating / trainable antenna or turret
	double FacingYaw = 0.0;
	double ScanAcc = 0.0;
	double RotorAngle = 0.0;
	double Now = 0.0;
	double GunAcc = 0.0;
	int32 GunHits = 0;
	TArray<FVector> Launchers;              // tube mouths (Godot) for launch positions
	int32 NextTube = 0;
	int32 SmokeTrail = 0;
	void BuildMesh();
	void UpdateTracks(double Dt);
	void UpdateLaunches();
	void UpdateGuns(double Dt);
	void Launch(AActor* Target);
	TWeakObjectPtr<AOsrRadarSite> Cue;      // IADS search radar
};
