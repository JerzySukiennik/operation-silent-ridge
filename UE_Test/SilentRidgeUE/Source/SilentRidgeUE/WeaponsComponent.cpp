// Weapons, targeting pod, countermeasures and threat warning for one jet.
#include "WeaponsComponent.h"
#include "WeaponBomb.h"
#include "SamMissile.h"
#include "EnemyFighter.h"
#include "MissionCommon.h"
#include "MissionCombat.h"
#include "MissionFx.h"
#include "MissionState.h"
#include "MissionTarget.h"
#include "OsrJetPawn.h"
#include "OsrPadInput.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

using namespace OsrMission;
using namespace OsrMath;

namespace
{
	const double ZOOM_FOV[UOsrWeaponsComponent::ZOOM_N] = {24.0, 8.0, 2.8, 1.0};   // vertical, degrees (WFOV .. NFOV x2)
	FVector Nose(const FQuat& Q) { return -BasisZ(Q); }
}

UOsrWeaponsComponent::UOsrWeaponsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
}

UOsrWeaponsComponent* UOsrWeaponsComponent::Of(const AActor* Jet)
{
	return Jet ? Jet->FindComponentByClass<UOsrWeaponsComponent>() : nullptr;
}

AOsrJetPawn* UOsrWeaponsComponent::Jet() const { return Cast<AOsrJetPawn>(GetOwner()); }

bool UOsrWeaponsComponent::IsLocal() const
{
	const AOsrJetPawn* J = Jet();
	return J && J->IsLocallyControlled();
}

void UOsrWeaponsComponent::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* Names[] = {TEXT("gun_loop"), TEXT("rwr_search"), TEXT("rwr_lock"), TEXT("rwr_launch"), TEXT("v_missile_launch"), TEXT("v_missile"), TEXT("bomb_release"),
		TEXT("flare_pop"), TEXT("chaff_pop"), TEXT("ui_click"), TEXT("v_bingo_flares"), TEXT("v_laser")};
	for (const TCHAR* N : Names)
	{
		if (USoundBase* S = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Mission/Audio/%s.%s"), N, N))) { Sounds.Add(FName(N), S); }
	}
	auto MakeLoop = [this](FName N) -> UAudioComponent*
	{
		USoundBase* S = Sounds.FindRef(N);
		if (!S) { return nullptr; }
		UAudioComponent* A = NewObject<UAudioComponent>(GetOwner());
		A->SetSound(S);
		A->bAutoActivate = false;
		A->bAllowSpatialization = false;
		A->bIsUISound = true;
		A->SetupAttachment(GetOwner()->GetRootComponent());
		A->RegisterComponent();
		return A;
	};
	GunAudio = MakeLoop("gun_loop");
	LockTone = MakeLoop("rwr_lock");
	LaunchTone = MakeLoop("rwr_launch");
	if (AOsrJetPawn* J = Jet()) { LastRespawn = J->RespawnCount; }
}

void UOsrWeaponsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (TgpCam) { TgpCam->Destroy(); }
	Super::EndPlay(Reason);
}

void UOsrWeaponsComponent::Rearm()
{
	GunRounds = GUN_MAX;
	Bombs = 2;
	Flares = 30;
	Chaff = 30;
	bTgpValid = false;
	bPointTrack = false;
	bLaserArm = false;
	TrackName.Reset();
	Bullets.Reset();
}

double UOsrWeaponsComponent::TgpFovDeg() const { return ZOOM_FOV[FMath::Clamp(ZoomIdx, 0, ZOOM_N - 1)]; }

FVector UOsrWeaponsComponent::PodPos() const
{
	const AOsrJetPawn* J = Jet();
	return J ? J->RenderPos + J->RenderRot.RotateVector(FVector(0.0, -0.9, -6.2)) : FVector::ZeroVector;
}

bool UOsrWeaponsComponent::bMasterArm() const
{
	const AOsrJetPawn* J = Jet();
	return J && J->bAlive && !J->bGearDown && OsrCombat::JetAirborne(J);
}

double UOsrWeaponsComponent::BombTti() const
{
	double Best = -1.0;
	for (AOsrLaserBomb* B : AOsrLaserBomb::InFlight(GetWorld(), GetOwner())) { if (Best < 0.0 || B->Tti < Best) { Best = B->Tti; } }
	return Best;
}

// ---------------------------------------------------------------- input

void UOsrWeaponsComponent::BindInput()
{
	AOsrJetPawn* J = Jet();
	APlayerController* PC = J ? Cast<APlayerController>(J->GetController()) : nullptr;
	UEnhancedInputComponent* EIC = J ? Cast<UEnhancedInputComponent>(J->InputComponent) : nullptr;
	if (!PC || !EIC || !PC->GetLocalPlayer()) { return; }
	if (!Imc)
	{
		Imc = NewObject<UInputMappingContext>(this, TEXT("OsrWeaponsIMC"));
		auto Make = [this](const TCHAR* Name, std::initializer_list<FKey> Keys)
		{
			UInputAction* A = NewObject<UInputAction>(this, Name);
			A->ValueType = EInputActionValueType::Axis1D;
			A->bConsumeInput = false;         // share keys with the jet's own context (menu D-pad, look stick)
			Actions.Add(FName(Name), A);
			for (const FKey& K : Keys) { Imc->MapKey(A, K); }
		};
		Make(TEXT("WFire"), {EKeys::Gamepad_FaceButton_Bottom, EKeys::SpaceBar});
		Make(TEXT("WSelect"), {EKeys::Gamepad_FaceButton_Left, EKeys::Tab});
		// D-pad down / up are the carrier's hook / approach mode (gear down); weapons use them only with the gear up
		Make(TEXT("WTgp"), {EKeys::Gamepad_DPad_Up, EKeys::T});
		Make(TEXT("WLaser"), {EKeys::R});
		Make(TEXT("WFlare"), {EKeys::Gamepad_DPad_Left, EKeys::One});
		Make(TEXT("WChaff"), {EKeys::Gamepad_DPad_Right, EKeys::Two});
		Make(TEXT("WTrack"), {EKeys::Gamepad_LeftThumbstick, EKeys::X});
		Make(TEXT("WZoom"), {EKeys::Gamepad_RightThumbstick, EKeys::Z});
		Make(TEXT("WSlewX"), {EKeys::Gamepad_RightX});
		Make(TEXT("WSlewY"), {EKeys::Gamepad_RightY});
		Make(TEXT("WKL"), {EKeys::J});
		Make(TEXT("WKR"), {EKeys::L});
		Make(TEXT("WKU"), {EKeys::I});
		Make(TEXT("WKD"), {EKeys::K});
	}
	if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Sub->AddMappingContext(Imc, 1);
	}
	for (const auto& It : Actions) { EIC->BindActionValue(It.Value); }
	bInputBound = true;
}

double UOsrWeaponsComponent::Act(FName N) const
{
	const AOsrJetPawn* J = Jet();
	const UEnhancedInputComponent* EIC = J ? Cast<UEnhancedInputComponent>(J->InputComponent) : nullptr;
	const TObjectPtr<UInputAction>* A = Actions.Find(N);
	if (!EIC || !A) { return 0.0; }
	return EIC->GetBoundActionValue(*A).Get<float>();
}

bool UOsrWeaponsComponent::Edge(FName N, bool bNow)
{
	bool& P = Prev.FindOrAdd(N);
	const bool bE = bNow && !P;
	P = bNow;
	return bE;
}

void UOsrWeaponsComponent::ReadInput(double Dt)
{
	AOsrJetPawn* J = Jet();
	if (!J || J->bAutopilot) { return; }
	const FOsrPadState Pd = FOsrPadInput::Get();
	const bool bPaused = J->bPaused;
	const bool bFire = Act("WFire") > 0.5 || Pd.bCross;
	const bool bSel = Act("WSelect") > 0.5 || Pd.bSquare;
	const bool bTgp = Act("WTgp") > 0.5 || Pd.DPadUp();
	const bool bLaser = Act("WLaser") > 0.5;
	// Sony hat: 6 = left, 2 = right (the up-diagonals 7 / 1 belong to D-pad up = pod)
	const bool bFlare = Act("WFlare") > 0.5 || Pd.Hat == 5 || Pd.Hat == 6;
	const bool bChaff = Act("WChaff") > 0.5 || Pd.Hat == 2 || Pd.Hat == 3;
	const bool bGearUp = !J->bGearDown;
	const bool bTrack = Act("WTrack") > 0.5 || Pd.bL3;
	const bool bZoom = Act("WZoom") > 0.5 || Pd.bR3;
	// after the pause menu closes, ignore buttons still held from it (A = confirm)
	if (bPaused || bWasPaused)
	{
		bWasPaused = bPaused || bFire;
		for (const TCHAR* N : {TEXT("WFire"), TEXT("WSelect"), TEXT("WTgp"), TEXT("WLaser"), TEXT("WFlare"), TEXT("WChaff"), TEXT("WTrack"), TEXT("WZoom")}) { Prev.Add(FName(N), true); }
		bTrigger = false;
		return;
	}
	const bool bFireEdge = Edge("WFire", bFire) && bGearUp;
	if (Edge("WSelect", bSel) && bGearUp) { CmdSelect(); }
	// D-pad up: tap = pod view, hold 0.6 s = laser arm (gear up only: gear down it is the carrier approach mode)
	if (bTgp && bGearUp)
	{
		TgpHold += Dt;
		if (TgpHold >= 0.6 && !bTgpHoldUsed) { bTgpHoldUsed = true; CmdLaserArm(); }
	}
	else
	{
		if (TgpHold > 0.0 && TgpHold < 0.6 && bGearUp) { CmdToggleTgp(); }
		TgpHold = 0.0;
		bTgpHoldUsed = false;
	}
	if (Edge("WLaser", bLaser)) { CmdLaserArm(); }
	if (Edge("WFlare", bFlare)) { CmdFlare(); }
	if (Edge("WChaff", bChaff)) { CmdChaff(); }
	if (Edge("WTrack", bTrack)) { CmdTrack(); }
	if (Edge("WZoom", bZoom)) { CmdZoom(); }
	// hold flare (D-pad down) > 0.6 s = countermeasure program (flare + chaff pairs)
	CmHold = bFlare ? CmHold + Dt : 0.0;
	if (CmHold > 0.6)
	{
		CmProgAcc += Dt;
		if (CmProgAcc > 0.45) { CmProgAcc = 0.0; DropOne(true); DropOne(false); }
	}
	if (Selected == EOsrWeapon::Gun) { bTrigger = bFire && bGearUp; }
	else
	{
		bTrigger = false;
		if (bFireEdge) { CmdReleaseBomb(); }
	}
	if (bTgpView)
	{
		auto Merge = [](double A, double B) { return FMath::Abs(A) >= FMath::Abs(B) ? A : B; };
		FVector2D Slew(Merge(Act("WSlewX"), Pd.RX) + Act("WKR") - Act("WKL"), Merge(Act("WSlewY"), -Pd.RY) + Act("WKU") - Act("WKD"));
		if (Slew.Size() > 0.12) { SlewTgp(Slew * FMath::Min(1.0, (Slew.Size() - 0.12) / 0.88) / FMath::Max(Slew.Size(), 1e-3), Dt); }
	}
}

// ---------------------------------------------------------------- commands

void UOsrWeaponsComponent::CmdSelect()
{
	Selected = Selected == EOsrWeapon::Gun ? EOsrWeapon::Lgb : EOsrWeapon::Gun;
	PlayUi("ui_click", -8.0);
}

void UOsrWeaponsComponent::CmdToggleTgp()
{
	bTgpView = !bTgpView;
	if (bTgpView && !bTgpValid)
	{
		// start looking 12 deg below the nose
		const AOsrJetPawn* J = Jet();
		if (J)
		{
			const FQuat Q = J->RenderRot;
			const FVector D = (Nose(Q) * FMath::Cos(Rad(12.0)) - BasisY(Q) * FMath::Sin(Rad(12.0))).GetSafeNormal();
			FVector Hit;
			if (RayTerrain(PodPos(), D, 40000.0, Hit)) { TgpPoint = Hit; bTgpValid = true; }
			else { TgpPoint = PodPos() + D * 15000.0; bTgpValid = true; }
		}
	}
	PlayUi("ui_click", -8.0);
}

void UOsrWeaponsComponent::CmdTrack()
{
	if (!bTgpValid) { return; }
	if (bPointTrack) { bPointTrack = false; TrackName.Reset(); PlayUi("ui_click", -10.0); return; }
	const double Range = FVector::Dist(PodPos(), TgpPoint);
	const double Radius = FMath::Max(15.0, Range * FMath::Tan(Rad(TgpFovDeg()) * 0.5) * 0.3);
	FVector Snap;
	FString Name;
	if (AOsrMissionTarget* T = AOsrMissionTarget::Find(this))
	{
		if (T->SnapTrack(TgpPoint, Radius, Snap, Name)) { TgpPoint = Snap; TrackName = Name; bPointTrack = true; PlayUi("ui_click", -6.0); return; }
	}
	for (AOsrRadarSite* S : AOsrRadarSite::All(GetWorld()))
	{
		if (!S->bDead && FVector::Dist(S->PosG(), TgpPoint) < Radius + 20.0) { TgpPoint = S->Antenna + FVector(0, -2.0, 0); TrackName = S->DisplayName; bPointTrack = true; PlayUi("ui_click", -6.0); return; }
	}
	bPointTrack = true;
	TrackName = TEXT("POINT");
	PlayUi("ui_click", -6.0);
}

void UOsrWeaponsComponent::CmdZoom()
{
	ZoomIdx = (ZoomIdx + 1) % ZOOM_N;
	PlayUi("ui_click", -12.0);
}

void UOsrWeaponsComponent::CmdLaserArm()
{
	bLaserArm = !bLaserArm;
	PlayUi("ui_click", -6.0);
}

void UOsrWeaponsComponent::CmdFlare()
{
	FlareLeft = 2; SalvoAcc = 1.0;
}

void UOsrWeaponsComponent::CmdChaff()
{
	ChaffLeft = 2; SalvoAcc = 1.0;
}

void UOsrWeaponsComponent::DesignatePoint(const FVector& P, bool bPoint)
{
	TgpPoint = P;
	bTgpValid = true;
	bPointTrack = bPoint;
	FVector Snap; FString Name;
	if (AOsrMissionTarget* T = AOsrMissionTarget::Find(this)) { if (T->SnapTrack(P, 3.0, Snap, Name)) { TrackName = Name; } }
}

void UOsrWeaponsComponent::SlewTgp(const FVector2D& Rate, double Dt)
{
	if (!bTgpValid) { return; }
	const FVector Pod = PodPos();
	FVector Dir = (TgpPoint - Pod).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Dir, FVector(0, 1, 0)).GetSafeNormal();
	const FVector Up = FVector::CrossProduct(Right, Dir);
	const double K = Rad(TgpFovDeg()) * 0.75 * Dt;
	Dir = (Dir + Right * (Rate.X * K) + Up * (Rate.Y * K)).GetSafeNormal();
	FVector Hit;
	if (RayTerrain(Pod, Dir, 40000.0, Hit)) { TgpPoint = Hit; }
	else { TgpPoint = Pod + Dir * 15000.0; }
	bPointTrack = false;
	TrackName.Reset();
}

bool UOsrWeaponsComponent::CmdReleaseBomb()
{
	AOsrJetPawn* J = Jet();
	if (!J || Bombs <= 0 || !bMasterArm()) { PlayUi("ui_click", -14.0); return false; }
	const FQuat Q = J->Model.Orientation;
	const FVector P = J->Model.Position + Q.RotateVector(FVector(0.0, -1.4, 0.3));
	const FVector V = J->Model.Velocity + Q.RotateVector(FVector(0.0, -4.0, 0.0));
	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AOsrLaserBomb* B = GetWorld()->SpawnActor<AOsrLaserBomb>(AOsrLaserBomb::StaticClass(), FTransform(ToUE(P)), Sp);
	if (!B) { return false; }
	B->Release(P, V, BombCode, J, bTgpValid ? TgpPoint : PredImpact);
	--Bombs;
	if (AOsrMissionState* Ms = AOsrMissionState::Get(this)) { ++Ms->Stats.BombsReleased; }
	PlayUi("bomb_release", -2.0);
	UE_LOG(LogTemp, Display, TEXT("REPORT bomb_release alt_m=%.0f agl_m=%.0f speed_ms=%.0f pitch_deg=%.1f pred_miss_m=%.1f pred_tof_s=%.1f in_range=%d code=%d"),
		P.Y, P.Y - Ground(P.X, P.Z), J->Model.Velocity.Size(), J->Model.PitchDeg(), PredMiss, PredTof, bInRange ? 1 : 0, BombCode);
	return true;
}

void UOsrWeaponsComponent::DropOne(bool bFlare)
{
	AOsrJetPawn* J = Jet();
	if (!J || !J->bAlive) { return; }
	int32& Count = bFlare ? Flares : Chaff;
	if (Count <= 0) { return; }
	--Count;
	const FQuat Q = J->Model.Orientation;
	const FVector P = J->Model.Position + Q.RotateVector(FVector(FMath::FRandRange(-1.5, 1.5), -1.2, 3.0));
	const FVector V = J->Model.Velocity * 0.85 + Q.RotateVector(FVector(FMath::FRandRange(-12.0, 12.0), -18.0, 4.0));
	if (AOsrMissionState* Ms = AOsrMissionState::Get(this))
	{
		Ms->DropDecoy(J, bFlare, P, V);
		if (bFlare) { ++Ms->Stats.FlaresUsed; } else { ++Ms->Stats.ChaffUsed; }
	}
	if (!bFlare)
	{
		for (AOsrRadarSite* S : AOsrRadarSite::All(GetWorld())) { S->OnChaff(J, J->Model.Velocity); }
		for (TActorIterator<AOsrEnemyFighter> It(GetWorld()); It; ++It) { It->OnChaff(J); }
	}
	PlayUi(bFlare ? FName("flare_pop") : FName("chaff_pop"), -6.0);
}

// ---------------------------------------------------------------- tick

void UOsrWeaponsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const double Dt = FMath::Min<double>(DeltaTime, 0.1);
	AOsrJetPawn* J = Jet();
	if (!J) { return; }
	if (J->RespawnCount != LastRespawn)
	{
		LastRespawn = J->RespawnCount;
		Rearm();
		bTgpView = false;
	}
	if (!bInputBound && IsLocal()) { BindInput(); }
	if (!J->bAlive) { bTgpView = false; bTrigger = false; }
	ReadInput(Dt);
	UpdateGun(Dt);
	UpdateCountermeasures(Dt);
	UpdateTgp(Dt);
	UpdateCues(Dt);
	UpdateThreats(Dt);
}

void UOsrWeaponsComponent::UpdateCountermeasures(double Dt)
{
	if (FlareLeft <= 0 && ChaffLeft <= 0) { return; }
	SalvoAcc += Dt;
	if (SalvoAcc > 0.15)
	{
		SalvoAcc = 0.0;
		if (FlareLeft > 0) { DropOne(true); --FlareLeft; }
		if (ChaffLeft > 0) { DropOne(false); --ChaffLeft; }
	}
}

void UOsrWeaponsComponent::UpdateGun(double Dt)
{
	AOsrJetPawn* J = Jet();
	AOsrMissionFx* Fx = AOsrMissionFx::Get(this);
	const bool bFiring = bTrigger && GunRounds > 0 && bMasterArm() && Selected == EOsrWeapon::Gun;
	if (GunAudio)
	{
		if (bFiring && !GunAudio->IsPlaying()) { GunAudio->Play(); }
		else if (!bFiring && GunAudio->IsPlaying()) { GunAudio->Stop(); }
	}
	if (bFiring)
	{
		GunAcc += Dt * GUN_RPS;
		const FQuat Q = J->Model.Orientation;
		const FVector Muzzle = J->Model.Position + Q.RotateVector(FVector(0.0, -1.15, -2.5));
		const FVector Fwd = Nose(Q);
		while (GunAcc >= 1.0 && GunRounds > 0)
		{
			GunAcc -= 1.0;
			--GunRounds;
			const double Disp = 0.0035;   // ~3.5 mil dispersion
			const FVector D = (Fwd + FVector(FMath::FRandRange(-Disp, Disp), FMath::FRandRange(-Disp, Disp), FMath::FRandRange(-Disp, Disp))).GetSafeNormal();
			const bool bTr = (RoundCounter++ % 4) == 0;
			Bullets.Add({Muzzle + Fwd * FMath::FRandRange(0.0, 12.0), J->Model.Velocity + D * MUZZLE_V, 0.0, bTr});
			if (bTr && Fx) { Fx->Tracer(Muzzle, J->Model.Velocity + D * MUZZLE_V, 2.5); }
		}
		if (AOsrMissionState* Ms = AOsrMissionState::Get(this)) { Ms->Stats.RoundsFired = GUN_MAX - GunRounds; }
		if (Fx) { Fx->Puff(Muzzle + Fwd * 1.5, J->Model.Velocity, 0.05, 0.4, 1.0, FLinearColor(1.0f, 0.7f, 0.35f) * 8.0f, 0.9, true, 0.0); }
	}
	else
	{
		GunAcc = 0.0;
	}
	// ballistics (drag + gravity), hits on terrain / damageables
	for (FBullet& B : Bullets)
	{
		const FVector P0 = B.P;
		const double Sp = B.V.Size();
		B.V += (-B.V * (1.1e-4 * Sp) + FVector(0, -9.81, 0)) * Dt;
		B.P += B.V * Dt;
		B.Age += Dt;
		const OsrCombat::FTraceResult R = OsrCombat::Trace(GetWorld(), P0, B.P, J, false);
		if (R.bHit)
		{
			if (R.Actor)
			{
				R.Actor->ApplyHit(R.P, R.Actor->bAirTarget ? 26.0 : 12.0, EOsrDamage::Bullet, J, R.Part);
				if (Fx) { Fx->Explosion(R.P, EOsrBlast::Small); }
			}
			else if (Fx && FMath::FRand() < 0.5)
			{
				Fx->GroundImpact(R.P, R.P.Y > 700.0, 1.0);
			}
			B.Age = 99.0;
		}
	}
	Bullets.RemoveAll([](const FBullet& B) { return B.Age > 3.5; });
}

void UOsrWeaponsComponent::UpdateTgp(double Dt)
{
	AOsrJetPawn* J = Jet();
	const FVector Pod = PodPos();
	bMasked = false;
	if (bTgpValid)
	{
		// EOTS field of regard: lower hemisphere + a little above the wing plane, not straight back
		const FVector D = (TgpPoint - Pod).GetSafeNormal();
		const FVector L = J->RenderRot.UnrotateVector(D);   // Godot body: x right, y up, z aft
		const double Above = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(L.Y, -1.0, 1.0)));
		const double FromNose = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(-L.Z, -1.0, 1.0)));
		bMasked = Above > 15.0 || FromNose > 155.0;
	}
	const double Tti = BombTti();
	bAutoLase = Tti >= 0.0 && Tti < 14.0;
	const double Range = FVector::Dist(Pod, TgpPoint);
	bLasing = (bLaserArm || bAutoLase) && bTgpValid && !bMasked && J->bAlive && Range < 25000.0 && LineOfSight(Pod, TgpPoint + FVector(0, 1.0, 0), 0.5);
	if (AOsrMissionState* Ms = AOsrMissionState::Get(this)) { Ms->SetLaser(J, LaserCode, bLasing, Pod, TgpPoint); }
	// camera
	TgpCamFwd = bTgpValid ? (TgpPoint - Pod).GetSafeNormal() : Nose(J->RenderRot);
	TgpCamPos = Pod + TgpCamFwd * 25.0;   // the real EOTS window sits outside the airframe: keep our own mesh out of the view
	TgpCamUp = FVector(0, 1, 0);
	if (!IsLocal()) { return; }
	APlayerController* PC = Cast<APlayerController>(J->GetController());
	if (!PC) { return; }
	if (bTgpView && J->bAlive)
	{
		if (!TgpCam)
		{
			FActorSpawnParameters Sp;
			Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			TgpCam = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Sp);
			UCameraComponent* C = TgpCam->GetCameraComponent();
			C->bConstrainAspectRatio = false;
			FPostProcessSettings& P = C->PostProcessSettings;
			P.bOverride_ColorSaturation = true; P.ColorSaturation = FVector4(0, 0, 0, 1);
			P.bOverride_ColorContrast = true; P.ColorContrast = FVector4(1.25, 1.25, 1.25, 1);
			P.bOverride_ColorGain = true; P.ColorGain = FVector4(0.92, 1.0, 0.95, 1);
			P.bOverride_FilmGrainIntensity = true; P.FilmGrainIntensity = 0.35f;
			P.bOverride_VignetteIntensity = true; P.VignetteIntensity = 0.7f;
			P.bOverride_MotionBlurAmount = true; P.MotionBlurAmount = 0.0f;
			P.bOverride_BloomIntensity = true; P.BloomIntensity = 0.2f;
			P.bOverride_AutoExposureBias = true; P.AutoExposureBias = 1.0f;   // pod AGC: shadowed slopes stay readable
		}
		double Aspect = 16.0 / 9.0;
		FVector2D Sz;
		if (GEngine && GEngine->GameViewport) { GEngine->GameViewport->GetViewportSize(Sz); if (Sz.Y > 1.0) { Aspect = Sz.X / Sz.Y; } }
		const double HFov = 2.0 * Deg(FMath::Atan(FMath::Tan(Rad(TgpFovDeg()) * 0.5) * Aspect));
		TgpCam->GetCameraComponent()->SetFieldOfView(float(HFov));
		TgpCam->SetActorLocationAndRotation(ToUE(TgpCamPos), FRotationMatrix::MakeFromXZ(DirToUE(TgpCamFwd), FVector(0, 0, 1)).ToQuat());
		if (PC->GetViewTarget() != TgpCam) { PC->SetViewTarget(TgpCam); }
	}
	else if (TgpCam && PC->GetViewTarget() == TgpCam)
	{
		PC->SetViewTarget(J);
	}
}

void UOsrWeaponsComponent::UpdateCues(double Dt)
{
	// the predictor flies whole bomb trajectories: spread the work (one "release now" sim every 0.2 s, and one step of the
	// release-point sweep per frame) so it never costs a frame spike
	AOsrJetPawn* J = Jet();
	if (!J->bAlive || Bombs <= 0 || (Selected != EOsrWeapon::Lgb && !bTgpView))
	{
		bInRange = false; TimeToRelease = -1.0; SweepK = 0;
		return;
	}
	const FVector P = J->Model.Position + J->Model.Orientation.RotateVector(FVector(0.0, -1.4, 0.3));
	const FVector V = J->Model.Velocity;
	CueAcc += Dt;
	if (CueAcc >= 0.2)
	{
		CueAcc = 0.0;
		const FOsrBombSim S = AOsrLaserBomb::Simulate(P, V, bTgpValid ? TgpPoint : P, bTgpValid);
		PredImpact = S.Impact; PredTof = S.Tof; PredMiss = S.bImpact && bTgpValid ? S.Miss : 1e9;
		bInRange = bTgpValid && S.bImpact && S.Miss < 2.5;
		if (bInRange) { TimeToRelease = 0.0; SweepK = 0; }
	}
	if (!bTgpValid || bInRange) { return; }
	// sweep: straight-line extrapolation 1..20 s ahead, one candidate per frame
	++SweepK;
	const FVector Pk = P + V * double(SweepK);
	bool bDone = SweepK >= 20 || Pk.Y < Ground(Pk.X, Pk.Z) + 30.0;
	if (!bDone)
	{
		const FOsrBombSim Sk = AOsrLaserBomb::Simulate(Pk, V, TgpPoint, true);
		if (Sk.bImpact && Sk.Miss < 2.5) { SweepBest = SweepK; bDone = true; }
	}
	if (bDone)
	{
		TimeToRelease = SweepBest > 0 ? double(SweepBest) : -1.0;
		SweepBest = -1;
		SweepK = 0;
	}
}

void UOsrWeaponsComponent::UpdateThreats(double Dt)
{
	AOsrJetPawn* J = Jet();
	RwrAcc += Dt;
	if (RwrAcc >= 0.2)
	{
		RwrAcc = 0.0;
		AOsrRadarSite::RwrPicture(GetWorld(), J, Rwr);
		for (TActorIterator<AOsrEnemyFighter> It(GetWorld()); It; ++It)
		{
			FOsrRwrContact C;
			if (It->RwrFor(J, C)) { Rwr.Add(C); }
		}
		for (FOsrRwrContact& C : Rwr)
		{
			const FString Key = C.Symbol + FString::Printf(TEXT("%.0f"), C.From.X);
			C.bNew = !KnownEmitters.Contains(Key);
			if (C.bNew) { KnownEmitters.Add(Key); PlayUi("rwr_search", -10.0); }
		}
		if (Rwr.Num() == 0) { KnownEmitters.Reset(); }
	}
	// missiles homing on us: radar ones show on the RWR (launch), IR ones only through DAS
	MissilesInbound = 0;
	IrInbound = 0;
	NearestMissileDist = 1e9;
	const FVector Me = J->Model.Position;
	for (AOsrMissile* M : AOsrMissile::HomingOn(GetWorld(), J))
	{
		if (M->bIr()) { ++IrInbound; } else { ++MissilesInbound; }
		const double D = FVector::Dist(M->P, Me);
		if (D < NearestMissileDist) { NearestMissileDist = D; NearestMissileDir = (M->P - Me).GetSafeNormal(); }
		if (!KnownMissiles.Contains(M))
		{
			KnownMissiles.Add(M);
			PlayUi(M->bIr() ? FName("v_missile") : FName("v_missile_launch"), 2.0);
		}
	}
	bool bTrack = false, bLaunch = MissilesInbound > 0;
	for (const FOsrRwrContact& C : Rwr) { bTrack |= C.State == EOsrRwr::Track; bLaunch |= C.State == EOsrRwr::Launch; }
	if (!J->bAlive) { bTrack = bLaunch = false; }
	if (LaunchTone)
	{
		if (bLaunch && !LaunchTone->IsPlaying()) { LaunchTone->Play(); }
		else if (!bLaunch && LaunchTone->IsPlaying()) { LaunchTone->Stop(); }
	}
	if (LockTone)
	{
		const bool bL = bTrack && !bLaunch;
		if (bL && !LockTone->IsPlaying()) { LockTone->Play(); }
		else if (!bL && LockTone->IsPlaying()) { LockTone->Stop(); }
	}
	// periodic search chirp while painted
	SearchChirpAcc += Dt;
	if (Rwr.Num() > 0 && !bTrack && !bLaunch && SearchChirpAcc > 4.0) { SearchChirpAcc = 0.0; PlayUi("rwr_search", -14.0); }
}

void UOsrWeaponsComponent::PlayUi(FName N, double VolDb)
{
	if (!IsLocal()) { return; }
	if (USoundBase* S = Sounds.FindRef(N)) { UGameplayStatics::PlaySound2D(this, S, float(FMath::Pow(10.0, VolDb / 20.0))); }
}
