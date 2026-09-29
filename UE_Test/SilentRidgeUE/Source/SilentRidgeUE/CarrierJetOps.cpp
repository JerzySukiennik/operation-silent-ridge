// F-35C carrier ops: deck kinematics, catapult, arresting gear, bolter, gear/hook animation, DFP + auto-throttle, LSO feed, crew cues.
#include "CarrierJetOps.h"
#include "CarrierActor.h"
#include "CarrierLayout.h"
#include "CarrierF35Anim.h"
#include "CarrierSubsystem.h"
#include "DeckCrew.h"
#include "OsrPadInput.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

using namespace OsrMath;

namespace
{
	constexpr double Ops_SIM_DT = 1.0 / 120.0;
	constexpr double Ops_KT = 0.514444;
	constexpr double Ops_G0 = 9.80665;
	const double Ops_GEAR_H = -CarrierF35Anim::MainWheelL.Z * 0.01;                                   // origin above the deck on the wheels, m
	const double Ops_NOSE_X = CarrierF35Anim::NoseWheel.X * 0.01;                                     // nose gear ahead of the origin, m
	const double Ops_WHEELBASE = (CarrierF35Anim::NoseWheel.X - CarrierF35Anim::MainWheelL.X) * 0.01;
	constexpr double Ops_VEND_AIR = 150.0 * Ops_KT;                                                       // end-of-stroke airspeed
	constexpr double Ops_DFP_GS = 3.5;
	constexpr double Ops_HOOK_DOWN_AIR = 35.0;

	FVector Ops_Fwd2(double YawDeg) { const double A = FMath::DegreesToRadians(YawDeg); return FVector(FMath::Cos(A), FMath::Sin(A), 0.0); }
	FVector2D Ops_F2(double YawDeg) { const double A = FMath::DegreesToRadians(YawDeg); return FVector2D(FMath::Cos(A), FMath::Sin(A)); }
	FVector2D Ops_CatDir(int32 C) { return Ops_F2(CarrierLayout::Cats[C].HdgDeg); }
	FVector2D Ops_CatStart(int32 C) { return FVector2D(CarrierLayout::Cats[C].X, CarrierLayout::Cats[C].Y); }
	// jet-local check points (UE cm): nose, tail, wingtips, belly, nozzle
	const FVector Ops_CRASH_PTS[] = {FVector(830, 0, 0), FVector(-700, 0, 60), FVector(-150, -650, -20), FVector(-150, 650, -20), FVector(0, 0, -140), FVector(-583, 0, -33)};
}

UCarrierJetOps::UCarrierJetOps()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

double UCarrierJetOps::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void UCarrierJetOps::Setup(AOsrJetPawn* InJet, ACarrierActor* InCarrier)
{
	Jet = InJet;
	Carrier = InCarrier;
	Jet->DeckHook = this;
	Jet->PrimaryActorTick.AddPrerequisite(this, PrimaryComponentTick);
	Jet->AddTickPrerequisiteActor(Carrier);
	AddTickPrerequisiteActor(Carrier);
	BuildParts();
	for (const TCHAR* N : {TEXT("radio_click"), TEXT("cat_shot"), TEXT("wire_trap"), TEXT("pilot_ball"), TEXT("lso_roger_ball"), TEXT("lso_power"), TEXT("lso_low"),
		TEXT("lso_high"), TEXT("lso_come_left"), TEXT("lso_right"), TEXT("lso_slow"), TEXT("lso_fast"), TEXT("lso_easy"), TEXT("lso_waveoff"), TEXT("lso_bolter"), TEXT("lso_hook")})
	{
		if (USoundBase* S = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Carrier/Audio/%s.%s"), N, N))) { Sounds.Add(FName(N), S); }
	}
	if (USoundBase* S = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/gear_clunk.gear_clunk"))) { Sounds.Add(TEXT("gear_clunk"), S); }
}

void UCarrierJetOps::BuildParts()
{
	UStaticMeshComponent* Body = Jet->GetJetMesh();
	if (UStaticMesh* B = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Carrier/F35/SM_F35Body.SM_F35Body"))) { Body->SetStaticMesh(B); }
	for (int32 K = 0; K < CarrierF35Anim::NumParts; ++K)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Jet);
		C->SetupAttachment(Body);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->bReceivesDecals = false;
		const FString N = CarrierF35Anim::PartMeshes[K];
		if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Carrier/F35/%s.%s"), *N, *N))) { C->SetStaticMesh(M); }
		C->RegisterComponent();
		GearParts.Add(C);
	}
	// launch bar (F-35C nose gear): a bar from the nose axle forward, down onto the shuttle when hooked up
	if (GearParts.IsValidIndex(1))
	{
		const CarrierF35Anim::FKey& Kd = CarrierF35Anim::Keys[1][CarrierF35Anim::NumSamples - 1];
		const FTransform Down(FMatrix(FVector(Kd.X[0], Kd.X[1], Kd.X[2]), FVector(Kd.Y[0], Kd.Y[1], Kd.Y[2]), FVector(Kd.Z[0], Kd.Z[1], Kd.Z[2]), FVector(Kd.L[0], Kd.L[1], Kd.L[2])));
		const FVector Axle = CarrierF35Anim::NoseWheel + FVector(0.0, 0.0, 32.0);
		LaunchBar = NewObject<UStaticMeshComponent>(Jet);
		LaunchBar->SetupAttachment(GearParts[1]);
		LaunchBar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))) { LaunchBar->SetStaticMesh(Cube); }
		if (UMaterialInterface* HM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carrier/Materials/M_Hook.M_Hook"))) { LaunchBar->SetMaterial(0, HM); }
		const FTransform BarJet(FRotator(-14.0, 0.0, 0.0), Axle + FVector(55.0, 0.0, -14.0), FVector(1.1, 0.08, 0.06));
		LaunchBar->SetRelativeTransform(BarJet.GetRelativeTransform(Down));
		LaunchBar->RegisterComponent();
	}
	Hook = NewObject<UStaticMeshComponent>(Jet);
	Hook->SetupAttachment(Body);
	Hook->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Carrier/F35/SM_F35Hook.SM_F35Hook"))) { Hook->SetStaticMesh(M); }
	Hook->SetRelativeLocation(CarrierF35Anim::HookPivot);
	Hook->RegisterComponent();
	UpdateParts(0.0);
}

void UCarrierJetOps::UpdateParts(double Dt)
{
	const double Target = Jet->bGearDown ? 1.0 : 0.0;
	GearPhase = Dt > 0.0 ? MoveToward(GearPhase, Target, Dt / 5.5) : Target;
	const double F = GearPhase * (CarrierF35Anim::NumSamples - 1);
	const int32 I0 = FMath::Clamp(FMath::FloorToInt(F), 0, CarrierF35Anim::NumSamples - 2);
	const double A = F - I0;
	for (int32 K = 0; K < GearParts.Num(); ++K)
	{
		auto Q = [](const CarrierF35Anim::FKey& Kf)
		{
			return FQuat(FMatrix(FVector(Kf.X[0], Kf.X[1], Kf.X[2]), FVector(Kf.Y[0], Kf.Y[1], Kf.Y[2]), FVector(Kf.Z[0], Kf.Z[1], Kf.Z[2]), FVector::ZeroVector));
		};
		const CarrierF35Anim::FKey& K0 = CarrierF35Anim::Keys[K][I0];
		const CarrierF35Anim::FKey& K1 = CarrierF35Anim::Keys[K][I0 + 1];
		const FVector L = FMath::Lerp(FVector(K0.L[0], K0.L[1], K0.L[2]), FVector(K1.L[0], K1.L[1], K1.L[2]), A);
		GearParts[K]->SetRelativeLocationAndRotation(L, FQuat::Slerp(Q(K0), Q(K1), A).GetNormalized());
	}
	// tailhook: stowed 0 deg; down trails at 35 deg in the air, rides the deck on the ground
	double HookTarget = 0.0;
	if (bHookDown)
	{
		HookTarget = Ops_HOOK_DOWN_AIR;
		if (OnDeck())
		{
			const double Hp = (CarrierF35Anim::HookPivot.Z * 0.01 + Ops_GEAR_H);
			HookTarget = FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Hp / (CarrierF35Anim::HookArm * 0.01), 0.0, 1.0))) - Pitch, 0.0, 60.0);
		}
	}
	HookAngle = Dt > 0.0 ? MoveToward(HookAngle, HookTarget, Dt * (HookTarget > HookAngle ? 45.0 : 25.0)) : HookTarget;
	if (Hook) { Hook->SetRelativeRotation(FRotator(HookAngle, 0.0, 0.0)); }
}

// ---------------------------------------------------------------- frames

FTransform UCarrierJetOps::LocalJetTransform(const FVector2D& P, double YawDeg, double PitchDeg) const
{
	// pitch about the main-wheel contact line so the mains stay on the deck
	const FQuat Ry = FRotator(0.0, YawDeg, 0.0).Quaternion();
	const FQuat R = FRotator(PitchDeg, YawDeg, 0.0).Quaternion();
	const FVector MainC(CarrierF35Anim::MainWheelL.X, 0.0, CarrierF35Anim::MainWheelL.Z);
	const FVector DeckMain = FVector(P.X * 100.0, P.Y * 100.0, CarrierLayout::DeckZ * 100.0) + Ry.RotateVector(FVector(MainC.X, 0.0, 0.0));
	return FTransform(R, DeckMain - R.RotateVector(MainC));
}

FTransform UCarrierJetOps::JetUE() const
{
	return FTransform(AOsrJetPawn::RotToUE(Jet->Model.Orientation), AOsrJetPawn::ToUE(Jet->Model.Position));
}

FVector UCarrierJetOps::PointLocal(const FVector& JetLocalCm) const
{
	return Carrier->UEToLocal(JetUE().TransformPosition(JetLocalCm));
}

FVector UCarrierJetOps::HookTipLocal() const
{
	const double A = FMath::DegreesToRadians(HookAngle);
	const FVector Tip = CarrierF35Anim::HookPivot + FVector(-FMath::Cos(A), 0.0, -FMath::Sin(A)) * CarrierF35Anim::HookArm;
	return PointLocal(Tip);
}

FVector2D UCarrierJetOps::LadderPoint() const
{
	// boarding ladder on the left side, below the cockpit
	const FVector2D F = Ops_F2(Yaw), R(-F.Y, F.X);
	return Pos + F * 3.6 - R * 2.4;
}

void UCarrierJetOps::Blast(FVector& OutNozzleUE, FVector& OutDirUE, double& OutStrength) const
{
	const FTransform T = Jet->GetActorTransform();
	OutNozzleUE = T.TransformPosition(FVector(-583.0, 0.0, -33.0));
	OutDirUE = -T.GetRotation().GetForwardVector();
	OutStrength = Jet->bAlive ? Jet->Model.Thrust / FF35FlightModel::THRUST_MIL_SL : 0.0;
}

// ---------------------------------------------------------------- state

FString UCarrierJetOps::StateName() const
{
	switch (State)
	{
	case EDeckState::Airborne: return TEXT("airborne");
	case EDeckState::Parked: return TEXT("parked");
	case EDeckState::Taxi: return TEXT("taxi");
	case EDeckState::Hookup: return TEXT("hookup");
	case EDeckState::OnCat: return TEXT("on_cat");
	case EDeckState::Launch: return TEXT("launch");
	case EDeckState::Rollout: return TEXT("rollout");
	case EDeckState::Arrest: return TEXT("arrest");
	default: return TEXT("stopped");
	}
}

void UCarrierJetOps::SetState(EDeckState S)
{
	if (S != State) { UE_LOG(LogTemp, Display, TEXT("REPORT carrier_state %s -> t=%.1f"), *UEnum::GetValueAsString(S), Now()); }
	State = S;
	StateT = 0.0;
}

void UCarrierJetOps::ParkAt(const FVector2D& P, double YawDeg)
{
	const FTransform W = LocalJetTransform(P, YawDeg, 0.0) * Carrier->Xform();
	const FQuat Q = W.GetRotation();
	Jet->RespawnAt(AOsrJetPawn::FromUE(W.GetLocation()), AOsrJetPawn::LookingAt(AOsrJetPawn::DirToUE(Q.GetForwardVector()), AOsrJetPawn::DirToUE(Q.GetUpVector())), 0.0);
	Jet->bGearDown = true;
	Jet->Model.bGearDown = true;
	Jet->Controls.SetLever(0.0);
	Jet->Model.Engine = 0.0;
	Pos = P; Yaw = YawDeg; Speed = 0.0; Pitch = 0.0;
	CurL = PrevL = FVector(P.X, P.Y, YawDeg);
	CurPitch = PrevPitch = 0.0;
	Accum = 0.0;
	bHookDown = false;
	HookAngle = 0.0;
	bDfp = false;
	bSaluted = false;
	CatIndex = HookupCat = -1;
	Wire = -1;
	bTouched = false;
	bHookPrevValid = false;
	Lso.Reset();
	Carrier->SetWirePull(-1, FVector::ZeroVector);
	for (int32 K = 0; K < 4; ++K) { Carrier->SetJbd(K, false); }
	SetState(EDeckState::Parked);
	UpdateParts(0.0);
	ApplyDeckPose(0.0);
}

void UCarrierJetOps::PlaceOnApproach(double RangeM, double SpeedMs)
{
	using namespace CarrierLayout;
	// stabilized landing frame: the hook on a 3.5 deg ship-referenced glideslope, lined up, on-speed attitude
	const FVector2D Td = ACarrierActor::LandPoint(TouchdownS, 0.0);
	const double LandYaw = Carrier->Pose().YawDeg + LandHdgDeg;
	const FQuat Yq = FRotator(0.0, LandYaw, 0.0).Quaternion();
	const FVector TdW = Carrier->LocalToUE(FVector(Td.X, Td.Y, DeckZ));
	const FVector HookW = TdW + Yq.RotateVector(FVector(-RangeM, 0.0, RangeM * FMath::Tan(FMath::DegreesToRadians(Ops_DFP_GS)))) * 100.0;
	const double Gamma = -2.9, Aoa = FLsoMonitor::OnSpeedAoa;
	const FQuat R = FRotator(Gamma + Aoa, LandYaw, 0.0).Quaternion();
	bHookDown = true;
	HookAngle = Ops_HOOK_DOWN_AIR;
	const double A = FMath::DegreesToRadians(HookAngle);
	const FVector TipJ = CarrierF35Anim::HookPivot + FVector(-FMath::Cos(A), 0.0, -FMath::Sin(A)) * CarrierF35Anim::HookArm;
	const FVector OriginW = HookW - R.RotateVector(TipJ);
	Jet->RespawnAt(AOsrJetPawn::FromUE(OriginW), AOsrJetPawn::LookingAt(AOsrJetPawn::DirToUE(R.GetForwardVector()), AOsrJetPawn::DirToUE(R.GetUpVector())), SpeedMs);
	const FVector VelDir = FRotator(Gamma, LandYaw, 0.0).Vector();
	Jet->Model.Velocity = AOsrJetPawn::DirToUE(VelDir) * SpeedMs;
	Jet->Model.RefreshAirData();
	Jet->bGearDown = true;
	Jet->Model.bGearDown = true;
	Jet->Model.Engine = 0.5;
	Jet->Controls.SetLever(0.5);
	GearPhase = 1.0;
	bDfp = true;
	ApcInt = 0.0;
	LastLeverSet = -1.0;
	Wire = -1;
	bTouched = false;
	bHookPrevValid = false;
	Lso.Reset();
	Carrier->SetWirePull(-1, FVector::ZeroVector);
	SetState(EDeckState::Airborne);
	UpdateParts(0.0);
}

void UCarrierJetOps::Board()
{
	bManned = true;
	if (State == EDeckState::Parked) { SetState(EDeckState::Taxi); }
	Jet->Controls.SetLever(0.0);
}

void UCarrierJetOps::Unman()
{
	bManned = false;
	if (OnDeck()) { Speed = 0.0; SetState(EDeckState::Parked); }
	Jet->Controls.SetLever(0.0);
}

bool UCarrierJetOps::CanExit() const
{
	return (State == EDeckState::Stopped || State == EDeckState::Parked || State == EDeckState::Taxi) && FMath::Abs(Speed) < 0.6;
}

// ---------------------------------------------------------------- IOsrJetDeckHook

bool UCarrierJetOps::OverrideCollision()
{
	return OnDeck();
}

bool UCarrierJetOps::SuppressImpactWarning() const
{
	if (OnDeck()) { return true; }
	if (!Jet->bGearDown) { return false; }
	const FVector L = Carrier->GodotToLocal(Jet->Model.Position);
	return FVector2D(L.X, L.Y).Size() < 4500.0 && L.Z < 700.0;
}

bool UCarrierJetOps::HandleRespawn()
{
	UCarrierSubsystem* S = GetWorld()->GetSubsystem<UCarrierSubsystem>();
	if (S && S->IsDeckMode())
	{
		S->RespawnOnDeck();
		return true;
	}
	// air start: the stock respawn, carrier ops back to airborne
	State = EDeckState::Airborne;
	bDfp = false;
	Lso.Reset();
	return false;
}

bool UCarrierJetOps::PreStep(double Dt)
{
	Accum -= Dt;
	if (State == EDeckState::Airborne)
	{
		if (bDfp) { UpdateDfp(Dt); }
		return false;
	}
	DeckStep(Dt);
	if (State == EDeckState::Airborne) { return false; }   // just released (catapult end / bolter / rolled off the deck)
	ApplyDeckPose(Dt);
	return true;
}

void UCarrierJetOps::ApplyDeckPose(double Dt)
{
	PrevL = CurL;
	PrevPitch = CurPitch;
	CurL = FVector(Pos.X, Pos.Y, Yaw);
	CurPitch = Pitch;
	const FTransform W = LocalJetTransform(Pos, Yaw, Pitch) * Carrier->Xform();
	const FQuat Q = W.GetRotation();
	FF35FlightModel& M = Jet->Model;
	M.Position = AOsrJetPawn::FromUE(W.GetLocation());
	M.Orientation = AOsrJetPawn::LookingAt(AOsrJetPawn::DirToUE(Q.GetForwardVector()), AOsrJetPawn::DirToUE(Q.GetUpVector()));
	M.Velocity = Carrier->VelocityGodot() + AOsrJetPawn::DirToUE(Carrier->Xform().TransformVector(Ops_Fwd2(Yaw))) * Speed;
	M.Rates = FVector::ZeroVector;
	if (!bManned)
	{
		M.InThrottle = 0.0;
		M.InAb = 0.0;
	}
	Jet->bGearDown = true;
	M.bGearDown = true;
	if (Dt > 0.0) { M.StepEngine(Dt); }
	else { M.RefreshAirData(); }
}

void UCarrierJetOps::PostInterpolate()
{
	if (!OnDeck()) { return; }
	const double A = FMath::Clamp(Accum / Ops_SIM_DT, 0.0, 1.0);
	const FVector2D P = FMath::Lerp(FVector2D(PrevL.X, PrevL.Y), FVector2D(CurL.X, CurL.Y), A);
	const double Yw = PrevL.Z + FMath::FindDeltaAngleDegrees(PrevL.Z, CurL.Z) * A;
	const FTransform W = LocalJetTransform(P, Yw, FMath::Lerp(PrevPitch, CurPitch, A)) * Carrier->Xform();
	const FQuat Q = W.GetRotation();
	Jet->RenderPos = AOsrJetPawn::FromUE(W.GetLocation());
	Jet->RenderRot = AOsrJetPawn::LookingAt(AOsrJetPawn::DirToUE(Q.GetForwardVector()), AOsrJetPawn::DirToUE(Q.GetUpVector()));
}

void UCarrierJetOps::Release(const FVector& VelLocalMs, double PitchDeg)
{
	// back to the flight model: deck pose + attitude, ship velocity + relative velocity, no body rates
	const FTransform W = LocalJetTransform(Pos, Yaw, PitchDeg) * Carrier->Xform();
	const FQuat Q = W.GetRotation();
	FF35FlightModel& M = Jet->Model;
	M.Position = AOsrJetPawn::FromUE(W.GetLocation() + FVector(0, 0, 5.0));
	M.Orientation = AOsrJetPawn::LookingAt(AOsrJetPawn::DirToUE(Q.GetForwardVector()), AOsrJetPawn::DirToUE(Q.GetUpVector()));
	M.Velocity = Carrier->VelocityGodot() + AOsrJetPawn::DirToUE(Carrier->Xform().TransformVector(VelLocalMs));
	M.Rates = FVector::ZeroVector;
	M.RefreshAirData();
	Wire = -1;
	bHookPrevValid = false;
	NoTouchT = 1.2;
	SetState(EDeckState::Airborne);
}

void UCarrierJetOps::CheckHookup()
{
	using namespace CarrierLayout;
	const FVector2D F = Ops_F2(Yaw);
	const FVector2D Nose = Pos + F * Ops_NOSE_X;
	for (int32 C = 0; C < UE_ARRAY_COUNT(Cats); ++C)
	{
		const FVector2D D = Ops_CatDir(C), N(-D.Y, D.X);
		const FVector2D R = Nose - Ops_CatStart(C);
		const double Along = FVector2D::DotProduct(R, D), Lat = FVector2D::DotProduct(R, N);
		if (Along > -7.0 && Along < 1.5 && FMath::Abs(Lat) < 2.4 && FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, Cats[C].HdgDeg)) < 15.0 && Speed < 4.5)
		{
			HookupCat = CatIndex = C;
			HookupFromPos = Pos;
			HookupFromYaw = Yaw;
			Speed = 0.0;
			Carrier->SetJbd(C, true);
			SetState(EDeckState::Hookup);
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_hookup cat=%d along=%.1f lat=%.1f"), C + 1, Along, Lat);
			return;
		}
	}
}

void UCarrierJetOps::DeckStep(double Dt)
{
	using namespace CarrierLayout;
	StateT += Dt;
	FF35FlightModel& M = Jet->Model;
	const FOsrControlInput& In = Jet->Controls.Input;
	const double Mass = FMath::Max(M.Mass, 1000.0);
	auto Roll = [&](double SteerScale)
	{
		const double Steer = bManned ? FMath::Clamp(In.Yaw + In.Roll * 0.8, -1.0, 1.0) * SteerScale : 0.0;
		const double MaxSteer = Lerp(55.0, 7.0, Smoothstep(2.0, 22.0, FMath::Abs(Speed)));
		const double Delta = FMath::DegreesToRadians(Steer * MaxSteer);
		double A = M.Thrust / Mass - 0.015 * Ops_G0;
		const double Air = Speed + ACarrierActor::SpeedMs * FMath::Cos(FMath::DegreesToRadians(Yaw));
		A -= 0.5 * M.Rho * Air * FMath::Abs(Air) * 62.1 * 0.12 / Mass;
		if (In.bBrake && bManned) { A -= 5.5; }
		Speed = FMath::Max(0.0, Speed + A * Dt);
		Yaw += FMath::RadiansToDegrees(Speed * FMath::Tan(Delta) / Ops_WHEELBASE) * Dt;
		Pos += Ops_F2(Yaw) * Speed * Dt;
	};
	switch (State)
	{
	case EDeckState::Parked:
		Speed = 0.0;
		break;
	case EDeckState::Taxi:
	{
		const FVector2D Before = Pos;
		Roll(1.0);
		// island / JBD panels stop the jet (no damage model on deck yet)
		const FVector2D Nose = Pos + Ops_F2(Yaw) * 8.0;
		if (Nose.X > Island.Min.X - 1.0 && Nose.X < Island.Max.X + 1.0 && Nose.Y > Island.Min.Y - 1.0) { Pos = Before; Speed = 0.0; }
		if (!ACarrierActor::InDeckOutline(Pos.X, Pos.Y)) { Release(Ops_Fwd2(Yaw) * Speed, 0.0); return; }
		CheckHookup();
		break;
	}
	case EDeckState::Hookup:
	{
		const FVector2D D = Ops_CatDir(HookupCat);
		const double K = Smoothstep(0.0, 2.2, StateT);
		Pos = FMath::Lerp(HookupFromPos, Ops_CatStart(HookupCat) - D * Ops_NOSE_X, K);
		Yaw = HookupFromYaw + FMath::FindDeltaAngleDegrees(HookupFromYaw, Cats[HookupCat].HdgDeg) * K;
		Speed = 0.0;
		if (StateT > 2.6)
		{
			bSaluted = false;
			LaunchWait = -1.0;
			SetState(EDeckState::OnCat);
		}
		break;
	}
	case EDeckState::OnCat:
	{
		Speed = 0.0;
		const bool bMil = M.Engine >= 0.97 && (M.InThrottle >= 0.99 || M.InAb > 0.0);
		if (bSaluted && LaunchWait >= 0.0)
		{
			LaunchWait -= Dt;
			if (!bMil) { bSaluted = false; LaunchWait = -1.0; }   // shooter suspends: power came off
			else if (LaunchWait <= 0.0)
			{
				CatS = 0.0; CatV = 0.0; LaunchStrokeS = 0.0;
				Play(TEXT("cat_shot"), 1.0f);
				SetState(EDeckState::Launch);
			}
		}
		else if (bMil && bAutoSalute && StateT > 1.0 && !bSaluted)
		{
			bSaluted = true;
			LaunchWait = 2.2;
			if (Carrier->Crew) { Carrier->Crew->StartLaunchSignal(); }
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_salute t=%.1f (auto)"), Now());
		}
		break;
	}
	case EDeckState::Launch:
	{
		const FCat& C = Cats[HookupCat];
		const double VEndRel = Ops_VEND_AIR - ACarrierActor::SpeedMs * FMath::Cos(FMath::DegreesToRadians(C.HdgDeg));
		const double A = VEndRel * VEndRel / (2.0 * C.Stroke);
		CatV += A * Dt;
		CatS += CatV * Dt;
		LaunchStrokeS += Dt;
		const FVector2D D = Ops_CatDir(HookupCat);
		Pos = Ops_CatStart(HookupCat) + D * (FMath::Min(CatS, C.Stroke) - Ops_NOSE_X);
		Yaw = C.HdgDeg;
		Speed = CatV;
		Pitch = FMath::Min(Pitch + Dt * 1.5, 2.0);          // nose strut extends at the end of the stroke
		if (CatS >= C.Stroke)
		{
			const int32 Cat = HookupCat;
			Release(Ops_Fwd2(Yaw) * CatV, 6.0);
			Pitch = 0.0;
			Jet->Model.Rates.X = FMath::DegreesToRadians(3.0);
			LaunchEndKt = Jet->Model.Tas / Ops_KT;
			UE_LOG(LogTemp, Display, TEXT("REPORT catapult_launch cat=%d stroke_s=%.2f end_kt=%.0f accel_g=%.1f"), Cat + 1, LaunchStrokeS, LaunchEndKt, A / Ops_G0);
			Carrier->OnCatapultLaunched.Broadcast(Jet);
			CatIndex = -1;
			FTimerHandle H;
			TWeakObjectPtr<ACarrierActor> Cw = Carrier;
			GetWorld()->GetTimerManager().SetTimer(H, [Cw, Cat]() { if (Cw.IsValid()) { Cw->SetJbd(Cat, false); } }, 3.0f, false);
		}
		break;
	}
	case EDeckState::Rollout:
	{
		Pitch = MoveToward(Pitch, 0.0, 14.0 * Dt);
		Roll(0.25);
		if (!ACarrierActor::InDeckOutline(Pos.X, Pos.Y))
		{
			// off the angled deck end without a wire: bolter (or a deck run-off at low speed)
			Release(Ops_Fwd2(Yaw) * Speed, 7.0);
			Jet->Model.Rates.X = FMath::DegreesToRadians(4.0);
			Lso.Bolter(Now());
			LastGrade = ELsoGrade::Bolter;
			ResultT = Now();
			ResultText = TEXT("BOLTER");
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_bolter t=%.1f speed_kt=%.0f"), Now(), Jet->Model.Tas / Ops_KT);
			Carrier->OnBolter.Broadcast(Jet);
			return;
		}
		break;
	}
	case EDeckState::Arrest:
	{
		Pitch = MoveToward(Pitch, 0.0, 10.0 * Dt);
		const double Ramp = FMath::Min(StateT / 0.25, 1.0);
		const double Acc = ArrestAcc * Ramp;
		MaxDecelG = FMath::Max(MaxDecelG, Acc / Ops_G0);
		Speed = FMath::Max(0.0, Speed - Acc * Dt);
		Pos += Ops_F2(Yaw) * Speed * Dt;
		Carrier->SetWirePull(Wire, HookTipLocal());
		if (Speed <= 0.0)
		{
			ArrestS = StateT;
			const int32 W = Wire + 1;
			LastWire = W;
			LastGrade = Lso.GradeTrap(W);
			ResultT = Now();
			ResultText = FString::Printf(TEXT("%s  %d-WIRE"), *ACarrierActor::GradeText(LastGrade), W);
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_trap wire=%d grade=%s comment=\"%s\" arrest_s=%.2f max_decel_g=%.1f sink_ms=%.1f"),
				W, *ACarrierActor::GradeText(LastGrade), *Lso.LastComment, ArrestS, MaxDecelG, TouchSinkMs);
			Jet->Controls.SetLever(0.0);
			SetState(EDeckState::Stopped);
			Carrier->OnTrap.Broadcast(Jet, LastGrade, W);
		}
		break;
	}
	case EDeckState::Stopped:
		Speed = 0.0;
		if (!bHookDown)
		{
			Carrier->SetWirePull(-1, FVector::ZeroVector);
			Wire = -1;
			if (bManned && In.Throttle > 0.25 && HookAngle < 3.0 && StateT > 1.0) { SetState(EDeckState::Taxi); }
		}
		break;
	default:
		break;
	}
	// wire engagement on the deck (touchdown roll)
	if (State == EDeckState::Rollout && bHookDown && Wire < 0)
	{
		const FTransform W = LocalJetTransform(Pos, Yaw, Pitch) * Carrier->Xform();
		const double A = FMath::DegreesToRadians(HookAngle);
		const FVector TipJ = CarrierF35Anim::HookPivot + FVector(-FMath::Cos(A), 0.0, -FMath::Sin(A)) * CarrierF35Anim::HookArm;
		const FVector Tip = Carrier->UEToLocal(W.TransformPosition(TipJ));
		if (bHookPrevValid)
		{
			const FVector2D L0 = ACarrierActor::ToLanding(FVector2D(HookPrevL.X, HookPrevL.Y));
			const FVector2D L1 = ACarrierActor::ToLanding(FVector2D(Tip.X, Tip.Y));
			for (int32 K = 0; K < 4; ++K)
			{
				if (L0.X < WiresS[K] && L1.X >= WiresS[K] && FMath::Abs(L1.Y) <= WireHalf && Tip.Z < DeckZ + 0.3) { Engage(K); break; }
			}
		}
		HookPrevL = Tip;
		bHookPrevValid = true;
	}
}

void UCarrierJetOps::Touchdown(const FVector& VelRelLocal)
{
	// deck pose from the flight state
	const FVector O = Carrier->GodotToLocal(Jet->Model.Position);
	const FTransform JW = JetUE();
	const FVector FL = Carrier->Xform().InverseTransformVector(JW.GetRotation().GetForwardVector());
	Pos = FVector2D(O.X, O.Y);
	Yaw = FMath::RadiansToDegrees(FMath::Atan2(FL.Y, FL.X));
	Pitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FL.Z, -1.0, 1.0))), -2.0, 15.0);
	Speed = FMath::Max(0.0, FVector::DotProduct(VelRelLocal, Ops_Fwd2(Yaw)));
	CurL = PrevL = FVector(Pos.X, Pos.Y, Yaw);
	CurPitch = PrevPitch = Pitch;
	TouchSinkMs = -VelRelLocal.Z;
	bTouched = true;
	bDfp = false;
	if (bManned || Jet->bAutopilot) { Jet->Controls.SetLever(1.0); Jet->AutopilotLever = 1.0; }   // MIL on touchdown (bolter insurance)
	Play(TEXT("gear_clunk"), 1.4f);
	UE_LOG(LogTemp, Display, TEXT("REPORT carrier_touchdown t=%.1f sink_ms=%.1f speed_rel_kt=%.0f s=%.0f lat=%.1f"), Now(), TouchSinkMs, Speed / Ops_KT,
		ACarrierActor::ToLanding(Pos).X, ACarrierActor::ToLanding(Pos).Y);
}

void UCarrierJetOps::Engage(int32 W)
{
	Wire = W;
	ArrestV0 = Speed;
	ArrestAcc = FMath::Max(Speed, 1.0) / 1.975;   // ~2.1 s to a stop incl. the 0.25 s build-up
	MaxDecelG = 0.0;
	Jet->Controls.SetLever(1.0);
	Play(TEXT("wire_trap"), 1.0f);
	UE_LOG(LogTemp, Display, TEXT("REPORT carrier_wire_engaged wire=%d v_rel_kt=%.0f"), W + 1, Speed / Ops_KT);
	SetState(EDeckState::Arrest);
}

void UCarrierJetOps::PostStep(double Dt)
{
	using namespace CarrierLayout;
	if (State != EDeckState::Airborne || !Jet->bAlive) { return; }
	if (NoTouchT > 0.0) { NoTouchT -= Dt; bHookPrevValid = false; return; }
	const FVector O = Carrier->GodotToLocal(Jet->Model.Position);
	if (O.X < SternX - 400.0 || O.X > BowX + 150.0 || FMath::Abs(O.Y) > 200.0 || O.Z > DeckZ + 60.0)
	{
		bHookPrevValid = false;
		return;
	}
	const FVector ShipV = Carrier->Pose().VelUE;
	const FVector VRel = Carrier->Xform().InverseTransformVector(AOsrJetPawn::ToUE(Jet->Model.Velocity) - ShipV) * 0.01;
	// hook: in-flight engagement (hook tip crosses a wire close to the deck)
	if (bHookDown && HookAngle > 15.0)
	{
		const FVector Tip = HookTipLocal();
		if (bHookPrevValid)
		{
			const FVector2D L0 = ACarrierActor::ToLanding(FVector2D(HookPrevL.X, HookPrevL.Y));
			const FVector2D L1 = ACarrierActor::ToLanding(FVector2D(Tip.X, Tip.Y));
			for (int32 K = 0; K < 4; ++K)
			{
				if (L0.X < WiresS[K] && L1.X >= WiresS[K] && FMath::Abs(L1.Y) <= WireHalf && FMath::Min(Tip.Z, HookPrevL.Z) < DeckZ + 0.25)
				{
					Touchdown(VRel);
					HookPrevL = Tip;
					Engage(K);
					return;
				}
			}
		}
		HookPrevL = Tip;
		bHookPrevValid = true;
	}
	// wheels on the deck
	if (GearPhase > 0.95)
	{
		for (const FVector& Wp : {CarrierF35Anim::MainWheelL, CarrierF35Anim::MainWheelR})
		{
			const FVector L = PointLocal(Wp);
			if (L.Z <= DeckZ + 0.02 && ACarrierActor::InDeckOutline(L.X, L.Y))
			{
				if (-VRel.Z > 8.5)
				{
					UE_LOG(LogTemp, Display, TEXT("REPORT carrier_hard_landing sink_ms=%.1f"), -VRel.Z);
					Jet->CrashAt(Carrier->LocalToGodot(L));
					return;
				}
				Touchdown(VRel);
				if (Lso.bWaveOff) { Lso.LastComment = TEXT("landed after wave-off"); }
				SetState(EDeckState::Rollout);
				bHookPrevValid = false;
				return;
			}
		}
	}
	// ramp strike / hull / island / belly landing
	for (const FVector& Cp : Ops_CRASH_PTS)
	{
		const FVector L = PointLocal(Cp);
		const bool bBelly = GearPhase < 0.95 && L.Z <= DeckZ && ACarrierActor::InDeckOutline(L.X, L.Y);
		if (bBelly || (ACarrierActor::InsideShip(L) && (L.Z < DeckZ - 0.6 || L.Z > DeckZ + 0.02)))
		{
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_crash point=(%.0f,%.0f,%.1f) %s"), L.X, L.Y, L.Z, bBelly ? TEXT("belly") : TEXT("ship structure"));
			Jet->CrashAt(Carrier->LocalToGodot(L));
			return;
		}
	}
}

// ---------------------------------------------------------------- approach mode

void UCarrierJetOps::UpdateDfp(double Dt)
{
	FF35FlightModel& M = Jet->Model;
	if (!Jet->bGearDown) { bDfp = false; return; }
	// pilot moved the throttle: auto-throttle off (DFP too), full manual control
	if (LastLeverSet >= 0.0 && FMath::Abs(Jet->Controls.GetLever() - LastLeverSet) > 0.03 && !Jet->bAutopilot)
	{
		bDfp = false;
		LastLeverSet = -1.0;
		return;
	}
	const double V = FMath::Max(M.Tas, 30.0);
	const double Gamma = M.PathAngle;
	const FVector Vh = FVector(M.Velocity.X, 0.0, M.Velocity.Z).GetSafeNormal();
	const double ShipAlong = FVector::DotProduct(Carrier->VelocityGodot(), Vh);
	const double Closure = FMath::Max(V * FMath::Cos(Gamma) - ShipAlong, 20.0);
	const double GammaRef = -FMath::Atan(Closure * FMath::Tan(FMath::DegreesToRadians(Ops_DFP_GS)) / V);
	// stick = glideslope command: neutral holds the ship-referenced 3.5 deg, +-1 = -+3 deg
	const double GammaCmd = GammaRef + Jet->Controls.Input.Pitch * FMath::DegreesToRadians(3.0);
	const double Cb = FMath::Cos(M.Bank);
	const double Comp = Cb >= 0.5 ? 1.0 / Cb : 4.0 * Cb;
	const double N0 = FMath::Cos(Gamma) * Comp;
	const double NDes = N0 + V / Ops_G0 * (1.7 * (GammaCmd - Gamma)) - M.Rates.X * V / Ops_G0 * 0.15;
	const double NHi = 4.0, NLo = -1.0;
	const double S = NDes >= N0 ? (NDes - N0) / (NHi - N0) : (NDes - N0) / (N0 - NLo);
	M.InPitch = FMath::Clamp(S, -1.0, 1.0);
	// auto-throttle (APC): hold on-speed AoA
	const double AoaErr = FMath::RadiansToDegrees(M.Alpha) - FLsoMonitor::OnSpeedAoa;
	ApcInt = FMath::Clamp(ApcInt + AoaErr * Dt * 0.03, -0.35, 0.35);
	ApcLever = FMath::Clamp(0.42 + 0.07 * AoaErr + ApcInt, 0.05, 1.0);
	M.InThrottle = ApcLever;
	M.InAb = 0.0;
	Jet->Controls.SetLever(ApcLever);
	Jet->AutopilotLever = ApcLever;
	LastLeverSet = ApcLever;
}

void UCarrierJetOps::UpdateLso(double Dt)
{
	using namespace CarrierLayout;
	const double T = Now();
	bLsoGroove = false;
	if (State == EDeckState::Airborne && Jet->bAlive)
	{
		const FVector2D Td = ACarrierActor::LandPoint(TouchdownS, 0.0);
		const FVector TipW = Carrier->LocalToUE(HookTipLocal());
		const FVector S = Carrier->UEToStabilized(TipW, FVector(Td.X, Td.Y, DeckZ));
		const FVector2D D = Ops_F2(LandHdgDeg), N(-D.Y, D.X);
		const double Along = S.X * D.X + S.Y * D.Y, Lat = S.X * N.X + S.Y * N.Y;
		FLsoInput In;
		In.RangeM = -Along;
		// glideslope error as an angle seen from the lens; in close (< 60 m) the height error over 60 m (no blow-up at the ramp)
		const double Rg = FMath::Max(In.RangeM, 0.0);
		In.GsDevDeg = FMath::RadiansToDegrees(FMath::Atan2(S.Z - Rg * FMath::Tan(FMath::DegreesToRadians(Ops_DFP_GS)), FMath::Max(Rg, 60.0)));
		In.LineupM = Lat;
		In.AoaDeg = FMath::RadiansToDegrees(Jet->Model.Alpha);
		In.FuelKlb = Jet->Model.Fuel * 2.20462 / 1000.0;
		In.bGear = Jet->bGearDown;
		In.bHook = bHookDown;
		In.bBehind = Along < 20.0;
		const FVector FL = Carrier->Xform().InverseTransformVector(JetUE().GetRotation().GetForwardVector());
		const double Hdg = FMath::RadiansToDegrees(FMath::Atan2(FL.Y, FL.X));
		In.bAligned = FMath::Abs(Lat) < In.RangeM * 0.25 + 60.0 && FMath::Abs(FMath::FindDeltaAngleDegrees(Hdg, LandHdgDeg)) < 40.0;
		const bool bWasWo = Lso.bWaveOff;
		bLsoGroove = Lso.Update(In, T);
		if (Lso.bWaveOff && !bWasWo)
		{
			LastGrade = ELsoGrade::WaveOff;
			ResultT = T;
			ResultText = FString::Printf(TEXT("WAVE-OFF (%s)"), *Lso.WaveOffReason);
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_waveoff reason=%s range_m=%.0f"), *Lso.WaveOffReason, In.RangeM);
			Carrier->OnWaveOff.Broadcast(Jet);
		}
	}
	const bool bWo = Lso.bWaveOff && T - Lso.WaveOffT < 6.0;
	Carrier->SetIflols(Lso.BallDev, bLsoGroove, bWo, false);
	// radio: play new calls (click + clip)
	const TArray<FLsoCall>& Calls = Lso.Calls();
	while (LastCallCount < Calls.Num() && Calls[LastCallCount].T <= T)
	{
		Play(TEXT("radio_click"), 0.6f);
		Play(Calls[LastCallCount].Clip, 1.0f);
		++LastCallCount;
	}
	if (LastCallCount > Calls.Num()) { LastCallCount = Calls.Num(); }
}

// ---------------------------------------------------------------- crew cues

void UCarrierJetOps::DirectCrew()
{
	ADeckCrew* Crew = Carrier ? Carrier->Crew.Get() : nullptr;
	if (!Crew) { return; }
	const FVector2D F = Ops_F2(Yaw);
	const FVector2D Nose = Pos + F * Ops_NOSE_X;
	FVector2D P; double Y;
	const int32 Cat = State == EDeckState::Hookup || State == EDeckState::OnCat || State == EDeckState::Launch ? HookupCat : 0;
	if (State == EDeckState::Taxi && bManned)
	{
		// yellow shirt: stands ahead of the jet and brings it to the lead-in of cat 1
		const FVector2D D = Ops_CatDir(0);
		const FVector2D Target = Ops_CatStart(0) - D * 3.0;
		const FVector2D To = Target - Nose;
		const double Dist = To.Size();
		const double Cross = F.X * To.Y - F.Y * To.X;
		const double Err = FMath::RadiansToDegrees(FMath::Atan2(Cross, FVector2D::DotProduct(F, To)));
		// near the cat line the jet has to line up with it instead of pointing at the shuttle
		const double AlignErr = FMath::FindDeltaAngleDegrees(Yaw, CarrierLayout::Cats[0].HdgDeg);
		const double E = Dist < 12.0 ? AlignErr : Err;
		ECrewSignal Sig = ECrewSignal::ComeAhead;
		if (Dist < 3.5) { Sig = ECrewSignal::Stop; }
		else if (E > 6.0) { Sig = ECrewSignal::TurnRight; }
		else if (E < -6.0) { Sig = ECrewSignal::TurnLeft; }
		const FVector2D Spot = Nose + (To.GetSafeNormal() * 0.6 + F * 0.4).GetSafeNormal() * FMath::Min(16.0, Dist + 6.0);
		Crew->Command(ECrewRole::Director, Spot, 0.0, Sig);
		Crew->Face(ECrewRole::Director, Nose);
	}
	else if (State == EDeckState::Stopped && bManned)
	{
		const FVector2D Spot = Nose + F * 14.0;
		Crew->Command(ECrewRole::Director, Spot, 0.0, bHookDown ? ECrewSignal::HookUp : ECrewSignal::ComeAhead);
		Crew->Face(ECrewRole::Director, Nose);
	}
	else if (State == EDeckState::Hookup || State == EDeckState::OnCat || State == EDeckState::Launch)
	{
		ADeckCrew::CatHome(Cat, ECrewRole::Director, P, Y);
		const FVector2D D = Ops_CatDir(Cat), N(-D.Y, D.X);
		Crew->Command(ECrewRole::Director, Ops_CatStart(Cat) + D * 9.0 + N * 11.0, 0.0, State == EDeckState::Hookup ? ECrewSignal::Stop : ECrewSignal::Idle);
		Crew->Face(ECrewRole::Director, Nose);
	}
	else
	{
		Crew->Command(ECrewRole::Director, FVector2D(CarrierLayout::PlayerJet.X, CarrierLayout::PlayerJet.Y) + FVector2D(14.0, -5.0), CarrierLayout::PlayerJet.Z + 180.0, ECrewSignal::Idle);
	}
	// catapult crew
	const bool bMil = Jet->Model.Engine >= 0.97;
	for (ECrewRole R : {ECrewRole::Shooter, ECrewRole::Hook1, ECrewRole::Hook2, ECrewRole::Checker})
	{
		ADeckCrew::CatHome(Cat, R, P, Y);
		ECrewSignal S = ECrewSignal::Idle;
		const FVector2D D = Ops_CatDir(Cat), N(-D.Y, D.X);
		if (State == EDeckState::Hookup && (R == ECrewRole::Hook1 || R == ECrewRole::Hook2))
		{
			P = Ops_CatStart(Cat) - D * 1.0 + N * (R == ECrewRole::Hook1 ? 1.6 : -1.6);
			S = ECrewSignal::Crouch;
		}
		else if (State == EDeckState::OnCat)
		{
			if (R == ECrewRole::Shooter) { S = bSaluted ? ECrewSignal::Launch : ECrewSignal::RunUp; }
			if (R == ECrewRole::Checker && bMil) { S = ECrewSignal::ThumbUp; }
			if ((R == ECrewRole::Hook1 || R == ECrewRole::Hook2) && StateT < 2.0) { S = ECrewSignal::ThumbUp; }
		}
		if (R == ECrewRole::Shooter && S == ECrewSignal::Launch) { continue; }   // keep the running launch timeline
		Crew->Command(R, P, Y, S);
	}
	Crew->Command(ECrewRole::Lso, CarrierLayout::LsoPlatform + FVector2D(0.5, -2.2), CarrierLayout::LandHdgDeg + 180.0, Lso.bActive ? ECrewSignal::Pickle : ECrewSignal::Idle);
}

// ---------------------------------------------------------------- input + tick

void UCarrierJetOps::ReadInput()
{
	bManned = Jet->IsLocallyControlled() || Jet->bAutopilot;
	APlayerController* PC = Cast<APlayerController>(Jet->GetController());
	if (!PC || Jet->bPaused) { return; }
	const FOsrPadState P = FOsrPadInput::Get();
	const bool bDn = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Down) || PC->IsInputKeyDown(EKeys::H) || P.DPadDown();
	const bool bUp = PC->IsInputKeyDown(EKeys::Gamepad_DPad_Up) || PC->IsInputKeyDown(EKeys::V) || P.DPadUp();
	const bool bSal = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom) || PC->IsInputKeyDown(EKeys::SpaceBar) || P.bCross;
	if (bDn && !bPrevDpadDown && !Jet->bAutopilot) { bHookDown = !bHookDown; }
	if (bUp && !bPrevDpadUp && !Jet->bAutopilot && State == EDeckState::Airborne)
	{
		bDfp = !bDfp && Jet->bGearDown;
		ApcInt = 0.0;
		LastLeverSet = -1.0;
	}
	if (bSal && !bPrevSalute && State == EDeckState::OnCat && !bSaluted && Jet->Model.Engine >= 0.97)
	{
		bSaluted = true;
		LaunchWait = 2.2;
		if (Carrier->Crew) { Carrier->Crew->StartLaunchSignal(); }
		UE_LOG(LogTemp, Display, TEXT("REPORT carrier_salute t=%.1f"), Now());
	}
	bPrevDpadDown = bDn;
	bPrevDpadUp = bUp;
	bPrevSalute = bSal;
}

void UCarrierJetOps::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Jet || !Carrier) { return; }
	const double Dt = FMath::Min<double>(DeltaTime, 0.1);
	if (Jet->bAlive) { Accum = FMath::Min(Accum + Dt, 0.25); }
	ReadInput();
	UpdateParts(Dt);
	UpdateLso(Dt);
	DirectCrew();
	// the pilot hears his own jet; a jet on deck is heard from the character's camera (2D layers scaled by distance)
	if (Jet->IsLocallyControlled()) { Jet->AudioGainScale = 1.0; }
	else if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		const double D = PC->PlayerCameraManager ? FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Jet->GetActorLocation()) * 0.01 : 1000.0;
		Jet->AudioGainScale = FMath::Clamp(18.0 / FMath::Max(D, 1.0), 0.0, 1.0);
	}
	// prompts
	Prompt.Reset();
	if (!Jet->IsLocallyControlled()) { return; }
	switch (State)
	{
	case EDeckState::Taxi:
	{
		const FVector2D D = Ops_CatDir(0), N(-D.Y, D.X);
		const FVector2D R = Pos + Ops_F2(Yaw) * Ops_NOSE_X - Ops_CatStart(0);
		Prompt = FString::Printf(TEXT("Follow the yellow shirt to CAT 1 (%.0f m, %.1f m %s of the line) - RT throttle, LB/RB or stick steer, B brake"),
			FMath::Max(0.0, -FVector2D::DotProduct(R, D)), FMath::Abs(FVector2D::DotProduct(R, N)), FVector2D::DotProduct(R, N) > 0.0 ? TEXT("right") : TEXT("left"));
		break;
	}
	case EDeckState::Hookup: Prompt = TEXT("Hooking up - launch bar down, holdback on, JBD raising"); break;
	case EDeckState::OnCat:
		Prompt = bSaluted ? TEXT("Shooter: LAUNCH...") : (Jet->Model.Engine >= 0.97 ? TEXT("MIL power set - SALUTE the shooter (A / Cross / Space)") : TEXT("Tension - run up to MIL power (RT, hold at 100 %)"));
		break;
	case EDeckState::Stopped: Prompt = bHookDown ? TEXT("Trapped - raise the hook (D-pad down / H)") : TEXT("Taxi clear (RT) or climb out (hold X / Square / F)"); break;
	case EDeckState::Parked: Prompt = TEXT("Climb out: hold X / Square / F"); break;
	default: break;
	}
}

void UCarrierJetOps::Play(FName Name, float Vol)
{
	if (USoundBase* S = Sounds.FindRef(Name))
	{
		if (Jet->IsLocallyControlled() || Jet->bAutopilot) { UGameplayStatics::PlaySound2D(this, S, Vol); }
	}
}
