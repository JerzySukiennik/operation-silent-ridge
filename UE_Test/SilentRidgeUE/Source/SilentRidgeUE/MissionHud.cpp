// Mission HUD drawing (canvas, same green HMD style as OsrHud).
#include "MissionHud.h"
#include "MissionState.h"
#include "MissionCommon.h"
#include "MissionTarget.h"
#include "WeaponsComponent.h"
#include "WeaponBomb.h"
#include "EnemyFighter.h"
#include "OsrJetPawn.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"

using namespace OsrMission;
using namespace OsrMath;

namespace
{
	const FLinearColor GREEN = FLinearColor(FColor(92, 255, 133, 242));
	const FLinearColor GREEN_DIM = FLinearColor(FColor(92, 255, 133, 140));
	const FLinearColor SHADOW = FLinearColor(FColor(0, 18, 5, 128));
	const FLinearColor WARN = FLinearColor(FColor(255, 219, 77, 255));
	const FLinearColor RED = FLinearColor(FColor(255, 80, 60, 255));
	const FLinearColor WHITE = FLinearColor(FColor(235, 240, 235, 255));
	const FLinearColor TGPW = FLinearColor(FColor(230, 235, 230, 235));
	enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

	struct FCtx
	{
		UCanvas* C = nullptr;
		UFont* Font = nullptr;
		double S = 1.0;
		FVector2D Size, Centre;
		void Line(const FVector2D& A, const FVector2D& B, double W, const FLinearColor& Col) const
		{
			FCanvasLineItem Sh(A, B); Sh.LineThickness = float((W + 2.2) * S); Sh.SetColor(SHADOW); C->DrawItem(Sh);
			FCanvasLineItem It(A, B); It.LineThickness = float(W * S); It.SetColor(Col); C->DrawItem(It);
		}
		void Poly(const TArray<FVector2D>& P, double W, const FLinearColor& Col) const { for (int32 I = 0; I + 1 < P.Num(); ++I) { Line(P[I], P[I + 1], W, Col); } }
		void Circle(const FVector2D& Ctr, double R, double W, const FLinearColor& Col, int32 N = 32) const
		{
			TArray<FVector2D> P;
			for (int32 I = 0; I <= N; ++I) { const double A = UE_DOUBLE_TWO_PI * I / N; P.Add(Ctr + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R); }
			Poly(P, W, Col);
		}
		void Box(const FVector2D& Ctr, const FVector2D& Half, double W, const FLinearColor& Col) const
		{
			Poly({Ctr + FVector2D(-Half.X, -Half.Y), Ctr + FVector2D(Half.X, -Half.Y), Ctr + Half, Ctr + FVector2D(-Half.X, Half.Y), Ctr + FVector2D(-Half.X, -Half.Y)}, W, Col);
		}
		void Diamond(const FVector2D& Ctr, double R, double W, const FLinearColor& Col) const
		{
			Poly({Ctr + FVector2D(0, -R), Ctr + FVector2D(R, 0), Ctr + FVector2D(0, R), Ctr + FVector2D(-R, 0), Ctr + FVector2D(0, -R)}, W, Col);
		}
		void Fill(const FVector2D& Tl, const FVector2D& Sz, const FLinearColor& Col) const
		{
			FCanvasTileItem T(Tl, Sz, Col); T.BlendMode = SE_BLEND_Translucent; C->DrawItem(T);
		}
		void Text(const FVector2D& Pos, const FString& Txt, double Px, int32 Align, const FLinearColor& Col) const
		{
			if (!Font) { return; }
			const double FontPx = Font->GetMaxCharHeight();
			const float Sc = float(Px * S / FMath::Max(FontPx, 1.0) * 1.12);
			float W = 0.f, H = 0.f;
			C->TextSize(Font, Txt, W, H, Sc, Sc);
			FVector2D P = Pos;
			if (Align == AL_CENTER) { P.X -= W * 0.5; } else if (Align == AL_RIGHT) { P.X -= W; }
			P.Y -= H * 0.5;
			FCanvasTextItem It(P, FText::FromString(Txt), Font, Col);
			It.Scale = FVector2D(Sc, Sc);
			It.bOutlined = true;
			It.OutlineColor = SHADOW;
			C->DrawItem(It);
		}
		/** Perspective projection of a Godot-frame point for a camera (UE rotation, vertical FOV in degrees). */
		bool Project(const FVector& PG, const FVector& CamG, const FQuat& CamRotUE, double FovV, FVector2D& Out) const
		{
			const FVector L = CamRotUE.UnrotateVector(DirToUE(PG - CamG));
			if (L.X <= 1e-3) { return false; }
			const double F = (Size.Y * 0.5) / FMath::Tan(Rad(FovV) * 0.5);
			Out = FVector2D(Centre.X + L.Y / L.X * F, Centre.Y - L.Z / L.X * F);
			return true;
		}
	};

	FCtx MakeCtx(UCanvas* Canvas)
	{
		FCtx X;
		X.C = Canvas;
		X.Font = GEngine ? GEngine->GetLargeFont() : nullptr;
		X.Size = FVector2D(Canvas->ClipX, Canvas->ClipY);
		X.S = X.Size.Y / 1080.0;
		X.Centre = X.Size * 0.5;
		return X;
	}

	bool Blink(double Period = 0.5, double On = 0.32) { return FMath::Fmod(FPlatformTime::Seconds(), Period) < On; }

	FString Mmss(double T) { const int32 S = FMath::Max(0, FMath::RoundToInt(T)); return FString::Printf(TEXT("%d:%02d"), S / 60, S % 60); }

	FString Thousands(int64 V)
	{
		FString Str = FString::Printf(TEXT("%lld"), FMath::Abs(V)), Out;
		while (Str.Len() > 3) { Out = TEXT(",") + Str.Right(3) + Out; Str = Str.LeftChop(3); }
		return (V < 0 ? TEXT("-") : TEXT("")) + Str + Out;
	}
}

bool FOsrMissionHud::TgpFullscreen(const AOsrJetPawn* Jet)
{
	const UOsrWeaponsComponent* W = UOsrWeaponsComponent::Of(Jet);
	return W && W->bTgpView && Jet->bAlive;
}

// ---------------------------------------------------------------- overlay (any pawn)

void FOsrMissionHud::DrawOverlay(UCanvas* Canvas, APawn* Pawn)
{
	if (!Canvas || !Pawn) { return; }
	const AOsrMissionState* Ms = AOsrMissionState::Get(Pawn);
	if (!Ms) { return; }
	const FCtx X = MakeCtx(Canvas);
	const double S = X.S;
	// objective line (top left)
	X.Text(FVector2D(28, 36) * S, Ms->ObjectiveText(Pawn), 17, AL_LEFT, Ms->Phase == EOsrMissionPhase::Fail ? WARN : GREEN);
	X.Text(FVector2D(28, 60) * S, FString::Printf(TEXT("T+ %s"), *Mmss(Ms->Stats.LaunchTime >= 0.0 ? Ms->MissionTime - Ms->Stats.LaunchTime : 0.0)), 14, AL_LEFT, GREEN_DIM);
	// radio subtitles (bottom centre)
	double Y = X.Size.Y / S - 118.0;
	int32 Shown = 0;
	for (int32 I = Ms->RadioLines.Num() - 1; I >= 0 && Shown < 2; --I)
	{
		const FOsrRadioLine& L = Ms->RadioLines[I];
		if (Ms->MissionTime - L.Time > L.Duration) { continue; }
		X.Text(FVector2D(X.Centre.X, Y * S), L.Who + TEXT(": ") + L.Text, 19, AL_CENTER, WHITE);
		Y -= 28.0;
		++Shown;
	}
	// briefing card
	if (!Ms->bBriefingDismissed && Ms->Phase != EOsrMissionPhase::Success)
	{
		TArray<FString> Lines;
		Ms->BriefingText().ParseIntoArrayLines(Lines);
		const FVector2D Tl = FVector2D(X.Centre.X / S - 560, 230) * S;
		const FVector2D Sz = FVector2D(1120, 70 + Lines.Num() * 30) * S;
		X.Fill(Tl, Sz, FLinearColor(0.0f, 0.04f, 0.02f, 0.72f));
		X.Box(Tl + Sz * 0.5, Sz * 0.5, 1.4, GREEN);
		double Ly = 32.0;
		for (int32 I = 0; I < Lines.Num(); ++I)
		{
			X.Text(Tl + FVector2D(24, Ly) * S, Lines[I], I == 0 ? 21 : 16, AL_LEFT, I == 0 ? GREEN : WHITE);
			Ly += 30.0;
		}
		X.Text(Tl + FVector2D(Sz.X / S - 24, Sz.Y / S - 22) * S, TEXT("A / CROSS / ENTER: close"), 14, AL_RIGHT, GREEN_DIM);
	}
	// results
	if (Ms->Phase == EOsrMissionPhase::Success || (Ms->Phase == EOsrMissionPhase::Fail && Ms->PhaseTime < 3.5))
	{
		const bool bOk = Ms->Phase == EOsrMissionPhase::Success;
		X.Fill(FVector2D::ZeroVector, X.Size, FLinearColor(0.0f, 0.0f, 0.0f, bOk ? 0.55f : 0.35f));
		X.Text(X.Centre + FVector2D(0, -300) * S, bOk ? TEXT("MISSION SUCCESS") : TEXT("SHOT DOWN / MISSION FAILED"), 46, AL_CENTER, bOk ? GREEN : WARN);
		if (!bOk) { X.Text(X.Centre + FVector2D(0, -240) * S, Ms->FailReason + TEXT(" — back to the carrier, the sortie continues"), 20, AL_CENTER, WHITE); }
		const FOsrMissionStats& St = Ms->Stats;
		const double Dur = (St.EndTime > 0.0 ? St.EndTime : Ms->MissionTime) - FMath::Max(St.LaunchTime, 0.0);
		const FString Rows[][2] = {
			{TEXT("Mission time (launch to trap)"), Mmss(Dur)},
			{TEXT("Radar detections"), FString::FromInt(St.Detections)},
			{TEXT("SAMs launched / evaded"), FString::Printf(TEXT("%d / %d"), St.SamsLaunched, St.SamsEvaded)},
			{TEXT("Enemy air-to-air missiles"), FString::FromInt(St.AamsLaunched)},
			{TEXT("Bombs released / on the shaft"), FString::Printf(TEXT("%d / %d"), St.BombsReleased, St.BombsOnTarget)},
			{TEXT("Best miss distance"), St.BestMissM >= 0.0 ? FString::Printf(TEXT("%.1f m"), St.BestMissM) : FString(TEXT("-"))},
			{TEXT("25 mm rounds fired"), FString::FromInt(St.RoundsFired)},
			{TEXT("Flares / chaff"), FString::Printf(TEXT("%d / %d"), St.FlaresUsed, St.ChaffUsed)},
			{TEXT("Kills air / ground"), FString::Printf(TEXT("%d / %d"), St.AirKills, St.GroundKills)},
			{TEXT("Jets lost"), FString::FromInt(St.Deaths)},
			{TEXT("LSO grade / wire"), St.LsoGrade.IsEmpty() ? FString(TEXT("-")) : FString::Printf(TEXT("%s / %d-wire%s"), *St.LsoGrade, St.Wire, St.Bolters > 0 ? *FString::Printf(TEXT("  (%d bolter%s)"), St.Bolters, St.Bolters > 1 ? TEXT("s") : TEXT("")) : TEXT(""))},
		};
		double Ry = -180.0;
		for (const auto& R : Rows)
		{
			X.Text(X.Centre + FVector2D(-40, Ry) * S, R[0], 21, AL_RIGHT, GREEN_DIM);
			X.Text(X.Centre + FVector2D(0, Ry) * S, R[1], 21, AL_LEFT, WHITE);
			Ry += 36.0;
		}
		if (bOk && Ms->PhaseTime > 4.0) { X.Text(X.Centre + FVector2D(0, Ry + 30) * S, TEXT("A / CROSS / ENTER: fly the mission again"), 18, AL_CENTER, GREEN); }
	}
}

// ---------------------------------------------------------------- jet pages

void FOsrMissionHud::DrawJet(UCanvas* Canvas, const AOsrJetPawn* Jet)
{
	const UOsrWeaponsComponent* W = UOsrWeaponsComponent::Of(Jet);
	if (!Canvas || !Jet || !W || !Jet->bAlive) { return; }
	const FCtx X = MakeCtx(Canvas);
	const double S = X.S;
	const FVector Me = Jet->Model.Position;
	const bool bTgp = W->bTgpView;
	const FLinearColor& Pri = bTgp ? TGPW : GREEN;
	const double Tti = W->BombTti();

	if (bTgp)
	{
		// ---- targeting pod page (camera looks at the designated point: the crosshair is the LOS)
		const FVector2D C = X.Centre;
		const double G = 26.0 * S, L = 150.0 * S;
		for (const FVector2D& D : {FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1)}) { X.Line(C + D * G, C + D * L, 1.6, Pri); }
		X.Line(C + FVector2D(-6, 0) * S, C + FVector2D(6, 0) * S, 1.2, Pri);
		const FVector2D Br(X.Size.X * 0.36, X.Size.Y * 0.36);
		for (double Sx : {-1.0, 1.0})
		{
			for (double Sy : {-1.0, 1.0})
			{
				const FVector2D K = C + FVector2D(Br.X * Sx, Br.Y * Sy);
				X.Line(K, K - FVector2D(Sx * 40.0 * S, 0), 2.0, Pri);
				X.Line(K, K - FVector2D(0, Sy * 40.0 * S), 2.0, Pri);
			}
		}
		const TCHAR* FovName[] = {TEXT("WFOV"), TEXT("MFOV"), TEXT("NFOV"), TEXT("NFOV x2.8")};
		X.Text(FVector2D(60, 70) * S, FString::Printf(TEXT("TGP  TV  %s %.1f°"), FovName[FMath::Clamp(W->ZoomIdx, 0, 3)], W->TgpFovDeg()), 20, AL_LEFT, Pri);
		X.Text(FVector2D(X.Centre.X, 70 * S), W->bPointTrack ? FString::Printf(TEXT("POINT  %s"), *W->TrackName) : FString(TEXT("AREA")), 22, AL_CENTER, Pri);
		X.Text(FVector2D(X.Size.X - 60 * S, 70 * S), FString::Printf(TEXT("CODE %d"), W->LaserCode), 20, AL_RIGHT, Pri);
		const double Rng = FVector::Dist(W->PodPos(), W->TgpPoint);
		X.Text(FVector2D(60, 110) * S, FString::Printf(TEXT("RNG %.1f km   ELEV %s ft"), Rng / 1000.0, *Thousands(int64(W->TgpPoint.Y / 0.3048 / 10.0) * 10)), 18, AL_LEFT, Pri);
		const FVector Lb = Jet->RenderRot.UnrotateVector((W->TgpPoint - W->PodPos()).GetSafeNormal());
		X.Text(FVector2D(60, 140) * S, FString::Printf(TEXT("AZ %+04.0f  EL %+03.0f"), Deg(FMath::Atan2(Lb.X, -Lb.Z)), Deg(FMath::Asin(FMath::Clamp(Lb.Y, -1.0, 1.0)))), 18, AL_LEFT, Pri);
		// laser
		const FString Las = W->bLasing ? (W->bAutoLase && !W->bLaserArm ? TEXT("AUTO LASE") : TEXT("LASING")) : (W->bLaserArm ? TEXT("LASER ARM") : TEXT("LASER SAFE"));
		X.Text(FVector2D(X.Size.X - 60 * S, 110 * S), Las, 20, AL_RIGHT, W->bLasing ? WARN : Pri);
		if (W->bLasing && Blink(0.4, 0.22))
		{
			X.Text(C + FVector2D(60, -60) * S, TEXT("L"), 34, AL_CENTER, WARN);
			// the spot itself (visible to the pod camera): a flickering sparkle on the LOS
			const double R = (5.0 + 3.0 * FMath::Sin(FPlatformTime::Seconds() * 40.0)) * S;
			X.Line(C + FVector2D(-R, -R), C + FVector2D(R, R), 2.0, WHITE);
			X.Line(C + FVector2D(-R, R), C + FVector2D(R, -R), 2.0, WHITE);
		}
		if (W->bMasked && Blink()) { X.Text(C + FVector2D(0, 90) * S, TEXT("MASK"), 30, AL_CENTER, WARN); }
		// own bombs in flight inside the pod view
		const FQuat CamQ = FRotationMatrix::MakeFromXZ(DirToUE(W->TgpCamFwd), FVector(0, 0, 1)).ToQuat();
		for (AOsrLaserBomb* B : AOsrLaserBomb::InFlight(Jet->GetWorld(), Jet))
		{
			FVector2D Bp;
			if (X.Project(B->P, W->TgpCamPos, CamQ, W->TgpFovDeg(), Bp))
			{
				X.Circle(Bp, 7.0 * S, 1.6, WARN, 12);
			}
		}
		// weapon line + minimal flight data (fly while looking through the pod)
		FString Wl = FString::Printf(TEXT("LGB %d"), W->Bombs);
		if (Tti >= 0.0) { Wl += FString::Printf(TEXT("   TTI %.0f"), Tti); }
		else if (W->bInRange) { Wl += Blink() ? TEXT("   IN RNG — RELEASE") : TEXT("   IN RNG"); }
		else if (W->TimeToRelease > 0.0) { Wl += FString::Printf(TEXT("   REL %.0f s"), W->TimeToRelease); }
		else if (W->Bombs > 0) { Wl += TEXT("   OUT OF RNG"); }
		X.Text(FVector2D(X.Centre.X, X.Size.Y - 170 * S), Wl, 24, AL_CENTER, W->bInRange && Tti < 0.0 ? WARN : Pri);
		const FOsrTelemetry T = Jet->Telemetry();
		X.Text(FVector2D(60 * S, X.Size.Y - 170 * S), FString::Printf(TEXT("%.0f KCAS  ALT %s  R %s  HDG %03.0f  G %.1f"), T.IasKt, *Thousands(int64(T.AltFt / 10) * 10), *Thousands(int64(FMath::Max(T.RadarAltFt, 0.0))), T.HeadingDeg, T.G), 18, AL_LEFT, Pri);
		if (T.TimeToImpact < 5.0 && Blink()) { X.Text(C + FVector2D(0, -120) * S, TEXT("PULL UP"), 36, AL_CENTER, WARN); }
		X.Text(FVector2D(X.Centre.X, X.Size.Y - 130 * S), TEXT("R-STICK slew   L3 track   R3 zoom   D-UP hold: laser arm   D-UP: exit"), 14, AL_CENTER, GREEN_DIM);
	}
	else
	{
		// ---- target designator box + release cue in the HMD
		if (W->bTgpValid)
		{
			FVector2D Td;
			if (X.Project(W->TgpPoint, FromUE(Jet->CamPosUE), Jet->CamRotUE, Jet->CamFovV, Td) && FMath::Abs(Td.X - X.Centre.X) < X.Size.X * 0.48 && FMath::Abs(Td.Y - X.Centre.Y) < X.Size.Y * 0.48)
			{
				X.Box(Td, FVector2D(11, 11) * S, 1.8, GREEN);
				if (W->bLasing && Blink(0.4, 0.22)) { X.Text(Td + FVector2D(20, -20) * S, TEXT("L"), 18, AL_CENTER, WARN); }
				X.Text(Td + FVector2D(0, 26) * S, FString::Printf(TEXT("%.1f"), FVector::Dist(Me, W->TgpPoint) / 1852.0), 14, AL_CENTER, GREEN_DIM);
			}
			else
			{
				// arrow at the HMD edge towards the designation
				const FVector Lc = Jet->CamRotUE.UnrotateVector(DirToUE(W->TgpPoint - FromUE(Jet->CamPosUE)));
				const FVector2D Dir = FVector2D(Lc.Y, -Lc.Z).GetSafeNormal();
				const FVector2D A = X.Centre + Dir * 260.0 * S;
				X.Line(A, A - Dir * 30.0 * S + FVector2D(-Dir.Y, Dir.X) * 12.0 * S, 1.8, GREEN);
				X.Line(A, A - Dir * 30.0 * S - FVector2D(-Dir.Y, Dir.X) * 12.0 * S, 1.8, GREEN);
				X.Text(A - Dir * 50.0 * S, TEXT("TD"), 14, AL_CENTER, GREEN);
			}
		}
		if (W->Selected == EOsrWeapon::Lgb && W->Bombs > 0 && Tti < 0.0 && W->bTgpValid)
		{
			const FVector2D Cp = X.Centre + FVector2D(0, 190) * S;
			if (W->bInRange) { if (Blink()) { X.Text(Cp, TEXT("IN RNG — RELEASE"), 24, AL_CENTER, WARN); } }
			else if (W->TimeToRelease > 0.0) { X.Text(Cp, FString::Printf(TEXT("REL %.0f"), W->TimeToRelease), 22, AL_CENTER, GREEN); }
			else { X.Text(Cp, TEXT("OUT OF RNG"), 18, AL_CENTER, GREEN_DIM); }
		}
		if (Tti >= 0.0) { X.Text(X.Centre + FVector2D(0, 190) * S, FString::Printf(TEXT("TTI %.0f%s"), Tti, W->bLasing ? TEXT("  LASING") : TEXT("  NO LASER")), 22, AL_CENTER, W->bLasing ? GREEN : WARN); }
		if (W->Selected == EOsrWeapon::Gun)
		{
			// gun cross (boresight + 25 mm drop at 800 m)
			FVector2D Gp;
			const FVector Nose = -BasisZ(Jet->RenderRot);
			const FVector Aim = Me + Nose * 800.0 + FVector(0, -0.5 * 9.81 * FMath::Square(800.0 / 1036.0), 0);
			if (X.Project(Aim, FromUE(Jet->CamPosUE), Jet->CamRotUE, Jet->CamFovV, Gp))
			{
				X.Circle(Gp, 16.0 * S, 1.6, GREEN, 20);
				X.Line(Gp + FVector2D(-4, 0) * S, Gp + FVector2D(4, 0) * S, 1.6, GREEN);
			}
		}
		// hostile aircraft markers (DAS cueing): airborne fighters within 20 km with line of sight
		for (TActorIterator<AOsrEnemyFighter> It(Jet->GetWorld()); It; ++It)
		{
			if (It->bDead || It->State == EOsrFighterState::Parked || It->State == EOsrFighterState::Takeoff) { continue; }
			const double D = FVector::Dist(It->P, Me);
			if (D > 20000.0 || !LineOfSight(Me, It->P, 1.0)) { continue; }
			FVector2D Hp;
			if (X.Project(It->P, FromUE(Jet->CamPosUE), Jet->CamRotUE, Jet->CamFovV, Hp))
			{
				X.Diamond(Hp, 12.0 * S, 1.8, RED);
				X.Text(Hp + FVector2D(0, -24) * S, FString::Printf(TEXT("57  %.1f"), D / 1852.0), 14, AL_CENTER, RED);
			}
		}
	}

	// ---- weapons status (bottom right)
	{
		const FVector2D B(X.Size.X - 250 * S, X.Size.Y - 250 * S);
		const bool bArm = W->bMasterArm();
		X.Text(B, bArm ? TEXT("MASTER ARM") : TEXT("MASTER SAFE"), 16, AL_LEFT, bArm ? Pri : WARN);
		const bool bGun = W->Selected == EOsrWeapon::Gun;
		const FVector2D G1 = B + FVector2D(0, 32) * S, G2 = B + FVector2D(110, 32) * S;
		X.Text(G1, FString::Printf(TEXT("GUN %d"), W->GunRounds), 18, AL_LEFT, bGun ? Pri : GREEN_DIM);
		X.Text(G2, FString::Printf(TEXT("LGB %d"), W->Bombs), 18, AL_LEFT, !bGun ? Pri : GREEN_DIM);
		X.Box((bGun ? G1 : G2) + FVector2D(44, 0) * S, FVector2D(50, 13) * S, 1.4, Pri);
		X.Text(B + FVector2D(0, 62) * S, FString::Printf(TEXT("FLR %d   CHF %d"), W->Flares, W->Chaff), 16, AL_LEFT, (W->Flares < 6 || W->Chaff < 6) ? WARN : GREEN_DIM);
		X.Text(B + FVector2D(0, 90) * S, FString::Printf(TEXT("%s  %d"), W->bLasing ? TEXT("LASE") : (W->bLaserArm ? TEXT("LSR ARM") : TEXT("LSR SAFE")), W->LaserCode), 16, AL_LEFT, W->bLasing ? WARN : GREEN_DIM);
	}

	// ---- RWR scope (bottom left)
	{
		const FVector2D Rc(170 * S, X.Size.Y - 200 * S);
		const double R = 88.0 * S;
		X.Circle(Rc, R, 1.2, GREEN_DIM, 40);
		X.Circle(Rc, R * 0.5, 1.0, GREEN_DIM, 28);
		X.Line(Rc + FVector2D(0, -R - 6 * S), Rc + FVector2D(0, -R + 6 * S), 1.4, GREEN_DIM);
		X.Text(Rc + FVector2D(0, R + 18 * S), TEXT("RWR"), 13, AL_CENTER, GREEN_DIM);
		const double Hdg = FMath::Atan2(Jet->Model.Velocity.X, -Jet->Model.Velocity.Z);
		for (const FOsrRwrContact& Ct : W->Rwr)
		{
			const FVector D = Ct.From - Me;
			const double Brg = FMath::Atan2(D.X, -D.Z) - Hdg;
			const double Rr = R * (1.0 - 0.72 * Ct.Severity);
			const FVector2D P = Rc + FVector2D(FMath::Sin(Brg), -FMath::Cos(Brg)) * Rr;
			const FLinearColor& Col = Ct.State == EOsrRwr::Search ? GREEN : (Ct.State == EOsrRwr::Track ? WARN : RED);
			if (Ct.State != EOsrRwr::Launch || Blink(0.3, 0.18)) { X.Text(P, Ct.Symbol, 16, AL_CENTER, Col); }
			if (Ct.State == EOsrRwr::Track) { X.Diamond(P, 15.0 * S, 1.4, Col); }
			if (Ct.State == EOsrRwr::Launch) { X.Circle(P, 16.0 * S, 1.6, Col, 16); }
		}
	}

	// ---- missile warning (RWR launch / DAS IR plume)
	if (W->MissilesInbound + W->IrInbound > 0)
	{
		const FVector2D Mc = X.Centre + FVector2D(0, -230) * S;
		if (Blink(0.35, 0.22))
		{
			X.Text(Mc, W->IrInbound > 0 && W->MissilesInbound == 0 ? TEXT("MISSILE  (DAS)") : TEXT("MISSILE LAUNCH"), 34, AL_CENTER, RED);
		}
		const double Hdg = FMath::Atan2(Jet->Model.Velocity.X, -Jet->Model.Velocity.Z);
		const double Brg = FMath::Atan2(W->NearestMissileDir.X, -W->NearestMissileDir.Z) - Hdg;
		const FVector2D Dir(FMath::Sin(Brg), -FMath::Cos(Brg));
		const FVector2D A = X.Centre + Dir * 300.0 * S;
		X.Line(A, A - Dir * 36.0 * S + FVector2D(-Dir.Y, Dir.X) * 14.0 * S, 3.0, RED);
		X.Line(A, A - Dir * 36.0 * S - FVector2D(-Dir.Y, Dir.X) * 14.0 * S, 3.0, RED);
		X.Text(A + Dir * 26.0 * S, FString::Printf(TEXT("%.1f"), W->NearestMissileDist / 1852.0), 16, AL_CENTER, RED);
	}
}
