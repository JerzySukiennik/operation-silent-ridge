// Carrier subsystem: deck start, pawn switching, respawn on deck, -autotest=carrier.
#include "CarrierSubsystem.h"
#include "CarrierActor.h"
#include "CarrierLayout.h"
#include "CarrierJetOps.h"
#include "CarrierF35Anim.h"
#include "OsrCharacter.h"
#include "OsrJetPawn.h"
#include "OsrPadInput.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "RHI.h"

using namespace OsrMath;

namespace
{
	constexpr double Sub_KT = 0.514444;
}

bool UCarrierSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (FParse::Param(FCommandLine::Get(), TEXT("nocarrier"))) { return false; }
	const UWorld* W = Cast<UWorld>(Outer);
	return W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE);
}

void UCarrierSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FString Mode;
	FParse::Value(FCommandLine::Get(), TEXT("autotest="), Mode);
	bAutotest = Mode == TEXT("carrier");
	// the canyon autotest (-autotest) and other autotests keep the airborne start; normal play and -autotest=carrier start on deck
	bDeckMode = !FParse::Param(FCommandLine::Get(), TEXT("autotest")) && (Mode.IsEmpty() || bAutotest) && !FParse::Param(FCommandLine::Get(), TEXT("airstart"));
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Carrier = InWorld.SpawnActor<ACarrierActor>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	Phase = TEXT("setup");
	if (bAutotest) { UE_LOG(LogTemp, Display, TEXT("REPORT autotest=carrier start gpu=%s"), *GRHIAdapterName); }
}

AOsrCharacter* UCarrierSubsystem::SpawnWalker(const FVector& LocalM, double YawDeg)
{
	UWorld* W = GetWorld();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FRotator R(0.0, Carrier->Pose().YawDeg + YawDeg, 0.0);
	return W->SpawnActor<AOsrCharacter>(Carrier->LocalToUE(LocalM), R, P);
}

void UCarrierSubsystem::SetupDeck()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	Jet = PC ? Cast<AOsrJetPawn>(PC->GetPawn()) : nullptr;
	if (!Jet || !Carrier) { return; }
	bSetupDone = true;
	// plain -autotest (canyon regression) runs the stage-2 jet untouched: no carrier ops on the jet
	if (FParse::Param(FCommandLine::Get(), TEXT("autotest"))) { return; }
	Ops = NewObject<UCarrierJetOps>(Jet, TEXT("CarrierOps"));
	Ops->RegisterComponent();
	Ops->Setup(Jet, Carrier);
	if (!bDeckMode)
	{
		Ops->State = EDeckState::Airborne;   // air start (other autotests, -airstart): carrier ops available for the recovery
		Ops->bManned = true;
		return;
	}
	Ops->ParkAt(FVector2D(CarrierLayout::PlayerJet.X, CarrierLayout::PlayerJet.Y), CarrierLayout::PlayerJet.Z);
	Foot = SpawnWalker(FVector(CarrierLayout::Spawn.X, CarrierLayout::Spawn.Y, CarrierLayout::DeckZ + 1.0), 20.0);
	if (Foot)
	{
		PC->Possess(Foot);
		PC->SetControlRotation(FRotator(-8.0, Carrier->Pose().YawDeg + 20.0, 0.0));
		Ops->Unman();
		Carrier->LastSpawnedOnDeck = Foot;
		SpawnBroadcastIn = 5;
		UE_LOG(LogTemp, Display, TEXT("REPORT carrier_spawn on_foot=true local=%s"), *Carrier->UEToLocal(Foot->GetActorLocation()).ToString());
	}
}

void UCarrierSubsystem::Fade(double From, double To, double Time)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager) { PC->PlayerCameraManager->StartCameraFade(float(From), float(To), float(Time), FLinearColor::Black, false, To > 0.5); }
	}
}

void UCarrierSubsystem::EnterJet()
{
	if (SwitchT >= 0.0 || !Jet || !Ops || !Ops->OnDeck()) { return; }
	Fade(0.0, 1.0, 0.35);
	SwitchT = 0.4;
	SwitchTo = 1;
}

void UCarrierSubsystem::ExitJet()
{
	if (SwitchT >= 0.0 || !Ops || !Ops->CanExit()) { return; }
	Fade(0.0, 1.0, 0.35);
	SwitchT = 0.4;
	SwitchTo = 2;
}

void UCarrierSubsystem::DoSwitch()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) { return; }
	if (SwitchTo == 1 && Jet)
	{
		if (Foot) { Foot->Destroy(); Foot = nullptr; }
		PC->Possess(Jet);
		Ops->Board();
		UE_LOG(LogTemp, Display, TEXT("REPORT carrier_boarded t=%.1f"), GetWorld()->GetTimeSeconds());
	}
	else if (SwitchTo == 2 && Ops)
	{
		const FVector2D L = Ops->LadderPoint();
		const FVector2D F(FMath::Cos(FMath::DegreesToRadians(Ops->DeckYaw())), FMath::Sin(FMath::DegreesToRadians(Ops->DeckYaw())));
		Foot = SpawnWalker(FVector(L.X - F.X * 1.5, L.Y - F.Y * 1.5, CarrierLayout::DeckZ + 1.0), Ops->DeckYaw() - 90.0);
		if (Foot)
		{
			Ops->Unman();
			PC->Possess(Foot);
			PC->SetControlRotation(FRotator(-10.0, Carrier->Pose().YawDeg + Ops->DeckYaw() + 150.0, 0.0));
		}
	}
	Fade(1.0, 0.0, 0.6);
	SwitchTo = 0;
}

void UCarrierSubsystem::RespawnOnDeck()
{
	if (!Carrier || !Ops) { return; }
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	Ops->ParkAt(FVector2D(CarrierLayout::PlayerJet.X, CarrierLayout::PlayerJet.Y), CarrierLayout::PlayerJet.Z);
	if (!Foot) { Foot = SpawnWalker(FVector(CarrierLayout::Spawn.X, CarrierLayout::Spawn.Y, CarrierLayout::DeckZ + 1.0), 20.0); }
	else
	{
		Foot->SetActorLocation(Carrier->LocalToUE(FVector(CarrierLayout::Spawn.X, CarrierLayout::Spawn.Y, CarrierLayout::DeckZ + 1.0)), false, nullptr, ETeleportType::ResetPhysics);
		Foot->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	}
	if (PC && Foot)
	{
		if (PC->GetPawn() != Foot) { PC->Possess(Foot); }
		PC->SetControlRotation(FRotator(-8.0, Carrier->Pose().YawDeg + 20.0, 0.0));
	}
	Ops->Unman();
	Fade(1.0, 0.0, 1.0);
	Carrier->LastSpawnedOnDeck = Foot;
	Carrier->OnSpawnedOnDeck.Broadcast(Foot);
	UE_LOG(LogTemp, Display, TEXT("REPORT carrier_respawn_on_deck t=%.1f"), GetWorld()->GetTimeSeconds());
}

void UCarrierSubsystem::Tick(float DeltaTime)
{
	if (!Carrier) { return; }
	if (!bSetupDone) { SetupDeck(); }
	if (!bSetupDone) { return; }
	if (SpawnBroadcastIn >= 0 && --SpawnBroadcastIn < 0 && Foot) { Carrier->OnSpawnedOnDeck.Broadcast(Foot); }
	if (SwitchT >= 0.0)
	{
		SwitchT -= DeltaTime;
		if (SwitchT < 0.0) { DoSwitch(); }
	}
	// climb out: hold X / Square / F while stopped on deck
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && Jet && PC->GetPawn() == Jet && Ops && bDeckMode && !Jet->bPaused)
	{
		const FOsrPadState P = FOsrPadInput::Get();
		const bool bHold = PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left) || PC->IsInputKeyDown(EKeys::F) || P.bSquare;
		ExitHold = bHold && Ops->CanExit() ? ExitHold + DeltaTime : 0.0;
		if (ExitHold > 0.7) { ExitHold = 0.0; ExitJet(); }
	}
	if (bAutotest) { Autotest(FMath::Min<double>(DeltaTime, 0.1)); }
}

// ---------------------------------------------------------------- -autotest=carrier

void UCarrierSubsystem::Shot(const FString& Name, int32 Slot)
{
	if (bShot[Slot]) { return; }
	bShot[Slot] = true;
	if (FParse::Param(FCommandLine::Get(), TEXT("noshots"))) { return; }
	FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Autotest") / (Name + TEXT(".png")), true, false);
	UE_LOG(LogTemp, Display, TEXT("REPORT shot %s t=%.1f"), *Name, Total);
}

FString UCarrierSubsystem::Perf(const TArray<float>& F)
{
	TArray<float> S = F;
	S.Sort();
	const int32 N = S.Num();
	if (N == 0) { return TEXT("frames=0"); }
	double Sum = 0.0;
	for (float X : S) { Sum += X; }
	const int32 K = FMath::Max(N / 100, 1);
	double Low = 0.0;
	for (int32 J = 0; J < K; ++J) { Low += S[N - 1 - J]; }
	Low /= K;
	return FString::Printf(TEXT("frames=%d avg_fps=%.1f low1_fps=%.1f p50_ms=%.1f"), N, N / FMath::Max(Sum, 1e-3), 1.0 / FMath::Max(Low, 1e-4), S[N / 2] * 1000.0);
}

void UCarrierSubsystem::AutoTaxi()
{
	// taxi autopilot: pure pursuit of the cat 1 line (nose gear), slow speed, brakes to hold ~3 m/s
	const CarrierLayout::FCat& C = CarrierLayout::Cats[0];
	const double H = FMath::DegreesToRadians(C.HdgDeg);
	const FVector2D D(FMath::Cos(H), FMath::Sin(H)), S(C.X, C.Y);
	const double Yw = FMath::DegreesToRadians(Ops->DeckYaw());
	const FVector2D F(FMath::Cos(Yw), FMath::Sin(Yw));
	const FVector2D Nose = Ops->DeckPos() + F * (CarrierF35Anim::NoseWheel.X * 0.01);
	const double Along = FVector2D::DotProduct(Nose - S, D);
	FVector2D Aim = S + D * FMath::Min(Along + 9.0, 0.5);
	if (Along < -28.0) { Aim = S - D * 18.0; }
	const FVector2D To = Aim - Nose;
	const double Err = FMath::RadiansToDegrees(FMath::Atan2(F.X * To.Y - F.Y * To.X, FVector2D::DotProduct(F, To)));
	FOsrControlInput& Ap = Jet->AutopilotInput;
	Ap.Yaw = FMath::Clamp(Err / 30.0, -1.0, 1.0);
	Ap.Roll = 0.0;
	Ap.Pitch = 0.0;
	const double Target = Along > -9.0 ? 1.8 : 3.2;
	Ap.Throttle = Ops->DeckSpeed() < Target ? 0.18 : 0.0;
	Ap.bBrake = Ops->DeckSpeed() > Target + 0.6;
	Ap.Afterburner = 0.0;
}

void UCarrierSubsystem::AutoApproach(double Dt)
{
	// DFP holds the glideslope; the autopilot trims glideslope errors with the stick and flies lineup with bank
	FOsrControlInput& Ap = Jet->AutopilotInput;
	const FVector2D Td = ACarrierActor::LandPoint(CarrierLayout::TouchdownS, 0.0);
	const FVector Sb = Carrier->UEToStabilized(Jet->GetActorLocation(), FVector(Td.X, Td.Y, CarrierLayout::DeckZ));
	const double H = FMath::DegreesToRadians(CarrierLayout::LandHdgDeg);
	const double Lat = -Sb.X * FMath::Sin(H) + Sb.Y * FMath::Cos(H);
	const double LatRate = Dt > 0.0 ? (Lat - LatPrev) / Dt : 0.0;
	LatPrev = Lat;
	const double BankDes = FMath::Clamp(-(0.6 * Lat + 2.2 * LatRate), -18.0, 18.0);
	const FF35FlightModel& M = Jet->Model;
	Ap.Roll = FMath::Clamp((FMath::DegreesToRadians(BankDes) - M.Bank) * 2.5 - M.Rates.Z * 0.25, -1.0, 1.0);
	Ap.Pitch = Ops->Lso.bActive ? FMath::Clamp(-Ops->Lso.BallDev * 0.8, -0.5, 0.5) : 0.0;
	Ap.Yaw = 0.0;
	Ap.Throttle = FMath::Min(Jet->AutopilotLever, 1.0);
	Ap.Afterburner = 0.0;
	Ap.bBrake = false;
}

void UCarrierSubsystem::Autotest(double Dt)
{
	T += Dt;
	Total += Dt;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !Ops || !Jet) { return; }
	if (Phase == TEXT("setup"))
	{
		if (T > 3.0) { Shot(TEXT("deck_on_foot"), 0); }
		if (T > 3.6) { Phase = FParse::Param(FCommandLine::Get(), TEXT("noelevator")) ? TEXT("walk") : TEXT("elevator"); T = 0.0; }
	}
	else if (Phase == TEXT("elevator"))
	{
		// ride elevator 2 down to the hangar deck, walk in, walk back, ride up (based movement on a moving platform)
		FramesDeck.Add(float(Dt));
		const CarrierLayout::FElevator& E = CarrierLayout::Elevators[1];
		const FVector PlatC((E.X0 + E.X1) * 0.5, (E.Y0 + E.Y1) * 0.5 + 3.0, 0.0);
		const double Yin = Carrier->Pose().YawDeg - 90.0;   // facing inboard (port)
		const FVector Inboard = FRotator(0.0, Yin, 0.0).Vector();
		if (Foot)
		{
			Foot->bAutoDrive = true;
			Foot->AutoMoveWorld = FVector::ZeroVector;
			if (T < Dt * 1.5)
			{
				Foot->SetActorLocation(Carrier->LocalToUE(FVector(PlatC.X, PlatC.Y, CarrierLayout::DeckZ + 1.0)), false, nullptr, ETeleportType::ResetPhysics);
				PC->SetControlRotation(FRotator(-10.0, Yin, 0.0));
			}
			if (T > 1.0 && T < 1.2) { Carrier->CommandElevator(true); }
			if (T > 14.0 && T < 18.0) { Foot->AutoMoveWorld = Inboard; PC->SetControlRotation(FRotator(-5.0, Yin, 0.0)); }
			if (T > 18.5) { Shot(TEXT("hangar"), 11); }
			if (T > 18.6 && T - Dt <= 18.6)
			{
				const FVector L = Carrier->UEToLocal(Foot->GetActorLocation());
				UE_LOG(LogTemp, Display, TEXT("REPORT carrier_hangar_walk local=(%.1f, %.1f, %.2f) elevator_down=%s in_hangar=%s"), L.X, L.Y, L.Z,
					Carrier->IsElevatorDown() ? TEXT("true") : TEXT("false"), L.Z < CarrierLayout::HangarCeil && L.Y < CarrierLayout::Hangar.Max.Y ? TEXT("true") : TEXT("false"));
			}
			if (T > 19.0 && T < 23.5)
			{
				const FVector Goal = Carrier->LocalToUE(FVector(PlatC.X, PlatC.Y, CarrierLayout::HangarFloor + 1.0));
				FVector To = Goal - Foot->GetActorLocation(); To.Z = 0.0;
				Foot->AutoMoveWorld = To.Size() > 60.0 ? To.GetSafeNormal() : FVector::ZeroVector;
			}
			if (T > 24.0 && T < 24.2) { Carrier->CommandElevator(false); }
			if (T > 37.0)
			{
				const FVector L = Carrier->UEToLocal(Foot->GetActorLocation());
				UE_LOG(LogTemp, Display, TEXT("REPORT carrier_elevator_up local_z=%.2f deck_z=%.1f on_deck=%s"), L.Z, CarrierLayout::DeckZ, FMath::Abs(L.Z - CarrierLayout::DeckZ - 0.92) < 0.5 ? TEXT("true") : TEXT("false"));
				Shot(TEXT("elevator_up"), 12);
				Phase = TEXT("walk");
				T = 0.0;
			}
		}
		else { Phase = TEXT("walk"); T = 0.0; }
	}
	else if (Phase == TEXT("walk"))
	{
		FramesDeck.Add(float(Dt));
		if (Foot)
		{
			const FVector2D Lad = Ops->LadderPoint();
			const FVector Goal = Carrier->LocalToUE(FVector(Lad.X, Lad.Y, CarrierLayout::DeckZ + 0.9));
			FVector To = Goal - Foot->GetActorLocation(); To.Z = 0.0;
			Foot->bAutoDrive = true;
			Foot->AutoMoveWorld = To.Size() > 150.0 ? To.GetSafeNormal() : FVector::ZeroVector;
			PC->SetControlRotation(FRotator(-12.0, To.Rotation().Yaw + 25.0, 0.0));
			if (T > 5.0) { Shot(TEXT("deck_walk"), 1); }
			Foot->bAutoInteract = To.Size() < 250.0 && FMath::Fmod(T, 0.5) < 0.25;
			if (T > 45.0 && To.Size() > 250.0)
			{
				UE_LOG(LogTemp, Display, TEXT("REPORT carrier_walk_timeout dist_m=%.1f -> teleport"), To.Size() * 0.01);
				Foot->SetActorLocation(Goal, false, nullptr, ETeleportType::ResetPhysics);
			}
		}
		if (PC->GetPawn() == Jet)
		{
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_walk_to_jet s=%.1f"), T);
			Jet->bAutopilot = true;
			Phase = TEXT("taxi");
			T = 0.0;
		}
	}
	else if (Phase == TEXT("taxi"))
	{
		FramesDeck.Add(float(Dt));
		Jet->bAutopilot = true;
		AutoTaxi();
		if (T > 4.0) { Shot(TEXT("deck_taxi"), 2); }
		if (Ops->State == EDeckState::Hookup || Ops->State == EDeckState::OnCat)
		{
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_taxi_to_cat s=%.1f"), T);
			Phase = TEXT("cat");
			T = 0.0;
		}
		if (T > 70.0)
		{
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_taxi_timeout state=%s pos=%s yaw=%.0f -> placed at the cat"), *Ops->StateName(), *Ops->DeckPos().ToString(), Ops->DeckYaw());
			const CarrierLayout::FCat& C = CarrierLayout::Cats[0];
			const double H = FMath::DegreesToRadians(C.HdgDeg);
			Ops->ParkAt(FVector2D(C.X, C.Y) - FVector2D(FMath::Cos(H), FMath::Sin(H)) * (CarrierF35Anim::NoseWheel.X * 0.01 + 2.0), C.HdgDeg);
			Ops->Board();
			T = 0.0;
		}
	}
	else if (Phase == TEXT("cat"))
	{
		FramesDeck.Add(float(Dt));
		FOsrControlInput& Ap = Jet->AutopilotInput;
		Ap.Yaw = 0.0; Ap.bBrake = false; Ap.Pitch = 0.0; Ap.Roll = 0.0;
		Ap.Throttle = Ops->State == EDeckState::OnCat || Ops->State == EDeckState::Launch ? 1.0 : 0.0;
		Ops->bAutoSalute = true;
		if (Ops->State == EDeckState::OnCat && Ops->StateT > 1.5) { Shot(TEXT("cat_hookup"), 3); }
		if (Ops->State == EDeckState::Launch && Ops->StateT > 1.0) { Shot(TEXT("cat_launch"), 4); }
		if (Ops->State == EDeckState::Airborne)
		{
			Phase = TEXT("climb");
			T = 0.0;
		}
		if (T > 40.0) { UE_LOG(LogTemp, Display, TEXT("REPORT carrier_cat_timeout state=%s"), *Ops->StateName()); Phase = TEXT("climb"); T = 0.0; }
	}
	else if (Phase == TEXT("climb"))
	{
		FOsrControlInput& Ap = Jet->AutopilotInput;
		const FF35FlightModel& M = Jet->Model;
		const double GammaDes = FMath::DegreesToRadians(T < 6.0 ? 6.0 : 1.0);
		Ap.Pitch = FMath::Clamp((GammaDes - M.PathAngle) * 3.0 - M.Rates.X * 0.3, -0.6, 0.8);
		Ap.Roll = FMath::Clamp(-M.Bank * 2.0, -1.0, 1.0);
		Ap.Throttle = 1.0;
		Ap.Afterburner = 0.0;
		if (T > 2.5 && Jet->bGearDown) { Ap.bGearToggle = true; }
		if (T > 1.5) { Shot(TEXT("after_launch"), 5); }
		// external views of the carrier for the look review (fixed cameras, then back to the chase cam)
		auto View = [&](const FVector& FromLocal, const FVector& AtLocal)
		{
			const FVector From = Carrier->LocalToUE(FromLocal), At = Carrier->LocalToUE(AtLocal);
			ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(From, (At - From).Rotation());
			if (Cam) { Cam->GetCameraComponent()->SetFieldOfView(50.f); Cam->SetLifeSpan(1.2f); PC->SetViewTarget(Cam); }
		};
		if (T > 3.6 && !bShot[13] && T < 4.0) { View(FVector(-330.0, 260.0, 95.0), FVector(-10.0, 0.0, 10.0)); }
		if (T > 4.4) { Shot(TEXT("carrier_overview"), 13); }
		if (T > 5.2 && !bShot[14] && T < 5.6) { View(FVector(230.0, -150.0, 26.0), FVector(0.0, 0.0, 16.0)); }
		if (T > 6.0) { Shot(TEXT("carrier_bow"), 14); }
		if (T > 6.8 && !bShot[15] && T < 7.2) { View(FVector(-10.0, 60.0, 30.0), FVector(-30.0, 20.0, 30.0)); }
		if (T > 7.6) { Shot(TEXT("carrier_island"), 15); }
		if (T > 8.3 && PC->GetViewTarget() != Jet) { PC->SetViewTarget(Jet); }
		if (T > 3.0 && T - Dt <= 3.0)
		{
			const FVector L = Carrier->GodotToLocal(M.Position);
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_after_launch ias_kt=%.0f alt_ft=%.0f vs_fpm=%.0f alive=%s x_local=%.0f"), M.Cas / Sub_KT, M.Position.Y / 0.3048, M.Velocity.Y / 0.3048 * 60.0, Jet->bAlive ? TEXT("true") : TEXT("false"), L.X);
		}
		if (T > 9.0)
		{
			Jet->bGearDown = true;
			Ops->PlaceOnApproach(2300.0, 140.0 * Sub_KT);
			++Approaches;
			LatPrev = 0.0;
			Phase = TEXT("approach");
			T = 0.0;
		}
	}
	else if (Phase == TEXT("approach"))
	{
		if (Ops->State == EDeckState::Airborne) { FramesApproach.Add(float(Dt)); }
		AutoApproach(Dt);
		if (Ops->Lso.bActive && Ops->Lso.bBallCalled && T > 4.0) { Shot(Approaches == 1 ? TEXT("approach_ball") : TEXT("approach2_ball"), Approaches == 1 ? 6 : 9); }
		if (Ops->State == EDeckState::Arrest && Ops->StateT > 0.7) { Shot(TEXT("trap"), 7); }
		if (Ops->State == EDeckState::Stopped && Ops->StateT > 1.0)
		{
			Shot(TEXT("trapped"), 8);
			if (Ops->StateT > 1.6)
			{
				TrapText = FString::Printf(TEXT("%s comment=\"%s\""), *Ops->ResultText, *Ops->Lso.LastComment);
				// hook up, climb out, walk back in (pawn switching on the moving deck)
				Ops->bHookDown = false;
				Jet->AutopilotInput.Throttle = 0.0;
				Jet->AutopilotInput.bBrake = true;
				Jet->Controls.SetLever(0.0);
				Phase = TEXT("exit");
				T = 0.0;
			}
		}
		if (!Jet->bAlive || T > 60.0)
		{
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_approach_fail alive=%s state=%s t=%.0f"), Jet->bAlive ? TEXT("true") : TEXT("false"), *Ops->StateName(), T);
			Phase = TEXT("done");
			T = 0.0;
		}
	}
	else if (Phase == TEXT("exit"))
	{
		FOsrControlInput& Ap = Jet->AutopilotInput;
		Ap.Throttle = 0.0; Ap.Afterburner = 0.0; Ap.Yaw = 0.0; Ap.bBrake = true;
		if (T > 1.5 && PC->GetPawn() == Jet && Ops->CanExit()) { Jet->bAutopilot = false; ExitJet(); }
		if (Foot && PC->GetPawn() == Foot)
		{
			const FVector L = Carrier->UEToLocal(Foot->GetActorLocation());
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_climb_out ok=true t=%.1f local=(%.1f, %.1f, %.2f) jet_state=%s"), T, L.X, L.Y, L.Z, *Ops->StateName());
			Phase = TEXT("reenter");
			T = 0.0;
		}
		if (T > 12.0) { UE_LOG(LogTemp, Display, TEXT("REPORT carrier_climb_out ok=false state=%s"), *Ops->StateName()); Phase = TEXT("bolter_setup"); T = 0.0; }
	}
	else if (Phase == TEXT("reenter"))
	{
		if (T > 1.5) { Shot(TEXT("climb_out"), 16); }
		if (Foot && T > 2.0)
		{
			const FVector2D Lad = Ops->LadderPoint();
			FVector To = Carrier->LocalToUE(FVector(Lad.X, Lad.Y, CarrierLayout::DeckZ + 0.9)) - Foot->GetActorLocation(); To.Z = 0.0;
			Foot->bAutoDrive = true;
			Foot->AutoMoveWorld = To.Size() > 150.0 ? To.GetSafeNormal() : FVector::ZeroVector;
			Foot->bAutoInteract = To.Size() < 300.0 && FMath::Fmod(T, 0.5) < 0.25;
		}
		if (PC->GetPawn() == Jet && T > 2.5)
		{
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_reboard ok=true t=%.1f"), T);
			Phase = TEXT("bolter_setup");
			T = 0.0;
		}
		if (T > 25.0) { UE_LOG(LogTemp, Display, TEXT("REPORT carrier_reboard ok=false")); EnterJet(); Phase = TEXT("bolter_setup"); T = 0.0; }
	}
	else if (Phase == TEXT("bolter_setup"))
	{
		if (T > 1.0 && PC->GetPawn() == Jet)
		{
			// second pass: hook up on purpose -> LSO "hook down" + wave-off, touch-and-go = bolter
			Jet->bAutopilot = true;
			Ops->PlaceOnApproach(2300.0, 140.0 * Sub_KT);
			Ops->bHookDown = false;
			++Approaches;
			LatPrev = 0.0;
			Phase = TEXT("bolter");
			T = 0.0;
		}
	}
	else if (Phase == TEXT("bolter"))
	{
		AutoApproach(Dt);
		if (Ops->State == EDeckState::Rollout) { Shot(TEXT("bolter_roll"), 10); }
		const bool bDone = (Ops->Lso.Bolters > 0 && Ops->State == EDeckState::Airborne && T > 3.0) || !Jet->bAlive || T > 50.0 || Ops->State == EDeckState::Stopped;
		if (bDone)
		{
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_bolter_test bolters=%d waveoffs=%d alive=%s state=%s"), Ops->Lso.Bolters, Ops->Lso.WaveOffs, Jet->bAlive ? TEXT("true") : TEXT("false"), *Ops->StateName());
			Phase = TEXT("done");
			T = 0.0;
		}
	}
	else if (Phase == TEXT("done"))
	{
		if (T < Dt * 1.5)
		{
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_perf_deck %s"), *Perf(FramesDeck));
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_perf_approach %s"), *Perf(FramesApproach));
			UE_LOG(LogTemp, Display, TEXT("REPORT carrier_summary launch_end_kt=%.0f stroke_s=%.2f trap=\"%s\" wire=%d arrest_s=%.2f decel_g=%.1f traps=%d bolters=%d waveoffs=%d total_s=%.0f"),
				Ops->LaunchEndKt, Ops->LaunchStrokeS, *TrapText, Ops->LastWire, Ops->ArrestS, Ops->MaxDecelG, Ops->Lso.Traps, Ops->Lso.Bolters, Ops->Lso.WaveOffs, Total);
			UE_LOG(LogTemp, Display, TEXT("REPORT AUTOTEST DONE"));
		}
		if (T > 1.5) { UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); }
	}
	if (Total > 240.0 && Phase != TEXT("done"))
	{
		UE_LOG(LogTemp, Display, TEXT("REPORT AUTOTEST TIMEOUT phase=%s"), *Phase);
		Phase = TEXT("done");
		T = 0.0;
	}
}
