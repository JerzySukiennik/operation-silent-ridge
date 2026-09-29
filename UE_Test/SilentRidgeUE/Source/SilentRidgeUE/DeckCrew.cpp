// Deck crew placement, jersey colours and procedural hand signals on the UE mannequin.
#include "DeckCrew.h"
#include "CarrierActor.h"
#include "CarrierLayout.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* Crew_MANNY = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
	const TCHAR* Crew_IDLE = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle");
	const TCHAR* Crew_WALK = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd");
	const FLinearColor Crew_YELLOW(0.85f, 0.62f, 0.02f), Crew_GREEN(0.06f, 0.35f, 0.08f), Crew_WHITE(0.8f, 0.8f, 0.78f), Crew_BLUE(0.03f, 0.1f, 0.45f);
	const FLinearColor Crew_BROWN(0.22f, 0.12f, 0.05f), Crew_PURPLE(0.3f, 0.05f, 0.35f), Crew_REDJ(0.6f, 0.04f, 0.03f), Crew_KHAKI(0.36f, 0.3f, 0.18f);
	constexpr double Crew_WALK_SPEED = 1.5;   // m/s
}

ADeckCrew::ADeckCrew()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	GetRootComponent()->SetMobility(EComponentMobility::Movable);
}

FVector ADeckCrew::Dir(double F, double R, double U, bool bRight) const
{
	return (FwdCS * F + RightCS * (bRight ? R : -R) + UpCS * U).GetSafeNormal();
}

void ADeckCrew::CatHome(int32 CatIndex, ECrewRole InRole, FVector2D& OutPos, double& OutYaw)
{
	using namespace CarrierLayout;
	const FCat& C = Cats[FMath::Clamp(CatIndex, 0, 3)];
	const double H = FMath::DegreesToRadians(C.HdgDeg);
	const FVector2D D(FMath::Cos(H), FMath::Sin(H)), N(-D.Y, D.X);
	const FVector2D S(C.X, C.Y);
	const double Side = C.Y > 0.0 ? -1.0 : 1.0;   // inboard side of the cat
	switch (InRole)
	{
	case ECrewRole::Shooter: OutPos = S + D * 6.0 + N * (Side * 13.0); OutYaw = C.HdgDeg + (Side > 0 ? -120.0 : 120.0); break;
	case ECrewRole::Hook1: OutPos = S + D * 3.0 + N * 9.5; OutYaw = C.HdgDeg - 150.0; break;
	case ECrewRole::Hook2: OutPos = S + D * 3.0 - N * 9.5; OutYaw = C.HdgDeg + 150.0; break;
	case ECrewRole::Checker: OutPos = S - D * 4.0 + N * (Side * 10.0); OutYaw = C.HdgDeg + (Side > 0 ? -90.0 : 90.0); break;
	default: OutPos = S + D * 14.0; OutYaw = C.HdgDeg + 180.0; break;
	}
}

void ADeckCrew::Add(ECrewRole InRole, const FVector2D& Pos, double Yaw, const FLinearColor& Jersey, bool bPoseable, const FLinearColor& Pants)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Crew_MANNY);
	UMaterialInterface* Suit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carrier/Materials/M_CrewSuit.M_CrewSuit"));
	UStaticMesh* Helmet = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Carrier/Meshes/SM_Helmet.SM_Helmet"));
	if (!Mesh) { return; }
	FMember M;
	M.Role = InRole;
	M.Pos = M.Target = Pos;
	M.Yaw = M.TargetYaw = Yaw;
	M.Phase = FMath::FRandRange(0.0, 6.0);
	USkinnedMeshComponent* C = nullptr;
	if (bPoseable)
	{
		UPoseableMeshComponent* P = NewObject<UPoseableMeshComponent>(this);
		P->SetSkinnedAssetAndUpdate(Mesh);
		M.Pose = P;
		C = P;
	}
	else
	{
		USkeletalMeshComponent* S = NewObject<USkeletalMeshComponent>(this);
		S->SetSkeletalMesh(Mesh);
		S->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		M.Anim = S;
		C = S;
	}
	C->SetupAttachment(GetRootComponent());
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->bReceivesDecals = false;
	UMaterialInstanceDynamic* Mid = Suit ? UMaterialInstanceDynamic::Create(Suit, this) : nullptr;
	if (Mid)
	{
		Mid->SetVectorParameterValue(TEXT("Torso"), Jersey);
		Mid->SetVectorParameterValue(TEXT("Pants"), Pants);
		Mid->SetVectorParameterValue(TEXT("Gloves"), InRole == ECrewRole::Lso || InRole == ECrewRole::LsoBackup ? FLinearColor(0.05f, 0.04f, 0.03f) : FLinearColor(0.35f, 0.3f, 0.2f));
		Mid->SetScalarParameterValue(TEXT("Stripes"), 1.0f);
		for (int32 I = 0; I < Mesh->GetMaterials().Num(); ++I) { C->SetMaterial(I, Mid); }
		Mids.Add(Mid);
	}
	C->RegisterComponent();
	if (M.Anim)
	{
		if (UAnimSequence* A = LoadObject<UAnimSequence>(nullptr, Crew_IDLE)) { M.Anim->PlayAnimation(A, true); M.Anim->SetPosition(float(M.Phase)); }
	}
	if (Helmet)
	{
		UStaticMeshComponent* H = NewObject<UStaticMeshComponent>(this);
		H->SetStaticMesh(Helmet);
		H->SetupAttachment(C, TEXT("head"));
		H->SetRelativeTransform(HelmetRel);
		H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (UMaterialInterface* HM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carrier/Materials/M_HelmetShell.M_HelmetShell")))
		{
			UMaterialInstanceDynamic* HMid = UMaterialInstanceDynamic::Create(HM, this);
			HMid->SetVectorParameterValue(TEXT("Color"), Jersey * 0.9f);
			H->SetMaterial(0, HMid);
			Mids.Add(HMid);
		}
		H->RegisterComponent();
		Keep.Add(H);
	}
	Keep.Add(C);
	Crew.Add(M);
	Place(Crew.Last());
}

void ADeckCrew::Init(ACarrierActor* InCarrier)
{
	using namespace CarrierLayout;
	Carrier = InCarrier;
	AttachToActor(InCarrier, FAttachmentTransformRules::KeepRelativeTransform);
	SetActorRelativeTransform(FTransform::Identity);
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, Crew_MANNY);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("OSR crew: mannequin missing"));
		return;
	}
	// pose drivers (invisible, always animating)
	auto Driver = [&](const TCHAR* Anim)
	{
		USkeletalMeshComponent* D = NewObject<USkeletalMeshComponent>(this);
		D->SetSkeletalMesh(Mesh);
		D->SetupAttachment(GetRootComponent());
		D->SetVisibility(false);
		D->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		D->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		D->RegisterComponent();
		if (UAnimSequence* A = LoadObject<UAnimSequence>(nullptr, Anim)) { D->PlayAnimation(A, true); }
		PrimaryActorTick.AddPrerequisite(D, D->PrimaryComponentTick);
		return D;
	};
	IdleDriver = Driver(Crew_IDLE);
	WalkDriver = Driver(Crew_WALK);
	// component-space frame of the mannequin (faces +Y) and the helmet offset from the reference pose
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	auto RefCS = [&](FName B) { const int32 I = Ref.FindBoneIndex(B); return I != INDEX_NONE ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, I) : FTransform::Identity; };
	const FVector HR = RefCS(TEXT("hand_r")).GetLocation(), HL = RefCS(TEXT("hand_l")).GetLocation();
	RightCS = FVector(HR.X - HL.X, HR.Y - HL.Y, 0.0).GetSafeNormal();
	UpCS = FVector(0, 0, 1);
	FwdCS = FVector::CrossProduct(RightCS, UpCS).GetSafeNormal();
	if (FwdCS.Y < 0.0) { FwdCS = -FwdCS; }   // the template mannequin faces +Y
	const FTransform Head = RefCS(TEXT("head"));
	const FTransform Want(FRotationMatrix::MakeFromXZ(FwdCS, UpCS).ToQuat(), Head.GetLocation() + FwdCS * 2.5 + UpCS * 9.5, FVector(1.08));
	HelmetRel = Want.GetRelativeTransform(Head);
	UE_LOG(LogTemp, Display, TEXT("OSR crew: right=%s fwd=%s head=%s"), *RightCS.ToString(), *FwdCS.ToString(), *Head.GetLocation().ToString());

	FVector2D P; double Y;
	const FVector2D Jet(PlayerJet.X, PlayerJet.Y);
	Add(ECrewRole::Director, Jet + FVector2D(14.0, -5.0), PlayerJet.Z + 180.0, Crew_YELLOW, true);
	CatHome(0, ECrewRole::Shooter, P, Y); Add(ECrewRole::Shooter, P, Y, Crew_YELLOW, true, Crew_KHAKI);
	CatHome(0, ECrewRole::Hook1, P, Y); Add(ECrewRole::Hook1, P, Y, Crew_GREEN, true);
	CatHome(0, ECrewRole::Hook2, P, Y); Add(ECrewRole::Hook2, P, Y, Crew_GREEN, true);
	CatHome(0, ECrewRole::Checker, P, Y); Add(ECrewRole::Checker, P, Y, Crew_WHITE, true);
	Add(ECrewRole::Lso, LsoPlatform + FVector2D(0.5, -2.2), LandHdgDeg + 180.0, Crew_WHITE, true, Crew_KHAKI);
	Add(ECrewRole::LsoBackup, LsoPlatform + FVector2D(-1.6, -2.6), LandHdgDeg + 170.0, Crew_WHITE, false, Crew_KHAKI);
	// ambient crew around the parked row, island and elevators
	const struct { double X, Y, Yaw; FLinearColor C; } Amb[] = {
		{-44.0, 16.5, 120.0, Crew_BLUE}, {-50.5, 17.2, 60.0, Crew_BROWN}, {-92.0, 16.0, 200.0, Crew_REDJ}, {-106.0, 15.5, 10.0, Crew_BLUE},
		{-20.0, 19.5, -60.0, Crew_PURPLE}, {-4.0, 21.0, 180.0, Crew_WHITE}, {33.0, 21.0, 150.0, Crew_BROWN}, {-118.0, 16.0, 90.0, Crew_GREEN},
	};
	for (const auto& A : Amb) { Add(ECrewRole::Ambient, FVector2D(A.X, A.Y), A.Yaw, A.C, false); }
}

ADeckCrew::FMember* ADeckCrew::Find(ECrewRole InRole)
{
	for (FMember& M : Crew) { if (M.Role == InRole) { return &M; } }
	return nullptr;
}

const ADeckCrew::FMember* ADeckCrew::Find(ECrewRole InRole) const
{
	for (const FMember& M : Crew) { if (M.Role == InRole) { return &M; } }
	return nullptr;
}

FVector2D ADeckCrew::PositionOf(ECrewRole InRole) const
{
	const FMember* M = Find(InRole);
	return M ? M->Pos : FVector2D::ZeroVector;
}

void ADeckCrew::Command(ECrewRole InRole, const FVector2D& Pos, double YawDeg, ECrewSignal Signal)
{
	if (FMember* M = Find(InRole))
	{
		M->Target = Pos;
		M->TargetYaw = YawDeg;
		if (M->Signal != Signal) { M->SignalT = 0.0; }
		M->Signal = Signal;
	}
}

void ADeckCrew::Face(ECrewRole InRole, const FVector2D& LookAt)
{
	if (FMember* M = Find(InRole))
	{
		const FVector2D D = LookAt - M->Pos;
		if (D.SizeSquared() > 0.01) { M->TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)); }
	}
}

void ADeckCrew::StartLaunchSignal()
{
	if (FMember* M = Find(ECrewRole::Shooter)) { M->Signal = ECrewSignal::Launch; M->SignalT = 0.0; }
}

void ADeckCrew::Place(FMember& M)
{
	USkinnedMeshComponent* C = M.Pose ? static_cast<USkinnedMeshComponent*>(M.Pose) : static_cast<USkinnedMeshComponent*>(M.Anim);
	if (!C) { return; }
	C->SetRelativeLocationAndRotation(FVector(M.Pos.X, M.Pos.Y, CarrierLayout::DeckZ) * 100.0, FRotator(0.0, M.Yaw - 90.0, 0.0));
}

void ADeckCrew::AimArm(UPoseableMeshComponent* P, bool bRight, const FVector& UpperDir, const FVector& ForeDir)
{
	const FName Up = bRight ? TEXT("upperarm_r") : TEXT("upperarm_l");
	const FName Lo = bRight ? TEXT("lowerarm_r") : TEXT("lowerarm_l");
	const FName Hd = bRight ? TEXT("hand_r") : TEXT("hand_l");
	auto Aim = [P](FName Bone, FName Child, const FVector& D)
	{
		const FVector A = P->GetBoneLocationByName(Bone, EBoneSpaces::ComponentSpace);
		const FVector B = P->GetBoneLocationByName(Child, EBoneSpaces::ComponentSpace);
		const FVector Cur = (B - A).GetSafeNormal();
		if (Cur.IsNearlyZero()) { return; }
		const FQuat Delta = FQuat::FindBetweenNormals(Cur, D);
		const FQuat R = P->GetBoneRotationByName(Bone, EBoneSpaces::ComponentSpace).Quaternion();
		P->SetBoneRotationByName(Bone, (Delta * R).Rotator(), EBoneSpaces::ComponentSpace);
	};
	Aim(Up, Lo, UpperDir);
	Aim(Lo, Hd, ForeDir);
}

void ADeckCrew::Lean(UPoseableMeshComponent* P, double Deg)
{
	const FName B = TEXT("spine_01");
	const FQuat R = P->GetBoneRotationByName(B, EBoneSpaces::ComponentSpace).Quaternion();
	P->SetBoneRotationByName(B, (FQuat(RightCS, FMath::DegreesToRadians(Deg)) * R).Rotator(), EBoneSpaces::ComponentSpace);
}

void ADeckCrew::ApplySignal(FMember& M, double Dt)
{
	UPoseableMeshComponent* P = M.Pose;
	if (!P) { return; }
	P->CopyPoseFromSkeletalComponent(M.bMoving ? WalkDriver.Get() : IdleDriver.Get());
	M.SignalT += Dt;
	const double T = Time + M.Phase;
	const double Osc = 0.5 + 0.5 * FMath::Sin(T * 5.5);
	switch (M.Signal)
	{
	case ECrewSignal::ComeAhead:
		for (int32 S = 0; S < 2; ++S)
		{
			const bool bR = S == 1;
			AimArm(P, bR, Dir(0.35, 0.45, 0.6, bR), FMath::Lerp(Dir(0.25, 0.1, 1.0, bR), Dir(-0.6, 0.15, 0.55, bR), Osc).GetSafeNormal());
		}
		break;
	case ECrewSignal::TurnLeft:
	case ECrewSignal::TurnRight:
	{
		// the director faces the pilot: the pilot's left is the director's right
		const bool bPointR = M.Signal == ECrewSignal::TurnLeft;
		AimArm(P, bPointR, Dir(0.15, 1.0, -0.25, bPointR), Dir(0.2, 1.0, -0.35, bPointR));
		AimArm(P, !bPointR, Dir(0.35, 0.45, 0.6, !bPointR), FMath::Lerp(Dir(0.25, 0.1, 1.0, !bPointR), Dir(-0.6, 0.15, 0.55, !bPointR), Osc).GetSafeNormal());
		break;
	}
	case ECrewSignal::Stop:
		AimArm(P, true, Dir(0.15, 0.4, 0.95, true), Dir(0.1, -0.9, 0.45, true));
		AimArm(P, false, Dir(0.15, 0.4, 0.95, false), Dir(0.1, -0.9, 0.45, false));
		break;
	case ECrewSignal::RunUp:
	{
		const double A = T * 9.0;
		AimArm(P, true, Dir(0.1, 0.25, 1.0, true), Dir(0.35 * FMath::Cos(A), 0.35 * FMath::Sin(A), 1.0, true));
		AimArm(P, false, Dir(0.4, 0.8, -0.2, false), Dir(0.8, 0.5, -0.1, false));
		break;
	}
	case ECrewSignal::Launch:
	{
		const double S = M.SignalT;
		if (S < 0.7) { AimArm(P, true, Dir(0.1, 0.2, 1.0, true), Dir(0.1, 0.1, 1.0, true)); }
		else if (S < 1.5)
		{
			const double K = FMath::Clamp((S - 0.7) / 0.5, 0.0, 1.0);
			Lean(P, 40.0 * K);
			AimArm(P, true, FMath::Lerp(Dir(0.1, 0.2, 1.0, true), Dir(0.55, 0.1, -0.8, true), K).GetSafeNormal(), Dir(0.6, 0.0, -0.8, true));
		}
		else
		{
			Lean(P, FMath::Max(0.0, 40.0 - (S - 1.5) * 80.0));
			AimArm(P, true, Dir(1.0, 0.05, 0.1, true), Dir(1.0, 0.0, 0.12, true));
		}
		break;
	}
	case ECrewSignal::Crouch:
		Lean(P, 45.0);
		AimArm(P, true, Dir(0.6, 0.2, -0.75, true), Dir(0.8, 0.0, -0.5, true));
		AimArm(P, false, Dir(0.6, 0.2, -0.75, false), Dir(0.8, 0.0, -0.5, false));
		break;
	case ECrewSignal::Pickle:
		AimArm(P, true, Dir(0.3, 0.2, 0.9, true), Dir(0.15, 0.0, 1.0, true));
		break;
	case ECrewSignal::ThumbUp:
		AimArm(P, true, Dir(0.8, 0.3, 0.35, true), Dir(0.3, 0.0, 1.0, true));
		break;
	case ECrewSignal::HookUp:
		// "raise the hook": arm forward, forearm flicking up
		AimArm(P, true, Dir(0.9, 0.2, 0.2, true), FMath::Lerp(Dir(1.0, 0.0, 0.0, true), Dir(0.3, 0.0, 1.0, true), Osc).GetSafeNormal());
		break;
	default:
		break;
	}
}

void ADeckCrew::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	for (FMember& M : Crew)
	{
		const FVector2D D = M.Target - M.Pos;
		const double Dist = D.Size();
		M.bMoving = Dist > 0.25;
		if (M.bMoving)
		{
			M.Pos += D / Dist * FMath::Min(Dist, Crew_WALK_SPEED * DeltaSeconds * (Dist > 6.0 ? 2.2 : 1.0));
			const double WalkYaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			M.Yaw += FMath::FindDeltaAngleDegrees(M.Yaw, WalkYaw) * FMath::Min(1.0, DeltaSeconds * 6.0);
		}
		else
		{
			M.Yaw += FMath::FindDeltaAngleDegrees(M.Yaw, M.TargetYaw) * FMath::Min(1.0, DeltaSeconds * 4.0);
		}
		Place(M);
		ApplySignal(M, DeltaSeconds);
	}
}
