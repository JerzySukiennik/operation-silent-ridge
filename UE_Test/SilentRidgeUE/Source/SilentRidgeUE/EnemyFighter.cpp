// Enemy fighter flight + AI + mesh.
#include "EnemyFighter.h"
#include "SamMissile.h"
#include "MissionCommon.h"
#include "MissionFx.h"
#include "MissionState.h"
#include "OsrJetPawn.h"
#include "OsrWorldLayout.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"

using namespace OsrMission;

namespace
{
	constexpr double G0 = 9.81;
	constexpr double MASS = 25000.0, WING_S = 78.8, CD0 = 0.021, K_IND = 0.11, T_MIL = 176600.0, T_AB = 284400.0;
	constexpr double MIN_AGL = 140.0;
	double Sm(double A, double B, double X) { const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0); return T * T * (3.0 - 2.0 * T); }
}

const TCHAR* AOsrEnemyFighter::StateName(EOsrFighterState S)
{
	switch (S)
	{
	case EOsrFighterState::Parked: return TEXT("parked");
	case EOsrFighterState::Takeoff: return TEXT("takeoff");
	case EOsrFighterState::Climb: return TEXT("climb");
	case EOsrFighterState::Intercept: return TEXT("intercept");
	case EOsrFighterState::Attack: return TEXT("attack");
	case EOsrFighterState::Search: return TEXT("search");
	case EOsrFighterState::Rtb: return TEXT("rtb");
	default: return TEXT("wreck");
	}
}

AOsrEnemyFighter::AOsrEnemyFighter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Body = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->bUseAsyncCooking = false;
	Flame = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Flame"));
	Flame->SetupAttachment(Body);
	Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flame->SetCastShadow(false);
	bAirTarget = true;
	DisplayName = TEXT("SU-57");
	Health = 100.0;
}

// ---------------------------------------------------------------- mesh (Su-57-like, local x fwd, y up, z right, metres)

void AOsrEnemyFighter::BuildMesh()
{
	FMeshBuilder M, F;
	const FLinearColor Top = Rgb(104, 114, 124), Belly = Rgb(150, 158, 166), Dark = Rgb(34, 36, 40), Glass = Rgb(170, 130, 40), Noz = Rgb(60, 58, 56);
	struct FSt { double X, W, H, Yc; };
	const FSt St[] = {{10.0, 0.02, 0.02, 0.0}, {9.0, 0.34, 0.32, 0.0}, {7.6, 0.58, 0.55, 0.05}, {5.8, 0.8, 0.7, 0.12}, {4.0, 1.05, 0.78, 0.12}, {2.0, 1.7, 0.74, 0.08},
		{0.0, 2.3, 0.72, 0.05}, {-3.0, 2.45, 0.7, 0.0}, {-6.0, 2.3, 0.62, -0.02}, {-8.2, 1.95, 0.55, -0.05}, {-9.4, 1.7, 0.45, -0.05}};
	const int32 Seg = 14, Ns = UE_ARRAY_COUNT(St);
	auto Ring = [&](int32 I, int32 J) { const double A = UE_DOUBLE_TWO_PI * J / Seg; return FVector(St[I].X, St[I].Yc + St[I].H * FMath::Sin(A), St[I].W * FMath::Cos(A)); };
	for (int32 I = 0; I + 1 < Ns; ++I)
	{
		for (int32 J = 0; J < Seg; ++J)
		{
			const FVector A = Ring(I, J), B = Ring(I, J + 1), C = Ring(I + 1, J + 1), D = Ring(I + 1, J);
			const bool bUpper = FMath::Sin(UE_DOUBLE_TWO_PI * (J + 0.5) / Seg) > 0.0;
			M.Quad(A, B, C, D, bUpper ? Top : Belly);
		}
	}
	// flat plate with thickness from a planform (x,z) quad at height Y (both sides + mirrored)
	auto Plate = [&](FMeshBuilder& Mb, TArray<FVector> Q, double Th, const FLinearColor& C, bool bMirror)
	{
		for (int32 Side = 0; Side < (bMirror ? 2 : 1); ++Side)
		{
			TArray<FVector> Pts = Q;
			if (Side == 1) { for (FVector& X : Pts) { X.Z = -X.Z; } Swap(Pts[1], Pts[3]); }
			const FVector N = FVector::CrossProduct(Pts[1] - Pts[0], Pts[3] - Pts[0]).GetSafeNormal();
			const FVector O = N * (Th * 0.5);
			TArray<FVector> Tp, Bt;
			for (const FVector& X : Pts) { Tp.Add(X + O); Bt.Add(X - O); }
			Mb.Quad(Tp[0], Tp[1], Tp[2], Tp[3], C);
			Mb.Quad(Bt[0], Bt[3], Bt[2], Bt[1], C);
			for (int32 K = 0; K < 4; ++K)
			{
				const int32 L = (K + 1) % 4;
				Mb.Quad(Bt[K], Bt[L], Tp[L], Tp[K], C);
			}
		}
	};
	auto UpFacing = [](TArray<FVector> Q)
	{
		// order the planform so its normal points up (+y)
		if (FVector::CrossProduct(Q[1] - Q[0], Q[3] - Q[0]).Y < 0.0) { Swap(Q[1], Q[3]); }
		return Q;
	};
	// blended wing, LEVCON, stabilators
	Plate(M, UpFacing({FVector(1.8, 0.0, 1.9), FVector(-5.4, -0.05, 7.05), FVector(-6.8, -0.05, 7.05), FVector(-7.2, 0.0, 2.2)}), 0.22, Top, true);
	Plate(M, UpFacing({FVector(4.8, 0.05, 1.0), FVector(1.8, 0.02, 1.9), FVector(2.0, 0.02, 2.7), FVector(3.9, 0.04, 1.2)}), 0.12, Top, true);
	Plate(M, UpFacing({FVector(-6.9, -0.1, 2.2), FVector(-9.1, -0.12, 4.9), FVector(-10.1, -0.12, 4.9), FVector(-10.0, -0.1, 2.2)}), 0.14, Top, true);
	// canted all-moving fins (26 deg out)
	for (double Sd : {-1.0, 1.0})
	{
		const double Cant = FMath::DegreesToRadians(26.0);
		const FVector Span(0.0, FMath::Cos(Cant), Sd * FMath::Sin(Cant));
		const FVector Root(0.0, 0.5, Sd * 1.65);
		TArray<FVector> Q = {Root + FVector(-5.6, 0, 0), Root + FVector(-7.8, 0, 0) + Span * 3.1, Root + FVector(-9.9, 0, 0) + Span * 3.1, Root + FVector(-9.4, 0, 0)};
		// thin plate across the fin normal
		const FVector N = FVector::CrossProduct(Q[1] - Q[0], Q[3] - Q[0]).GetSafeNormal() * 0.07;
		M.Quad(Q[0] + N, Q[1] + N, Q[2] + N, Q[3] + N, Top);
		M.Quad(Q[0] - N, Q[3] - N, Q[2] - N, Q[1] - N, Top);
	}
	// engines + nozzles + intakes
	for (double Sd : {-1.0, 1.0})
	{
		M.Cylinder(FVector(-10.2, -0.38, Sd * 0.95), FVector(1, 0, 0), 12.2, 0.64, 0.66, 12, Belly, false);
		M.Cylinder(FVector(-10.9, -0.38, Sd * 0.95), FVector(1, 0, 0), 0.7, 0.52, 0.62, 12, Noz, false);
		M.Cylinder(FVector(-10.85, -0.38, Sd * 0.95), FVector(1, 0, 0), 0.05, 0.0, 0.5, 12, Dark, false);
		M.Box(FVector(3.1, -0.45, Sd * 1.25), FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector(1.1, 0.42, 0.42), Belly);
		M.Box(FVector(4.22, -0.45, Sd * 1.25), FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector(0.02, 0.36, 0.36), Dark);
		F.Cylinder(FVector(-10.9, -0.38, Sd * 0.95), FVector(-1, 0, 0), 5.0, 0.48, 0.1, 10, FLinearColor(1.0f, 0.55f, 0.25f) * 6.0f, false);
	}
	// canopy (gold-tinted like the real one)
	{
		const FSt Cn[] = {{6.9, 0.02, 0.02, 0.62}, {6.0, 0.42, 0.38, 0.72}, {4.6, 0.5, 0.48, 0.78}, {3.3, 0.36, 0.3, 0.78}, {2.6, 0.02, 0.02, 0.72}};
		for (int32 I = 0; I + 1 < 5; ++I)
		{
			for (int32 J = 0; J < 7; ++J)
			{
				auto R = [&](int32 Ii, int32 Jj) { const double A = UE_DOUBLE_PI * Jj / 7.0; return FVector(Cn[Ii].X, Cn[Ii].Yc + Cn[Ii].H * FMath::Sin(A), Cn[Ii].W * FMath::Cos(A)); };
				M.Quad(R(I, J), R(I, J + 1), R(I + 1, J + 1), R(I + 1, J), Glass);
			}
		}
	}
	// tiny red star-free national marking is omitted on purpose (fictional country)
	M.Commit(Body, 0, LitMaterial());
	F.Commit(Flame, 0, PuffMaterial(true));
	Flame->SetVisibility(false);
}

// ---------------------------------------------------------------- lifecycle

void AOsrEnemyFighter::Park(const FVector& At, double Yaw)
{
	ParkP = At;
	ParkYaw = Yaw;
	ResetFighter();
}

void AOsrEnemyFighter::ResetFighter()
{
	if (Body->GetNumSections() == 0) { BuildMesh(); }
	State = EOsrFighterState::Parked;
	P = ParkP;
	V = FVector::ZeroVector;
	Up = FVector(0, 1, 0);
	Health = 100.0;
	bDead = false;
	Aams = 2;
	GunRounds = 150;
	StateT = 0.0;
	SetActorHiddenInGame(false);
	Target.Reset();
	UpdateTransform();
}

void AOsrEnemyFighter::Scramble(const FVector& RunwayStart, const FVector& RunwayDir, double Delay)
{
	if (bDead || State != EOsrFighterState::Parked) { return; }
	RwyStart = RunwayStart;
	RwyDir = RunwayDir.GetSafeNormal();
	StartDelay = Delay;
	State = EOsrFighterState::Takeoff;
	StateT = 0.0;
	P = RwyStart;
	V = FVector::ZeroVector;
	UpdateTransform();
}

void AOsrEnemyFighter::UpdateTransform()
{
	FVector Fwd = V.SizeSquared() > 1.0 ? V.GetSafeNormal() : FVector(FMath::Cos(ParkYaw), 0.0, -FMath::Sin(ParkYaw));
	if (State == EOsrFighterState::Takeoff && V.Size() < 1.0) { Fwd = RwyDir; }
	FVector U = (Up - Fwd * FVector::DotProduct(Up, Fwd)).GetSafeNormal();
	if (U.SizeSquared() < 0.5) { U = FVector(0, 1, 0); }
	SetActorLocationAndRotation(ToUE(P), FRotationMatrix::MakeFromXZ(DirToUE(Fwd), DirToUE(U)).ToQuat());
	// hit volumes: fuselage + wing
	Boxes.SetNum(2);
	const FVector R = FVector::CrossProduct(Fwd, U).GetSafeNormal();
	Boxes[0].C = P; Boxes[0].Ax = Fwd; Boxes[0].Ay = U; Boxes[0].Az = R; Boxes[0].Half = FVector(10.0, 1.3, 2.4); Boxes[0].Name = TEXT("SU-57"); Boxes[0].Part = 0;
	Boxes[1].C = P - Fwd * 3.0; Boxes[1].Ax = Fwd; Boxes[1].Ay = U; Boxes[1].Az = R; Boxes[1].Half = FVector(3.5, 0.5, 7.0); Boxes[1].Name = TEXT("SU-57"); Boxes[1].Part = 1;
}

// ---------------------------------------------------------------- flight

void AOsrEnemyFighter::Fly(const FVector& DesiredDir, double DesiredSpeed, double Dt, double MaxG)
{
	const double S = FMath::Max(V.Size(), 30.0);
	const FVector Vh = V.SizeSquared() > 1.0 ? V / S : DesiredDir;
	const double Rho = 1.225 * FMath::Exp(-FMath::Max(P.Y, 0.0) / 8500.0);
	const double Q = 0.5 * Rho * S * S;
	const double ALatMax = FMath::Min(MaxG * G0, 1.35 * Q * WING_S / MASS);
	FVector D = DesiredDir.GetSafeNormal();
	FVector ALat = (D - Vh * FVector::DotProduct(D, Vh)) * S * 2.2;
	if (FVector::DotProduct(D, Vh) < 0.0) { ALat = ALat.GetSafeNormal() * ALatMax; }          // behind: max-rate turn
	ALat += FVector(0, G0, 0) - Vh * (G0 * Vh.Y);                                               // 1 g lift support
	if (ALat.Size() > ALatMax) { ALat = ALat.GetSafeNormal() * ALatMax; }
	const double Cl = ALat.Size() * MASS / FMath::Max(Q * WING_S, 1.0);
	const double Drag = Q * WING_S * (CD0 + K_IND * Cl * Cl);
	const double Thr = FMath::Clamp((DesiredSpeed - S) * 0.04 + 0.7, 0.2, 1.4);
	Ab = FMath::Clamp((Thr - 1.0) / 0.4, 0.0, 1.0);
	const double Lapse = FMath::Pow(Rho / 1.225, 0.7);
	const double Thrust = (Thr <= 1.0 ? Thr * T_MIL : T_MIL + Ab * (T_AB - T_MIL)) * Lapse;
	const FVector A = Vh * ((Thrust - Drag) / MASS) + ALat + FVector(0, -G0, 0);
	V += A * Dt;
	P += V * Dt;
	// lift direction = aircraft up (bank-to-turn)
	const FVector WantUp = ALat.SizeSquared() > 1.0 ? ALat.GetSafeNormal() : FVector(0, 1, 0);
	Up = FMath::Lerp(Up, WantUp, FMath::Clamp(Dt * 4.0, 0.0, 1.0)).GetSafeNormal();
}

bool AOsrEnemyFighter::Sense(AOsrJetPawn*& Out)
{
	Out = nullptr;
	double Best = 1e12;
	for (AOsrJetPawn* J : OsrCombat::Jets(GetWorld()))
	{
		if (!OsrCombat::JetAirborne(J)) { continue; }
		const FVector Tp = OsrCombat::JetPos(J);
		const double R = FVector::Dist(Tp, P);
		bool bKnown = false;
		bRadarTrack = false;
		const bool bLos = R < 30000.0 && LineOfSight(P, Tp, 3.0);
		// own AESA vs a stealthy target: ~26 km in the forward +-60 deg, broken briefly by chaff
		if (bLos && RadarBlind <= 0.0 && R < 26000.0 && AngleBetween(V, Tp - P) < FMath::DegreesToRadians(60.0)) { bKnown = true; bRadarTrack = true; }
		if (bLos && R < 6000.0) { bKnown = true; }            // visual / IRST
		if (!bKnown)
		{
			// GCI: any ground radar holding a track datalinks it
			for (AOsrRadarSite* S : AOsrRadarSite::All(GetWorld()))
			{
				const AOsrRadarSite::FTrack* T = S->TrackOf(J);
				if (T && T->Q >= 0.5 && !S->bDead) { bKnown = true; break; }
			}
		}
		if (bKnown && R < Best) { Best = R; Out = J; }
	}
	if (Out) { LastKnown = OsrCombat::JetPos(Out); LastKnownT = Now; Target = Out; }
	return Out != nullptr;
}

void AOsrEnemyFighter::Attack(AOsrJetPawn* J, double Dt)
{
	const FVector Tp = OsrCombat::JetPos(J), Tv = OsrCombat::JetVel(J);
	const FVector Rel = Tp - P;
	const double R = Rel.Size();
	const double Off = AngleBetween(V, Rel);
	// IR missile: rear-aspect range longer, afterburner makes the target hotter
	const double Tail = FMath::Max(0.0, FVector::DotProduct(Tv.GetSafeNormal(), Rel.GetSafeNormal()));
	const double MaxR = (4000.0 + 5000.0 * Tail) * (J->Model.Ab > 0.05 ? 1.3 : 1.0);
	if (Aams > 0 && Now - LastShot > 9.0 && R > 1000.0 && R < MaxR && Off < FMath::DegreesToRadians(25.0) && LineOfSight(P, Tp, 2.0) && OsrCombat::WeaponsFree(GetWorld()))
	{
		LastShot = Now;
		--Aams;
		const FVector Fwd = V.GetSafeNormal();
		FActorSpawnParameters Sp;
		Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AOsrMissile* M = GetWorld()->SpawnActor<AOsrMissile>(AOsrMissile::StaticClass(), FTransform(ToUE(P)), Sp))
		{
			M->Launch(EOsrMissileKind::AAM_IR, P + Fwd * 4.0 - Up * 1.2, Fwd, V.Size() + 15.0, J, this, nullptr);
			if (AOsrMissionState* Ms = AOsrMissionState::Get(this)) { ++Ms->Stats.AamsLaunched; }
			UE_LOG(LogTemp, Display, TEXT("REPORT aam_launch by=%s range_m=%.0f off_deg=%.0f"), *Callsign, R, FMath::RadiansToDegrees(Off));
		}
	}
	// gun: GSh-30-1, 1,800 rpm, 150 rds
	const double Tof = R / 860.0;
	const FVector Lead = Tp + (Tv - V) * Tof * 0.0 + Tv * Tof;
	const double AimErr = AngleBetween(V, Lead - P);
	if (GunRounds > 0 && R < 900.0 && AimErr < FMath::DegreesToRadians(2.5) && OsrCombat::WeaponsFree(GetWorld()))
	{
		GunAcc += Dt;
		AOsrMissionFx* Fx = AOsrMissionFx::Get(this);
		const FVector Fwd = V.GetSafeNormal();
		static double TrAcc = 0.0;
		TrAcc += Dt;
		while (TrAcc > 0.1)
		{
			TrAcc -= 0.1;
			GunRounds -= 3;
			if (Fx) { Fx->Tracer(P + Fwd * 8.0, V + (Fwd + FVector(FMath::FRandRange(-0.008, 0.008), FMath::FRandRange(-0.008, 0.008), FMath::FRandRange(-0.008, 0.008))) * 860.0, 1.5); }
		}
		if (GunAcc > 0.25)
		{
			GunAcc = 0.0;
			if (Fx) { Fx->SoundAt("gun_burst", P, -2.0, 0.7); }
			const double Pk = 0.22 * (1.0 - R / 900.0) * (1.0 - AimErr / FMath::DegreesToRadians(2.5));
			if (FMath::FRand() < Pk)
			{
				if (Fx) { Fx->Explosion(Tp, EOsrBlast::Aircraft); }
				OsrCombat::KillJet(J, Tp, TEXT("Su-57 gun"));
			}
		}
	}
}

void AOsrEnemyFighter::OnChaff(const AActor* ChaffFrom)
{
	if (Target.Get() == ChaffFrom && FMath::FRand() < 0.5) { RadarBlind = 2.5; }
}

void AOsrEnemyFighter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FMath::Min<double>(DeltaSeconds, 0.05);
	Now += Dt;
	StateT += Dt;
	RadarBlind = FMath::Max(0.0, RadarBlind - Dt);
	AOsrMissionFx* Fx = AOsrMissionFx::Get(this);
	switch (State)
	{
	case EOsrFighterState::Parked:
		return;
	case EOsrFighterState::Wreck:
	{
		V += FVector(0, -G0, 0) * Dt;
		V *= FMath::Exp(-0.05 * Dt);
		P += V * Dt;
		Up = FQuat(V.GetSafeNormal(), Dt * 4.0).RotateVector(Up);
		TrailAcc += Dt;
		if (Fx && TrailAcc > 0.04) { TrailAcc = 0.0; Fx->Puff(P, V * 0.1, 6.0, 3.0, 14.0, FLinearColor(0.06f, 0.06f, 0.06f), 0.8, false, 0.3, 1.0); Fx->Puff(P, FVector::ZeroVector, 0.3, 2.0, 5.0, FLinearColor(1.0f, 0.5f, 0.15f) * 5.0f, 0.9, true, 0.0); }
		if (P.Y < Ground(P.X, P.Z))
		{
			if (Fx) { Fx->Explosion(FVector(P.X, Ground(P.X, P.Z), P.Z), EOsrBlast::Aircraft); }
			SetActorHiddenInGame(true);
			State = EOsrFighterState::Parked;   // inert
			Boxes.Reset();
			return;
		}
		UpdateTransform();
		Boxes.Reset();
		return;
	}
	case EOsrFighterState::Takeoff:
	{
		if (StateT < StartDelay) { return; }
		const double S = V.Size();
		if (S < 88.0)
		{
			V = RwyDir * (S + 7.5 * Dt);
			P += V * Dt;
			P.Y = Ground(P.X, P.Z) + 2.3;
			Ab = 1.0;
		}
		else
		{
			const FVector Climb = (RwyDir + FVector(0, 0.25, 0)).GetSafeNormal();
			Fly(Climb, 230.0, Dt, 4.0);
			if (P.Y - Ground(P.X, P.Z) > 150.0) { State = EOsrFighterState::Climb; StateT = 0.0; }
		}
		break;
	}
	default:
	{
		AOsrJetPawn* J = nullptr;
		const bool bKnown = Sense(J);
		const bool bFeetWet = J && OsrCombat::JetPos(J).X < OsrWorld::Mouth.X - 4000.0;
		FVector Want = V.GetSafeNormal();
		double WantSpeed = 300.0;
		if (State == EOsrFighterState::Climb)
		{
			FVector H = Flat(LastKnown - P).GetSafeNormal();
			if (H.SizeSquared() < 0.5) { H = Flat(V).GetSafeNormal(); }
			Want = (H + FVector(0, 0.3, 0)).GetSafeNormal();
			WantSpeed = 280.0;
			if (P.Y > Ground(P.X, P.Z) + 900.0 || StateT > 35.0) { State = EOsrFighterState::Intercept; StateT = 0.0; }
		}
		else if (State == EOsrFighterState::Rtb)
		{
			Want = (ParkP + FVector(0, 1500, 0) - P).GetSafeNormal();
			WantSpeed = 250.0;
		}
		else if (bKnown && !bFeetWet && (Aams > 0 || GunRounds > 0))
		{
			const FVector Tp = OsrCombat::JetPos(J), Tv = OsrCombat::JetVel(J);
			const double R = FVector::Dist(Tp, P);
			const double Tlead = FMath::Clamp(R / 600.0, 0.0, 4.0);
			FVector Aim = Tp + Tv * Tlead * (R < 3000.0 ? 0.6 : 0.3);
			// stay above the terrain-masked canyon floor: aim at least MIN_AGL above the ground under the aim point
			Aim.Y = FMath::Max(Aim.Y, Ground(Aim.X, Aim.Z) + MIN_AGL);
			Want = (Aim - P).GetSafeNormal();
			WantSpeed = R > 8000.0 ? 380.0 : 300.0;
			State = R < 10000.0 ? EOsrFighterState::Attack : EOsrFighterState::Intercept;
			if (State == EOsrFighterState::Attack) { Attack(J, Dt); }
		}
		else
		{
			if (bFeetWet || (Aams <= 0 && GunRounds <= 0)) { State = EOsrFighterState::Rtb; StateT = 0.0; }
			else
			{
				State = EOsrFighterState::Search;
				// fly to the last known position, then orbit it
				FVector To = LastKnown + FVector(0, 800, 0) - P;
				if (Flat(To).Size() < 2500.0) { To = FVector::CrossProduct(Flat(To).GetSafeNormal(), FVector(0, 1, 0)); }
				Want = To.GetSafeNormal();
				WantSpeed = 260.0;
				if (Now - LastKnownT > 150.0) { State = EOsrFighterState::Rtb; }
			}
		}
		// terrain avoidance: predicted path must clear the ground by MIN_AGL for the next 6 s
		double Need = 0.0;
		for (int32 K = 1; K <= 6; ++K)
		{
			const FVector Q = P + V * double(K);
			const double Short = Ground(Q.X, Q.Z) + MIN_AGL - Q.Y;
			Need = FMath::Max(Need, Short / K);
		}
		if (Need > 0.0) { Want = (Want + FVector(0, FMath::Min(Need / 30.0, 3.0), 0)).GetSafeNormal(); }
		if (P.Y - Ground(P.X, P.Z) < MIN_AGL * 0.5) { Want = (Flat(Want).GetSafeNormal() + FVector(0, 1.2, 0)).GetSafeNormal(); }
		Fly(Want, WantSpeed, Dt, 9.0);
		if (P.Y < Ground(P.X, P.Z))
		{
			// flew into the ground (should be rare): crash
			bDead = true;
			if (Fx) { Fx->Explosion(P, EOsrBlast::Aircraft); }
			SetActorHiddenInGame(true);
			State = EOsrFighterState::Parked;
			Boxes.Reset();
			UE_LOG(LogTemp, Display, TEXT("REPORT fighter_crashed %s"), *Callsign);
			return;
		}
		break;
	}
	}
	UpdateTransform();
	Flame->SetVisibility(Ab > 0.1 && !bDead);
	if (Fx && Ab > 0.1)
	{
		const FVector Fwd = V.GetSafeNormal();
		const FVector Rt = FVector::CrossProduct(Fwd, Up).GetSafeNormal();
		for (double Sd : {-1.0, 1.0}) { Fx->Puff(P - Fwd * 11.5 + Rt * (Sd * 0.95) - Up * 0.38, V * 0.9, 0.05, 0.5, 0.9, FLinearColor(1.0f, 0.6f, 0.3f) * 8.0f, 0.8, true, 0.0); }
	}
}

void AOsrEnemyFighter::ApplyHit(const FVector& Hp, double Damage, EOsrDamage Kind, AActor* By, int32 Part)
{
	if (bDead) { return; }
	if (AOsrMissionFx* Fx = AOsrMissionFx::Get(this)) { Fx->Puff(Hp, V * 0.8, 1.2, 1.0, 5.0, FLinearColor(0.1f, 0.1f, 0.1f), 0.8, false, 0.5); }
	Super::ApplyHit(Hp, Damage, Kind, By, Part);
}

void AOsrEnemyFighter::OnKilled(AActor* By)
{
	if (AOsrMissionFx* Fx = AOsrMissionFx::Get(this)) { Fx->Explosion(P, EOsrBlast::Aircraft); }
	const bool bParked = State == EOsrFighterState::Parked;
	State = bParked ? EOsrFighterState::Parked : EOsrFighterState::Wreck;
	if (bParked) { SetActorHiddenInGame(true); Boxes.Reset(); }
	if (AOsrMissionState* Ms = AOsrMissionState::Get(this))
	{
		if (bParked) { ++Ms->Stats.GroundKills; } else { ++Ms->Stats.AirKills; }
		Ms->Radio(TEXT("WATCHTOWER"), bParked ? TEXT("Good hits, one Su-57 destroyed on the ground.") : TEXT("Splash one! Bandit destroyed."), NAME_None, 4.0);
	}
	UE_LOG(LogTemp, Display, TEXT("REPORT fighter_killed %s parked=%d"), *Callsign, bParked ? 1 : 0);
}

bool AOsrEnemyFighter::RwrFor(const AActor* Receiver, FOsrRwrContact& Out) const
{
	if (bDead || State == EOsrFighterState::Parked || State == EOsrFighterState::Takeoff || State == EOsrFighterState::Wreck) { return false; }
	const AOsrJetPawn* J = Cast<AOsrJetPawn>(Receiver);
	if (!J) { return false; }
	const FVector Tp = OsrCombat::JetPos(J);
	const double R = FVector::Dist(Tp, P);
	if (R > 45000.0 || AngleBetween(V, Tp - P) > FMath::DegreesToRadians(60.0) || !LineOfSight(P, Tp, 2.0)) { return false; }
	Out.Symbol = TEXT("57");
	Out.From = P;
	Out.State = (State == EOsrFighterState::Attack && bRadarTrack && Target.Get() == J) ? EOsrRwr::Track : EOsrRwr::Search;
	Out.Severity = FMath::Clamp(1.0 - R / 20000.0, 0.0, 1.0);
	return true;
}
