// Mission VFX/audio implementation. Sprites use M_MissionPuff* (soft round falloff from UV, noise, depth fade); vertex RGB carries the colour
// and vertex alpha the opacity, so one additive + one translucent section cover fire, smoke, dust, flares, trails and tracers.
#include "MissionFx.h"
#include "MissionCommon.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace OsrMission;

namespace
{
	constexpr int32 MAX_PUFFS = 1400;
	constexpr int32 NUM_LIGHTS = 4;
	double Sm(double A, double B, double X) { const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0); return T * T * (3.0 - 2.0 * T); }
}

AOsrMissionFx::AOsrMissionFx()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Pmc = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Fx"));
	SetRootComponent(Pmc);
	Pmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pmc->SetCastShadow(false);
	Pmc->bUseAsyncCooking = false;
	Pmc->SetUsingAbsoluteLocation(true);
	Pmc->SetUsingAbsoluteRotation(true);
	Rng.Initialize(4242);
}

AOsrMissionFx* AOsrMissionFx::Get(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	for (TActorIterator<AOsrMissionFx> It(W); It; ++It) { return *It; }
	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return W->SpawnActor<AOsrMissionFx>(AOsrMissionFx::StaticClass(), FTransform::Identity, Sp);
}

void AOsrMissionFx::BeginPlay()
{
	Super::BeginPlay();
	MatAdd = UMaterialInstanceDynamic::Create(PuffMaterial(true), this);
	MatTr = UMaterialInstanceDynamic::Create(PuffMaterial(false), this);
	for (UMaterialInstanceDynamic* M : {MatAdd.Get(), MatTr.Get()})
	{
		if (!M) { continue; }
		M->SetVectorParameterValue("Color", FLinearColor::White);
		M->SetScalarParameterValue("Intensity", 1.0f);
		M->SetScalarParameterValue("Opacity", 1.0f);
		M->SetScalarParameterValue("NoiseAmt", 0.45f);
		M->SetScalarParameterValue("Tiling", 1.0f);
		M->SetScalarParameterValue("Scroll", 0.15f);
		M->SetScalarParameterValue("EdgeSoft", 0.0f);
	}
	for (int32 K = 0; K < NUM_LIGHTS; ++K)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(Pmc);
		L->SetUsingAbsoluteLocation(true);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(0.0f);
		L->SetCastShadows(false);
		L->SetAttenuationRadius(60000.0f);
		L->SetVisibility(false);
		L->RegisterComponent();
		Lights.Add(L);
	}
	Atten = NewObject<USoundAttenuation>(this);
	Atten->Attenuation.bAttenuate = true;
	Atten->Attenuation.bSpatialize = true;
	Atten->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Atten->Attenuation.AttenuationShapeExtents = FVector(30000.0f, 0, 0);   // full volume within 300 m
	Atten->Attenuation.FalloffDistance = 900000.0f;                          // audible out to ~9 km
	Atten->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Atten->Attenuation.dBAttenuationAtMax = -48.0f;
	Atten->Attenuation.bAttenuateWithLPF = true;
	Atten->Attenuation.LPFRadiusMin = 50000.0f;
	Atten->Attenuation.LPFRadiusMax = 600000.0f;
	LoadSounds();
}

void AOsrMissionFx::LoadSounds()
{
	const TCHAR* Game[] = {TEXT("crash_explosion"), TEXT("flyby_close"), TEXT("sonic_boom"), TEXT("engine_far_loop")};
	for (const TCHAR* N : Game)
	{
		if (USoundBase* S = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/%s.%s"), N, N))) { Sounds.Add(FName(N), S); }
	}
	const TCHAR* Mission[] = {TEXT("expl_bomb"), TEXT("expl_far"), TEXT("sam_launch"), TEXT("gun_burst"), TEXT("bullet_impact"), TEXT("flare_pop"), TEXT("chaff_pop"),
		TEXT("bomb_release"), TEXT("secondary"), TEXT("jet_pass")};
	for (const TCHAR* N : Mission)
	{
		if (USoundBase* S = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Mission/Audio/%s.%s"), N, N))) { Sounds.Add(FName(N), S); }
	}
}

FVector AOsrMissionFx::CameraPosG() const
{
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->PlayerCameraManager) { return FromUE(PC->PlayerCameraManager->GetCameraLocation()); }
	}
	return FVector::ZeroVector;
}

// ---------------------------------------------------------------- emitters

void AOsrMissionFx::Puff(const FVector& P, const FVector& V, double Life, double R0, double R1, const FLinearColor& Col, double Alpha, bool bAdditive, double Drag, double Rise)
{
	if (Puffs.Num() >= MAX_PUFFS) { Puffs.RemoveAt(0, 64, EAllowShrinking::No); }
	FPuff F;
	F.P = P; F.V = V; F.Age = 0.0; F.Life = FMath::Max(Life, 0.05); F.R0 = R0; F.R1 = R1; F.Alpha = Alpha; F.Drag = Drag; F.Rise = Rise;
	F.C = Col; F.bAdd = bAdditive; F.Rot = Rng.FRandRange(0.0f, 6.283f);
	Puffs.Add(F);
}

void AOsrMissionFx::Flash(const FVector& P, double Peak, double Life, const FLinearColor& Col)
{
	if (Lights.Num() == 0) { return; }
	const int32 K = NextLight++ % Lights.Num();
	Lights[K]->SetWorldLocation(ToUE(P));
	Lights[K]->SetLightColor(Col);
	Lights[K]->SetVisibility(true);
	Flashes.RemoveAll([K](const FFlash& F) { return F.Light == K; });
	Flashes.Add({K, 0.0, Life, Peak});
}

void AOsrMissionFx::Explosion(const FVector& P, EOsrBlast Kind, const FVector& Normal)
{
	double Scale = 1.0;
	int32 Fire = 10, Smoke = 8, Sparks = 16;
	FName Snd = "expl_bomb";
	switch (Kind)
	{
	case EOsrBlast::Small:     Scale = 0.35; Fire = 5; Smoke = 4; Sparks = 8; Snd = "bullet_impact"; break;
	case EOsrBlast::Missile:   Scale = 0.6; Fire = 7; Smoke = 5; Sparks = 14; Snd = "expl_far"; break;
	case EOsrBlast::Aircraft:  Scale = 1.1; Fire = 12; Smoke = 10; Sparks = 24; Snd = "crash_explosion"; break;
	case EOsrBlast::Secondary: Scale = 1.6; Fire = 12; Smoke = 10; Sparks = 20; Snd = "secondary"; break;
	case EOsrBlast::Huge:      Scale = 3.2; Fire = 22; Smoke = 18; Sparks = 36; Snd = "secondary"; break;
	default: break;
	}
	const FVector Up = Normal.GetSafeNormal();
	const bool bSnowy = P.Y > 1300.0 && Kind != EOsrBlast::Missile && Kind != EOsrBlast::Aircraft;
	for (int32 K = 0; K < Fire; ++K)
	{
		const FVector D = (Up * 0.8 + FVector(Rng.FRandRange(-1, 1), Rng.FRandRange(-0.3, 1), Rng.FRandRange(-1, 1))).GetSafeNormal();
		const double Sp = Rng.FRandRange(8.0, 30.0) * Scale;
		const FLinearColor C = FMath::Lerp(FLinearColor(1.0f, 0.45f, 0.12f), FLinearColor(0.9f, 0.22f, 0.04f), Rng.FRand());
		Puff(P + D * 2.0 * Scale, D * Sp, Rng.FRandRange(0.7, 1.4) * FMath::Sqrt(Scale), 5.0 * Scale, Rng.FRandRange(14.0, 24.0) * Scale, C * 1.8f, 0.9, true, 2.5, 4.0);
	}
	// bright core flash
	Puff(P, FVector::ZeroVector, 0.18, 10.0 * Scale, 30.0 * Scale, FLinearColor(1.0f, 0.7f, 0.4f) * 4.0f, 1.0, true, 0.0);
	for (int32 K = 0; K < Smoke; ++K)
	{
		const FVector D = (Up + FVector(Rng.FRandRange(-0.7, 0.7), Rng.FRandRange(0.0, 0.8), Rng.FRandRange(-0.7, 0.7))).GetSafeNormal();
		const float G = Rng.FRandRange(0.05f, 0.12f);
		Puff(P + D * 4.0 * Scale, D * Rng.FRandRange(4.0, 14.0) * Scale, Rng.FRandRange(6.0, 12.0) * Scale, 8.0 * Scale, Rng.FRandRange(30.0, 55.0) * Scale,
			FLinearColor(G, G * 0.95f, G * 0.9f), 0.8, false, 0.6, 3.0 * Scale);
	}
	if (Kind == EOsrBlast::Bomb || Kind == EOsrBlast::Huge || Kind == EOsrBlast::Secondary)
	{
		// thrown snow/earth: pale, heavy, falls back
		for (int32 K = 0; K < 10; ++K)
		{
			const FVector D = (Up * 1.2 + FVector(Rng.FRandRange(-1, 1), 0.0, Rng.FRandRange(-1, 1))).GetSafeNormal();
			const float G = bSnowy ? 0.7f : 0.22f;
			Puff(P, D * Rng.FRandRange(15.0, 45.0) * FMath::Sqrt(Scale), Rng.FRandRange(2.0, 4.0), 3.0 * Scale, 14.0 * Scale, FLinearColor(G, G * 0.94f, G * 0.86f), 0.7, false, 1.2, -6.0);
		}
	}
	for (int32 K = 0; K < Sparks; ++K)
	{
		const FVector D = (Up * 0.6 + FVector(Rng.FRandRange(-1, 1), Rng.FRandRange(-0.2, 1), Rng.FRandRange(-1, 1))).GetSafeNormal();
		Puff(P, D * Rng.FRandRange(40.0, 110.0) * FMath::Sqrt(Scale), Rng.FRandRange(0.6, 1.6), 0.9 * Scale, 0.3, FLinearColor(1.0f, 0.5f, 0.15f) * 4.0f, 1.0, true, 0.4, -9.0);
	}
	Flash(P + Up * 6.0, 4.0e6 * Scale * Scale, 0.35 + 0.2 * Scale, FLinearColor(1.0f, 0.6f, 0.3f));
	SoundAt(Snd, P, Kind == EOsrBlast::Small ? -6.0 : 2.0);
	const double Dist = FVector::Dist(CameraPosG(), P);
	Shake = FMath::Max(Shake, FMath::Clamp(Scale * 900.0 / FMath::Max(Dist, 50.0), 0.0, 1.5));
}

void AOsrMissionFx::GroundImpact(const FVector& P, bool bSnow, double Scale)
{
	const float G = bSnow ? 0.82f : 0.32f;
	for (int32 K = 0; K < 3; ++K)
	{
		const FVector D = FVector(Rng.FRandRange(-0.5, 0.5), 1.0, Rng.FRandRange(-0.5, 0.5)).GetSafeNormal();
		Puff(P, D * Rng.FRandRange(6.0, 16.0) * Scale, Rng.FRandRange(0.8, 1.6), 0.8 * Scale, Rng.FRandRange(3.0, 5.0) * Scale, FLinearColor(G, G, G), 0.8, false, 1.5, -3.0);
	}
	Puff(P, FVector(0, 2, 0), 0.08, 0.6 * Scale, 2.0 * Scale, FLinearColor(1.0f, 0.7f, 0.35f) * 6.0f, 1.0, true, 0.0);
}

void AOsrMissionFx::Tracer(const FVector& P, const FVector& V, double Life)
{
	if (Tracers.Num() > 400) { Tracers.RemoveAt(0, 50, EAllowShrinking::No); }
	Tracers.Add({P, V, 0.0, Life});
}

int32 AOsrMissionFx::BeginTrail(double Life, double W0, double W1, const FLinearColor& Col, double Alpha, bool bAdditive)
{
	FTrail T;
	T.Id = NextTrail++;
	T.Life = Life; T.W0 = W0; T.W1 = W1; T.C = Col; T.Alpha = Alpha; T.bAdd = bAdditive; T.bOpen = true; T.LastT = -1.0;
	Trails.Add(T);
	return T.Id;
}

void AOsrMissionFx::TrailPoint(int32 Id, const FVector& P)
{
	for (FTrail& T : Trails)
	{
		if (T.Id != Id) { continue; }
		if (T.P.Num() > 0 && Now - T.LastT < 0.05 && FVector::DistSquared(T.P.Last(), P) < 400.0)
		{
			T.P.Last() = P;      // keep the head attached to the emitter between samples
			return;
		}
		T.P.Add(P);
		T.T.Add(Now);
		T.LastT = Now;
		if (T.P.Num() > 400) { T.P.RemoveAt(0); T.T.RemoveAt(0); }
		return;
	}
}

void AOsrMissionFx::EndTrail(int32 Id)
{
	for (FTrail& T : Trails) { if (T.Id == Id) { T.bOpen = false; } }
}

void AOsrMissionFx::SoundAt(FName Sound, const FVector& P, double VolDb, double Pitch, bool bDelay)
{
	const double D = FVector::Dist(CameraPosG(), P);
	const double Delay = bDelay ? D / 340.0 : 0.0;
	if (Delay < 0.03) { PlayNow(Sound, P, VolDb, Pitch); return; }
	Pending.Add({Sound, P, Delay, VolDb, Pitch});
}

void AOsrMissionFx::Sound2D(FName Sound, double VolDb, double Pitch)
{
	if (USoundBase* S = Sounds.FindRef(Sound))
	{
		UGameplayStatics::PlaySound2D(this, S, float(FMath::Pow(10.0, VolDb / 20.0)), float(Pitch));
	}
}

void AOsrMissionFx::PlayNow(FName Sound, const FVector& P, double Vol, double Pitch)
{
	USoundBase* S = Sounds.FindRef(Sound);
	if (!S && Sound == FName("expl_bomb")) { S = Sounds.FindRef("crash_explosion"); }
	if (!S && Sound == FName("secondary")) { S = Sounds.FindRef("crash_explosion"); }
	if (!S && Sound == FName("expl_far")) { S = Sounds.FindRef("crash_explosion"); }
	if (!S) { return; }
	UGameplayStatics::PlaySoundAtLocation(this, S, ToUE(P), FRotator::ZeroRotator, float(FMath::Pow(10.0, Vol / 20.0)), float(Pitch), 0.0f, Atten);
}

// ---------------------------------------------------------------- simulation + mesh

void AOsrMissionFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FMath::Min<double>(DeltaSeconds, 0.1);
	Now += Dt;
	Shake = FMath::Max(0.0, Shake - Dt * 1.5);
	for (FPuff& F : Puffs)
	{
		F.Age += Dt;
		F.V *= FMath::Exp(-F.Drag * Dt);
		F.V.Y += F.Rise * Dt;
		F.P += F.V * Dt;
	}
	Puffs.RemoveAll([](const FPuff& F) { return F.Age >= F.Life; });
	for (FTracer& T : Tracers)
	{
		T.Age += Dt;
		T.V.Y -= 9.81 * Dt;
		T.P += T.V * Dt;
	}
	Tracers.RemoveAll([](const FTracer& T) { return T.Age >= T.Life; });
	for (FTrail& T : Trails)
	{
		while (T.T.Num() > 0 && Now - T.T[0] > T.Life) { T.T.RemoveAt(0); T.P.RemoveAt(0); }
		// smoke drifts and rises a little
		for (FVector& P : T.P) { P += FVector(1.5, 0.6, 0.8) * Dt; }
	}
	Trails.RemoveAll([](const FTrail& T) { return !T.bOpen && T.P.Num() == 0; });
	for (FFlash& F : Flashes)
	{
		F.Age += Dt;
		const double U = F.Age / F.Life;
		Lights[F.Light]->SetIntensity(float(F.Peak * FMath::Pow(FMath::Max(0.0, 1.0 - U), 2.0)));
		if (U >= 1.0) { Lights[F.Light]->SetVisibility(false); }
	}
	Flashes.RemoveAll([](const FFlash& F) { return F.Age >= F.Life; });
	for (FPendingSound& S : Pending) { S.Delay -= Dt; }
	for (int32 K = Pending.Num() - 1; K >= 0; --K)
	{
		if (Pending[K].Delay <= 0.0)
		{
			PlayNow(Pending[K].Sound, Pending[K].P, Pending[K].Vol, Pending[K].Pitch);
			Pending.RemoveAt(K);
		}
	}
	Rebuild();
}

void AOsrMissionFx::Rebuild()
{
	const FVector Cam = CameraPosG();
	// the section vertices are in world UE cm (component at the origin): keep the component at the camera to preserve precision
	const FVector Anchor = Cam;
	Pmc->SetWorldLocation(ToUE(Anchor));
	FMeshBuilder B[2];
	auto Sprite = [&](FMeshBuilder& M, const FVector& P, double R, float Rot, const FLinearColor& C)
	{
		const FVector ToCam = (Cam - P).GetSafeNormal();
		FVector Right = FVector::CrossProduct(FVector(0, 1, 0), ToCam);
		if (Right.SizeSquared() < 1e-6) { Right = FVector(1, 0, 0); }
		Right = Right.GetSafeNormal();
		const FVector Up = FVector::CrossProduct(ToCam, Right);
		const FVector Ra = (Right * FMath::Cos(Rot) + Up * FMath::Sin(Rot)) * R;
		const FVector Ua = (Up * FMath::Cos(Rot) - Right * FMath::Sin(Rot)) * R;
		const FVector L = P - Anchor;
		const int32 K0 = M.Vert(L - Ra - Ua, ToCam, FVector2D(0, 0), C);
		const int32 K1 = M.Vert(L + Ra - Ua, ToCam, FVector2D(1, 0), C);
		const int32 K2 = M.Vert(L + Ra + Ua, ToCam, FVector2D(1, 1), C);
		const int32 K3 = M.Vert(L - Ra + Ua, ToCam, FVector2D(0, 1), C);
		M.Tri(K0, K1, K2); M.Tri(K0, K2, K3);
		M.Tri(K0, K2, K1); M.Tri(K0, K3, K2);     // two-sided without relying on the material flag
	};
	for (const FPuff& F : Puffs)
	{
		const double U = F.Age / F.Life;
		const double R = FMath::Lerp(F.R0, F.R1, FMath::Pow(U, 0.5));
		const double A = F.Alpha * Sm(0.0, 0.06, U) * (1.0 - Sm(F.bAdd ? 0.3 : 0.45, 1.0, U));
		if (A < 0.004) { continue; }
		FLinearColor C = F.C;
		if (F.bAdd) { C = C * float(1.0 - 0.7 * U); }     // fire cools
		C.A = float(A);
		Sprite(B[F.bAdd ? 1 : 0], F.P, R, F.Rot + float(U * 0.6), C);
	}
	// ribbons (camera-facing strips); UV.x across (soft edges), UV.y = 0.5 so the radial falloff is only across
	for (const FTrail& T : Trails)
	{
		const int32 Np = T.P.Num();
		if (Np < 2) { continue; }
		FMeshBuilder& M = B[T.bAdd ? 1 : 0];
		const int32 Base = M.Num();
		for (int32 I = 0; I < Np; ++I)
		{
			const FVector P = T.P[I];
			const FVector Seg = (T.P[FMath::Min(I + 1, Np - 1)] - T.P[FMath::Max(I - 1, 0)]).GetSafeNormal();
			const FVector ToCam = (Cam - P).GetSafeNormal();
			FVector Side = FVector::CrossProduct(Seg, ToCam);
			Side = Side.SizeSquared() > 1e-8 ? Side.GetSafeNormal() : FVector(1, 0, 0);
			const double Age = FMath::Clamp((Now - T.T[I]) / T.Life, 0.0, 1.0);
			const double W = FMath::Lerp(T.W0, T.W1, FMath::Pow(Age, 0.6));
			const double HeadFade = I == Np - 1 && T.bOpen ? 0.0 : 1.0;
			FLinearColor C = T.C;
			C.A = float(T.Alpha * (1.0 - Age) * (1.0 - Age) * HeadFade);
			M.Vert(P + Side * W - Anchor, ToCam, FVector2D(0, 0.5), C);
			M.Vert(P - Side * W - Anchor, ToCam, FVector2D(1, 0.5), C);
		}
		for (int32 I = 0; I + 1 < Np; ++I)
		{
			const int32 A = Base + I * 2;
			M.Tri(A, A + 1, A + 3); M.Tri(A, A + 3, A + 2);
			M.Tri(A, A + 3, A + 1); M.Tri(A, A + 2, A + 3);
		}
	}
	// tracers: stretched soft quads along the velocity
	for (const FTracer& T : Tracers)
	{
		const FVector Dir = T.V.GetSafeNormal();
		const double Len = FMath::Min(T.V.Size() * 0.022, 30.0);
		const FVector ToCam = (Cam - T.P).GetSafeNormal();
		const FVector Side = FVector::CrossProduct(Dir, ToCam).GetSafeNormal() * 0.35;
		const FVector A0 = T.P - Dir * Len - Anchor, A1 = T.P - Anchor;
		const FLinearColor C = FLinearColor(1.0f, 0.75f, 0.4f, 1.0f) * 10.0f;
		FLinearColor Ca = C; Ca.A = float(1.0 - Sm(0.8, 1.0, T.Age / T.Life));
		FMeshBuilder& M = B[1];
		const int32 K0 = M.Vert(A0 - Side, ToCam, FVector2D(0, 0.1), Ca);
		const int32 K1 = M.Vert(A0 + Side, ToCam, FVector2D(1, 0.1), Ca);
		const int32 K2 = M.Vert(A1 + Side, ToCam, FVector2D(1, 0.9), Ca);
		const int32 K3 = M.Vert(A1 - Side, ToCam, FVector2D(0, 0.9), Ca);
		M.Tri(K0, K1, K2); M.Tri(K0, K2, K3);
		M.Tri(K0, K2, K1); M.Tri(K0, K3, K2);
	}
	// the builder converts local metres to UE cm (relative to the component placed at the camera)
	for (int32 S = 0; S < 2; ++S)
	{
		if (B[S].Num() > 0)
		{
			B[S].Commit(Pmc, S, S == 1 ? static_cast<UMaterialInterface*>(MatAdd) : static_cast<UMaterialInterface*>(MatTr));
			Pmc->SetMeshSectionVisible(S, true);
		}
		else if (Pmc->GetNumSections() > S)
		{
			Pmc->SetMeshSectionVisible(S, false);
		}
	}
}
