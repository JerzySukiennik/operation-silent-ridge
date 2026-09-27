// F-35 HMD-style HUD drawn on the canvas (port of hud_symbology.gd) plus the controls help and the pause menu.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "OsrHud.generated.h"

class AOsrJetPawn;
struct FOsrTelemetry;

UCLASS()
class SILENTRIDGEUE_API AOsrHud : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;

private:
	double S = 1.0;
	FVector2D Centre;
	FVector2D Size;
	UPROPERTY() TObjectPtr<UFont> Font;
	const AOsrJetPawn* Jet = nullptr;

	bool Project(const FVector& DirGodot, FVector2D& Out) const;
	void Line(const FVector2D& A, const FVector2D& B, double W = 1.6, const FLinearColor* Col = nullptr);
	void Poly(const TArray<FVector2D>& Pts, double W = 1.6, const FLinearColor* Col = nullptr);
	void Circle(const FVector2D& C, double R, double W, const FLinearColor& Col);
	void Box(const FVector2D& C, const FVector2D& Half, const FLinearColor& Col);
	void Text(const FVector2D& Pos, const FString& Txt, double Px, int32 Align, const FLinearColor& Col);

	FVector2D FlightPath();
	void Ladder(const FVector2D& Fpm);
	void Boresight();
	void SpeedBox(const FOsrTelemetry& T);
	void AltBox(const FOsrTelemetry& T);
	void HeadingTape(const FOsrTelemetry& T);
	void LowerBlocks(const FOsrTelemetry& T);
	void Warnings(const FOsrTelemetry& T);
	void Help();
	void PauseMenu();
};
