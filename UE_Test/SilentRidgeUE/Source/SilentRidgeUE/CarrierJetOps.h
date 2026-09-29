// Carrier ops for one F-35C (component on AOsrJetPawn, implements IOsrJetDeckHook): deck states (parked, taxi with nosewheel
// steering + brakes, catapult hook-up / tension / salute / stroke, touchdown roll, arrestment, bolter), animated gear (baked Godot
// animation) + tailhook, Delta Flight Path approach mode with auto-throttle, LSO/IFLOLS feed, launch-crew direction.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OsrJetPawn.h"
#include "LsoMonitor.h"
#include "CarrierJetOps.generated.h"

class ACarrierActor;
class UStaticMeshComponent;
class USoundBase;

UENUM()
enum class EDeckState : uint8 { Airborne, Parked, Taxi, Hookup, OnCat, Launch, Rollout, Arrest, Stopped };

UCLASS()
class SILENTRIDGEUE_API UCarrierJetOps : public UActorComponent, public IOsrJetDeckHook
{
	GENERATED_BODY()
public:
	UCarrierJetOps();
	void Setup(AOsrJetPawn* InJet, ACarrierActor* InCarrier);
	/** Puts the jet on deck, chocked, gear down, hook up, engine idle (carrier-local metres, heading deg). */
	void ParkAt(const FVector2D& Pos, double YawDeg);
	/** Puts the jet in the air in carrier-relative terms (autotest approach setup): local position m, heading deg, airspeed m/s. */
	void PlaceOnApproach(double RangeM, double SpeedMs);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// IOsrJetDeckHook
	virtual bool PreStep(double Dt) override;
	virtual void PostStep(double Dt) override;
	virtual void PostInterpolate() override;
	virtual bool OverrideCollision() override;
	virtual bool HandleRespawn() override;
	virtual bool SuppressImpactWarning() const override;

	bool OnDeck() const { return State != EDeckState::Airborne; }
	bool CanExit() const;
	void Board();                 // pilot climbed in: chocks off, brakes on
	void Unman();                 // pilot climbed out
	FString StateName() const;
	FVector2D DeckPos() const { return Pos; }
	double DeckYaw() const { return Yaw; }
	double DeckSpeed() const { return Speed; }
	/** Cockpit-side boarding point (carrier-local m). */
	FVector2D LadderPoint() const;
	/** Nozzle (carrier-local m) and blast direction for jet-blast effects on people. */
	void Blast(FVector& OutNozzleUE, FVector& OutDirUE, double& OutStrength) const;

	// state (HUD, crew, autotest)
	EDeckState State = EDeckState::Parked;
	double StateT = 0.0;
	bool bManned = false;
	bool bHookDown = false;
	double HookAngle = 0.0;
	double GearPhase = 1.0;
	bool bDfp = false;
	bool bSaluted = false;
	bool bAutoSalute = false;     // autotest
	int32 CatIndex = -1;
	FString Prompt;
	FLsoMonitor Lso;
	bool bLsoGroove = false;
	// results
	double LaunchEndKt = 0.0, LaunchStrokeS = 0.0, ArrestS = 0.0, MaxDecelG = 0.0, TouchSinkMs = 0.0;
	int32 LastWire = 0;
	ELsoGrade LastGrade = ELsoGrade::Ok;
	double ResultT = -99.0;       // time of the last trap/bolter/wave-off (HUD debrief)
	FString ResultText;

private:
	UPROPERTY() TObjectPtr<AOsrJetPawn> Jet;
	UPROPERTY() TObjectPtr<ACarrierActor> Carrier;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> GearParts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Hook;
	UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> Sounds;

	// deck kinematics (carrier-local)
	FVector2D Pos = FVector2D::ZeroVector;
	double Yaw = 0.0, Speed = 0.0, Pitch = 0.0;
	FVector PrevL = FVector::ZeroVector, CurL = FVector::ZeroVector;   // x, y, yaw for render interpolation
	double PrevPitch = 0.0, CurPitch = 0.0;
	double Accum = 0.0;
	double CatS = 0.0, CatV = 0.0, LaunchWait = -1.0;
	int32 Wire = -1;
	double ArrestV0 = 0.0, ArrestAcc = 0.0;
	FVector HookPrevL = FVector::ZeroVector;
	bool bHookPrevValid = false;
	bool bTouched = false;
	bool bPrevDpadDown = false, bPrevDpadUp = false, bPrevSalute = false;
	double ApcInt = 0.0, ApcLever = 0.45;
	double LastLeverSet = -1.0;
	int32 HookupCat = -1;
	FVector2D HookupFromPos = FVector2D::ZeroVector;
	double HookupFromYaw = 0.0;
	double LastCallT = -99.0;
	double NoTouchT = 0.0;   // after a release from the deck: no touchdown / structure checks while leaving the deck
	int32 LastCallCount = 0;

	void BuildParts();
	void UpdateParts(double Dt);
	void ReadInput();
	void SetState(EDeckState S);
	void DeckStep(double Dt);
	void ApplyDeckPose(double Dt);
	void Release(const FVector& VelLocalMs, double PitchDeg);
	FTransform LocalJetTransform(const FVector2D& P, double YawDeg, double PitchDeg) const;
	FTransform JetUE() const;
	FVector HookTipLocal() const;
	FVector PointLocal(const FVector& JetLocalCm) const;
	void Touchdown(const FVector& VelRelLocal);
	void Engage(int32 W);
	void UpdateDfp(double Dt);
	void UpdateLso(double Dt);
	void DirectCrew();
	void CheckHookup();
	void Play(FName Name, float Vol = 1.0f);
	double Now() const;
};
