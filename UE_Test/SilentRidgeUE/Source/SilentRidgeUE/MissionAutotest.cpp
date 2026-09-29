// Strike autotest (see header).
#include "MissionAutotest.h"
#include "MissionState.h"
#include "MissionCommon.h"
#include "MissionLayout.h"
#include "MissionTarget.h"
#include "WeaponsComponent.h"
#include "WeaponBomb.h"
#include "SamMissile.h"
#include "EnemyFighter.h"
#include "OsrJetPawn.h"
#include "OsrWorldLayout.h"
#include "OsrTerrain.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

using namespace OsrMission;
using namespace OsrMath;

void FOsrStrikeAutotest::LoadPath()
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
}

void FOsrStrikeAutotest::Shot(const FString& Name, bool bOnce)
{
	if (bOnce && Shots.Contains(Name)) { return; }
	Shots.Add(Name);
	if (FParse::Param(FCommandLine::Get(), TEXT("noshots"))) { return; }
	FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Autotest") / (Name + TEXT(".png")), true, false);
	UE_LOG(LogTemp, Display, TEXT("REPORT shot %s t=%.1f"), *Name, Total);
}

void FOsrStrikeAutotest::Spectate(AOsrJetPawn* Jet, const FVector& FromG, const FVector& AtG, double Seconds)
{
	APlayerController* PC = Cast<APlayerController>(Jet->GetController());
	if (!PC) { return; }
	ACameraActor* C = Spectator.Get();
	if (!C)
	{
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		C = Jet->GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Sp);
		Spectator = C;
	}
	C->SetActorLocationAndRotation(ToUE(FromG), FRotationMatrix::MakeFromXZ(DirToUE((AtG - FromG).GetSafeNormal()), FVector(0, 0, 1)).ToQuat());
	PC->SetViewTarget(C);
	SpectatorUntil = Total + Seconds;
}

FVector2D FOsrStrikeAutotest::Pilot(AOsrJetPawn* Jet, const FVector& HeadingDir, double Gamma, double Speed)
{
	const FF35FlightModel& M = Jet->Model;
	const FVector To = Flat(HeadingDir).GetSafeNormal();
	const FVector Fwd = Flat(M.Velocity).GetSafeNormal();
	const double Err = FMath::Atan2(FVector::CrossProduct(Fwd, To).Y * -1.0, FVector::DotProduct(Fwd, To));
	const double Omega = FMath::Clamp(Err * 1.2, -0.25, 0.25);
	const double BankDes = FMath::Clamp(FMath::Atan(Omega * M.Tas / 9.81), Rad(-75.0), Rad(75.0));
	const double Roll = FMath::Clamp((BankDes - M.Bank) * 2.5 - M.Rates.Z * 0.2, -1.0, 1.0);
	const double Cb = FMath::Cos(M.Bank);
	const double N0 = FMath::Cos(M.PathAngle) * (Cb >= 0.5 ? 1.0 / Cb : 4.0 * Cb);
	const double NNeed = FMath::Cos(M.PathAngle) / FMath::Max(Cb, 0.2);
	const double Ff = NNeed > N0 ? (NNeed - N0) / FMath::Max(7.5 - N0, 0.5) : 0.0;
	const double Pitch = FMath::Clamp(Ff + (Gamma - M.PathAngle) * 3.0 - M.Rates.X * 0.3, -0.7, 1.0);
	Jet->AutopilotLever = FMath::Clamp(Jet->AutopilotLever + (Speed - M.Tas) * 0.0015, 0.3, 1.6);
	return FVector2D(Pitch, Roll);
}

FVector2D FOsrStrikeAutotest::CanyonPilot(AOsrJetPawn* Jet)
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
	const FVector Look = Path[FMath::Min(PathI + 3, Path.Num() - 1)];
	double FloorH = 0.0;
	for (int32 K = 0; K < 8; ++K)
	{
		const FVector Q = Pos + M.Velocity * (K * 0.2);
		FloorH = FMath::Max(FloorH, Ground(Q.X, Q.Z));
	}
	const double GammaDes = FMath::Clamp((FloorH + 70.0 - Pos.Y) * 0.012, -0.12, 0.3);
	return Pilot(Jet, Look - Pos, GammaDes, 215.0);
}

void FOsrStrikeAutotest::Report(const TCHAR* Seg, TArray<float>& F) const
{
	TArray<float> S = F;
	S.Sort();
	const int32 N = S.Num();
	if (N == 0) { UE_LOG(LogTemp, Display, TEXT("REPORT perf seg=%s frames=0"), Seg); return; }
	double Sum = 0.0;
	for (float X : S) { Sum += X; }
	double Low1 = 0.0;
	const int32 K = FMath::Max(int32(N * 0.01), 1);
	for (int32 J = 0; J < K; ++J) { Low1 += S[N - 1 - J]; }
	Low1 /= K;
	UE_LOG(LogTemp, Display, TEXT("REPORT perf seg=%s frames=%d avg_fps=%.1f low1_fps=%.1f min_fps=%.1f p50_ms=%.1f"),
		Seg, N, N / FMath::Max(Sum, 0.001), 1.0 / FMath::Max(Low1, 0.0001), 1.0 / FMath::Max<double>(S[N - 1], 0.0001), S[N / 2] * 1000.0);
}

void FOsrStrikeAutotest::Step(AOsrMissionState* Ms, double Dt)
{
	APlayerController* PC = Ms->GetWorld()->GetFirstPlayerController();
	AOsrJetPawn* Jet = PC ? Cast<AOsrJetPawn>(PC->GetPawn()) : nullptr;
	if (!Jet) { return; }
	UOsrWeaponsComponent* W = UOsrWeaponsComponent::Of(Jet);
	AOsrMissionTarget* Tg = AOsrMissionTarget::Find(Ms);
	if (!W || !Tg) { return; }
	Jet->bAutopilot = true;
	FOsrControlInput& Ap = Jet->AutopilotInput;
	T += Dt;
	Total += Dt;
	Ms->bAutotestInvulnerable = true;
	if (Total > 2.0 && Phase != TEXT("spawn")) { FramesAll.Add(float(Dt)); }
	bool bSam = false;
	for (TActorIterator<AOsrMissile> It(Ms->GetWorld()); It; ++It) { if (It->bAlive) { bSam = true; break; } }
	if (bSam) { FramesSam.Add(float(Dt)); }
	if (Spectator.IsValid() && SpectatorUntil >= 0.0 && Total > SpectatorUntil)
	{
		SpectatorUntil = -1.0;
		PC->SetViewTarget(Jet);
	}
	const FVector Vent = Tg->VentCentre;
	const FVector Aim = bMiss ? Vent + FVector(40.0, 0.0, 0.0) : Vent;
	FVector2D Pr(0, 0);
	const FVector Pos = Jet->Model.Position;
	const double DistVent = Hdist(Pos, Vent);
	const double Ytop = Vent.Y + 1300.0;

	if (Phase == TEXT("spawn"))
	{
		Ap.Throttle = 0.85;
		if (T > 0.5)
		{
			FString Mode;
			FParse::Value(FCommandLine::Get(), TEXT("autotest="), Mode);
			bMiss = Mode.Contains(TEXT("miss"));
			LoadPath();
			// ~11 km before the canyon end
			int32 K = FMath::Max(0, Path.Num() - 56);
			PathI = K;
			FVector P = Path[K];
			P.Y = Ground(P.X, P.Z) + 80.0;
			const FVector D = Flat(Path[FMath::Min(K + 2, Path.Num() - 1)] - Path[K]).GetSafeNormal();
			Jet->RespawnAt(P, AOsrJetPawn::LookingAt(D, FVector(0, 1, 0)), 215.0);
			Jet->AutopilotLever = 0.9;
			W->Selected = EOsrWeapon::Lgb;
			UE_LOG(LogTemp, Display, TEXT("REPORT strike start mode=%s path_i=%d vent=(%.0f, %.1f, %.0f) dist_km=%.1f"), bMiss ? TEXT("miss") : TEXT("hit"), K, Vent.X, Vent.Y, Vent.Z, Hdist(P, Vent) / 1000.0);
			Phase = TEXT("canyon");
			T = 0.0;
		}
		return;
	}
	if (Phase == TEXT("canyon"))
	{
		Pr = CanyonPilot(Jet);
		if (T > 4.0) { Shot(TEXT("strike_canyon")); }
		if (DistVent < 9500.0 || PathI >= Path.Num() - 3)
		{
			UE_LOG(LogTemp, Display, TEXT("REPORT strike popup dist_km=%.1f t=%.1f detections=%d"), DistVent / 1000.0, Total, Ms->Stats.Detections);
			Phase = TEXT("popup");
			T = 0.0;
		}
	}
	else if (Phase == TEXT("popup") || Phase == TEXT("attack"))
	{
		FramesTarget.Add(float(Dt));
		const double Gamma = Pos.Y < Ytop - 120.0 ? Rad(22.0) : FMath::Clamp((Ytop - Pos.Y) * 0.004, -0.1, 0.1);
		Pr = Pilot(Jet, Vent - Pos, Gamma, 235.0);
		if (Phase == TEXT("popup") && Pos.Y > Ytop - 400.0)
		{
			W->DesignatePoint(Aim, true);
			if (!W->bLaserArm) { W->CmdLaserArm(); }
			if (!W->bTgpView) { W->CmdToggleTgp(); }
			while (W->ZoomIdx != 2) { W->CmdZoom(); }
			Phase = TEXT("attack");
			T = 0.0;
		}
		if (Phase == TEXT("attack"))
		{
			if (T > 2.0) { Shot(TEXT("tgp_view")); }
			if (W->bInRange && T > 2.5)
			{
				if (W->CmdReleaseBomb())
				{
					bReleased = true;
					Shot(TEXT("tgp_release"), false);
					Phase = TEXT("guide");
					T = 0.0;
				}
			}
			if (DistVent < 1500.0)
			{
				UE_LOG(LogTemp, Display, TEXT("REPORT strike no_release (never in range) pred_miss=%.1f"), W->PredMiss);
				Phase = TEXT("egress");
				T = 0.0;
			}
		}
	}
	else if (Phase == TEXT("guide"))
	{
		FramesTarget.Add(float(Dt));
		// offset turn keeps the pod unmasked (target on the low wing side) while the bomb flies
		const FVector ToV = Flat(Vent - Pos).GetSafeNormal();
		const FVector Off = FQuat(FVector(0, 1, 0), TurnSign * Rad(55.0)).RotateVector(ToV);
		Pr = Pilot(Jet, Off, FMath::Clamp((Ytop - Pos.Y) * 0.004, -0.1, 0.1), 235.0);
		const TArray<AOsrLaserBomb*> Bombs = AOsrLaserBomb::InFlight(Ms->GetWorld(), Jet);
		if (Bombs.Num() > 0 && T > 1.0 && T < 1.2) { UE_LOG(LogTemp, Display, TEXT("REPORT strike guiding tti=%.1f lasing=%d masked=%d"), Bombs[0]->Tti, W->bLasing ? 1 : 0, W->bMasked ? 1 : 0); }
		if (Bombs.Num() == 0 && !bImpact && T > 0.5)
		{
			bImpact = true;
			ImpactT = Total;
			UE_LOG(LogTemp, Display, TEXT("REPORT strike_result %s target_destroyed=%d best_miss_m=%.1f t=%.1f"), Ms->bTargetDestroyed ? TEXT("HIT") : TEXT("MISS"), Ms->bTargetDestroyed ? 1 : 0, Ms->Stats.BestMissM, Total);
		}
		if (bImpact)
		{
			if (Total - ImpactT > 0.25) { Shot(TEXT("tgp_impact")); }
			if (Total - ImpactT > 1.5 && W->bTgpView) { W->CmdToggleTgp(); }
			if (Total - ImpactT > 1.6 && !Shots.Contains(TEXT("shaft_secondaries")))
			{
				if (SpectatorUntil < 0.0) { Spectate(Jet, Vent + FVector(-420.0, 160.0, 260.0), Vent + FVector(0, 30.0, 0), 5.0); }
				if (Total - ImpactT > 4.2) { Shot(TEXT("shaft_secondaries")); }
			}
			if (Total - ImpactT > 6.5)
			{
				Phase = TEXT("egress");
				T = 0.0;
			}
		}
		if (T > 60.0) { UE_LOG(LogTemp, Display, TEXT("REPORT strike bomb_timeout")); Phase = TEXT("egress"); T = 0.0; }
	}
	else if (Phase == TEXT("egress"))
	{
		if (T < 10.0) { FramesTarget.Add(float(Dt)); }
		double FloorH = 0.0;
		for (int32 K = 0; K < 10; ++K)
		{
			const FVector Q = Pos + Jet->Model.Velocity * (K * 0.3);
			FloorH = FMath::Max(FloorH, Ground(Q.X, Q.Z));
		}
		const FVector Home = OsrWorld::Mouth - Pos;
		const double Gamma = FMath::Clamp((FloorH + 180.0 - Pos.Y) * 0.006, -0.25, 0.3);
		Pr = Pilot(Jet, Home, Gamma, 290.0);
		// countermeasures while anything homes on us
		CmAcc += Dt;
		if ((W->MissilesInbound > 0 || W->IrInbound > 0) && CmAcc > 0.9)
		{
			CmAcc = 0.0;
			W->CmdFlare();
			W->CmdChaff();
			if (!Shots.Contains(TEXT("flares")) && SpectatorUntil < 0.0)
			{
				Ap.bLookBack = true;
			}
		}
		if (Ap.bLookBack && CmAcc > 0.45) { Shot(TEXT("flares")); }
		if (Ap.bLookBack && CmAcc > 0.7) { Ap.bLookBack = false; }
		// a SAM in flight: side view of it once
		if (!Shots.Contains(TEXT("sam_launch")) && SpectatorUntil < 0.0)
		{
			for (TActorIterator<AOsrMissile> It(Ms->GetWorld()); It; ++It)
			{
				if (It->bAlive && !It->bIr() && It->Age > 1.5 && It->Age < 6.0)
				{
					const FVector Vh = It->V.GetSafeNormal();
					const FVector Side = FVector::CrossProduct(Vh, FVector(0, 1, 0)).GetSafeNormal();
					Spectate(Jet, It->P - Vh * 90.0 + Side * 70.0 + FVector(0, 25.0, 0), It->P + Vh * 60.0, 1.0);
					Shots.Add(TEXT("sam_pending"));
					break;
				}
			}
		}
		if (Shots.Contains(TEXT("sam_pending")) && SpectatorUntil > 0.0 && SpectatorUntil - Total < 0.5) { Shot(TEXT("sam_launch")); }
		// the fighters: chase view of one
		if (!Shots.Contains(TEXT("fighters")) && SpectatorUntil < 0.0)
		{
			for (TActorIterator<AOsrEnemyFighter> It(Ms->GetWorld()); It; ++It)
			{
				if (!It->bDead && (It->State == EOsrFighterState::Attack || It->State == EOsrFighterState::Intercept || It->State == EOsrFighterState::Climb))
				{
					const FVector Vh = It->V.GetSafeNormal();
					const FVector Side = FVector::CrossProduct(Vh, FVector(0, 1, 0)).GetSafeNormal();
					Spectate(Jet, It->P - Vh * 38.0 + Side * 14.0 + It->Up * 7.0, It->P + Vh * 20.0, 1.2);
					Shots.Add(TEXT("fighters_pending"));
					UE_LOG(LogTemp, Display, TEXT("REPORT fighter %s state=%s speed_ms=%.0f agl_m=%.0f dist_to_player_km=%.1f"), *It->Callsign, AOsrEnemyFighter::StateName(It->State), It->V.Size(), It->P.Y - Ground(It->P.X, It->P.Z), FVector::Dist(It->P, Pos) / 1000.0);
					break;
				}
			}
		}
		if (Shots.Contains(TEXT("fighters_pending")) && SpectatorUntil > 0.0 && SpectatorUntil - Total < 0.6) { Shot(TEXT("fighters")); }
		if (T > 70.0 || Pos.X < OsrWorld::Mouth.X)
		{
			Report(TEXT("all"), FramesAll);
			Report(TEXT("target"), FramesTarget);
			Report(TEXT("sam"), FramesSam);
			const FOsrMissionStats& S = Ms->Stats;
			UE_LOG(LogTemp, Display, TEXT("REPORT strike stats detections=%d sams=%d evaded=%d aams=%d bombs=%d on_target=%d flares=%d chaff=%d would_be_kills=%d phase=%s"),
				S.Detections, S.SamsLaunched, S.SamsEvaded, S.AamsLaunched, S.BombsReleased, S.BombsOnTarget, S.FlaresUsed, S.ChaffUsed, Ms->WouldBeKills, AOsrMissionState::PhaseName(Ms->Phase));
			UE_LOG(LogTemp, Display, TEXT("REPORT AUTOTEST DONE"));
			Phase = TEXT("done");
			T = 0.0;
		}
	}
	else if (Phase == TEXT("done"))
	{
		if (T > 1.0) { UKismetSystemLibrary::QuitGame(Ms, nullptr, EQuitPreference::Quit, false); }
		return;
	}
	Ap.Pitch = Pr.X;
	Ap.Roll = Pr.Y;
	Ap.Throttle = FMath::Min(Jet->AutopilotLever, 1.0);
	Ap.Afterburner = FMath::Clamp(Jet->AutopilotLever - 1.0, 0.0, 1.0);
	if (Total > 260.0 && Phase != TEXT("done"))
	{
		UE_LOG(LogTemp, Display, TEXT("REPORT AUTOTEST TIMEOUT phase=%s"), *Phase);
		Report(TEXT("all"), FramesAll);
		Phase = TEXT("done");
		T = 0.0;
	}
}
