// Guided missile flight + guidance + fuze.
#include "SamMissile.h"
#include "RadarSite.h"
#include "MissionCommon.h"
#include "MissionCombat.h"
#include "MissionFx.h"
#include "MissionState.h"
#include "OsrJetPawn.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"

using namespace OsrMission;

namespace
{
	constexpr double G0 = 9.81;
	double Sm(double A, double B, double X) { const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0); return T * T * (3.0 - 2.0 * T); }
}

AOsrMissile::AOsrMissile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	Body = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->bUseAsyncCooking = false;
}

void AOsrMissile::BeginPlay()
{
	Super::BeginPlay();
}

TArray<AOsrMissile*> AOsrMissile::HomingOn(UWorld* W, const AActor* Receiver)
{
	TArray<AOsrMissile*> Out;
	if (!W) { return Out; }
	for (TActorIterator<AOsrMissile> It(W); It; ++It)
	{
		if (It->bAlive && It->Target.Get() == Receiver) { Out.Add(*It); }
	}
	return Out;
}

void AOsrMissile::BuildBody(double Len, double Rad)
{
	// body along local +X (Godot), nose at +X; converted by the builder, then the actor is rotated to the velocity
	FMeshBuilder M;
	const FLinearColor White = Rgb(214, 214, 206), Grey = Rgb(120, 124, 118), Dark = Rgb(40, 40, 40);
	const FVector X(1, 0, 0);
	M.Cylinder(FVector(-Len * 0.5, 0, 0), X, Len * 0.78, Rad, Rad, 10, Kind == EOsrMissileKind::AAM_IR ? White : Grey, false);
	M.Cylinder(FVector(-Len * 0.5 + Len * 0.78, 0, 0), X, Len * 0.22, Rad, 0.0, 10, White, false);
	M.Cylinder(FVector(-Len * 0.5 - 0.05, 0, 0), X, 0.06, Rad * 0.8, Rad, 10, Dark, true);
	for (int32 K = 0; K < 4; ++K)
	{
		const double A = K * UE_DOUBLE_HALF_PI + UE_DOUBLE_PI * 0.25;
		const FVector Sp(0, FMath::Cos(A), FMath::Sin(A));
		const FVector Nrm = FVector::CrossProduct(X, Sp).GetSafeNormal();
		M.Box(FVector(-Len * 0.42, 0, 0) + Sp * (Rad + Rad * 1.6), X, Sp, Nrm, FVector(Len * 0.07, Rad * 1.6, 0.012), Grey);
		M.Box(FVector(Len * 0.18, 0, 0) + Sp * (Rad + Rad * 0.8), X, Sp, Nrm, FVector(Len * 0.04, Rad * 0.8, 0.01), Grey);
	}
	M.Commit(Body, 0, LitMaterial());
}

void AOsrMissile::Launch(EOsrMissileKind InKind, const FVector& InP, const FVector& Dir, double Speed, AActor* InTarget, AActor* InShooter, AOsrRadarSite* InSite)
{
	Kind = InKind;
	P = InP;
	V = Dir.GetSafeNormal() * Speed;
	Target = InTarget;
	Shooter = InShooter;
	Site = InSite;
	bAlive = true;
	Age = 0.0;
	LastAim = InTarget ? OsrCombat::JetPos(Cast<AOsrJetPawn>(InTarget)) : P + Dir * 5000.0;
	double Len = 5.6, Rad = 0.12;
	switch (Kind)
	{
	case EOsrMissileKind::S400_9M96:    Burn = 7.5; Accel = 165.0; DragK = 2.4e-5; MaxG = 22.0; Fuse = 12.0; Lethal = 9.0; MaxTime = 70.0; SeekerFov = 0.9; Len = 5.6; Rad = 0.12; break;
	case EOsrMissileKind::Pantsir_57E6: Burn = 2.4; Accel = 470.0; DragK = 3.4e-5; MaxG = 28.0; Fuse = 9.0; Lethal = 7.0; MaxTime = 26.0; SeekerFov = 0.0; Len = 3.2; Rad = 0.09; break;
	case EOsrMissileKind::Tor_9M331:    Burn = 4.0; Accel = 220.0; DragK = 4.0e-5; MaxG = 25.0; Fuse = 9.0; Lethal = 7.0; MaxTime = 25.0; SeekerFov = 0.0; Len = 2.9; Rad = 0.12; break;
	case EOsrMissileKind::AAM_IR:       Burn = 5.0; Accel = 150.0; DragK = 4.5e-5; MaxG = 32.0; Fuse = 7.0; Lethal = 6.0; MaxTime = 22.0; SeekerFov = 0.8; Len = 2.9; Rad = 0.085; break;
	}
	BuildBody(Len, Rad);
	if (AOsrMissionFx* Fx = AOsrMissionFx::Get(this))
	{
		Trail = Fx->BeginTrail(Kind == EOsrMissileKind::AAM_IR ? 7.0 : 14.0, 0.7, 7.0, FLinearColor(0.82f, 0.82f, 0.8f), Kind == EOsrMissileKind::AAM_IR ? 0.45 : 0.6, false);
	}
	SetActorLocationAndRotation(ToUE(P), FRotationMatrix::MakeFromX(DirToUE(V.GetSafeNormal())).ToQuat());
}

bool AOsrMissile::AimPoint(FVector& Aim)
{
	AOsrJetPawn* J = Cast<AOsrJetPawn>(Target.Get());
	const bool bTargetOk = OsrCombat::JetAlive(J);
	const FVector Tp = bTargetOk ? OsrCombat::JetPos(J) : LastAim;
	// decoyed seekers follow the decoy while it lives
	if (bDecoyed)
	{
		if (const AOsrMissionState* Ms = AOsrMissionState::Get(this))
		{
			for (const FOsrDecoy& D : Ms->Decoys) { if (D.Id == DecoyId) { Aim = D.P; return true; } }
		}
		return false;
	}
	if (!bTargetOk) { return false; }
	const FVector Rel = Tp - P;
	const double R = Rel.Size();
	const bool bInFov = SeekerFov > 0.0 && AngleBetween(V, Rel) < SeekerFov;
	switch (Kind)
	{
	case EOsrMissileKind::AAM_IR:
		if (bInFov && R < 10000.0 && LineOfSight(P, Tp, 1.0)) { Aim = Tp; return true; }
		return false;
	case EOsrMissileKind::S400_9M96:
		// active radar terminal inside 9 km with its own line of sight; command midcourse from the fire-control radar before
		if (R < 9000.0 && bInFov && LineOfSight(P, Tp, 2.0)) { Aim = Tp; return true; }
		[[fallthrough]];
	default:
	{
		const AOsrRadarSite* S = Site.Get();
		if (S && S->IsGuiding(J) && LineOfSight(S->Antenna, P, 2.0)) { Aim = Tp; return true; }
		return false;
	}
	}
}

void AOsrMissile::CheckDecoys()
{
	const AOsrMissionState* Ms = AOsrMissionState::Get(this);
	if (!Ms || bDecoyed) { return; }
	AOsrJetPawn* J = Cast<AOsrJetPawn>(Target.Get());
	if (!J) { return; }
	const bool bRadarSeeker = Kind == EOsrMissileKind::S400_9M96 && FVector::Dist(P, OsrCombat::JetPos(J)) < 9000.0;
	for (const FOsrDecoy& D : Ms->Decoys)
	{
		if (SeenDecoys.Contains(D.Id)) { continue; }
		SeenDecoys.Add(D.Id);
		if (D.Owner.Get() != J) { continue; }
		const bool bMatches = D.bFlare ? bIr() : bRadarSeeker;
		if (!bMatches) { continue; }
		if (AngleBetween(V, D.P - P) > FMath::Max(SeekerFov, 0.5)) { continue; }
		double Pd;
		if (D.bFlare)
		{
			// flares: harder with the afterburner lit, easier when the missile sees the jet's nose/beam (engine hidden)
			const FVector JetFwd = J->Model.Velocity.GetSafeNormal();
			const double Tail = FMath::Max(0.0, FVector::DotProduct(JetFwd, (J->Model.Position - P).GetSafeNormal()));
			Pd = 0.5 * (J->Model.Ab > 0.05 ? 0.55 : 1.0) * (1.0 - 0.35 * Tail);
		}
		else
		{
			const FVector Tv = OsrCombat::JetVel(J);
			const double Vr = FMath::Abs(FVector::DotProduct(Tv - V, (OsrCombat::JetPos(J) - P).GetSafeNormal()));
			Pd = 0.25 + 0.35 * (1.0 - Sm(200.0, 900.0, Vr));
		}
		if (FMath::FRand() < Pd)
		{
			bDecoyed = true;
			DecoyId = D.Id;
			UE_LOG(LogTemp, Display, TEXT("REPORT missile_decoyed kind=%d by=%s p=%.2f"), int32(Kind), D.bFlare ? TEXT("flare") : TEXT("chaff"), Pd);
			return;
		}
	}
}

void AOsrMissile::Detonate(bool bKill, bool bTerrain)
{
	if (!bAlive) { return; }
	bAlive = false;
	AOsrMissionFx* Fx = AOsrMissionFx::Get(this);
	if (Fx)
	{
		Fx->Explosion(P, bTerrain ? EOsrBlast::Small : EOsrBlast::Missile);
		Fx->EndTrail(Trail);
	}
	AOsrJetPawn* J = Cast<AOsrJetPawn>(Target.Get());
	AOsrMissionState* Ms = AOsrMissionState::Get(this);
	if (bKill && OsrCombat::JetAlive(J))
	{
		const TCHAR* Name = Kind == EOsrMissileKind::S400_9M96 ? TEXT("S-400 missile") : Kind == EOsrMissileKind::Pantsir_57E6 ? TEXT("Pantsir missile") : Kind == EOsrMissileKind::Tor_9M331 ? TEXT("Tor missile") : TEXT("Su-57 IR missile");
		UE_LOG(LogTemp, Display, TEXT("REPORT missile_kill kind=%d miss_m=%.1f"), int32(Kind), ClosestM);
		OsrCombat::KillJet(J, OsrCombat::JetPos(J), Name);
	}
	else if (OsrCombat::JetAlive(J) && Ms)
	{
		++Ms->Stats.SamsEvaded;
		UE_LOG(LogTemp, Display, TEXT("REPORT missile_evaded kind=%d closest_m=%.0f decoyed=%d age=%.1f"), int32(Kind), ClosestM, bDecoyed ? 1 : 0, Age);
	}
	SetActorHiddenInGame(true);
	SetLifeSpan(0.5);
}

void AOsrMissile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAlive) { return; }
	const double Dt = FMath::Min<double>(DeltaSeconds, 0.05);
	Age += Dt;
	AOsrMissionFx* Fx = AOsrMissionFx::Get(this);
	AOsrJetPawn* J = Cast<AOsrJetPawn>(Target.Get());
	// sub-step for accuracy at 1 km/s
	const int32 N = FMath::Clamp(int32(FMath::CeilToInt(V.Size() * Dt / 25.0)), 1, 8);
	const double H = Dt / N;
	const bool bColdLaunch = Kind == EOsrMissileKind::S400_9M96 || Kind == EOsrMissileKind::Tor_9M331;
	for (int32 S = 0; S < N && bAlive; ++S)
	{
		const double T = Age - Dt + H * (S + 1);
		const bool bIgnited = !bColdLaunch || T > 0.35;
		const bool bBurning = bIgnited && T < Burn + (bColdLaunch ? 0.35 : 0.0);
		const double Speed = FMath::Max(V.Size(), 1.0);
		const FVector Vh = V / Speed;
		CheckDecoys();
		FVector Aim = LastAim;
		const bool bGuide = T > (bColdLaunch ? 0.6 : 0.15) && AimPoint(Aim);
		FVector ALat = FVector::ZeroVector;
		if (bGuide)
		{
			LastAim = Aim;
			bGuided = true;
		}
		const FVector TgtV = bGuide && !bDecoyed && J ? OsrCombat::JetVel(J) : FVector::ZeroVector;
		if (T > (bColdLaunch ? 0.6 : 0.15))
		{
			const FVector R = (bGuide ? Aim : LastAim) - P;
			const double Rl = FMath::Max(R.Size(), 1.0);
			const FVector Vrel = TgtV - V;
			const FVector Om = FVector::CrossProduct(R, Vrel) / (Rl * Rl);
			const double Vc = FMath::Max(-FVector::DotProduct(R / Rl, Vrel), 150.0);
			ALat = 4.0 * Vc * FVector::CrossProduct(Om, R / Rl);
			// early pitch-over / heading capture: pure pursuit blend while the LOS rate term is small
			ALat += (R / Rl - Vh) * Speed * 1.2;
			ALat += FVector(0, G0, 0);                                             // gravity compensation
			ALat -= Vh * FVector::DotProduct(ALat, Vh);
			const double GAvail = MaxG * G0 * FMath::Min(1.0, FMath::Square(Speed / 350.0)) * (bColdLaunch && T < 1.2 ? 0.5 : 1.0);
			if (ALat.Size() > GAvail) { ALat = ALat.GetSafeNormal() * GAvail; }
		}
		const double Rho = FMath::Exp(-FMath::Max(P.Y, 0.0) / 8500.0);
		const FVector ADrag = -Vh * DragK * Rho * Speed * Speed;
		const FVector AThr = bBurning ? Vh * Accel : FVector::ZeroVector;
		V += (ALat + ADrag + AThr + FVector(0, -G0, 0)) * H;
		const FVector P0 = P;
		P += V * H;
		// terrain
		if (P.Y < Ground(P.X, P.Z)) { P.Y = Ground(P.X, P.Z); Detonate(false, true); break; }
		// proximity fuze vs the real target (and a decoy)
		if (OsrCombat::JetAlive(J))
		{
			const FVector Tp = OsrCombat::JetPos(J);
			const FVector D = P - P0;
			const double U = FMath::Clamp(FVector::DotProduct(Tp - P0, D) / FMath::Max(D.SizeSquared(), 1e-6), 0.0, 1.0);
			const double Dist = FVector::Dist(P0 + D * U, Tp);
			ClosestM = FMath::Min(ClosestM, Dist);
			if (Dist < Fuse && (Dist > PrevDist || Dist < Lethal * 0.4))
			{
				const double Pk = 1.0 - Sm(Lethal * 0.45, Lethal, Dist);
				P = P0 + D * U;
				Detonate(FMath::FRand() < Pk, false);
				break;
			}
			if (PrevDist < 400.0 && Dist > PrevDist + 1.0 && Dist > Fuse) { Detonate(false, false); break; }   // overshoot
			PrevDist = Dist;
		}
		if (bDecoyed && FVector::Dist(P, LastAim) < 12.0) { Detonate(false, false); break; }
	}
	if (!bAlive) { return; }
	const double Speed = V.Size();
	const bool bBurningNow = (!bColdLaunch || Age > 0.35) && Age < Burn + (bColdLaunch ? 0.35 : 0.0);
	if (Age > MaxTime || (Age > Burn + 2.0 && Speed < 180.0)) { Detonate(false, false); return; }
	SetActorLocationAndRotation(ToUE(P), FRotationMatrix::MakeFromX(DirToUE(V.GetSafeNormal())).ToQuat());
	if (Fx)
	{
		if (bBurningNow || (Kind == EOsrMissileKind::AAM_IR && Age < Burn + 1.0))
		{
			Fx->TrailPoint(Trail, P - V.GetSafeNormal() * 3.0);
			Fx->Puff(P - V.GetSafeNormal() * 2.4, -V * 0.05, 0.06, 0.5, 1.4, FLinearColor(1.0f, 0.8f, 0.5f) * 30.0f, 1.0, true, 0.0);
		}
		else if (Age < Burn + 1.2)
		{
			Fx->EndTrail(Trail);
		}
	}
}
