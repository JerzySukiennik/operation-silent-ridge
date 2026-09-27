// HMD symbology (hud_symbology.gd): flight path marker, conformal pitch ladder, speed/altitude boxes, heading tape, G/AoA, throttle/AB, fuel, PULL UP/ALTITUDE warnings, help, pause menu.
#include "OsrHud.h"
#include "OsrJetPawn.h"
#include "OsrControls.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"

using namespace OsrMath;

namespace
{
	const FLinearColor GREEN = FLinearColor(FColor(92, 255, 133, 242));
	const FLinearColor GREEN_DIM = FLinearColor(FColor(92, 255, 133, 140));
	const FLinearColor SHADOW = FLinearColor(FColor(0, 18, 5, 128));
	const FLinearColor WARN = FLinearColor(FColor(255, 219, 77, 255));
	constexpr double LADDER_SPAN = 12.5;
	constexpr double CONFORMAL_LIMIT = 18.0;
	enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

	FString Thousands(int64 V)
	{
		const bool bNeg = V < 0;
		FString Str = FString::Printf(TEXT("%lld"), FMath::Abs(V));
		FString Out;
		while (Str.Len() > 3)
		{
			Out = TEXT(",") + Str.Right(3) + Out;
			Str = Str.LeftChop(3);
		}
		return (bNeg ? TEXT("-") : TEXT("")) + Str + Out;
	}
}

bool AOsrHud::Project(const FVector& DirGodot, FVector2D& Out) const
{
	const FVector L = Jet->CamRotUE.UnrotateVector(AOsrJetPawn::DirToUE(DirGodot));
	if (L.X <= 1e-4) { return false; }
	const double F = (Size.Y * 0.5) / FMath::Tan(Rad(Jet->CamFovV) * 0.5);
	Out = FVector2D(Centre.X + L.Y / L.X * F, Centre.Y - L.Z / L.X * F);
	return true;
}

void AOsrHud::Line(const FVector2D& A, const FVector2D& B, double W, const FLinearColor* Col)
{
	FCanvasLineItem Sh(A, B);
	Sh.LineThickness = float((W + 2.2) * S);
	Sh.SetColor(SHADOW);
	Canvas->DrawItem(Sh);
	FCanvasLineItem It(A, B);
	It.LineThickness = float(W * S);
	It.SetColor(Col ? *Col : GREEN);
	Canvas->DrawItem(It);
}

void AOsrHud::Poly(const TArray<FVector2D>& Pts, double W, const FLinearColor* Col)
{
	for (int32 I = 0; I + 1 < Pts.Num(); ++I) { Line(Pts[I], Pts[I + 1], W, Col); }
}

void AOsrHud::Circle(const FVector2D& C, double R, double W, const FLinearColor& Col)
{
	TArray<FVector2D> P;
	for (int32 I = 0; I < 25; ++I)
	{
		const double A = UE_DOUBLE_TWO_PI * I / 24.0;
		P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
	}
	Poly(P, W, &Col);
}

void AOsrHud::Box(const FVector2D& C, const FVector2D& Half, const FLinearColor& Col)
{
	Poly({C + FVector2D(-Half.X, -Half.Y), C + FVector2D(Half.X, -Half.Y), C + Half, C + FVector2D(-Half.X, Half.Y), C + FVector2D(-Half.X, -Half.Y)}, 1.6, &Col);
}

void AOsrHud::Text(const FVector2D& Pos, const FString& Txt, double Px, int32 Align, const FLinearColor& Col)
{
	if (!Font) { return; }
	const double FontPx = Font->GetMaxCharHeight();
	const float Sc = float(Px * S / FMath::Max(FontPx, 1.0) * 1.12);
	float W = 0.f, H = 0.f;
	Canvas->TextSize(Font, Txt, W, H, Sc, Sc);
	FVector2D P = Pos;
	if (Align == AL_CENTER) { P.X -= W * 0.5; }
	else if (Align == AL_RIGHT) { P.X -= W; }
	P.Y -= H * 0.5;
	FCanvasTextItem It(P, FText::FromString(Txt), Font, Col);
	It.Scale = FVector2D(Sc, Sc);
	It.bOutlined = true;
	It.OutlineColor = SHADOW;
	Canvas->DrawItem(It);
}

void AOsrHud::DrawHUD()
{
	Super::DrawHUD();
	Jet = Cast<AOsrJetPawn>(GetOwningPawn());
	if (!Jet || !Canvas) { return; }
	if (!Font && GEngine) { Font = GEngine->GetLargeFont(); }
	Size = FVector2D(Canvas->ClipX, Canvas->ClipY);
	S = Size.Y / 1080.0;
	Centre = Size * 0.5;
	const FOsrTelemetry T = Jet->Telemetry();
	if (!T.bAlive)
	{
		Text(Centre + FVector2D(0, -40) * S, TEXT("CRASHED"), 40, AL_CENTER, WARN);
		Help();
		PauseMenu();
		return;
	}
	const FVector V = Jet->Model.Velocity;
	const FVector VDir = V.Size() > 5.0 ? V.GetSafeNormal() : -BasisZ(Jet->RenderRot);
	const FVector CamFwdG = AOsrJetPawn::DirToUE(Jet->CamRotUE.GetForwardVector());   // UE->Godot swaps the same axes
	if (Deg(FMath::Acos(FMath::Clamp(FVector::DotProduct(CamFwdG, VDir), -1.0, 1.0))) < CONFORMAL_LIMIT)
	{
		const FVector2D Fpm = FlightPath();
		Ladder(Fpm);
		Boresight();
	}
	SpeedBox(T);
	AltBox(T);
	HeadingTape(T);
	LowerBlocks(T);
	Warnings(T);
	Help();
	PauseMenu();
}

FVector2D AOsrHud::FlightPath()
{
	const FVector V = Jet->Model.Velocity;
	const FVector Dir = V.Size() > 5.0 ? V.GetSafeNormal() : -BasisZ(Jet->RenderRot);
	FVector2D P;
	const bool bOk = Project(Dir, P);
	FVector2D Fpm = bOk ? P : Centre;
	const FVector2D Lim(Size.X * 0.32, Size.Y * 0.36);
	const FVector2D D = Fpm - Centre;
	bool bCaged = false;
	if (FMath::Abs(D.X) > Lim.X || FMath::Abs(D.Y) > Lim.Y || !bOk)
	{
		Fpm = Centre + FVector2D(FMath::Clamp(D.X, -Lim.X, Lim.X), FMath::Clamp(D.Y, -Lim.Y, Lim.Y));
		bCaged = true;
	}
	const double R = 10.0 * S;
	const FLinearColor& Col = bCaged ? GREEN_DIM : GREEN;
	Circle(Fpm, R, 1.8, Col);
	Line(Fpm + FVector2D(-R, 0), Fpm + FVector2D(-R - 15.0 * S, 0), 1.8, &Col);
	Line(Fpm + FVector2D(R, 0), Fpm + FVector2D(R + 15.0 * S, 0), 1.8, &Col);
	Line(Fpm + FVector2D(0, -R), Fpm + FVector2D(0, -R - 9.0 * S), 1.8, &Col);
	return Fpm;
}

void AOsrHud::Ladder(const FVector2D& Fpm)
{
	const FVector V = Jet->Model.Velocity;
	const FVector Fwd = V.Size() > 5.0 ? V : -BasisZ(Jet->RenderRot);
	FVector H(Fwd.X, 0.0, Fwd.Z);
	if (H.SizeSquared() < 1e-6) { H = -BasisZ(Jet->RenderRot); H.Y = 0.0; }
	H = H.GetSafeNormal();
	const FVector Right = FVector::CrossProduct(H, FVector(0, 1, 0)).GetSafeNormal();
	const double FpmPitch = Deg(FMath::Asin(FMath::Clamp(Fwd.GetSafeNormal().Y, -1.0, 1.0)));
	FVector2D Hz;
	const bool bHz = Project(H, Hz);
	for (int32 DegI = -90; DegI < 95; DegI += 5)
	{
		if (FMath::Abs(DegI - FpmPitch) > LADDER_SPAN) { continue; }
		const double Th = Rad(DegI);
		const FVector Dir = H * FMath::Cos(Th) + FVector(0, 1, 0) * FMath::Sin(Th);
		FVector2D C0, C1;
		if (!Project(Dir, C0) || !Project((Dir * 4000.0 + Right * 200.0).GetSafeNormal(), C1)) { continue; }
		FVector2D C = C0;
		const FVector2D Ax = (C1 - C).GetSafeNormal();
		C += Ax * FVector2D::DotProduct(Ax, Fpm - C);
		if (FVector2D::Distance(C, Centre) > Size.Y * 0.3) { continue; }
		const double Gap = 34.0 * S;
		const double Ln = (DegI == 0 ? 230.0 : 78.0) * S;
		FVector2D Tick = FVector2D::ZeroVector;
		if (DegI != 0 && bHz) { Tick = (Hz - C).GetSafeNormal() * 9.0 * S; }
		for (double Side : {-1.0, 1.0})
		{
			const FVector2D P0 = C + Ax * Gap * Side;
			const FVector2D P1 = C + Ax * (Gap + Ln) * Side;
			if (DegI < 0)
			{
				for (int32 K = 0; K < 5; ++K)
				{
					const double F0 = double(K) / 5.0;
					const double F1 = F0 + 0.6 / 5.0;
					Line(FMath::Lerp(P0, P1, F0), FMath::Lerp(P0, P1, F1), 1.5);
				}
			}
			else
			{
				Line(P0, P1, DegI != 0 ? 1.6 : 1.9);
			}
			if (DegI != 0)
			{
				Line(P1, P1 + Tick, 1.5);
				const FVector2D Lp = P1 + Ax * Side * 18.0 * S - Tick * 0.3;
				Text(Lp, FString::FromInt(FMath::Abs(DegI)), 17, AL_CENTER, GREEN);
			}
		}
	}
}

void AOsrHud::Boresight()
{
	FVector2D C;
	if (!Project(-BasisZ(Jet->RenderRot), C)) { return; }
	const double K = S;
	Poly({C + FVector2D(-20, 0) * K, C + FVector2D(-10, 0) * K, C + FVector2D(-5, 8) * K, C + FVector2D(0, 0) * K, C + FVector2D(5, 8) * K, C + FVector2D(10, 0) * K, C + FVector2D(20, 0) * K}, 1.4, &GREEN_DIM);
}

void AOsrHud::SpeedBox(const FOsrTelemetry& T)
{
	const FVector2D C = Centre + FVector2D(-330, 0) * S;
	Box(C, FVector2D(52, 19) * S, GREEN);
	Text(C, FString::FromInt(FMath::RoundToInt(T.IasKt)), 26, AL_CENTER, GREEN);
	Text(C + FVector2D(0, -34) * S, TEXT("KCAS"), 15, AL_CENTER, GREEN_DIM);
	Text(C + FVector2D(0, 36) * S, FString::Printf(TEXT("M %.2f"), T.Mach), 20, AL_CENTER, GREEN);
}

void AOsrHud::AltBox(const FOsrTelemetry& T)
{
	const FVector2D C = Centre + FVector2D(330, 0) * S;
	Box(C, FVector2D(62, 19) * S, GREEN);
	const int64 Alt = int64(FMath::RoundToDouble(T.AltFt / 10.0) * 10.0);
	Text(C, Thousands(Alt), 26, AL_CENTER, GREEN);
	const double Vs = T.VsFpm;
	Text(C + FVector2D(0, -34) * S, FString(Vs >= 0.0 ? TEXT("+") : TEXT("")) + Thousands(int64(FMath::RoundToDouble(Vs / 10.0) * 10.0)), 15, AL_CENTER, GREEN_DIM);
	if (T.RadarAltFt < 5000.0)
	{
		Text(C + FVector2D(0, 36) * S, TEXT("R ") + Thousands(int64(FMath::Max(T.RadarAltFt, 0.0))), 20, AL_CENTER, GREEN);
	}
}

void AOsrHud::HeadingTape(const FOsrTelemetry& T)
{
	const double Y = 96.0 * S;
	const double HalfW = 190.0 * S;
	const double Span = 30.0;
	const double Hdg = T.HeadingDeg;
	const double Ppd = HalfW / Span;
	const int32 Start = int32(FMath::FloorToDouble((Hdg - Span) / 5.0)) * 5;
	for (int32 D = Start; D < int32(Hdg + Span) + 6; D += 5)
	{
		const double Off = (D - Hdg) * Ppd;
		if (FMath::Abs(Off) > HalfW) { continue; }
		const double X = Centre.X + Off;
		const bool bMajor = ((D % 10) + 10) % 10 == 0;
		Line(FVector2D(X, Y), FVector2D(X, Y - (bMajor ? 12.0 : 6.0) * S), 1.4);
		if (bMajor && FMath::Abs(Off) > 32.0 * S)
		{
			Text(FVector2D(X, Y - 26.0 * S), FString::Printf(TEXT("%03d"), ((D % 360) + 360) % 360), 16, AL_CENTER, GREEN_DIM);
		}
	}
	Line(FVector2D(Centre.X - HalfW, Y), FVector2D(Centre.X + HalfW, Y), 1.2, &GREEN_DIM);
	const FVector2D C(Centre.X, Y - 26.0 * S);
	Box(C, FVector2D(30, 14) * S, GREEN);
	Text(C, FString::Printf(TEXT("%03d"), FMath::RoundToInt(Hdg) % 360), 20, AL_CENTER, GREEN);
	Line(FVector2D(Centre.X, Y + 2.0 * S), FVector2D(Centre.X - 6.0 * S, Y + 11.0 * S), 1.4);
	Line(FVector2D(Centre.X, Y + 2.0 * S), FVector2D(Centre.X + 6.0 * S, Y + 11.0 * S), 1.4);
}

void AOsrHud::LowerBlocks(const FOsrTelemetry& T)
{
	const FVector2D L = Centre + FVector2D(-330, 150) * S;
	Text(L, FString::Printf(TEXT("G  %.1f"), T.G), 22, AL_CENTER, GREEN);
	Text(L + FVector2D(0, 28) * S, FString::Printf(TEXT("%.1f"), T.GMax), 17, AL_CENTER, GREEN_DIM);
	Text(L + FVector2D(0, 58) * S, FString::Printf(TEXT("α  %.1f"), T.AoaDeg), 22, AL_CENTER, GREEN);
	const FVector2D R = Centre + FVector2D(330, 150) * S;
	const FString Thr = T.bAb ? FString::Printf(TEXT("AB  %d"), FMath::RoundToInt(T.AbLevel * 100.0)) : FString::Printf(TEXT("THR %d"), FMath::RoundToInt(FMath::Min(T.Lever, 1.0) * 100.0));
	Text(R, Thr, 22, AL_CENTER, GREEN);
	const double Dp = Jet->Controls.DetentProgress();
	if (Dp > 0.0 && !T.bAb)
	{
		const FVector2D B0 = R + FVector2D(-40, 16) * S;
		Line(B0, B0 + FVector2D(80.0 * Dp, 0) * S, 3.0);
	}
	const int64 FuelLb = int64(FMath::RoundToDouble(T.FuelKg * 2.20462 / 10.0) * 10.0);
	Text(R + FVector2D(0, 30) * S, TEXT("FUEL ") + Thousands(FuelLb), 17, AL_CENTER, GREEN_DIM);
	FString TagTxt;
	if (T.bGear) { TagTxt += TEXT("GEAR"); }
	if (T.bBrake) { TagTxt += TagTxt.IsEmpty() ? TEXT("BRK") : TEXT(" BRK"); }
	if (!TagTxt.IsEmpty()) { Text(R + FVector2D(0, 58) * S, TagTxt, 20, AL_CENTER, GREEN); }
}

void AOsrHud::Warnings(const FOsrTelemetry& T)
{
	const double Tti = T.TimeToImpact;
	const bool bBlink = FMath::Fmod(FPlatformTime::Seconds(), 0.5) < 0.32;
	const FVector2D C = Centre + FVector2D(0, 118) * S;
	if (Tti < 3.5)
	{
		if (bBlink)
		{
			Box(C, FVector2D(92, 24) * S, WARN);
			Text(C, TEXT("PULL UP"), 32, AL_CENTER, WARN);
		}
	}
	else if (Tti < 7.0)
	{
		if (bBlink) { Text(C, TEXT("ALTITUDE"), 28, AL_CENTER, WARN); }
	}
	else if (T.FuelKg < 900.0)
	{
		Text(C, TEXT("BINGO FUEL"), 22, AL_CENTER, WARN);
	}
	if (T.AoaDeg > 35.0 && bBlink) { Text(C + FVector2D(0, 40) * S, TEXT("AOA"), 22, AL_CENTER, WARN); }
}

void AOsrHud::Help()
{
	if (!Jet->bShowHelp)
	{
		Text(FVector2D(28, Size.Y / S - 28) * S, TEXT("BACK · CONTROLS    START · MENU"), 14, AL_LEFT, GREEN_DIM);
		return;
	}
	const FVector2D Tl = FVector2D(60, 250) * S;
	const FVector2D Sz = FVector2D(820, 60 + GOsrLayoutHelpCount * 34) * S;
	FCanvasTileItem Bg(Tl, Sz, FLinearColor(0.0f, 0.05f, 0.02f, 0.62f));
	Bg.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Bg);
	Box(Tl + Sz * 0.5, Sz * 0.5, GREEN);
	Text(Tl + FVector2D(24, 30) * S, TEXT("CONTROLS  (BACK to close)"), 20, AL_LEFT, GREEN);
	double Y = 72.0;
	for (int32 I = 0; I < GOsrLayoutHelpCount; ++I)
	{
		Text(Tl + FVector2D(24, Y) * S, GOsrLayoutHelp[I].Key, 18, AL_LEFT, GREEN);
		Text(Tl + FVector2D(200, Y) * S, GOsrLayoutHelp[I].Text, 18, AL_LEFT, GREEN_DIM);
		Y += 34.0;
	}
}

void AOsrHud::PauseMenu()
{
	if (!Jet->bPaused) { return; }
	FCanvasTileItem Dim(FVector2D::ZeroVector, Size, FLinearColor(0.0f, 0.02f, 0.01f, 0.72f));
	Dim.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Dim);
	const FVector2D C = Centre + FVector2D(0, -120) * S;
	Text(C, TEXT("PAUSED — the mission keeps running"), 26, AL_CENTER, GREEN);
	for (int32 I = 0; I < AOsrJetPawn::MenuCount; ++I)
	{
		const FVector2D P = Centre + FVector2D(0, -40 + I * 64) * S;
		const bool bSel = I == Jet->MenuIndex;
		if (bSel)
		{
			FCanvasTileItem Hl(P - FVector2D(240, 26) * S, FVector2D(480, 52) * S, FLinearColor(0.2f, 0.9f, 0.4f, 0.18f));
			Hl.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Hl);
		}
		Box(P, FVector2D(240, 26) * S, bSel ? GREEN : GREEN_DIM);
		Text(P, Jet->MenuLabel(I), 22, AL_CENTER, bSel ? GREEN : GREEN_DIM);
	}
	Text(Centre + FVector2D(0, 250) * S, TEXT("D-pad / stick: select    A: confirm    B / START: resume"), 16, AL_CENTER, GREEN_DIM);
}
