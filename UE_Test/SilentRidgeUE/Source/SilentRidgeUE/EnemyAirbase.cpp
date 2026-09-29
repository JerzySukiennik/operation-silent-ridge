// Enemy airbase geometry + scramble.
#include "EnemyAirbase.h"
#include "EnemyFighter.h"
#include "MissionCommon.h"
#include "MissionLayout.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"

using namespace OsrMission;

namespace
{
	FLinearColor NoSnow(FLinearColor C) { C.A = 0.05f; return C; }
}

AOsrEnemyAirbase::AOsrEnemyAirbase()
{
	Pmc = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Airbase"));
	SetRootComponent(Pmc);
	Pmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pmc->bUseAsyncCooking = false;
	DisplayName = TEXT("AIRBASE");
	Health = 1e9;
}

AOsrEnemyAirbase* AOsrEnemyAirbase::Find(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	for (TActorIterator<AOsrEnemyAirbase> It(W); It; ++It) { return *It; }
	return nullptr;
}

void AOsrEnemyAirbase::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

void AOsrEnemyAirbase::Build()
{
	const FVector2D C2 = OsrLayout::RunwayCentre;
	Centre = FVector(C2.X, Ground(C2.X, C2.Y), C2.Y);
	SetActorLocation(ToUE(Centre));
	const FVector D(FMath::Sin(OsrLayout::RunwayHeadingRad), 0.0, FMath::Cos(OsrLayout::RunwayHeadingRad));
	const FVector S(-D.Z, 0.0, D.X);
	const double Len = OsrLayout::RunwayLength, Hw = OsrLayout::RunwayWidth * 0.5;
	FMeshBuilder M;
	auto L = [this](const FVector& W) { return W - Centre; };
	const FLinearColor Asphalt = NoSnow(Rgb(56, 57, 60)), Paint = NoSnow(Rgb(225, 225, 220)), Concrete = Rgb(150, 148, 142), ConcreteNs = NoSnow(Rgb(120, 119, 115));

	// ---- runway profile: max ground across the width, max-filtered and smoothed along the length
	const int32 N = int32(Len / 20.0);
	TArray<double> Hraw, H;
	for (int32 I = 0; I <= N; ++I)
	{
		const FVector P = Centre + D * (-Len * 0.5 + I * 20.0);
		double Mx = -1e9;
		for (double O = -Hw - 8.0; O <= Hw + 8.0; O += 6.0) { const FVector Q = P + S * O; Mx = FMath::Max(Mx, Ground(Q.X, Q.Z)); }
		Hraw.Add(Mx);
	}
	H.SetNum(Hraw.Num());
	for (int32 I = 0; I <= N; ++I)
	{
		double Mx = -1e9;
		for (int32 K = FMath::Max(0, I - 3); K <= FMath::Min(N, I + 3); ++K) { Mx = FMath::Max(Mx, Hraw[K]); }
		H[I] = Mx;
	}
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		TArray<double> Sm = H;
		for (int32 I = 1; I < N; ++I) { Sm[I] = FMath::Max((H[I - 1] + 2.0 * H[I] + H[I + 1]) * 0.25, Hraw[I]); }
		H = Sm;
	}
	for (int32 I = 0; I < N; ++I)
	{
		const FVector A = Centre + D * (-Len * 0.5 + I * 20.0), B = Centre + D * (-Len * 0.5 + (I + 1) * 20.0);
		const FVector Ya(0, H[I] + 0.3, 0), Yb(0, H[I + 1] + 0.3, 0);
		auto Pt = [&](const FVector& Base, const FVector& Y, double O) { return L(FVector(Base.X, 0, Base.Z) + S * O + Y); };
		// surface (normal up), skirts down both sides
		const FVector Q0 = Pt(A, Ya, -Hw), Q1 = Pt(A, Ya, Hw), Q2 = Pt(B, Yb, Hw), Q3 = Pt(B, Yb, -Hw);
		if (FVector::CrossProduct(Q1 - Q0, Q3 - Q0).Y > 0.0) { M.Quad(Q0, Q1, Q2, Q3, Asphalt); } else { M.Quad(Q0, Q3, Q2, Q1, Asphalt); }
		const FVector Dn(0, -8.0, 0);
		for (double Sd : {-1.0, 1.0})
		{
			const FVector E0 = Pt(A, Ya, Sd * Hw), E1 = Pt(B, Yb, Sd * Hw);
			M.Quad(E0, E1, E1 + Dn, E0 + Dn, Concrete);
			M.Quad(E0, E0 + Dn, E1 + Dn, E1, Concrete);
		}
		// centreline dashes every 60 m, edge lights every 60 m
		const FVector Mid = (A + B) * 0.5;
		const double Hm = (H[I] + H[I + 1]) * 0.5 + 0.34;
		if (I % 3 == 0)
		{
			const FVector Pc = L(FVector(Mid.X, Hm, Mid.Z));
			M.Box(Pc, D, FVector(0, 1, 0), S, FVector(9.0, 0.03, 0.45), Paint, false);
			for (double Sd : {-1.0, 1.0}) { M.Box(Pc + S * (Sd * (Hw + 1.5)) + FVector(0, 0.3, 0), D, FVector(0, 1, 0), S, FVector(0.2, 0.3, 0.2), NoSnow(Rgb(240, 230, 180)), false); }
		}
	}
	// threshold bars + touchdown zones
	for (double End : {-1.0, 1.0})
	{
		const int32 I = End < 0 ? 2 : N - 2;
		const FVector P = Centre + D * (End * (Len * 0.5 - 40.0));
		for (int32 K = -6; K <= 6; ++K)
		{
			if (K == 0) { continue; }
			M.Box(L(FVector(P.X, H[I] + 0.34, P.Z) + S * (K * 3.2)), D, FVector(0, 1, 0), S, FVector(15.0, 0.03, 0.8), Paint, false);
		}
		for (int32 Tz = 1; Tz <= 3; ++Tz)
		{
			const FVector Q = P - D * (End * (120.0 + Tz * 150.0));
			const int32 J = FMath::Clamp(int32(((Q - Centre) | D) / 20.0 + N * 0.5), 0, N);
			for (double Sd : {-1.0, 1.0}) { M.Box(L(FVector(Q.X, H[J] + 0.34, Q.Z) + S * (Sd * 9.0)), D, FVector(0, 1, 0), S, FVector(11.0, 0.03, 1.4), Paint, false); }
		}
	}
	RwyStart = FVector(0, 0, 0) + Centre - D * (Len * 0.5 - 120.0);
	RwyStart.Y = H[6] + 2.3;
	RwyDir = D;

	// ---- flat pads helper (taxiway segments, apron)
	auto Pad = [&](const FVector& Ctr, const FVector& Ax, double HalfL, double HalfW, const FLinearColor& Col)
	{
		const FVector Az = FVector::CrossProduct(Ax, FVector(0, 1, 0)).GetSafeNormal();
		double Lo = 1e9, Hi = -1e9;
		for (int32 I = -3; I <= 3; ++I) { for (int32 J = -2; J <= 2; ++J) { const FVector Q = Ctr + Ax * (HalfL * I / 3.0) + Az * (HalfW * J / 2.0); const double G = Ground(Q.X, Q.Z); Lo = FMath::Min(Lo, G); Hi = FMath::Max(Hi, G); } }
		M.Box(L(FVector(Ctr.X, (Lo - 3.0 + Hi + 0.3) * 0.5, Ctr.Z)), Ax, FVector(0, 1, 0), Az, FVector(HalfL, (Hi + 0.3 - Lo + 3.0) * 0.5, HalfW), Col, false);
		return Hi + 0.3;
	};
	// parallel taxiway + three links
	for (int32 K = 0; K < 8; ++K)
	{
		const double A0 = -Len * 0.5 + 80.0 + K * (Len - 160.0) / 8.0;
		Pad(Centre + S * 170.0 + D * (A0 + (Len - 160.0) / 16.0), D, (Len - 160.0) / 16.0 + 2.0, 11.5, ConcreteNs);
	}
	for (double A : {-Len * 0.5 + 80.0, 0.0, Len * 0.5 - 80.0}) { Pad(Centre + S * 95.0 + D * A, S, 72.0, 11.5, ConcreteNs); }
	// apron
	const double ApronY = Pad(Centre + S * 300.0, D, 170.0, 70.0, ConcreteNs);
	// hardened aircraft shelters (arched, doors face the apron)
	const FVector Ax = S;
	const double ShelterYaw = FMath::Atan2(-Ax.Z, Ax.X);
	TArray<FVector> ShelterFront;
	for (int32 K = 0; K < 4; ++K)
	{
		const FVector P = Centre + S * 400.0 + D * ((K - 1.5) * 70.0);
		double Lo = 1e9;
		for (int32 I = -2; I <= 2; ++I) { const FVector Q = P + S * (I * 12.0); Lo = FMath::Min(Lo, Ground(Q.X, Q.Z)); }
		const double Y = FMath::Max(Lo, ApronY - 0.3);
		M.Box(L(FVector(P.X, Y - 1.0, P.Z)), S, FVector(0, 1, 0), D, FVector(20.0, 1.3, 16.0), Concrete);
		M.Arch(L(FVector(P.X, Y + 0.3, P.Z)), ShelterYaw, 14.0, 34.0, 0.62, 12, Concrete, true);
		M.Box(L(FVector(P.X, Y + 4.3, P.Z) - S * 17.05), S, FVector(0, 1, 0), D, FVector(0.1, 4.0, 11.0), NoSnow(Rgb(62, 66, 60)), false);
		FOsrHitBox B; B.C = FVector(P.X, Y + 4.0, P.Z); B.Ax = S; B.Ay = FVector(0, 1, 0); B.Az = D; B.Half = FVector(17.0, 4.5, 14.0); B.Name = TEXT("SHELTER"); B.Part = Boxes.Num();
		Boxes.Add(B);
		ShelterFront.Add(FVector(P.X, ApronY, P.Z) - S * 45.0);
	}
	// Quonset hangars + tower + fuel farm + radome on the far end
	for (int32 K = 0; K < 3; ++K)
	{
		const FVector P = Centre + S * 330.0 + D * (330.0 + K * 34.0);
		double Lo = 1e9, Hi = -1e9;
		for (int32 I = -2; I <= 2; ++I) { const FVector Q = P + S * (I * 10.0); const double G = Ground(Q.X, Q.Z); Lo = FMath::Min(Lo, G); Hi = FMath::Max(Hi, G); }
		M.Box(L(FVector(P.X, (Lo - 2.0 + Hi + 0.2) * 0.5, P.Z)), S, FVector(0, 1, 0), D, FVector(22.0, (Hi + 0.2 - Lo + 2.0) * 0.5, 11.0), ConcreteNs, false);
		M.Arch(L(FVector(P.X, Hi + 0.2, P.Z)), ShelterYaw, 10.0, 40.0, 0.85, 12, Rgb(136, 140, 138), true);
	}
	{
		const FVector P = Centre + S * 250.0 + D * (-240.0);
		const double G = Ground(P.X, P.Z);
		M.BoxYaw(L(FVector(P.X, G + 8.0, P.Z)), ShelterYaw, FVector(3.5, 10.0, 3.5), Rgb(170, 166, 150));
		M.BoxYaw(L(FVector(P.X, G + 19.5, P.Z)), ShelterYaw, FVector(4.5, 1.6, 4.5), NoSnow(Rgb(40, 60, 70)));
		M.BoxYaw(L(FVector(P.X, G + 21.3, P.Z)), ShelterYaw, FVector(5.0, 0.3, 5.0), Rgb(90, 90, 90));
		M.BoxYaw(L(FVector(P.X + 25.0, G + 4.0, P.Z + 10.0)), ShelterYaw, FVector(20.0, 4.5, 8.0), Rgb(160, 156, 140));
		M.Sphere(L(FVector(P.X + 25.0, G + 12.0, P.Z + 10.0)), 5.0, 12, Rgb(225, 225, 220));
	}
	for (int32 K = 0; K < 4; ++K)
	{
		const FVector P = Centre + S * (470.0 + (K % 2) * 22.0) + D * (-420.0 + (K / 2) * 22.0);
		const double G = Ground(P.X, P.Z);
		M.Cylinder(L(FVector(P.X, G - 1.0, P.Z)), FVector(0, 1, 0), 9.0, 7.0, 7.0, 18, Rgb(186, 188, 180));
	}
	M.Commit(Pmc, 0, LitMaterial());
	// the Su-57 pair parked in front of the two middle shelters
	if (Fighters.Num() == 0)
	{
		for (int32 K = 0; K < 2; ++K)
		{
			FActorSpawnParameters Sp;
			Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AOsrEnemyFighter* F = GetWorld()->SpawnActor<AOsrEnemyFighter>(AOsrEnemyFighter::StaticClass(), FTransform::Identity, Sp);
			if (!F) { continue; }
			F->Callsign = K == 0 ? TEXT("BANDIT 1") : TEXT("BANDIT 2");
			const FVector At = ShelterFront[K + 1] + FVector(0, 2.3, 0);
			// nose toward the runway (-S)
			const double Yaw = FMath::Atan2(S.Z, -S.X);
			F->Park(At, Yaw);
			Fighters.Add(F);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("OSR mission: airbase built (%d verts), runway %.0f m, profile %.0f..%.0f m"), M.Num(), Len, FMath::Min(H), FMath::Max(H));
}

void AOsrEnemyAirbase::Scramble()
{
	if (bScrambled) { return; }
	bScrambled = true;
	for (int32 K = 0; K < Fighters.Num(); ++K)
	{
		if (Fighters[K] && !Fighters[K]->bDead) { Fighters[K]->Scramble(RwyStart, RwyDir, 3.0 + K * 18.0); }
	}
	UE_LOG(LogTemp, Display, TEXT("REPORT scramble fighters=%d"), Fighters.Num());
}

void AOsrEnemyAirbase::ResetBase()
{
	bScrambled = false;
	for (AOsrEnemyFighter* F : Fighters) { if (F) { F->ResetFighter(); } }
}
