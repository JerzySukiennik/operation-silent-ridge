// Laser-guided bomb flight, seeker, guidance and impact.
#include "WeaponBomb.h"
#include "MissionCommon.h"
#include "MissionCombat.h"
#include "MissionFx.h"
#include "MissionState.h"
#include "MissionTarget.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"

using namespace OsrMission;

namespace { constexpr double G0 = 9.81; }

AOsrLaserBomb::AOsrLaserBomb()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	Body = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->bUseAsyncCooking = false;
}

TArray<AOsrLaserBomb*> AOsrLaserBomb::InFlight(UWorld* W, const AActor* Owner)
{
	TArray<AOsrLaserBomb*> Out;
	if (!W) { return Out; }
	for (TActorIterator<AOsrLaserBomb> It(W); It; ++It)
	{
		if (It->bAlive && (!Owner || It->OwnerJet.Get() == Owner)) { Out.Add(*It); }
	}
	return Out;
}

void AOsrLaserBomb::Step(FVector& P, FVector& V, const FVector* Spot, double Dt)
{
	const double Speed = FMath::Max(V.Size(), 1.0);
	const FVector Vh = V / Speed;
	const double Rf = FMath::Exp(-FMath::Max(P.Y, 0.0) / 8500.0);
	FVector ALat = FVector::ZeroVector;
	if (Spot)
	{
		const FVector R = *Spot - P;
		const double Rl = FMath::Max(R.Size(), 1.0);
		const FVector Rh = R / Rl;
		const FVector Om = FVector::CrossProduct(R, -V) / (Rl * Rl);
		const double Vc = FMath::Max(FVector::DotProduct(Rh, V), 50.0);
		ALat = NAV_N * Vc * FVector::CrossProduct(Om, Rh);
		ALat += FVector(0, G0 * 0.9, 0);                         // gravity bias (glide)
		ALat -= Vh * FVector::DotProduct(ALat, Vh);
		const double Q = 0.5 * 1.225 * Rf * Speed * Speed;
		const double AMax = FMath::Min(44.0, 1.045e-3 * Q);
		if (ALat.Size() > AMax) { ALat = ALat.GetSafeNormal() * AMax; }
	}
	const FVector ADrag = -Vh * (DRAG_K * Rf * Speed * Speed + ALat.Size() * 0.16);   // parasite + induced (L/D ~ 6)
	V += (ALat + ADrag + FVector(0, -G0, 0)) * Dt;
	P += V * Dt;
}

FOsrBombSim AOsrLaserBomb::Simulate(const FVector& InP, const FVector& InV, const FVector& Aim, bool bGuided, double MaxT)
{
	FOsrBombSim S;
	FVector P = InP, V = InV;
	const double Dt = 0.05;
	double T = 0.0;
	while (T < MaxT)
	{
		const bool bSee = bGuided && AngleBetween(V, Aim - P) < SEEKER_FOV;
		const FVector P0 = P;
		Step(P, V, bSee ? &Aim : nullptr, Dt);
		T += Dt;
		const double G = Ground(P.X, P.Z);
		// the aim point may sit on a structure above the terrain (vent collar): stop at its height near the aim
		const double Floor = FVector2D(P.X - Aim.X, P.Z - Aim.Z).Size() < 30.0 ? FMath::Max(G, Aim.Y) : G;
		if (P.Y <= Floor)
		{
			const double F = (P0.Y - Floor) / FMath::Max(P0.Y - P.Y, 1e-6);
			S.Impact = FMath::Lerp(P0, P, FMath::Clamp(F, 0.0, 1.0));
			S.bImpact = true;
			S.Tof = T - Dt * (1.0 - F);
			S.Miss = FVector2D(S.Impact.X - Aim.X, S.Impact.Z - Aim.Z).Size();
			S.ImpactAngleDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(-V.GetSafeNormal().Y, -1.0, 1.0)));
			return S;
		}
	}
	S.Tof = MaxT;
	return S;
}

void AOsrLaserBomb::Release(const FVector& InP, const FVector& InV, int32 InCode, AActor* InOwner, const FVector& InPlannedAim)
{
	P = InP;
	V = InV;
	Code = InCode;
	OwnerJet = InOwner;
	PlannedAim = InPlannedAim;
	bAlive = true;
	Age = 0.0;
	// GBU-12-class body: olive drab Mk 82 with a yellow HE band, seeker nose, canards, pop-out tail wings (local +X = nose)
	FMeshBuilder M;
	const FLinearColor Od = Rgb(92, 96, 70), Yel = Rgb(200, 170, 40), Grey = Rgb(150, 150, 146), Glass = Rgb(40, 60, 80);
	const FVector X(1, 0, 0);
	M.Cylinder(FVector(-1.5, 0, 0), X, 2.2, 0.14, 0.137, 12, Od, true);
	M.Cylinder(FVector(0.35, 0, 0), X, 0.1, 0.1375, 0.1375, 12, Yel, false);
	M.Cylinder(FVector(0.7, 0, 0), X, 0.45, 0.137, 0.09, 12, Od, false);
	M.Cylinder(FVector(1.15, 0, 0), X, 0.5, 0.09, 0.07, 10, Grey, false);
	M.Cylinder(FVector(1.65, 0, 0), X, 0.18, 0.07, 0.0, 10, Glass, false);
	for (int32 K = 0; K < 4; ++K)
	{
		const double A = K * UE_DOUBLE_HALF_PI + UE_DOUBLE_PI * 0.25;
		const FVector Sp(0, FMath::Cos(A), FMath::Sin(A));
		const FVector Nrm = FVector::CrossProduct(X, Sp).GetSafeNormal();
		M.Box(FVector(1.35, 0, 0) + Sp * 0.14, X, Sp, Nrm, FVector(0.1, 0.07, 0.008), Grey);
		M.Box(FVector(-1.35, 0, 0) + Sp * 0.33, X, Sp, Nrm, FVector(0.2, 0.2, 0.01), Grey);
	}
	M.Commit(Body, 0, LitMaterial());
	SetActorLocationAndRotation(ToUE(P), FRotationMatrix::MakeFromX(DirToUE(V.GetSafeNormal())).ToQuat());
}

void AOsrLaserBomb::Impact(const FVector& At, bool bShaft, const FString& What)
{
	if (!bAlive) { return; }
	bAlive = false;
	const FVector Dir = V.GetSafeNormal();
	AOsrMissionTarget* Tg = AOsrMissionTarget::Find(this);
	const double Miss = Tg ? FVector2D(At.X - Tg->VentCentre.X, At.Z - Tg->VentCentre.Z).Size() : -1.0;
	const double Angle = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(-Dir.Y, -1.0, 1.0)));
	if (AOsrMissionFx* Fx = AOsrMissionFx::Get(this)) { Fx->Explosion(At, EOsrBlast::Bomb); }
	OsrCombat::Blast(GetWorld(), At, 35.0, 450.0, OwnerJet.Get());
	UE_LOG(LogTemp, Display, TEXT("REPORT bomb_impact what=%s shaft=%d miss_to_shaft_m=%.1f impact_angle_deg=%.0f tof_s=%.1f speed_ms=%.0f seeker=%d code=%d"),
		*What, bShaft ? 1 : 0, Miss, Angle, Age, V.Size(), bSeeking ? 1 : 0, Code);
	if (bShaft && Tg && !Tg->bDestroyed)
	{
		Tg->bDestroyed = true;
		if (AOsrMissionState* Ms = AOsrMissionState::Get(this)) { Ms->NotifyTargetDestroyed(); }
	}
	if (AOsrMissionState* Ms = AOsrMissionState::Get(this)) { Ms->NotifyBombImpact(bShaft, Miss, What); }
	SetActorHiddenInGame(true);
	SetLifeSpan(0.5);
}

void AOsrLaserBomb::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAlive) { return; }
	const double Dt = FMath::Min<double>(DeltaSeconds, 0.05);
	Age += Dt;
	const AOsrMissionState* Ms = AOsrMissionState::Get(this);
	AOsrMissionTarget* Tg = AOsrMissionTarget::Find(this);
	FVector Spot;
	bSeeking = Ms && Age > 0.4 && Ms->FindLaserSpot(Code, P, V.GetSafeNormal(), SEEKER_FOV, Spot);
	const int32 N = FMath::Clamp(int32(FMath::CeilToInt(V.Size() * Dt / 10.0)), 1, 6);
	const double H = Dt / N;
	for (int32 K = 0; K < N && bAlive; ++K)
	{
		const FVector P0 = P;
		Step(P, V, bSeeking ? &Spot : nullptr, H);
		FVector Hit;
		FString What;
		if (Tg && Tg->BombArrived(P0, P, Hit, What)) { P = Hit; Impact(Hit, true, What); break; }
		const OsrCombat::FTraceResult R = OsrCombat::Trace(GetWorld(), P0, P, OwnerJet.Get(), false);
		if (R.bHit)
		{
			FString Name = R.bTerrain ? FString(TEXT("GROUND")) : (R.Actor ? (R.Actor->Boxes.IsValidIndex(R.Part) ? R.Actor->Boxes[R.Part].Name : R.Actor->DisplayName) : FString(TEXT("?")));
			P = R.P;
			Impact(R.P, false, Name);
			break;
		}
	}
	if (!bAlive) { return; }
	PredictAcc += Dt;
	if (PredictAcc > 0.25)
	{
		PredictAcc = 0.0;
		const FOsrBombSim S = Simulate(P, V, bSeeking ? Spot : PlannedAim, true, 120.0);
		Tti = S.Tof;
	}
	else
	{
		Tti = FMath::Max(0.0, Tti - Dt);
	}
	SetActorLocationAndRotation(ToUE(P), FRotationMatrix::MakeFromX(DirToUE(V.GetSafeNormal())).ToQuat());
}
