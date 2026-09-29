// Carrier HUD layer (canvas).
#include "DeckHud.h"
#include "CarrierActor.h"
#include "CarrierJetOps.h"
#include "CarrierLayout.h"
#include "CarrierSubsystem.h"
#include "OsrCharacter.h"
#include "OsrJetPawn.h"
#include "GameFramework/HUD.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"

namespace
{
	const FLinearColor GREEN(FColor(92, 255, 133, 242));
	const FLinearColor WHITE(FColor(240, 240, 235, 245));
	const FLinearColor YELLOW(FColor(255, 214, 70, 255));
	const FLinearColor AMBER(FColor(255, 150, 20, 255));
	const FLinearColor RED(FColor(255, 45, 30, 255));
	const FLinearColor SHADOW(FColor(0, 0, 0, 150));
	const FLinearColor PANEL(FColor(0, 0, 0, 120));

	struct FCtx
	{
		UCanvas* C = nullptr;
		UFont* Font = nullptr;
		double S = 1.0;
		void Text(const FVector2D& P, const FString& T, double Px, int32 Align, const FLinearColor& Col) const
		{
			if (!Font) { return; }
			const float Sc = float(Px * S / FMath::Max<double>(Font->GetMaxCharHeight(), 1.0) * 1.12);
			float W = 0.f, H = 0.f;
			C->TextSize(Font, T, W, H, Sc, Sc);
			FVector2D Q = P;
			if (Align == 1) { Q.X -= W * 0.5; }
			else if (Align == 2) { Q.X -= W; }
			Q.Y -= H * 0.5;
			FCanvasTextItem It(Q, FText::FromString(T), Font, Col);
			It.Scale = FVector2D(Sc, Sc);
			It.bOutlined = true;
			It.OutlineColor = SHADOW;
			C->DrawItem(It);
		}
		void Rect(const FVector2D& Tl, const FVector2D& Sz, const FLinearColor& Col) const
		{
			FCanvasTileItem T(Tl, Sz, Col);
			T.BlendMode = SE_BLEND_Translucent;
			C->DrawItem(T);
		}
		void Line(const FVector2D& A, const FVector2D& B, double W, const FLinearColor& Col) const
		{
			FCanvasLineItem L(A, B);
			L.LineThickness = float(W * S);
			L.SetColor(Col);
			C->DrawItem(L);
		}
		void Dot(const FVector2D& P, double R, const FLinearColor& Col) const { Rect(P - FVector2D(R, R) * S, FVector2D(R, R) * 2.0 * S, Col); }
	};

	void Menu(const FCtx& X, const FVector2D& Size, const TArray<FString>& Items, int32 Sel)
	{
		const FVector2D Sz(560 * X.S, (70 + Items.Num() * 48) * X.S);
		const FVector2D Tl = Size * 0.5 - Sz * 0.5;
		X.Rect(Tl, Sz, FLinearColor(0, 0, 0, 0.72f));
		X.Text(FVector2D(Size.X * 0.5, Tl.Y + 30 * X.S), TEXT("PAUSED"), 26, 1, GREEN);
		for (int32 I = 0; I < Items.Num(); ++I)
		{
			X.Text(FVector2D(Size.X * 0.5, Tl.Y + (78 + I * 48) * X.S), (I == Sel ? TEXT("> ") : TEXT("  ")) + Items[I] + (I == Sel ? TEXT(" <") : TEXT("  ")), 22, 1, I == Sel ? YELLOW : WHITE);
		}
	}

	void Meatball(const FCtx& X, const FVector2D& C, const UCarrierJetOps* Ops)
	{
		// IFLOLS repeater: green datum bars, amber ball (red at the bottom cells), wave-off lights flashing red
		const double Cell = 11.0 * X.S;
		X.Rect(C - FVector2D(95, 90) * X.S, FVector2D(190, 180) * X.S, PANEL);
		const bool bWo = Ops->Lso.bWaveOff;
		const bool bFlash = bWo && FMath::Fmod(FPlatformTime::Seconds(), 0.5) < 0.3;
		for (int32 I = 0; I < 5; ++I)
		{
			X.Dot(C + FVector2D(-40.0 - I * 12.0, 0.0) * X.S, 4.0, FLinearColor(0.1f, 1.0f, 0.2f));
			X.Dot(C + FVector2D(40.0 + I * 12.0, 0.0) * X.S, 4.0, FLinearColor(0.1f, 1.0f, 0.2f));
		}
		for (int32 I = 0; I < 3; ++I)
		{
			const FLinearColor W = bFlash ? RED : FLinearColor(0.15f, 0.02f, 0.02f);
			X.Dot(C + FVector2D(-26.0, -20.0 + I * 20.0) * X.S, 4.5, W);
			X.Dot(C + FVector2D(26.0, -20.0 + I * 20.0) * X.S, 4.5, W);
		}
		X.Line(C + FVector2D(0, -70) * X.S, C + FVector2D(0, 70) * X.S, 1.0, FLinearColor(0.3f, 0.3f, 0.3f, 0.6f));
		const double Dev = Ops->Lso.BallDev;
		if (!bWo && FMath::Abs(Dev) <= 0.85)
		{
			const double Y = -Dev / 0.8 * 5.5 * Cell;
			X.Dot(C + FVector2D(0.0, Y), 7.0, Dev < -0.55 ? RED : AMBER);
		}
		else if (!bWo)
		{
			X.Text(C + FVector2D(0.0, Dev > 0 ? -78.0 : 78.0) * X.S, Dev > 0 ? TEXT("BALL HIGH") : TEXT("BALL LOW"), 13, 1, Dev > 0 ? AMBER : RED);
		}
		X.Text(C + FVector2D(0.0, -104.0) * X.S, TEXT("IFLOLS"), 13, 1, GREEN);
	}
}

void FDeckHud::Draw(AHUD* Hud, UCanvas* Canvas, APawn* Pawn)
{
	if (!Hud || !Canvas || !Pawn) { return; }
	UCarrierSubsystem* Sub = Pawn->GetWorld()->GetSubsystem<UCarrierSubsystem>();
	if (!Sub || !Sub->GetCarrier()) { return; }
	FCtx X;
	X.C = Canvas;
	X.Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	const FVector2D Size(Canvas->ClipX, Canvas->ClipY);
	X.S = Size.Y / 1080.0;
	const double Now = Pawn->GetWorld()->GetTimeSeconds();
	ACarrierActor* Carrier = Sub->GetCarrier();
	UCarrierJetOps* Ops = Sub->PlayerOps();

	if (AOsrCharacter* Ch = Cast<AOsrCharacter>(Pawn))
	{
		// marker over the player's jet
		if (Ops && Ops->OnDeck())
		{
			const FVector2D Lad = Ops->LadderPoint();
			const FVector W = Carrier->LocalToUE(FVector(Lad.X, Lad.Y, CarrierLayout::DeckZ + 3.2));
			const FVector P = Hud->Project(W, true);
			const double D = FVector::Dist(W, Ch->GetActorLocation()) * 0.01;
			if (P.Z > 0.0 && D > 3.0)
			{
				const FVector2D Q(P.X, P.Y);
				X.Line(Q + FVector2D(-9, -16) * X.S, Q, 2.0, YELLOW);
				X.Line(Q + FVector2D(9, -16) * X.S, Q, 2.0, YELLOW);
				X.Text(Q + FVector2D(0, -30) * X.S, FString::Printf(TEXT("YOUR F-35C  %.0f m"), D), 16, 1, YELLOW);
			}
		}
		if (!Ch->Prompt.IsEmpty()) { X.Text(FVector2D(Size.X * 0.5, Size.Y * 0.80), Ch->Prompt, 24, 1, WHITE); }
		if (Ch->BlastLevel > 0.25) { X.Text(FVector2D(Size.X * 0.5, Size.Y * 0.30), TEXT("JET BLAST!"), 34, 1, RED); }
		X.Text(FVector2D(24 * X.S, Size.Y - 30 * X.S), TEXT("On deck - BACK / Share / F1: controls"), 15, 0, FLinearColor(1, 1, 1, 0.6f));
		if (Ch->bShowHelp)
		{
			const TCHAR* Rows[][2] = {
				{TEXT("Left stick / WASD"), TEXT("Walk (L3 / Shift: run)")},
				{TEXT("Right stick / mouse / IJKL"), TEXT("Camera")},
				{TEXT("A / Cross / Space"), TEXT("Jump")},
				{TEXT("X / Square / F"), TEXT("Interact: climb into your F-35C, elevator 2")},
				{TEXT("Start / Options / Esc"), TEXT("Menu (respawn at the island door, quit)")},
				{TEXT(""), TEXT("Your jet is parked by catapult 1 (yellow marker). Stay out of the jet blast!")},
			};
			const int32 N = UE_ARRAY_COUNT(Rows);
			const FVector2D Sz(980 * X.S, (60 + N * 34) * X.S);
			const FVector2D Tl(Size.X * 0.5 - Sz.X * 0.5, Size.Y * 0.12);
			X.Rect(Tl, Sz, FLinearColor(0, 0.02f, 0, 0.7f));
			X.Text(Tl + FVector2D(24, 26) * X.S, TEXT("ON FOOT - CONTROLS"), 20, 0, GREEN);
			for (int32 I = 0; I < N; ++I)
			{
				X.Text(Tl + FVector2D(24, 64 + I * 34) * X.S, Rows[I][0], 17, 0, GREEN);
				X.Text(Tl + FVector2D(330, 64 + I * 34) * X.S, Rows[I][1], 17, 0, WHITE);
			}
		}
		if (Ch->bPaused)
		{
			TArray<FString> It;
			for (int32 I = 0; I < AOsrCharacter::MenuCount; ++I) { It.Add(Ch->MenuLabel(I)); }
			Menu(X, Size, It, Ch->MenuIndex);
		}
		return;
	}

	AOsrJetPawn* Jet = Cast<AOsrJetPawn>(Pawn);
	if (!Jet || !Ops || Ops->GetOwner() != Jet) { return; }
	// prompt + state
	if (!Ops->Prompt.IsEmpty()) { X.Text(FVector2D(Size.X * 0.5, Size.Y * 0.74), Ops->Prompt, 24, 1, WHITE); }
	if (Sub->ExitHold > 0.05)
	{
		X.Rect(FVector2D(Size.X * 0.5 - 120 * X.S, Size.Y * 0.78), FVector2D(240 * X.S * FMath::Min(Sub->ExitHold / 0.7, 1.0), 8 * X.S), YELLOW);
	}
	// cues: HOOK, DFP / APC
	FString Cue;
	if (Ops->bHookDown) { Cue += TEXT("HOOK  "); }
	if (Ops->bDfp) { Cue += TEXT("DFP  APC"); }
	if (!Cue.IsEmpty()) { X.Text(FVector2D(Size.X * 0.5, Size.Y * 0.655), Cue, 18, 1, GREEN); }
	if (Ops->State == EDeckState::Launch) { X.Text(FVector2D(Size.X * 0.5, Size.Y * 0.30), FString::Printf(TEXT("CAT %d"), Ops->CatIndex + 1), 30, 1, YELLOW); }
	// meatball repeater during the approach
	if (Ops->bLsoGroove || Ops->Lso.bWaveOff) { Meatball(X, FVector2D(Size.X - 170 * X.S, Size.Y * 0.42), Ops); }
	// LSO radio (last calls, 5 s)
	int32 Row = 0;
	const TArray<FLsoCall>& Calls = Ops->Lso.Calls();
	for (int32 I = Calls.Num() - 1; I >= 0 && Row < 3; --I)
	{
		const double Age = Now - Calls[I].T;
		if (Age < 0.0 || Age > 5.0) { continue; }
		const float A = float(FMath::Clamp(1.0 - (Age - 4.0), 0.0, 1.0));
		const FLinearColor Col = Calls[I].Who == TEXT("LSO") ? FLinearColor(1.0f, 0.86f, 0.35f, A) : FLinearColor(0.75f, 0.9f, 1.0f, A);
		X.Text(FVector2D(Size.X * 0.5, Size.Y * 0.16 + Row * 30 * X.S), FString::Printf(TEXT("%s:  \"%s\""), *Calls[I].Who, *Calls[I].Text), 21, 1, Col);
		++Row;
	}
	// debrief
	if (Now - Ops->ResultT < 9.0 && !Ops->ResultText.IsEmpty())
	{
		const FVector2D Tl(Size.X * 0.5 - 300 * X.S, Size.Y * 0.40);
		X.Rect(Tl, FVector2D(600, 110) * X.S, FLinearColor(0, 0, 0, 0.6f));
		X.Text(Tl + FVector2D(300, 36) * X.S, Ops->ResultText, 34, 1, Ops->LastGrade == ELsoGrade::Bolter || Ops->LastGrade == ELsoGrade::WaveOff ? AMBER : GREEN);
		if (!Ops->Lso.LastComment.IsEmpty() && Ops->LastGrade != ELsoGrade::Bolter && Ops->LastGrade != ELsoGrade::WaveOff)
		{
			X.Text(Tl + FVector2D(300, 80) * X.S, FString::Printf(TEXT("LSO: %s"), *Ops->Lso.LastComment), 18, 1, WHITE);
		}
	}
}
