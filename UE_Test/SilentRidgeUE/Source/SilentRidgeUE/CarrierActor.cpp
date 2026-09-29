// Carrier actor: motion, meshes, devices (JBDs, wires, elevator, IFLOLS), deck lights, parked jets, wake.
#include "CarrierActor.h"
#include "CarrierLayout.h"
#include "CarrierF35Anim.h"
#include "DeckCrew.h"
#include "OsrJetPawn.h"
#include "OsrWorldLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	constexpr double Cv_LEG = 14000.0;       // racetrack straight legs, m
	constexpr double Cv_TURN_R = 1500.0;     // turn radius, m
	constexpr double Cv_START_BACK = 7000.0; // t = 0 is this far behind the layout point on the upwind leg
	constexpr int32 Cv_WAKE_SEGS = 12;
	constexpr double Cv_WAKE_LEN = 110.0;
	const FLinearColor Cv_AMBER(1.0f, 0.5f, 0.05f), Cv_RED(1.0f, 0.04f, 0.02f), Cv_GREEN(0.1f, 1.0f, 0.2f), Cv_WHITE(1.0f, 0.95f, 0.85f);
	constexpr int32 Cv_LENS_CELLS = 12;
	constexpr double Cv_LENS_RANGE = 0.8;    // +- deg covered by the cell column

	FVector2D Cv_V2(const FVector& V) { return FVector2D(V.X, V.Y); }
}

ACarrierActor::ACarrierActor()
{
	using namespace CarrierLayout;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Collision = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->SetMobility(EComponentMobility::Movable);
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	Collision->SetHiddenInGame(true);
	Collision->SetCastShadow(false);
	Collision->bReceivesDecals = false;
	auto Vis = [this](const TCHAR* N)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(N);
		C->SetupAttachment(Collision);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->bReceivesDecals = false;
		return C;
	};
	Hull = Vis(TEXT("Hull"));
	Deck = Vis(TEXT("Deck"));
	Hangar = Vis(TEXT("Hangar"));
	Hangar->SetCastShadow(false);
	Elevator = Vis(TEXT("Elevator2"));
	ElevatorBox = CreateDefaultSubobject<UBoxComponent>(TEXT("Elevator2Box"));
	ElevatorBox->SetupAttachment(Elevator);
	ElevatorBox->SetCollisionProfileName(TEXT("BlockAll"));
	ElevatorBox->SetMobility(EComponentMobility::Movable);
}

ACarrierActor* ACarrierActor::Get(const UObject* WorldContext)
{
	using namespace CarrierLayout;
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	for (TActorIterator<ACarrierActor> It(W); It; ++It) { return *It; }
	return nullptr;
}

FString ACarrierActor::GradeText(ELsoGrade G)
{
	using namespace CarrierLayout;
	switch (G)
	{
	case ELsoGrade::Perfect: return TEXT("_OK_");
	case ELsoGrade::Ok: return TEXT("OK");
	case ELsoGrade::Fair: return TEXT("(OK) FAIR");
	case ELsoGrade::NoGrade: return TEXT("NO GRADE");
	case ELsoGrade::Cut: return TEXT("CUT");
	case ELsoGrade::Bolter: return TEXT("BOLTER");
	default: return TEXT("WAVE-OFF");
	}
}

UStaticMeshComponent* ACarrierActor::AddMesh(const TCHAR* Name, const TCHAR* Path, USceneComponent* Parent)
{
	using namespace CarrierLayout;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
	C->SetupAttachment(Parent ? Parent : Collision);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->bReceivesDecals = false;
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, Path)) { C->SetStaticMesh(M); }
	C->RegisterComponent();
	return C;
}

// ---------------------------------------------------------------- motion

double ACarrierActor::Clock() const
{
	using namespace CarrierLayout;
	const UWorld* W = GetWorld();
	return (W ? W->GetTimeSeconds() : 0.0) + TimeOffset;
}

FCarrierPose ACarrierActor::Evaluate(double T) const
{
	using namespace CarrierLayout;
	// racetrack: upwind leg (into the wind, along Dir2D), port turn, downwind leg, port turn
	const FVector2D D = Dir2D;
	const FVector2D Left(D.Y, -D.X);
	const FVector2D A(StartUE.X * 0.01, StartUE.Y * 0.01);
	const FVector2D B = A + D * Cv_LEG;
	const double Per = 2.0 * Cv_LEG + 2.0 * UE_DOUBLE_PI * Cv_TURN_R;
	const double S = OsrMath::FPosMod(SpeedMs * T, Per);
	FVector2D P, Dir;
	double Heel = 0.0;
	if (S < Cv_LEG) { P = A + D * S; Dir = D; }
	else if (S < Cv_LEG + UE_DOUBLE_PI * Cv_TURN_R)
	{
		const double Th = (S - Cv_LEG) / Cv_TURN_R;
		P = B + Left * Cv_TURN_R + (-Left * FMath::Cos(Th) + D * FMath::Sin(Th)) * Cv_TURN_R;
		Dir = D * FMath::Cos(Th) + Left * FMath::Sin(Th);
		Heel = 1.2 * FMath::Min(FMath::Min(Th, UE_DOUBLE_PI - Th) / 0.3, 1.0);
	}
	else if (S < 2.0 * Cv_LEG + UE_DOUBLE_PI * Cv_TURN_R)
	{
		const double U = S - Cv_LEG - UE_DOUBLE_PI * Cv_TURN_R;
		P = B + Left * (2.0 * Cv_TURN_R) - D * U;
		Dir = -D;
	}
	else
	{
		const double Th = (S - 2.0 * Cv_LEG - UE_DOUBLE_PI * Cv_TURN_R) / Cv_TURN_R;
		P = A + Left * Cv_TURN_R + (Left * FMath::Cos(Th) - D * FMath::Sin(Th)) * Cv_TURN_R;
		Dir = -D * FMath::Cos(Th) - Left * FMath::Sin(Th);
		Heel = 1.2 * FMath::Min(FMath::Min(Th, UE_DOUBLE_PI - Th) / 0.3, 1.0);
	}
	const double Tau = 2.0 * UE_DOUBLE_PI;
	const double Roll = 0.55 * FMath::Sin(Tau * T / 15.5 + 0.3) + 0.22 * FMath::Sin(Tau * T / 9.1 + 1.9) + Heel;
	const double Pitch = 0.20 * FMath::Sin(Tau * T / 8.3) + 0.09 * FMath::Sin(Tau * T / 12.7 + 1.0);
	const double Heave = 0.25 * FMath::Sin(Tau * T / 7.9 + 0.7) + 0.1 * FMath::Sin(Tau * T / 5.3);
	FCarrierPose O;
	O.YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	O.RotUE = FRotator(Pitch, O.YawDeg, Roll).Quaternion();
	O.PosUE = FVector(P.X * 100.0, P.Y * 100.0, Heave * 100.0);
	O.VelUE = FVector(Dir.X, Dir.Y, 0.0) * SpeedMs * 100.0;
	return O;
}

FVector ACarrierActor::LocalToGodot(const FVector& LocalM) const
{
	using namespace CarrierLayout;
	return AOsrJetPawn::FromUE(LocalToUE(LocalM));
}

FVector ACarrierActor::GodotToLocal(const FVector& G) const
{
	using namespace CarrierLayout;
	return UEToLocal(AOsrJetPawn::ToUE(G));
}

FVector ACarrierActor::UEToStabilized(const FVector& WorldCm, const FVector& OriginLocalM) const
{
	using namespace CarrierLayout;
	const FVector O = LocalToUE(OriginLocalM);
	return FRotator(0.0, Cur.YawDeg, 0.0).Quaternion().UnrotateVector(WorldCm - O) * 0.01;
}

// ---------------------------------------------------------------- geometry

bool ACarrierActor::InDeckOutline(double X, double Y)
{
	using namespace CarrierLayout;
	const int32 N = UE_ARRAY_COUNT(Outline);
	bool bIn = false;
	for (int32 I = 0, J = N - 1; I < N; J = I++)
	{
		const FVector2D& A = Outline[I];
		const FVector2D& B = Outline[J];
		if (((A.Y > Y) != (B.Y > Y)) && (X < (B.X - A.X) * (Y - A.Y) / (B.Y - A.Y) + A.X)) { bIn = !bIn; }
	}
	return bIn;
}

bool ACarrierActor::InsideShip(const FVector& L)
{
	using namespace CarrierLayout;
	if (L.Z > DeckZ + 0.02)
	{
		return L.Z < DeckZ + 24.0 && L.X > Island.Min.X && L.X < Island.Max.X && L.Y > Island.Min.Y && L.Y < Island.Max.Y;
	}
	if (L.Z < -11.3) { return false; }
	if (InDeckOutline(L.X, L.Y) && L.Z > DeckZ - 9.0) { return true; }   // deck plate + sponsons under the overhang
	if (L.X < SternX || L.X > BowX) { return false; }
	const double Bow = FMath::Pow(FMath::Clamp((BowX - L.X) / 105.0, 0.0, 1.0), 0.42);
	return FMath::Abs(L.Y) < HullHalfBeamTop * FMath::Min(Bow, 0.95);
}

FVector2D ACarrierActor::LandDir()
{
	using namespace CarrierLayout;
	const double A = FMath::DegreesToRadians(LandHdgDeg);
	return FVector2D(FMath::Cos(A), FMath::Sin(A));
}

FVector2D ACarrierActor::LandPoint(double S, double Lat)
{
	using namespace CarrierLayout;
	const FVector2D D = LandDir();
	const FVector2D N(-D.Y, D.X);
	return LandR0 + D * S + N * Lat;
}

FVector2D ACarrierActor::ToLanding(const FVector2D& P)
{
	using namespace CarrierLayout;
	const FVector2D D = LandDir();
	const FVector2D N(-D.Y, D.X);
	const FVector2D R = P - LandR0;
	return FVector2D(FVector2D::DotProduct(R, D), FVector2D::DotProduct(R, N));
}

double ACarrierActor::ElevatorZ() const
{
	using namespace CarrierLayout;
	const double S = ElevatorPos * ElevatorPos * (3.0 - 2.0 * ElevatorPos);
	return FMath::Lerp(DeckZ, HangarFloor, S);
}

// ---------------------------------------------------------------- lifecycle

void ACarrierActor::BeginPlay()
{
	using namespace CarrierLayout;
	Super::BeginPlay();
	// upwind leg: perpendicular to the carrier -> fjord mouth line, sailing NNW (wind from the NNW); t = 0 is Cv_START_BACK m before the layout point
	const FVector2D CarrierXY(OsrWorld::Carrier.X, OsrWorld::Carrier.Z);
	const FVector2D MouthXY(OsrWorld::Mouth.X, OsrWorld::Mouth.Z);
	const FVector2D ToCoast = (MouthXY - CarrierXY).GetSafeNormal();
	Dir2D = FVector2D(-ToCoast.Y, ToCoast.X);
	if (Dir2D.Y > 0.0) { Dir2D = -Dir2D; }                 // UE -Y = north
	const FVector2D Start = CarrierXY - Dir2D * Cv_START_BACK;
	StartUE = FVector(Start.X * 100.0, Start.Y * 100.0, 0.0);
	Cur = Evaluate(Clock());
	SetActorLocationAndRotation(Cur.PosUE, Cur.RotUE, false, nullptr, ETeleportType::TeleportPhysics);

	auto SetM = [](UStaticMeshComponent* C, const TCHAR* P) { if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, P)) { C->SetStaticMesh(M); } };
	SetM(Collision, TEXT("/Game/Carrier/Meshes/SM_CarrierCollision.SM_CarrierCollision"));
	SetM(Hull, TEXT("/Game/Carrier/Meshes/SM_CarrierHull.SM_CarrierHull"));
	SetM(Deck, TEXT("/Game/Carrier/Meshes/SM_CarrierDeck.SM_CarrierDeck"));
	SetM(Hangar, TEXT("/Game/Carrier/Meshes/SM_CarrierHangar.SM_CarrierHangar"));
	SetM(Elevator, TEXT("/Game/Carrier/Meshes/SM_CarrierElevator.SM_CarrierElevator"));
	for (const FElevator& E : Elevators)
	{
		if (!E.bWorking) { continue; }
		Elevator->SetRelativeLocation(FVector((E.X0 + E.X1) * 50.0, (E.Y0 + E.Y1) * 50.0, DeckZ * 100.0));
		ElevatorBox->SetBoxExtent(FVector((E.X1 - E.X0) * 50.0, (E.Y1 - E.Y0) * 50.0, 45.0));
		ElevatorBox->SetRelativeLocation(FVector(0, 0, -45.0));
	}
	// jet blast deflectors (hinge at the aft edge, panel toward the jet)
	for (int32 K = 0; K < UE_ARRAY_COUNT(Cats); ++K)
	{
		const FCat& Ct = Cats[K];
		const double H = FMath::DegreesToRadians(Ct.HdgDeg);
		const FVector2D D(FMath::Cos(H), FMath::Sin(H));
		const FVector2D J = FVector2D(Ct.X, Ct.Y) - D * JbdGap;
		UStaticMeshComponent* C = AddMesh(*FString::Printf(TEXT("Jbd%d"), K + 1), TEXT("/Game/Carrier/Meshes/SM_CarrierJbd.SM_CarrierJbd"));
		C->SetRelativeLocationAndRotation(FVector(J.X * 100.0, J.Y * 100.0, DeckZ * 100.0 + 1.0), FRotator(0.0, Ct.HdgDeg, 0.0));
		Jbds.Add(C);
		JbdAng.Add(0.0);
		JbdTarget.Add(0.0);
	}
	// arresting wires: two halves each (bend towards an engaged hook)
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* WireMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carrier/Materials/M_Wire.M_Wire"));
	for (int32 W = 0; W < 4; ++W)
	{
		for (int32 H = 0; H < 2; ++H)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Wire%d_%d"), W + 1, H));
			C->SetupAttachment(Collision);
			C->SetMobility(EComponentMobility::Movable);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetStaticMesh(Cyl);
			if (WireMat) { C->SetMaterial(0, WireMat); }
			C->SetCastShadow(true);
			C->RegisterComponent();
			Wires.Add(C);
		}
		const FVector2D Mid = LandPoint(WiresS[W], 0.0);
		PlaceWire(W, FVector(Mid.X, Mid.Y, DeckZ + 0.1));
	}
	BuildLens();
	BuildLights();
	BuildParked();
	BuildWake();
	for (int32 K = 0; K < 3; ++K)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this, *FString::Printf(TEXT("HangarLamp%d"), K));
		L->SetupAttachment(Collision);
		L->SetRelativeLocation(FVector((-60.0 + K * 60.0) * 100.0, 150.0, (HangarCeil - 1.0) * 100.0));
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(9000.0f);
		L->SetAttenuationRadius(6500.0f);
		L->SetCastShadows(false);
		L->SetLightColor(FLinearColor(1.0f, 0.93f, 0.8f));
		L->SetVisibility(false);
		L->RegisterComponent();
		HangarLamps.Add(L);
	}
	Crew = GetWorld()->SpawnActor<ADeckCrew>();
	if (Crew) { Crew->Init(this); }
}

void ACarrierActor::PlaceWire(int32 W, const FVector& MidLocal)
{
	using namespace CarrierLayout;
	for (int32 H = 0; H < 2; ++H)
	{
		const FVector2D End2 = LandPoint(WiresS[W], H == 0 ? -WireHalf : WireHalf);
		const FVector End(End2.X, End2.Y, DeckZ + 0.1);
		const FVector D = MidLocal - End;
		const double Len = D.Size();
		UStaticMeshComponent* C = Wires[W * 2 + H];
		C->SetRelativeLocationAndRotation((End + D * 0.5) * 100.0, FRotationMatrix::MakeFromZ(D.GetSafeNormal()).Rotator());
		C->SetRelativeScale3D(FVector(0.036, 0.036, Len));
	}
}

void ACarrierActor::SetWirePull(int32 Wire, const FVector& HookLocal)
{
	using namespace CarrierLayout;
	for (int32 W = 0; W < 4; ++W)
	{
		const FVector2D Mid = LandPoint(WiresS[W], 0.0);
		PlaceWire(W, W == Wire ? FVector(HookLocal.X, HookLocal.Y, DeckZ + 0.12) : FVector(Mid.X, Mid.Y, DeckZ + 0.1));
	}
}

void ACarrierActor::SetJbd(int32 CatIndex, bool bUp)
{
	using namespace CarrierLayout;
	if (JbdTarget.IsValidIndex(CatIndex)) { JbdTarget[CatIndex] = bUp ? 52.0 : 0.0; }
}

void ACarrierActor::BuildLens()
{
	using namespace CarrierLayout;
	UStaticMesh* Sph = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* LM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carrier/Materials/M_CarrierLight.M_CarrierLight"));
	USceneComponent* Lens = NewObject<USceneComponent>(this, TEXT("Iflols"));
	Lens->SetupAttachment(Collision);
	Lens->SetMobility(EComponentMobility::Movable);
	// faces aft along the landing centreline
	Lens->SetRelativeLocationAndRotation(Iflols * 100.0, FRotator(0.0, LandHdgDeg + 180.0, 0.0));
	Lens->RegisterComponent();
	auto Add = [&](const FVector& Rel, double Size)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(Lens);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetStaticMesh(Sph);
		C->SetRelativeLocation(Rel * 100.0);
		C->SetRelativeScale3D(FVector(Size));
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(LM, this);
		C->SetMaterial(0, Mid);
		C->RegisterComponent();
		LensLights.Add(C);
		LensMids.Add(Mid);
	};
	for (int32 I = 0; I < Cv_LENS_CELLS; ++I) { Add(FVector(0.05, 0.0, (I - (Cv_LENS_CELLS - 1) * 0.5) * 0.2), 0.2); }   // 0..11 cells (bottom -> top)
	for (int32 S = -1; S <= 1; S += 2)
	{
		for (int32 I = 0; I < 5; ++I) { Add(FVector(0.05, S * (0.9 + I * 0.42), 0.0), 0.2); }                   // 12..21 datum
	}
	for (int32 S = -1; S <= 1; S += 2)
	{
		for (int32 I = 0; I < 3; ++I) { Add(FVector(0.05, S * 0.62, 0.35 - I * 0.35), 0.22); }                    // 22..27 wave-off
	}
	for (int32 S = -1; S <= 1; S += 2) { Add(FVector(0.05, S * 0.35, 1.45), 0.2); }                                 // 28..29 cut
}

void ACarrierActor::SetIflols(double BallDevDeg, bool bBall, bool bWaveOff, bool bCut)
{
	using namespace CarrierLayout;
	BallDev = BallDevDeg;
	bBallOn = bBall;
	bWaveOffOn = bWaveOff;
	bCutOn = bCut;
}

void ACarrierActor::UpdateLens(double Dt)
{
	using namespace CarrierLayout;
	LensTime += Dt;
	if (LensMids.Num() < 30) { return; }
	const int32 Cell = FMath::Clamp(FMath::RoundToInt((BallDev + Cv_LENS_RANGE) / (2.0 * Cv_LENS_RANGE) * (Cv_LENS_CELLS - 1)), 0, Cv_LENS_CELLS - 1);
	const bool bOnLens = bBallOn && FMath::Abs(BallDev) <= Cv_LENS_RANGE + 0.05;
	for (int32 I = 0; I < Cv_LENS_CELLS; ++I)
	{
		const bool bLit = bOnLens && I == Cell && !bWaveOffOn;
		LensMids[I]->SetVectorParameterValue(TEXT("Color"), I <= 1 ? Cv_RED : Cv_AMBER);
		LensMids[I]->SetScalarParameterValue(TEXT("Intensity"), bLit ? 60.0f : 0.02f);
	}
	for (int32 I = 12; I < 22; ++I)
	{
		LensMids[I]->SetVectorParameterValue(TEXT("Color"), Cv_GREEN);
		LensMids[I]->SetScalarParameterValue(TEXT("Intensity"), 40.0f);
	}
	const bool bFlash = bWaveOffOn && FMath::Fmod(LensTime, 0.5) < 0.3;
	for (int32 I = 22; I < 28; ++I)
	{
		LensMids[I]->SetVectorParameterValue(TEXT("Color"), Cv_RED);
		LensMids[I]->SetScalarParameterValue(TEXT("Intensity"), bFlash ? 90.0f : 0.02f);
	}
	for (int32 I = 28; I < 30; ++I)
	{
		LensMids[I]->SetVectorParameterValue(TEXT("Color"), Cv_GREEN);
		LensMids[I]->SetScalarParameterValue(TEXT("Intensity"), bCutOn ? 60.0f : 0.02f);
	}
	// readability: the whole lens grows with the viewer's distance (min. apparent size), staying above the deck
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager && LensLights.Num() > 0)
		{
			USceneComponent* Lens = LensLights[0]->GetAttachParent();
			const double Dist = FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Lens->GetComponentLocation()) * 0.01;
			const double F = FMath::Clamp(Dist / 250.0, 1.0, 5.0);
			Lens->SetRelativeScale3D(FVector(F));
			Lens->SetRelativeLocation(FVector(Iflols.X, Iflols.Y, FMath::Max(Iflols.Z, DeckZ + 1.35 * F)) * 100.0);
		}
	}
}

void ACarrierActor::BuildLights()
{
	using namespace CarrierLayout;
	UStaticMesh* Sph = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* LM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carrier/Materials/M_CarrierLight.M_CarrierLight"));
	DeckLights = NewObject<UInstancedStaticMeshComponent>(this, TEXT("DeckLights"));
	DeckLights->SetupAttachment(Collision);
	DeckLights->SetMobility(EComponentMobility::Movable);
	DeckLights->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DeckLights->SetCastShadow(false);
	DeckLights->SetStaticMesh(Sph);
	if (LM)
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(LM, this);
		Mid->SetVectorParameterValue(TEXT("Color"), Cv_WHITE);
		Mid->SetScalarParameterValue(TEXT("Intensity"), 6.0f);
		DeckLights->SetMaterial(0, Mid);
	}
	DeckLights->RegisterComponent();
	auto Add = [&](double X, double Y, double Z, double S) { DeckLights->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, Y, Z) * 100.0, FVector(S, S, S * 0.4))); };
	for (double S = 6.0; S < LandLen; S += 15.0)
	{
		const FVector2D P = LandPoint(S, 0.0);
		Add(P.X, P.Y, DeckZ, 0.18);
		for (int32 Sg = -1; Sg <= 1; Sg += 2)
		{
			const FVector2D E = LandPoint(S, Sg * LandWidth * 0.5);
			Add(E.X, E.Y, DeckZ, 0.16);
		}
	}
	for (int32 K = 0; K < 8; ++K)   // drop line down the stern under the landing centreline
	{
		Add(SternX - 0.6, LandR0.Y, DeckZ - 1.2 - K * 1.4, 0.3);
	}
}

void ACarrierActor::BuildParked()
{
	using namespace CarrierLayout;
	// parked F-35Cs: body + gear parts at the gear-down pose, one ISM per mesh (Nanite body)
	TArray<FTransform> Jets;
	for (const FVector& P : ParkedJets)
	{
		const double Z = DeckZ - CarrierF35Anim::MainWheelL.Z * 0.01;
		Jets.Add(FTransform(FRotator(0.0, P.Z, 0.0), FVector(P.X, P.Y, Z) * 100.0));
	}
	auto Make = [&](const FString& Mesh, const FTransform& Rel)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
		C->SetupAttachment(Collision);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->bReceivesDecals = false;
		if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Carrier/F35/%s.%s"), *Mesh, *Mesh)))
		{
			C->SetStaticMesh(M);
		}
		C->RegisterComponent();
		for (const FTransform& J : Jets) { C->AddInstance(Rel * J); }
		ParkedParts.Add(C);
	};
	Make(TEXT("SM_F35Body"), FTransform::Identity);
	for (int32 K = 0; K < CarrierF35Anim::NumParts; ++K)
	{
		const CarrierF35Anim::FKey& Kf = CarrierF35Anim::Keys[K][CarrierF35Anim::NumSamples - 1];
		const FMatrix M(FVector(Kf.X[0], Kf.X[1], Kf.X[2]), FVector(Kf.Y[0], Kf.Y[1], Kf.Y[2]), FVector(Kf.Z[0], Kf.Z[1], Kf.Z[2]), FVector(Kf.L[0], Kf.L[1], Kf.L[2]));
		Make(CarrierF35Anim::PartMeshes[K], FTransform(M));
	}
}

void ACarrierActor::BuildWake()
{
	using namespace CarrierLayout;
	UMaterialInterface* WM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carrier/Materials/M_CarrierWake.M_CarrierWake"));
	if (!WM) { return; }
	for (int32 K = 0; K < Cv_WAKE_SEGS + 2; ++K)
	{
		UDecalComponent* D = NewObject<UDecalComponent>(this, *FString::Printf(TEXT("Wake%d"), K));
		D->SetupAttachment(Collision);
		D->SetUsingAbsoluteLocation(true);
		D->SetUsingAbsoluteRotation(true);
		D->SetUsingAbsoluteScale(true);
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(WM, this);
		D->SetDecalMaterial(Mid);
		D->SortOrder = K;
		D->RegisterComponent();
		Wake.Add(D);
		WakeMids.Add(Mid);
	}
	UpdateWake();
}

void ACarrierActor::UpdateWake()
{
	using namespace CarrierLayout;
	if (Wake.Num() < Cv_WAKE_SEGS + 2) { return; }
	const double T = Clock();
	const FVector Up(0, 0, 1);
	for (int32 K = 0; K < Cv_WAKE_SEGS; ++K)
	{
		// stern positions of the recent past: the wake stays where the ship was
		const double DA = K * Cv_WAKE_LEN, DB = (K + 1) * Cv_WAKE_LEN;
		const FCarrierPose PA = Evaluate(T - DA / SpeedMs), PB = Evaluate(T - DB / SpeedMs);
		const FVector SA = PA.PosUE + PA.RotUE.GetForwardVector() * (SternX * 100.0);
		const FVector SB = PB.PosUE + PB.RotUE.GetForwardVector() * (SternX * 100.0);
		FVector Along = SA - SB; Along.Z = 0.0;
		const double Len = Along.Size();
		const FVector Mid = (SA + SB) * 0.5;
		const double Half = (18.0 + (DA + DB) * 0.5 * 0.05) * 100.0;
		Wake[K]->SetWorldLocationAndRotation(FVector(Mid.X, Mid.Y, 0.0), FRotationMatrix::MakeFromXZ(-Up, Along.GetSafeNormal()).Rotator());
		Wake[K]->DecalSize = FVector(450.0, Half, Len * 0.5 + 900.0);
		Wake[K]->MarkRenderStateDirty();
		WakeMids[K]->SetScalarParameterValue(TEXT("Strength"), float(FMath::Lerp(1.15, 0.12, double(K) / (Cv_WAKE_SEGS - 1))));
		WakeMids[K]->SetScalarParameterValue(TEXT("Spread"), float(0.2 + 0.6 * K / (Cv_WAKE_SEGS - 1.0)));
	}
	// bow waves: a foam band along each side of the forward hull, splaying out
	const FVector Fwd = Cur.RotUE.GetForwardVector();
	FVector F2(Fwd.X, Fwd.Y, 0.0); F2.Normalize();
	const FVector R2(-F2.Y, F2.X, 0.0);
	for (int32 S = 0; S < 2; ++S)
	{
		const double Sg = S == 0 ? -1.0 : 1.0;
		const FVector Dir = (F2 * FMath::Cos(FMath::DegreesToRadians(12.0)) - R2 * Sg * FMath::Sin(FMath::DegreesToRadians(12.0))).GetSafeNormal();
		const FVector C = Cur.PosUE + F2 * 9000.0 + R2 * Sg * 2600.0;
		Wake[Cv_WAKE_SEGS + S]->SetWorldLocationAndRotation(FVector(C.X, C.Y, 0.0), FRotationMatrix::MakeFromXZ(-Up, Dir).Rotator());
		Wake[Cv_WAKE_SEGS + S]->DecalSize = FVector(450.0, 700.0, 9000.0);
		Wake[Cv_WAKE_SEGS + S]->MarkRenderStateDirty();
		WakeMids[Cv_WAKE_SEGS + S]->SetScalarParameterValue(TEXT("Strength"), 0.75f);
		WakeMids[Cv_WAKE_SEGS + S]->SetScalarParameterValue(TEXT("Spread"), 0.3f);
	}
}

void ACarrierActor::UpdateDevices(double Dt)
{
	using namespace CarrierLayout;
	for (int32 K = 0; K < Jbds.Num(); ++K)
	{
		JbdAng[K] = OsrMath::MoveToward(JbdAng[K], JbdTarget[K], 22.0 * Dt);   // ~2.4 s to raise
		Jbds[K]->SetRelativeRotation(FRotator(JbdAng[K], Cats[K].HdgDeg, 0.0));
	}
	const double Target = bElevatorDown ? 1.0 : 0.0;
	ElevatorPos = OsrMath::MoveToward(ElevatorPos, Target, Dt / 11.0);
	FVector L = Elevator->GetRelativeLocation();
	L.Z = ElevatorZ() * 100.0;
	Elevator->SetRelativeLocation(L);
	// hangar lamps only when the camera is inside the hangar
	bool bIn = false;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			const FVector C = UEToLocal(PC->PlayerCameraManager->GetCameraLocation());
			bIn = C.Z > HangarFloor - 1.0 && C.Z < HangarCeil + 1.0 && C.X > CarrierLayout::Hangar.Min.X - 20.0 && C.X < CarrierLayout::Hangar.Max.X + 20.0 && C.Y > CarrierLayout::Hangar.Min.Y - 2.0 && C.Y < 40.0;
		}
	}
	for (UPointLightComponent* Lp : HangarLamps) { Lp->SetVisibility(bIn); }
}

void ACarrierActor::Tick(float DeltaSeconds)
{
	using namespace CarrierLayout;
	Super::Tick(DeltaSeconds);
	Cur = Evaluate(Clock());
	SetActorLocationAndRotation(Cur.PosUE, Cur.RotUE, false, nullptr, ETeleportType::None);
	UpdateDevices(DeltaSeconds);
	UpdateLens(DeltaSeconds);
	UpdateWake();
}
