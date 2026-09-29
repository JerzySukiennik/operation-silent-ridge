// Nimitz-class carrier: analytic motion (25 kt racetrack into the wind, gentle roll/pitch/heave), procedural hull/deck/island meshes,
// gameplay collision, working elevator #2, jet blast deflectors, 4 arresting wires, IFLOLS + deck lights, parked jets, wake decals.
// Frames: carrier-local metres (X bow, Y starboard, Z up from the waterline, CarrierLayout.h) <-> UE world cm <-> Godot metres.
// Integration events for the mission workstream (MILESTONE_M3): OnCatapultLaunched, OnTrap(Grade, Wire), OnBolter, OnSpawnedOnDeck.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CarrierActor.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UDecalComponent;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class ADeckCrew;

/** LSO grade (debrief): standard terminology. */
UENUM()
enum class ELsoGrade : uint8 { Perfect, Ok, Fair, NoGrade, Cut, Bolter, WaveOff };

DECLARE_MULTICAST_DELEGATE_OneParam(FOnCarrierJetEvent, AActor* /*Jet*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnCarrierTrap, AActor* /*Jet*/, ELsoGrade /*Grade*/, int32 /*Wire 1..4*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCarrierSpawn, APawn* /*Pawn spawned on deck*/);

struct FCarrierPose
{
	FVector PosUE = FVector::ZeroVector;     // waterline centre, cm
	FQuat RotUE = FQuat::Identity;
	FVector VelUE = FVector::ZeroVector;     // cm/s
	double YawDeg = 0.0;
};

UCLASS()
class SILENTRIDGEUE_API ACarrierActor : public AActor
{
	GENERATED_BODY()
public:
	ACarrierActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static ACarrierActor* Get(const UObject* WorldContext);
	static FString GradeText(ELsoGrade G);

	// ---- integration events (mission workstream)
	FOnCarrierJetEvent OnCatapultLaunched;
	FOnCarrierTrap OnTrap;
	FOnCarrierJetEvent OnBolter;
	FOnCarrierJetEvent OnWaveOff;
	FOnCarrierSpawn OnSpawnedOnDeck;
	/** Last pawn spawned on deck (for listeners that bind after the first spawn). */
	TWeakObjectPtr<APawn> LastSpawnedOnDeck;

	// ---- motion
	static constexpr double SpeedMs = 25.0 * 0.514444;
	FCarrierPose Evaluate(double T) const;
	const FCarrierPose& Pose() const { return Cur; }
	double Clock() const;
	FVector VelocityGodot() const { return FVector(Cur.VelUE.X, Cur.VelUE.Z, Cur.VelUE.Y) * 0.01; }

	// ---- frames (Local = carrier-local metres)
	FTransform Xform() const { return FTransform(Cur.RotUE, Cur.PosUE); }
	FVector LocalToUE(const FVector& LocalM) const { return Xform().TransformPosition(LocalM * 100.0); }
	FVector UEToLocal(const FVector& WorldCm) const { return Xform().InverseTransformPosition(WorldCm) * 0.01; }
	FVector LocalToGodot(const FVector& LocalM) const;
	FVector GodotToLocal(const FVector& G) const;
	FQuat LocalRotToUE(const FQuat& Local) const { return Cur.RotUE * Local; }
	/** Level (roll/pitch-free) frame yawed with the ship: for glideslope / lineup geometry (IFLOLS is stabilized). */
	FVector UEToStabilized(const FVector& WorldCm, const FVector& OriginLocalM) const;

	// ---- deck geometry queries (local metres)
	static bool InDeckOutline(double X, double Y);
	/** Point is inside the solid hull / island / deck plate (crash volume). */
	static bool InsideShip(const FVector& LocalM);
	static FVector2D LandDir();
	static FVector2D LandPoint(double S, double Lat);
	/** Landing-area coordinates of a local point: X = distance from the ramp along the centreline, Y = lateral (+ starboard). */
	static FVector2D ToLanding(const FVector2D& P);

	// ---- gameplay devices
	void SetJbd(int32 CatIndex, bool bUp);
	double JbdAngle(int32 CatIndex) const { return JbdAng.IsValidIndex(CatIndex) ? JbdAng[CatIndex] : 0.0; }
	/** Wire pulled out by an arrested hook (index 0..3), or -1 to relax all. HookLocal = hook point in local metres. */
	void SetWirePull(int32 Wire, const FVector& HookLocal);
	/** IFLOLS state: ball deviation (deg, + high), visible, wave-off flashing, cut lights. */
	void SetIflols(double BallDevDeg, bool bBall, bool bWaveOff, bool bCut);
	/** Working elevator: 0 = flight deck, 1 = hangar deck. */
	void CommandElevator(bool bDown) { bElevatorDown = bDown; }
	bool IsElevatorDown() const { return ElevatorPos > 0.99; }
	bool IsElevatorMoving() const { return ElevatorPos > 0.01 && ElevatorPos < 0.99; }
	double ElevatorZ() const;   // local metres (platform top)

	UPROPERTY() TObjectPtr<ADeckCrew> Crew;

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Collision;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Hull;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Deck;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Hangar;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Elevator;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> ElevatorBox;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Jbds;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Wires;          // 2 halves per wire
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> LensLights;     // IFLOLS: 12 cells, 2x datum rows, wave-off, cut
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> LensMids;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> DeckLights;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> ParkedParts;
	UPROPERTY() TArray<TObjectPtr<UDecalComponent>> Wake;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> WakeMids;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> HangarLamps;

private:
	FCarrierPose Cur;
	FVector2D Dir2D = FVector2D(1, 0);
	FVector StartUE = FVector::ZeroVector;
	double TimeOffset = 0.0;
	TArray<double> JbdAng, JbdTarget;
	bool bElevatorDown = false;
	double ElevatorPos = 0.0;
	double LensTime = 0.0;
	double BallDev = 0.0;
	bool bBallOn = false, bWaveOffOn = false, bCutOn = false;
	void BuildLens();
	void BuildLights();
	void BuildParked();
	void BuildWake();
	void UpdateWake();
	void UpdateLens(double Dt);
	void UpdateDevices(double Dt);
	void PlaceWire(int32 Wire, const FVector& MidLocal);
	UStaticMeshComponent* AddMesh(const TCHAR* Name, const TCHAR* Path, USceneComponent* Parent = nullptr);
};
