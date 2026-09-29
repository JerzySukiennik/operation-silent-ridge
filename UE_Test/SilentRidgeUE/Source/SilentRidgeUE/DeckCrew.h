// Flight deck crew: UE template mannequins in jersey colours (REFERENCES 4.6) with cranial helmets. Signal crew (plane director,
// shooter, hook-up crew, LSO) are poseable meshes that copy an idle/walk driver pose and override the arms procedurally for the hand
// signals; ambient crew play the idle animation. Positions are carrier-local (the crew actor rides on the carrier).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DeckCrew.generated.h"

class ACarrierActor;
class UPoseableMeshComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM()
enum class ECrewSignal : uint8 { Idle, ComeAhead, TurnLeft, TurnRight, Stop, RunUp, Launch, Crouch, Pickle, ThumbUp, HookUp };

UENUM()
enum class ECrewRole : uint8 { Director, Shooter, Hook1, Hook2, Checker, Lso, LsoBackup, Ambient };

UCLASS()
class SILENTRIDGEUE_API ADeckCrew : public AActor
{
	GENERATED_BODY()
public:
	ADeckCrew();
	void Init(ACarrierActor* InCarrier);
	virtual void Tick(float DeltaSeconds) override;

	/** Commands a signal crew member: walk to a carrier-local spot (m), face a yaw (deg) or a point, show a signal. */
	void Command(ECrewRole InRole, const FVector2D& Pos, double YawDeg, ECrewSignal Signal);
	void Face(ECrewRole InRole, const FVector2D& LookAt);
	/** Returns the member's carrier-local position (m). */
	FVector2D PositionOf(ECrewRole InRole) const;
	/** Home positions for the launch crew around a catapult (used when idle). */
	static void CatHome(int32 CatIndex, ECrewRole InRole, FVector2D& OutPos, double& OutYaw);
	/** Launch signal timeline started (shooter): seconds since. */
	void StartLaunchSignal();

private:
	struct FMember
	{
		ECrewRole Role = ECrewRole::Ambient;
		UPoseableMeshComponent* Pose = nullptr;
		USkeletalMeshComponent* Anim = nullptr;
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Target = FVector2D::ZeroVector;
		double Yaw = 0.0, TargetYaw = 0.0;
		ECrewSignal Signal = ECrewSignal::Idle;
		double Phase = 0.0;
		double SignalT = 0.0;
		bool bMoving = false;
	};
	UPROPERTY() TObjectPtr<ACarrierActor> Carrier;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> IdleDriver;
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> WalkDriver;
	UPROPERTY() TArray<TObjectPtr<USceneComponent>> Keep;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Mids;
	TArray<FMember> Crew;
	FVector RightCS = FVector(-1, 0, 0), FwdCS = FVector(0, 1, 0), UpCS = FVector(0, 0, 1);
	double Time = 0.0;
	FTransform HelmetRel;

	FMember* Find(ECrewRole InRole);
	const FMember* Find(ECrewRole InRole) const;
	void Add(ECrewRole InRole, const FVector2D& Pos, double Yaw, const FLinearColor& Jersey, bool bPoseable, const FLinearColor& Pants = FLinearColor(0.02f, 0.03f, 0.07f));
	void ApplySignal(FMember& M, double Dt);
	void AimArm(UPoseableMeshComponent* P, bool bRight, const FVector& UpperDir, const FVector& ForeDir);
	void Lean(UPoseableMeshComponent* P, double Deg);
	FVector Dir(double F, double R, double U, bool bRight) const;
	void Place(FMember& M);
};
