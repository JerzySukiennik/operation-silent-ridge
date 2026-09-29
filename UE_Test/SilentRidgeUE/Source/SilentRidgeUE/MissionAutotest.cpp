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
#include "GameFramework/HUD.h"

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
	if (PC->MyHUD) { PC->MyHUD->bShowHUD = false; }     // clean external shot
	SpectatorUntil = Total + Seconds;
	SpecPos = FromG;
	bSpecFixed = false;
	SpecFollow.Reset();
}

void FOsrStrikeAutotest::UpdateSpectator(AOsrJetPawn* Jet)
{
	ACameraActor* C = Spectator.Get();
	AActor* F = SpecFollow.Get();
	if (!C || !F || SpectatorUntil < 0.0) { return; }
	FVector P, V, Up(0, 1, 0);
	if (const AOsrMissile* M = Cast<AOsrMissile>(F)) { P = M->P; V = M->V; if (!M->bAlive) { SpecFollow.Reset(); return; } }
	else if (const AOsrEnemyFighter* E = Cast<AOsrEnemyFighter>(F)) { P = E->P; V = E->V; Up = E->Up; }
	else { return; }
	if (bSpecFixed)
	{
		C->SetActorLocationAndRotation(ToUE(SpecPos), FRotationMatrix::MakeFromXZ(DirToUE((P - SpecPos).GetSafeNormal()), FVector(0, 0, 1)).ToQuat());
		return;
	}
	const FVector Vh = V.GetSafeNormal();
	const FVector Side = FVector::CrossProduct(Vh, FVector(0, 1, 0)).GetSafeNormal();
	const bool bMissile = F->IsA<AOsrMissile>();
	const FVector From = bMissile ? P - Vh * 45.0 + Side * 22.0 + FVector(0, 6.0, 0) : P - Vh * 34.0 + Side * 12.0 + Up * 6.0;
	const FVector At = P + Vh * (bMissile ? 30.0 : 12.0);
	C->SetActorLocationAndRotation(ToUE(From), FRotationMatrix::MakeFromXZ(DirToUE((At - From).GetSafeNormal()), FVector(0, 0, 1)).ToQuat());
}

FVector2D FOsrStrikeAutotest::CanyonPilotReverse(AOsrJetPawn* Jet, double Agl, double Speed)
{
	// egress: fly the canyon path backwards (towards the sea), terrain following at Agl
	const FF35FlightModel& M = Jet->Model;
	const FVector Pos = M.Position;
	int32 Best = PathI;
	double BestD = TNumericLimits<double>::Max();
	for (int32 K = FMath::Max(0, PathI - 12); K <= FMath::Min(PathI + 12, Path.Num() - 1); ++K)
	{
		const double D = FVector2D(Path[K].X - Pos.X, Path[K].Z - Pos.Z).Size();
		if (D < BestD) { BestD = D; Best = K; }
	}
	PathI = Best;
	const FVector Look = Path[FMath::Max(PathI - 3, 0)];
	// anticipating terrain following: the flight path angle needed to clear every point of the next 7 s (straight line)
	const FVector Hv = Flat(M.Velocity);
	const double Hs = FMath::Max(Hv.Size(), 50.0);
	double GammaDes = -0.12;
	for (int32 K = 1; K <= 28; ++K)
	{
		const double Tk = K * 0.25;
		const FVector Q = Pos + Hv * Tk;
		const double Need = FMath::Atan2(Ground(Q.X, Q.Z) + Agl - Pos.Y, Hs * Tk);
		GammaDes = FMath::Max(GammaDes, Need);
	}
	GammaDes = FMath::Clamp(GammaDes, -0.2, 0.6);
	return Pilot(Jet, Look - Pos, GammaDes, Speed);
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
		SpecFollow.Reset();
		PC->SetViewTarget(Jet);
		if (PC->MyHUD) { PC->MyHUD->bShowHUD = true; }
	}
	UpdateSpectator(Jet);
	// countermeasures whenever something homes on us (all phases after the pop-up)
	CmAcc += Dt;
	if (Phase != TEXT("canyon") && (W->MissilesInbound > 0 || W->IrInbound > 0) && W->NearestMissileDist < 7000.0 && CmAcc > 1.6)
	{
		CmAcc = 0.0;
		if (W->IrInbound > 0) { W->CmdFlare(); }
		if (W->MissilesInbound > 0) { W->CmdChaff(); }
		if (W->IrInbound > 0 || W->Flares > 20) { bFlarePending = true; }
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
			AOsrMissionState::MakeAirborne(Jet);
			Jet->bGearDown = false;
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
		if (Phase == TEXT("popup") && T > 0.5 && !Shots.Contains(TEXT("vent_pending")))
		{
			Spectate(Jet, Vent + FVector(-190.0, 70.0, 150.0), Vent + FVector(0, -4.0, 0), 1.3);
			Shots.Add(TEXT("vent_pending"));
		}
		if (Shots.Contains(TEXT("vent_pending")) && SpectatorUntil > 0.0 && SpectatorUntil - Total < 0.5) { Shot(TEXT("vent_closeup")); }
		if (Phase == TEXT("popup") && T > 2.5 && !Shots.Contains(TEXT("plant_pending")) && SpectatorUntil < 0.0)
		{
			const FVector Pc(OsrLayout::PlantCentre.X, Ground(OsrLayout::PlantCentre.X, OsrLayout::PlantCentre.Y), OsrLayout::PlantCentre.Y);
			Spectate(Jet, Pc + FVector(-420.0, 190.0, 380.0), Pc + FVector(80.0, 0.0, -150.0), 1.3);
			Shots.Add(TEXT("plant_pending"));
		}
		if (Shots.Contains(TEXT("plant_pending")) && SpectatorUntil > 0.0 && SpectatorUntil - Total < 0.5) { Shot(TEXT("plant_overview")); }
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
			if (W->bInRange && T > 2.5 && FMath::Abs(Jet->Model.PitchDeg()) < 6.0)
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
		// beam run: turn perpendicular to the line of sight once, then wings level — the EOTS under the nose keeps the
		// target below the wing plane (a banked wheel around it would put it above the wing and mask the pod)
		const FVector Radial = Flat(Pos - Vent).GetSafeNormal();
		if (GuideDir.IsZero())
		{
			GuideDir = FVector::CrossProduct(FVector(0, 1, 0), Radial);
			if (FVector::DotProduct(GuideDir, Jet->Model.Velocity) < 0.0) { GuideDir = -GuideDir; }
		}
		const double Rr = Hdist(Pos, Vent);
		Pr = Pilot(Jet, GuideDir, FMath::Clamp((Ytop - Pos.Y) * 0.004, -0.1, 0.1), 235.0);
		const TArray<AOsrLaserBomb*> Bombs = AOsrLaserBomb::InFlight(Ms->GetWorld(), Jet);
		LogAcc += Dt;
		if (Bombs.Num() > 0 && LogAcc > 2.0)
		{
			LogAcc = 0.0;
			const AOsrLaserBomb* B = Bombs[0];
			UE_LOG(LogTemp, Display, TEXT("REPORT strike guiding t=%.1f tti=%.1f bomb_seeking=%d off_axis_deg=%.1f bomb_los=%d lasing=%d masked=%d range_km=%.1f"),
				T, B->Tti, B->bSeeking ? 1 : 0, FMath::RadiansToDegrees(AngleBetween(B->V, Aim - B->P)), LineOfSight(B->P, Aim + FVector(0, 1, 0), 0.3) ? 1 : 0,
				W->bLasing ? 1 : 0, W->bMasked ? 1 : 0, Rr / 1000.0);
		}
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
		if (T < 0.1)
		{
			// rejoin the canyon path near us, then fly it backwards
			double BestD = 1e12;
			for (int32 K = 0; K < Path.Num(); ++K) { const double D = Hdist(Path[K], Pos); if (D < BestD) { BestD = D; PathI = K; } }
		}
		Pr = CanyonPilotReverse(Jet, 150.0, 270.0);
		if (bFlarePending && !Shots.Contains(TEXT("flares")) && SpectatorUntil < 0.0 && LookBackT < 0.0)
		{
			W->CmdFlare();
			LookBackT = 0.0;
		}
		if (LookBackT >= 0.0)
		{
			LookBackT += Dt;
			Ap.bLookBack = true;
			if (LookBackT > 0.6 && SpectatorUntil < 0.0) { Shot(TEXT("flares")); }
			if (LookBackT > 0.9) { Ap.bLookBack = false; LookBackT = -99.0; }
		}
		// a SAM in flight: side view of it once
		if (!Shots.Contains(TEXT("sam_launch")) && SpectatorUntil < 0.0)
		{
			for (TActorIterator<AOsrMissile> It(Ms->GetWorld()); It; ++It)
			{
				if (It->bAlive && It->Kind == EOsrMissileKind::Pantsir_57E6 && It->Age > 0.1 && It->Age < 0.4 && LookBackT < 0.0)
				{
					// side-on from 350 m, level with the launcher: the booster plume and the smoke trail climbing away
					const FVector Vh = It->V.GetSafeNormal();
					FVector Side = FVector::CrossProduct(Flat(Vh).GetSafeNormal(), FVector(0, 1, 0)).GetSafeNormal();
					FVector Cam = It->P - Side * 350.0 - Flat(Vh).GetSafeNormal() * 120.0;
					Cam.Y = FMath::Max(It->P.Y, Ground(Cam.X, Cam.Z) + 15.0);
					Spectate(Jet, Cam, It->P, 2.2);
					SpecFollow = *It;
					bSpecFixed = true;
					Shots.Add(TEXT("sam_pending"));
					break;
				}
			}
		}
		if (Shots.Contains(TEXT("sam_pending")) && SpectatorUntil > 0.0 && SpectatorUntil - Total < 0.9) { Shot(TEXT("sam_launch")); }
		// the fighters: chase view of one
		if (!Shots.Contains(TEXT("fighters")) && SpectatorUntil < 0.0 && LookBackT < 0.0)
		{
			for (TActorIterator<AOsrEnemyFighter> It(Ms->GetWorld()); It; ++It)
			{
				if (!It->bDead && (It->State == EOsrFighterState::Attack || It->State == EOsrFighterState::Intercept || It->State == EOsrFighterState::Climb))
				{
					const FVector Vh = It->V.GetSafeNormal();
					const FVector Side = FVector::CrossProduct(Vh, FVector(0, 1, 0)).GetSafeNormal();
					Spectate(Jet, It->P - Vh * 34.0 + Side * 12.0 + It->Up * 6.0, It->P + Vh * 12.0, 1.2);
					SpecFollow = *It;
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
