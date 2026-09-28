// Game mode + integration autotest: ocean spawn shot, 45 s canyon autopilot at ~215 m/s / 70 m AGL, deliberate crash + respawn, REPORT lines, quit.
#include "OsrGameMode.h"
#include "OsrJetPawn.h"
#include "OsrHud.h"
#include "OsrTerrain.h"
#include "OsrWorldLayout.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"
#include "Kismet/KismetSystemLibrary.h"
#include "RHI.h"

using namespace OsrMath;

AOsrGameMode::AOsrGameMode()
{
	DefaultPawnClass = AOsrJetPawn::StaticClass();
	HUDClass = AOsrHud::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void AOsrGameMode::StartPlay()
{
	bFlythrough = FParse::Param(FCommandLine::Get(), TEXT("flythrough"));
	bAutotest = FParse::Param(FCommandLine::Get(), TEXT("autotest"));
	FParse::Value(FCommandLine::Get(), TEXT("canyon_time="), CanyonTime);
	if (!bFlythrough)
	{
		for (TActorIterator<ALevelSequenceActor> It(GetWorld()); It; ++It)
		{
			if (ULevelSequencePlayer* P = It->GetSequencePlayer()) { P->Stop(); }
			It->Destroy();
		}
	}
	Super::StartPlay();
	if (bAutotest)
	{
		FString Txt;
		FFileHelper::LoadFileToString(Txt, *(FPaths::ProjectContentDir() / TEXT("Data/canyon_path.txt")));
		TArray<FString> Lines;
		Txt.ParseIntoArrayLines(Lines);
		for (const FString& L : Lines)
		{
			TArray<FString> P;
			L.ParseIntoArrayWS(P);
			if (P.Num() == 3) { Path.Add(FVector(FCString::Atod(*P[0]), FCString::Atod(*P[1]), FCString::Atod(*P[2]))); }
		}
		UE_LOG(LogTemp, Display, TEXT("REPORT autotest start gpu=%s path_points=%d"), *GRHIAdapterName, Path.Num());
	}
}

void AOsrGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FollowOcean();
	if (!bAutotest) { return; }
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	AOsrJetPawn* Jet = PC ? Cast<AOsrJetPawn>(PC->GetPawn()) : nullptr;
	if (Jet) { AutotestStep(Jet, DeltaSeconds); }
}

void AOsrGameMode::FollowOcean()
{
	// the ocean grid (dense at its centre) rides under the camera, snapped so its vertices do not swim; waves use world position
	if (!Ocean.IsValid())
	{
		if (bOceanSearched) { return; }
		bOceanSearched = true;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("OsrOcean"))) { Ocean = *It; break; }
		}
		if (!Ocean.IsValid()) { return; }
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager) { return; }
	const FVector C = PC->PlayerCameraManager->GetCameraLocation();
	const double Snap = 400.0;   // cm, multiple of the centre quad spacing (~3 m)
	Ocean->SetActorLocation(FVector(FMath::GridSnap<double>(C.X, Snap), FMath::GridSnap<double>(C.Y, Snap), 0.0));
}

void AOsrGameMode::Shot(const FString& Name)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("noshots"))) { return; }   // screenshot readback costs ~0.4 s per shot: perf runs skip them
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Autotest");
	FScreenshotRequest::RequestScreenshot(Dir / (Name + TEXT(".png")), true, false);
	UE_LOG(LogTemp, Display, TEXT("REPORT shot %s t=%.1f"), *Name, Total);
}

void AOsrGameMode::StartCanyon(AOsrJetPawn* Jet)
{
	const FOsrTerrain& Ter = FOsrTerrain::Get();
	int32 I = 0;
	for (int32 K = 0; K < Path.Num(); ++K)
	{
		if (Ter.HeightAt(Path[K].X, Path[K].Z) > 5.0) { I = FMath::Max(K - 2, 0); break; }
	}
	PathI = I;
	const FVector A = Path[I];
	const FVector B = Path[FMath::Min(I + 2, Path.Num() - 1)];
	const FVector D = FVector(B.X - A.X, 0.0, B.Z - A.Z).GetSafeNormal();
	FVector P = A;
	P.Y = Ter.SurfaceAt(A.X, A.Z) + 90.0;
	Jet->RespawnAt(P, AOsrJetPawn::LookingAt(D, FVector(0, 1, 0)), 215.0);
	Jet->AutopilotLever = 0.85;
	Phase = TEXT("canyon");
	T = 0.0;
}

FVector2D AOsrGameMode::CanyonPilot(AOsrJetPawn* Jet)
{
	const FF35FlightModel& M = Jet->Model;
	const FVector Pos = M.Position;
	int32 Best = PathI;
	double BestD = TNumericLimits<double>::Max();
	for (int32 K = PathI; K < FMath::Min(PathI + 12, Path.Num()); ++K)
	{
		const double D = FVector2D(Path[K].X - Pos.X, Path[K].Z - Pos.Z).Size();
		if (D < BestD) { BestD = D; Best = K; }
	}
	PathI = Best;
	if (PathI >= Path.Num() - 4)
	{
		StartCanyon(Jet);
		return FVector2D::ZeroVector;
	}
	const FVector Look = Path[FMath::Min(PathI + 3, Path.Num() - 1)];
	const FVector To = FVector(Look.X - Pos.X, 0.0, Look.Z - Pos.Z).GetSafeNormal();
	const FVector Fwd = FVector(M.Velocity.X, 0.0, M.Velocity.Z).GetSafeNormal();
	const double Err = FMath::Atan2(FVector::CrossProduct(Fwd, To).Y * -1.0, FVector::DotProduct(Fwd, To));
	const double Omega = FMath::Clamp(Err * 1.6, -0.3, 0.3);
	const double BankDes = FMath::Clamp(FMath::Atan(Omega * M.Tas / 9.81), Rad(-78.0), Rad(78.0));
	const double Roll = FMath::Clamp((BankDes - M.Bank) * 2.5 - M.Rates.Z * 0.2, -1.0, 1.0);
	double FloorH = 0.0;
	for (int32 K = 0; K < 8; ++K)
	{
		const FVector Q = Pos + M.Velocity * (K * 0.2);
		FloorH = FMath::Max(FloorH, FOsrTerrain::Get().SurfaceAt(Q.X, Q.Z));
	}
	const double GammaDes = FMath::Clamp((FloorH + 70.0 - Pos.Y) * 0.012, -0.12, 0.3);
	const double Cb = FMath::Cos(M.Bank);
	const double N0 = FMath::Cos(M.PathAngle) * (Cb >= 0.5 ? 1.0 / Cb : 4.0 * Cb);
	const double NNeed = FMath::Cos(M.PathAngle) / FMath::Max(Cb, 0.2);
	const double Ff = NNeed > N0 ? (NNeed - N0) / FMath::Max(7.5 - N0, 0.5) : 0.0;
	const double Pitch = FMath::Clamp(Ff + (GammaDes - M.PathAngle) * 3.0 - M.Rates.X * 0.3, -0.7, 1.0);
	Jet->AutopilotLever = FMath::Clamp(Jet->AutopilotLever + (215.0 - M.Tas) * 0.0015, 0.3, 1.6);
	return FVector2D(Pitch, Roll);
}

void AOsrGameMode::AutotestStep(AOsrJetPawn* Jet, double Dt)
{
	Jet->bAutopilot = true;
	FOsrControlInput& Ap = Jet->AutopilotInput;
	T += Dt;
	Total += Dt;
	if (Phase == TEXT("spawn"))
	{
		Ap.Pitch = 0.0; Ap.Roll = 0.0; Ap.Throttle = 0.85; Ap.Afterburner = 0.0;
		if (T > 3.0 && Shots == 0)
		{
			Shot(TEXT("spawn_ocean"));
			Shots = -1;
		}
		if (T > 3.6)
		{
			Shots = 0;
			StartCanyon(Jet);
		}
	}
	else if (Phase == TEXT("canyon"))
	{
		if (T > 1.5)
		{
			Frames.Add(float(Dt));
			if (Dt > 0.05 && ShotT > 0.2)
			{
				++Hitches;
				if (Hitches <= 25)
				{
					UE_LOG(LogTemp, Display, TEXT("REPORT hitch t=%.2f ms=%.0f path_i=%d"), T, Dt * 1000.0, PathI);
				}
			}
		}
		const FVector2D Pr = CanyonPilot(Jet);
		Ap.Pitch = Pr.X;
		Ap.Roll = Pr.Y;
		Ap.Throttle = FMath::Min(Jet->AutopilotLever, 1.0);
		Ap.Afterburner = FMath::Clamp(Jet->AutopilotLever - 1.0, 0.0, 1.0);
		ShotT += Dt;
		if (ShotT > 5.0)
		{
			ShotT = 0.0;
			Shot(FString::Printf(TEXT("canyon_%02d"), Shots++));
		}
		if (T > CanyonTime)
		{
			ReportPerf(Jet);
			// overview shot (Godot hero "overview_4000m"): 4 km up, south-west of the fjord mouth, looking up the canyon
			const FVector From = OsrWorld::Mouth + FVector(-6000.0, 4000.0, 9000.0);
			const FVector To = OsrWorld::Mouth + FVector(14000.0, 600.0, -6000.0);
			Jet->RespawnAt(From, AOsrJetPawn::LookingAt((To - From).GetSafeNormal(), FVector(0, 1, 0)), 215.0);
			Phase = TEXT("overview");
			T = 0.0;
		}
	}
	else if (Phase == TEXT("overview"))
	{
		Ap.Pitch = 0.0; Ap.Roll = 0.0; Ap.Throttle = 0.85; Ap.Afterburner = 0.0;
		if (T > 2.5 && Shots < 50)
		{
			Shot(TEXT("overview"));       // captured at the end of this frame: move the jet only on a later tick
			Shots = 50;
		}
		if (T > 3.0)
		{
			StartCanyon(Jet);
			Phase = TEXT("crash");
			T = 0.0;
			CrashSeen = Jet->CrashCount;
			RespawnBase = Jet->RespawnCount;
		}
	}
	else if (Phase == TEXT("crash"))
	{
		Ap.Pitch = -1.0;
		Ap.Roll = 0.0;
		const bool bCrashed = Jet->CrashCount > CrashSeen;
		if (bCrashed && T > 1.2 && Shots < 100)
		{
			Shot(TEXT("crash"));
			Shots = 100;
		}
		if (bCrashed && Jet->bAlive && Jet->RespawnCount > RespawnBase)
		{
			bRespawnedSeen = true;
			T = 0.0;
			Phase = TEXT("after");
		}
	}
	else if (Phase == TEXT("after"))
	{
		Ap.Pitch = 0.0;
		if (T > 2.0 && Phase != TEXT("done"))
		{
			Shot(TEXT("respawned"));
			UE_LOG(LogTemp, Display, TEXT("REPORT crash_detected=%s respawned=%s total_s=%.1f"), Jet->CrashCount > CrashSeen ? TEXT("true") : TEXT("false"), bRespawnedSeen ? TEXT("true") : TEXT("false"), Total);
			UE_LOG(LogTemp, Display, TEXT("REPORT AUTOTEST DONE"));
			Phase = TEXT("done");
			T = 0.0;
		}
	}
	else if (Phase == TEXT("done") && T > 1.0)
	{
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
	if (Total > 65.0 + CanyonTime && Phase != TEXT("done"))
	{
		UE_LOG(LogTemp, Display, TEXT("REPORT AUTOTEST TIMEOUT phase=%s"), *Phase);
		Phase = TEXT("done");
		T = 0.0;
	}
}

void AOsrGameMode::ReportPerf(AOsrJetPawn* Jet)
{
	TArray<float> S = Frames;
	S.Sort();
	const int32 N = S.Num();
	if (N == 0) { return; }
	double Sum = 0.0;
	for (float F : S) { Sum += F; }
	double Low1 = 0.0;
	const int32 K = FMath::Max(int32(N * 0.01), 1);
	for (int32 J = 0; J < K; ++J) { Low1 += S[N - 1 - J]; }
	Low1 /= K;
	UE_LOG(LogTemp, Display, TEXT("REPORT canyon hitches=%d frames=%d avg_fps=%.1f low1_fps=%.1f min_fps=%.1f p50_ms=%.1f alive=%s crashes=%d"),
		Hitches, N, N / FMath::Max(Sum, 0.001), 1.0 / FMath::Max(Low1, 0.0001), 1.0 / FMath::Max<double>(S[N - 1], 0.0001), S[N / 2] * 1000.0, Jet->bAlive ? TEXT("true") : TEXT("false"), Jet->CrashCount);
	const FOsrTelemetry Tel = Jet->Telemetry();
	UE_LOG(LogTemp, Display, TEXT("REPORT canyon_end ias=%.0f radar_alt_ft=%.0f path_i=%d/%d"), Tel.IasKt, Tel.RadarAltFt, PathI, Path.Num());
}
