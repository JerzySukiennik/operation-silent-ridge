// Target plant geometry (procedural, vertex-coloured, snow on top faces via M_MissionLit; vertex alpha = snow mask) + kill logic.
#include "MissionTarget.h"
#include "MissionCommon.h"
#include "MissionLayout.h"
#include "MissionFx.h"
#include "MissionState.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"

using namespace OsrMission;

namespace
{
	const FLinearColor CONCRETE = Rgb(122, 119, 111);
	const FLinearColor CONCRETE_DK = Rgb(88, 86, 80);
	const FLinearColor STEEL = Rgb(74, 82, 74);
	const FLinearColor DARK = Rgb(14, 15, 16);
	const FLinearColor WINDOW = Rgb(26, 32, 38);
	const FLinearColor ROOF = Rgb(64, 70, 62);
	const FLinearColor RUST = Rgb(120, 72, 48);
	const FLinearColor RED = Rgb(170, 40, 32);
	const FLinearColor WHITE = Rgb(220, 220, 214);
	FLinearColor NoSnow(FLinearColor C) { C.A = 0.05f; return C; }
	FVector YawX(double Yaw) { return FVector(FMath::Cos(Yaw), 0.0, -FMath::Sin(Yaw)); }
	FVector YawZ(double Yaw) { return FVector(FMath::Sin(Yaw), 0.0, FMath::Cos(Yaw)); }
}

AOsrMissionTarget::AOsrMissionTarget()
{
	Pmc = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Plant"));
	SetRootComponent(Pmc);
	Pmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pmc->bUseAsyncCooking = false;
	Doors = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BlastDoors"));
	Doors->SetupAttachment(Pmc);
	Doors->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayName = TEXT("PLANT");
	Health = 1e9;
}

AOsrMissionTarget* AOsrMissionTarget::Find(const UObject* WorldContext)
{
	UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	for (TActorIterator<AOsrMissionTarget> It(W); It; ++It) { return *It; }
	return nullptr;
}

void AOsrMissionTarget::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

void AOsrMissionTarget::Build()
{
	const FVector2D Pc = OsrLayout::PlantCentre;
	Origin = FVector(Pc.X, Ground(Pc.X, Pc.Y), Pc.Y);
	SetActorLocation(ToUE(Origin));
	Boxes.Reset();
	TrackPts.Reset();
	PortalMouths.Reset();
	PortalOut.Reset();
	Stacks.Reset();
	FMeshBuilder M, D;

	auto FootRange = [](const FVector2D& C, double Yaw, double Hw, double Hd, double& Lo, double& Hi)
	{
		Lo = 1e9; Hi = -1e9;
		for (int32 I = -2; I <= 2; ++I)
		{
			for (int32 J = -2; J <= 2; ++J)
			{
				const FVector P = FVector(C.X, 0, C.Y) + YawX(Yaw) * (Hw * I / 2.0) + YawZ(Yaw) * (Hd * J / 2.0);
				const double G = Ground(P.X, P.Z);
				Lo = FMath::Min(Lo, G); Hi = FMath::Max(Hi, G);
			}
		}
	};
	auto AddHit = [this](const FVector& Cw, double Yaw, const FVector& Half, const FString& Name)
	{
		FOsrHitBox B; B.C = Cw; B.Ax = YawX(Yaw); B.Ay = FVector(0, 1, 0); B.Az = YawZ(Yaw); B.Half = Half; B.Name = Name; B.Part = Boxes.Num();
		Boxes.Add(B);
	};
	// generic flat-roofed building sitting on the lowest ground of its footprint (foundation hidden in the slope)
	auto Building = [&](const FVector2D& C, double Yaw, double W, double Dp, double H, const FLinearColor& Wall, const FString& Name, int32 Floors)
	{
		double Lo, Hi;
		FootRange(C, Yaw, W * 0.5, Dp * 0.5, Lo, Hi);
		const double Base = Lo - 1.5, Top = Hi + H;
		const FVector Cw(C.X, (Base + Top) * 0.5, C.Y);
		M.BoxYaw(L(Cw), Yaw, FVector(W * 0.5, (Top - Base) * 0.5, Dp * 0.5), Wall, false);
		// parapet + roof slab
		M.BoxYaw(L(FVector(C.X, Top + 0.2, C.Y)), Yaw, FVector(W * 0.5 - 0.5, 0.25, Dp * 0.5 - 0.5), ROOF);
		for (int32 F = 0; F < Floors; ++F)
		{
			const double Y = Hi + 2.2 + F * (H - 2.0) / FMath::Max(Floors, 1);
			for (double Sd : {-1.0, 1.0})
			{
				const FVector P = FVector(C.X, Y, C.Y) + YawZ(Yaw) * (Sd * (Dp * 0.5 + 0.08));
				M.BoxYaw(L(P), Yaw, FVector(W * 0.5 - 1.5, 0.65, 0.1), NoSnow(WINDOW), false);
			}
		}
		// roof units
		for (int32 K = 0; K < 3; ++K)
		{
			const FVector P = FVector(C.X, Top + 1.0, C.Y) + YawX(Yaw) * ((K - 1) * W * 0.28) + YawZ(Yaw) * (Dp * 0.15);
			M.BoxYaw(L(P), Yaw, FVector(1.6, 0.8, 1.2), CONCRETE_DK);
		}
		AddHit(Cw, Yaw, FVector(W * 0.5, (Top - Base) * 0.5, Dp * 0.5), Name);
		TrackPts.Add({FVector(C.X, Top, C.Y), Name});
		return Top;
	};
	auto Strip = [&](const TArray<FVector2D>& Pts, double Width, const FLinearColor& Col)
	{
		// draped road: resampled every 6 m, 0.25 m above the ground, flat across
		for (int32 K = 0; K + 1 < Pts.Num(); ++K)
		{
			const FVector2D A = Pts[K], B = Pts[K + 1];
			const int32 N = FMath::Max(1, int32(FVector2D::Distance(A, B) / 6.0));
			const FVector2D Dir = (B - A).GetSafeNormal();
			const FVector2D Side(-Dir.Y, Dir.X);
			for (int32 I = 0; I < N; ++I)
			{
				const FVector2D P0 = FMath::Lerp(A, B, double(I) / N), P1 = FMath::Lerp(A, B, double(I + 1) / N);
				auto Hm = [&](const FVector2D& P) { return FMath::Max3(Ground(P.X, P.Y), Ground(P.X + Side.X * Width * 0.5, P.Y + Side.Y * Width * 0.5), Ground(P.X - Side.X * Width * 0.5, P.Y - Side.Y * Width * 0.5)) + 0.25; };
				const double H0 = Hm(P0), H1 = Hm(P1);
				const FVector2D S = Side * (Width * 0.5);
				const FVector Q0 = L(FVector(P0.X - S.X, H0, P0.Y - S.Y)), Q1 = L(FVector(P0.X + S.X, H0, P0.Y + S.Y));
				const FVector Q2 = L(FVector(P1.X + S.X, H1, P1.Y + S.Y)), Q3 = L(FVector(P1.X - S.X, H1, P1.Y - S.Y));
				// orientation: make the normal point up
				if (FVector::CrossProduct(Q1 - Q0, Q3 - Q0).Y > 0.0) { M.Quad(Q0, Q1, Q2, Q3, Col); } else { M.Quad(Q0, Q3, Q2, Q1, Col); }
				// shoulders down into the ground so the edge never floats
				const FVector Dn(0, -3.0, 0);
				for (const FVector* E : {&Q0, &Q1})
				{
					(void)E;
				}
				M.Quad(Q0, Q3, Q3 + Dn, Q0 + Dn, Col);
				M.Quad(Q1, Q1 + Dn, Q2 + Dn, Q2, Col);
			}
		}
	};
	auto TWalls = [&](const FVector2D& From, const FVector2D& To, double Yaw)
	{
		const double Len = FVector2D::Distance(From, To);
		const int32 N = FMath::Max(1, int32(Len / 1.6));
		for (int32 K = 0; K < N; ++K)
		{
			const FVector2D P = FMath::Lerp(From, To, (K + 0.5) / N);
			const double G = Ground(P.X, P.Y);
			M.BoxYaw(L(FVector(P.X, G + 1.8 - 0.5, P.Y)), Yaw, FVector(0.75, 2.3, 0.18), CONCRETE);
			M.BoxYaw(L(FVector(P.X, G + 0.2, P.Y)), Yaw, FVector(0.75, 0.6, 0.9), CONCRETE_DK);
		}
	};

	// ---------------------------------------------------------------- surface complex (valley floor)
	const double Yv = 0.12;
	Building(FVector2D(11200, -2600), Yv, 52, 24, 14, CONCRETE, TEXT("POWER HALL"), 2);
	for (int32 K = 0; K < 2; ++K)
	{
		const FVector2D S(11188.0 + K * 26.0, -2628.0);
		const double G = Ground(S.X, S.Y);
		const FVector Base = L(FVector(S.X, G - 2.0, S.Y));
		for (int32 B = 0; B < 6; ++B)
		{
			const double H0 = B * 8.5, R0 = 2.8 - B * 0.12, R1 = 2.8 - (B + 1) * 0.12;
			M.Cylinder(Base + FVector(0, H0, 0), FVector(0, 1, 0), 8.5, R0, R1, 14, B == 5 ? RED : (B == 4 ? WHITE : CONCRETE), B == 5);
		}
		Stacks.Add(FVector(S.X, G - 2.0 + 51.0, S.Y));
		AddHit(FVector(S.X, G + 24.0, S.Y), 0.0, FVector(2.8, 26.0, 2.8), TEXT("STACK"));
	}
	Building(FVector2D(11385, -2475), Yv, 44, 16, 11, Rgb(132, 124, 108), TEXT("ADMIN"), 3);
	Building(FVector2D(11060, -2525), Yv, 36, 22, 9, Rgb(96, 104, 92), TEXT("WAREHOUSE"), 0);
	Building(FVector2D(11095, -2445), Yv, 30, 18, 8, Rgb(96, 104, 92), TEXT("WAREHOUSE"), 0);
	for (int32 K = 0; K < 3; ++K) { Building(FVector2D(11255, -2455 + K * 26.0), Yv, 30, 10, 6, Rgb(118, 112, 94), TEXT("BARRACKS"), 1); }
	// fuel tanks with a berm
	{
		const FVector2D C(11005, -2610);
		const double G = Ground(C.X, C.Y);
		M.BoxYaw(L(FVector(C.X, G + 0.2, C.Y)), Yv, FVector(26, 1.4, 20), CONCRETE_DK);
		for (int32 K = 0; K < 3; ++K)
		{
			const FVector2D T = C + FVector2D((K - 1) * 15.0, (K == 1 ? -6.0 : 5.0));
			const double Gt = Ground(T.X, T.Y);
			M.Cylinder(L(FVector(T.X, Gt - 1.0, T.Y)), FVector(0, 1, 0), 10.0, 6.0, 6.0, 20, Rgb(150, 152, 146));
			AddHit(FVector(T.X, Gt + 4.0, T.Y), 0.0, FVector(6, 5, 6), TEXT("FUEL TANK"));
		}
	}
	// helipad
	{
		const FVector2D C(11455, -2445);
		double Lo, Hi; FootRange(C, 0.0, 12, 12, Lo, Hi);
		M.Cylinder(L(FVector(C.X, Lo - 1.0, C.Y)), FVector(0, 1, 0), Hi - Lo + 1.3, 13.0, 13.0, 24, NoSnow(CONCRETE));
		const double Top = Hi + 0.32;
		M.BoxYaw(L(FVector(C.X - 3.0, Top, C.Y)), 0.0, FVector(0.6, 0.05, 4.0), NoSnow(WHITE));
		M.BoxYaw(L(FVector(C.X + 3.0, Top, C.Y)), 0.0, FVector(0.6, 0.05, 4.0), NoSnow(WHITE));
		M.BoxYaw(L(FVector(C.X, Top, C.Y)), 0.0, FVector(3.0, 0.05, 0.6), NoSnow(WHITE));
	}
	// perimeter fence (posts + rails) with a gap for the gully road, two guard towers
	{
		const FVector2D Cn[4] = {FVector2D(10955, -2685), FVector2D(11565, -2685), FVector2D(11565, -2355), FVector2D(10955, -2355)};
		for (int32 E = 0; E < 4; ++E)
		{
			const FVector2D A = Cn[E], B = Cn[(E + 1) % 4];
			const int32 N = int32(FVector2D::Distance(A, B) / 8.0);
			for (int32 K = 0; K <= N; ++K)
			{
				const FVector2D P = FMath::Lerp(A, B, double(K) / N);
				if (E == 0 && FMath::Abs(P.X - 11500.0) < 14.0) { continue; }
				const double G = Ground(P.X, P.Y);
				M.BoxYaw(L(FVector(P.X, G + 1.4, P.Y)), 0.0, FVector(0.08, 1.6, 0.08), Rgb(70, 70, 68), false);
				if (K < N && !(E == 0 && FMath::Abs(P.X + (B.X - A.X) / N * 0.5 - 11500.0) < 14.0))
				{
					const FVector2D Q = FMath::Lerp(A, B, double(K + 1) / N);
					const double Gq = Ground(Q.X, Q.Y);
					for (double Hr : {1.0, 2.7})
					{
						const FVector P0 = L(FVector(P.X, G + Hr, P.Y)), P1 = L(FVector(Q.X, Gq + Hr, Q.Y));
						const FVector Mid = (P0 + P1) * 0.5, Dir = (P1 - P0);
						M.Box(Mid, Dir.GetSafeNormal(), FVector(0, 1, 0), FVector::CrossProduct(Dir.GetSafeNormal(), FVector(0, 1, 0)).GetSafeNormal(), FVector(Dir.Size() * 0.5, 0.04, 0.04), Rgb(80, 80, 78), false);
					}
				}
			}
		}
		for (const FVector2D& T : {Cn[0], Cn[2]})
		{
			const double G = Ground(T.X, T.Y);
			for (int32 K = 0; K < 4; ++K)
			{
				const FVector2D O((K & 1) ? 1.6 : -1.6, (K & 2) ? 1.6 : -1.6);
				M.BoxYaw(L(FVector(T.X + O.X, G + 4.0, T.Y + O.Y)), 0.0, FVector(0.15, 4.5, 0.15), Rgb(90, 88, 80), false);
			}
			M.BoxYaw(L(FVector(T.X, G + 9.5, T.Y)), 0.0, FVector(2.2, 1.3, 2.2), Rgb(110, 116, 100));
			M.BoxYaw(L(FVector(T.X, G + 11.1, T.Y)), 0.0, FVector(2.6, 0.2, 2.6), ROOF);
		}
	}
	// trucks in the yard
	for (int32 K = 0; K < 5; ++K)
	{
		const FVector2D P(11130.0 + K * 9.0, -2480.0);
		const double G = Ground(P.X, P.Y);
		const double Yaw = Yv + UE_DOUBLE_HALF_PI;
		M.BoxYaw(L(FVector(P.X, G + 1.9, P.Y) + YawX(Yaw) * -1.0), Yaw, FVector(3.2, 1.3, 1.25), Rgb(78, 86, 66));
		M.BoxYaw(L(FVector(P.X, G + 1.6, P.Y) + YawX(Yaw) * 3.3), Yaw, FVector(1.1, 1.0, 1.2), Rgb(70, 78, 60));
		for (double Ax : {-2.8, 0.0, 3.3})
		{
			for (double Sd : {-1.0, 1.0})
			{
				M.Cylinder(L(FVector(P.X, G + 0.55, P.Y) + YawX(Yaw) * Ax + YawZ(Yaw) * (Sd * 1.25)), YawZ(Yaw) * Sd, 0.35, 0.55, 0.55, 8, Rgb(24, 24, 24));
			}
		}
	}
	// roads: valley road + gully road to the portals
	Strip({FVector2D(10700, -2470), FVector2D(10960, -2500), FVector2D(11300, -2530), FVector2D(11500, -2560), FVector2D(11750, -2540), FVector2D(12100, -2560)}, 9.0, NoSnow(Rgb(62, 60, 58)));
	Strip({FVector2D(11500, -2560), FVector2D(11500, -2700), FVector2D(11490, -2950), FVector2D(11500, -3200), FVector2D(11500, -3300)}, 8.0, NoSnow(Rgb(66, 64, 60)));
	Strip({FVector2D(11495, -3080), FVector2D(11560, -3110), FVector2D(11585, -3120)}, 7.0, NoSnow(Rgb(66, 64, 60)));

	// ---------------------------------------------------------------- tunnel portals (hardened, blast doors)
	auto Portal = [&](const FVector2D& C, double Yaw, const FString& Name)
	{
		const FVector Ax = YawX(Yaw), Az = YawZ(Yaw);
		const FVector Front = FVector(C.X, 0, C.Y) + Az * 8.0;
		const double Yf = Ground(Front.X, Front.Z);
		// headwall, embedded 6 m back into the slope and 3 m below the apron
		const FVector Hw = FVector(C.X, Yf + 7.0, C.Y);
		M.Box(L(Hw), Ax, FVector(0, 1, 0), Az, FVector(22.0, 10.0, 4.0), CONCRETE);
		M.Box(L(Hw - Az * 14.0 + FVector(0, 4.0, 0)), Ax, FVector(0, 1, 0), Az, FVector(20.0, 12.0, 10.0), CONCRETE_DK);
		// hood over the opening
		M.Box(L(FVector(C.X, Yf + 13.0, C.Y) + Az * 6.0), Ax, FVector(0, 1, 0), Az, FVector(10.5, 0.7, 4.0), CONCRETE);
		// wing walls splayed 30 deg
		for (double Sd : {-1.0, 1.0})
		{
			const FVector Wd = (Az * FMath::Cos(0.5) + Ax * Sd * FMath::Sin(0.5)).GetSafeNormal();
			const FVector Wx = FVector::CrossProduct(FVector(0, 1, 0), Wd).GetSafeNormal();
			const FVector Wc = FVector(C.X, Yf + 3.5, C.Y) + Ax * (Sd * 21.0) + Wd * 7.0 + Az * 2.0;
			M.Box(L(Wc), Wx, FVector(0, 1, 0), Wd, FVector(1.0, 6.0, 8.0), CONCRETE);
		}
		// dark tunnel mouth (recessed) + blast doors (separate component: blown out on the kill)
		const FVector Mouth = FVector(C.X, Yf + 4.8, C.Y) + Az * 4.05;
		M.Box(L(Mouth), Ax, FVector(0, 1, 0), Az, FVector(6.2, 5.0, 0.06), NoSnow(DARK), false);
		for (double Sd : {-1.0, 1.0})
		{
			D.Box(L(Mouth + Az * 0.35 + Ax * (Sd * 3.05)), Ax, FVector(0, 1, 0), Az, FVector(3.0, 4.9, 0.3), NoSnow(STEEL));
			D.Box(L(Mouth + Az * 0.7 + Ax * (Sd * 3.05) + FVector(0, 1.5, 0)), Ax, FVector(0, 1, 0), Az, FVector(2.6, 0.12, 0.05), NoSnow(Rgb(210, 180, 40)));
		}
		// apron slab
		const FVector Ap = FVector(C.X, Yf - 0.8, C.Y) + Az * 22.0;
		M.Box(L(Ap), Ax, FVector(0, 1, 0), Az, FVector(20.0, 1.1, 18.0), NoSnow(Rgb(92, 90, 86)));
		// blast (T-) walls funnelling the approach
		for (double Sd : {-1.0, 1.0})
		{
			const FVector A0 = FVector(C.X, 0, C.Y) + Ax * (Sd * 14.0) + Az * 14.0;
			const FVector A1 = A0 + Az * 26.0;
			TWalls(FVector2D(A0.X, A0.Z), FVector2D(A1.X, A1.Z), Yaw + UE_DOUBLE_HALF_PI);
		}
		AddHit(Hw, Yaw, FVector(22.0, 10.0, 4.0), Name);
		PortalMouths.Add(Mouth);
		PortalOut.Add(Az);
		TrackPts.Add({Mouth, Name});
	};
	Portal(OsrLayout::PortalA, 0.0, TEXT("PORTAL A"));
	Portal(OsrLayout::PortalB, -UE_DOUBLE_HALF_PI, TEXT("PORTAL B"));

	// ---------------------------------------------------------------- ventilation shaft terrace on the spur
	{
		const FVector2D V = OsrLayout::Vent;
		VentHalf = OsrLayout::VentHalf;
		double Lo, Hi;
		FootRange(V, 0.0, 14.0, 14.0, Lo, Hi);
		PadTop = Hi + 0.4;
		const double Base = Lo - 3.0;
		const double Hp = 14.0, Ho = VentHalf + 1.2;   // pad half size, collar outer half
		// pad = four slabs around the opening so the shaft is really open
		auto Slab = [&](double X0, double X1, double Z0, double Z1, const FLinearColor& Col)
		{
			const FVector Cc(V.X + (X0 + X1) * 0.5, (Base + PadTop) * 0.5, V.Y + (Z0 + Z1) * 0.5);
			const FVector H((X1 - X0) * 0.5, (PadTop - Base) * 0.5, (Z1 - Z0) * 0.5);
			M.BoxYaw(L(Cc), 0.0, H, Col, false);
			AddHit(Cc, 0.0, H, TEXT("VENT PAD"));
		};
		Slab(-Hp, Hp, -Hp, -VentHalf, CONCRETE);
		Slab(-Hp, Hp, VentHalf, Hp, CONCRETE);
		Slab(-Hp, -VentHalf, -VentHalf, VentHalf, CONCRETE);
		Slab(VentHalf, Hp, -VentHalf, VentHalf, CONCRETE);
		// collar (1 m) around the opening
		const double Ct = PadTop + 1.0;
		auto Collar = [&](double X0, double X1, double Z0, double Z1)
		{
			const FVector Cc(V.X + (X0 + X1) * 0.5, PadTop + 0.5, V.Y + (Z0 + Z1) * 0.5);
			M.BoxYaw(L(Cc), 0.0, FVector((X1 - X0) * 0.5, 0.5, (Z1 - Z0) * 0.5), CONCRETE_DK);
		};
		Collar(-Ho, Ho, -Ho, -VentHalf);
		Collar(-Ho, Ho, VentHalf, Ho);
		Collar(-Ho, -VentHalf, -VentHalf, VentHalf);
		Collar(VentHalf, Ho, -VentHalf, VentHalf);
		// shaft walls (dark) going down + bottom
		const double Depth = 40.0;
		for (int32 K = 0; K < 4; ++K)
		{
			const double A = K * UE_DOUBLE_HALF_PI;
			const FVector Dir(FMath::Cos(A), 0, FMath::Sin(A));
			M.BoxYaw(L(FVector(V.X, Ct - Depth * 0.5, V.Y) + Dir * (VentHalf + 0.1)), -A, FVector(0.1, Depth * 0.5, VentHalf), NoSnow(Rgb(30, 30, 30)), false);
		}
		M.BoxYaw(L(FVector(V.X, Ct - Depth, V.Y)), 0.0, FVector(VentHalf, 0.1, VentHalf), NoSnow(Rgb(8, 8, 8)));
		// grate over the opening (the film's "hatch": a penetrator goes through it)
		for (int32 K = -3; K <= 3; ++K)
		{
			const double O = K * VentHalf / 3.5;
			M.BoxYaw(L(FVector(V.X + O, Ct - 0.1, V.Y)), 0.0, FVector(0.05, 0.08, VentHalf), NoSnow(Rgb(48, 50, 48)), false);
			M.BoxYaw(L(FVector(V.X, Ct - 0.15, V.Y + O)), 0.0, FVector(VentHalf, 0.06, 0.05), NoSnow(Rgb(48, 50, 48)), false);
		}
		VentCentre = FVector(V.X, Ct, V.Y);
		// intake headhouse with louvres + fan housing, mast, floodlights, fence
		{
			const FVector Hh(V.X + 9.0, PadTop + 2.5, V.Y - 8.0);
			M.BoxYaw(L(Hh), 0.0, FVector(3.5, 2.5, 3.0), Rgb(132, 136, 126));
			for (int32 K = 0; K < 5; ++K)
			{
				M.BoxYaw(L(Hh + FVector(-3.55, -1.6 + K * 0.8, 0)), 0.0, FVector(0.05, 0.12, 2.5), NoSnow(Rgb(40, 42, 40)), false);
			}
			M.Cylinder(L(Hh + FVector(0, 2.5, 0)), FVector(0, 1, 0), 1.6, 1.6, 1.4, 16, Rgb(120, 124, 118));
			AddHit(Hh, 0.0, FVector(3.5, 2.5, 3.0), TEXT("VENT HOUSE"));
			TrackPts.Add({Hh + FVector(0, 2.5, 0), TEXT("VENT HOUSE")});
		}
		M.Cylinder(L(FVector(V.X - 10.0, PadTop, V.Y - 10.0)), FVector(0, 1, 0), 16.0, 0.25, 0.12, 8, Rgb(150, 150, 150));
		for (int32 K = 0; K < 4; ++K)
		{
			const FVector P(V.X + ((K & 1) ? 12.5 : -12.5), PadTop, V.Y + ((K & 2) ? 12.5 : -12.5));
			M.Cylinder(L(P), FVector(0, 1, 0), 8.0, 0.14, 0.1, 6, Rgb(90, 90, 90));
			M.BoxYaw(L(P + FVector(0, 8.1, 0)), 0.0, FVector(0.5, 0.25, 0.3), Rgb(200, 200, 190));
		}
		for (int32 E = 0; E < 4; ++E)
		{
			for (int32 K = -6; K <= 6; ++K)
			{
				const double O = K * 2.2;
				FVector P = E == 0 ? FVector(O, 0, -13.5) : E == 1 ? FVector(O, 0, 13.5) : E == 2 ? FVector(-13.5, 0, O) : FVector(13.5, 0, O);
				P += FVector(V.X, PadTop + 0.9, V.Y);
				M.BoxYaw(L(P), 0.0, FVector(0.05, 0.9, 0.05), Rgb(70, 70, 70), false);
			}
			const FVector Rc = E == 0 ? FVector(0, 0, -13.5) : E == 1 ? FVector(0, 0, 13.5) : E == 2 ? FVector(-13.5, 0, 0) : FVector(13.5, 0, 0);
			const FVector Rh = E < 2 ? FVector(13.5, 0.04, 0.04) : FVector(0.04, 0.04, 13.5);
			M.BoxYaw(L(Rc + FVector(V.X, PadTop + 1.7, V.Y)), 0.0, Rh, Rgb(80, 80, 80), false);
		}
		TrackPts.Insert({VentCentre, TEXT("VENT SHAFT")}, 0);
	}
	// cleared ground: the fenced complex, roads, portal aprons, the vent terrace
	ClearZones.Reset();
	for (double X = 10960.0; X <= 11570.0; X += 45.0)
	{
		for (double Z = -2690.0; Z <= -2350.0; Z += 45.0) { ClearZones.Add(FVector4(X, Ground(X, Z), Z, 40.0)); }
	}
	auto RoadZones = [this](const TArray<FVector2D>& Pts, double R)
	{
		for (int32 K = 0; K + 1 < Pts.Num(); ++K)
		{
			const int32 N = FMath::Max(1, int32(FVector2D::Distance(Pts[K], Pts[K + 1]) / (R * 0.8)));
			for (int32 I = 0; I <= N; ++I) { const FVector2D P = FMath::Lerp(Pts[K], Pts[K + 1], double(I) / N); ClearZones.Add(FVector4(P.X, Ground(P.X, P.Y), P.Y, R)); }
		}
	};
	RoadZones({FVector2D(10700, -2470), FVector2D(10960, -2500), FVector2D(11300, -2530), FVector2D(11500, -2560), FVector2D(11750, -2540), FVector2D(12100, -2560)}, 12.0);
	RoadZones({FVector2D(11500, -2560), FVector2D(11500, -2700), FVector2D(11490, -2950), FVector2D(11500, -3200), FVector2D(11500, -3300)}, 12.0);
	for (const FVector& Mo : PortalMouths) { ClearZones.Add(FVector4(Mo.X, Mo.Y, Mo.Z, 50.0)); }
	ClearZones.Add(FVector4(VentCentre.X, VentCentre.Y, VentCentre.Z, 32.0));
	M.Commit(Pmc, 0, LitMaterial());
	D.Commit(Doors, 0, LitMaterial());
	Doors->SetVisibility(true);
	UE_LOG(LogTemp, Display, TEXT("OSR mission: plant built, %d verts, vent (%.0f, %.1f, %.0f) pad top %.1f, %d hit boxes"), M.Num() + D.Num(), VentCentre.X, VentCentre.Y, VentCentre.Z, PadTop, Boxes.Num());
}

bool AOsrMissionTarget::BombArrived(const FVector& From, const FVector& To, FVector& Impact, FString& What)
{
	const double Yc = VentCentre.Y;
	if (From.Y >= Yc && To.Y < Yc)
	{
		const double T = (From.Y - Yc) / FMath::Max(From.Y - To.Y, 1e-6);
		const FVector Q = FMath::Lerp(From, To, T);
		if (FMath::Abs(Q.X - VentCentre.X) <= VentHalf && FMath::Abs(Q.Z - VentCentre.Z) <= VentHalf)
		{
			Impact = Q;
			What = TEXT("VENT SHAFT");
			return true;
		}
	}
	return false;
}

bool AOsrMissionTarget::SnapTrack(const FVector& P, double Radius, FVector& Out, FString& Name) const
{
	double Best = Radius;
	bool bOk = false;
	for (const FTrackPt& T : TrackPts)
	{
		const double D = FVector::Dist(T.P, P);
		if (D < Best) { Best = D; Out = T.P; Name = T.Name; bOk = true; }
	}
	return bOk;
}

void AOsrMissionTarget::ApplyHit(const FVector& P, double Damage, EOsrDamage Kind, AActor* By, int32 Part)
{
	// structures on the surface are decoration for a deep plant: only the shaft kills it (bombs report their own result)
	if (AOsrMissionFx* Fx = AOsrMissionFx::Get(this))
	{
		if (Kind == EOsrDamage::Bullet) { Fx->GroundImpact(P, false, 0.6); }
	}
}

void AOsrMissionTarget::ResetTarget()
{
	bDestroyed = false;
	DestroyT = -1.0;
	SecondaryStep = 0;
	Doors->SetVisibility(true);
	Doors->SetRelativeLocation(FVector::ZeroVector);
}

void AOsrMissionTarget::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FMath::Min<double>(DeltaSeconds, 0.1);
	AOsrMissionFx* Fx = AOsrMissionFx::Get(this);
	if (!Fx) { return; }
	if (bDestroyed && DestroyT < 0.0) { DestroyT = 0.0; SecondaryStep = 0; }
	if (DestroyT < 0.0)
	{
		// idle life: faint steam from the power-hall stacks
		SmokeAcc += Dt;
		if (SmokeAcc > 0.35)
		{
			SmokeAcc = 0.0;
			for (const FVector& S : Stacks) { Fx->Puff(S, FVector(1.5, 3.5, 0.8), 9.0, 2.5, 16.0, FLinearColor(0.75f, 0.76f, 0.78f), 0.35, false, 0.2, 0.4); }
		}
		return;
	}
	DestroyT += Dt;
	const double T = DestroyT;
	FRandomStream R(int32(T * 1000.0));
	// scripted secondary sequence: shaft blowout, both portals blow their doors, random secondaries, then a long smoke column
	struct FStep { double At; int32 What; };
	static const FStep Steps[] = {{0.6, 0}, {1.4, 1}, {2.1, 2}, {2.6, 0}, {3.3, 3}, {4.2, 1}, {4.9, 4}, {6.0, 2}, {7.5, 3}, {9.0, 4}};
	while (SecondaryStep < int32(UE_ARRAY_COUNT(Steps)) && T >= Steps[SecondaryStep].At)
	{
		const int32 W = Steps[SecondaryStep].What;
		if (W == 0)
		{
			Fx->Explosion(VentCentre + FVector(0, 6, 0), EOsrBlast::Huge, FVector(0, 1, 0));
			for (int32 K = 0; K < 14; ++K)
			{
				Fx->Puff(VentCentre, FVector(R.FRandRange(-8, 8), R.FRandRange(60, 140), R.FRandRange(-8, 8)), R.FRandRange(1.2, 2.2), 4.0, 22.0, FLinearColor(1.0f, 0.42f, 0.1f) * 2.0f, 1.0, true, 0.8, 0.0);
			}
		}
		else if (W == 1 || W == 2)
		{
			const int32 I = W - 1;
			if (PortalMouths.IsValidIndex(I))
			{
				const FVector Mo = PortalMouths[I], Out = PortalOut[I];
				Fx->Explosion(Mo + Out * 4.0, EOsrBlast::Secondary, Out);
				for (int32 K = 0; K < 16; ++K)
				{
					const FVector V = (Out * R.FRandRange(60, 160)) + FVector(R.FRandRange(-12, 12), R.FRandRange(-4, 20), R.FRandRange(-12, 12));
					Fx->Puff(Mo, V, R.FRandRange(1.0, 2.0), 3.0, 18.0, FLinearColor(1.0f, 0.42f, 0.1f) * 2.0f, 1.0, true, 1.6, 2.0);
				}
				if (W == 1) { Doors->SetVisibility(false); }
			}
		}
		else if (W == 3)
		{
			Fx->Explosion(Origin + FVector(-150, 10, -40), EOsrBlast::Secondary, FVector(0, 1, 0));
		}
		else
		{
			Fx->Explosion(VentCentre + FVector(R.FRandRange(-20, 20), 3, R.FRandRange(-20, 20)), EOsrBlast::Secondary, FVector(0, 1, 0));
		}
		++SecondaryStep;
	}
	// smoke columns (shaft + portals) for two minutes
	SmokeAcc += Dt;
	if (T < 150.0 && SmokeAcc > 0.12)
	{
		SmokeAcc = 0.0;
		const double Fade = 1.0 - FMath::Clamp((T - 90.0) / 60.0, 0.0, 1.0);
		const float G = R.FRandRange(0.05f, 0.1f);
		Fx->Puff(VentCentre + FVector(0, 4, 0), FVector(R.FRandRange(-3, 3), R.FRandRange(18, 30), R.FRandRange(-3, 3)), 16.0, 6.0, 55.0, FLinearColor(G, G, G), 0.8 * Fade, false, 0.12, 0.5);
		for (int32 I = 0; I < PortalMouths.Num(); ++I)
		{
			if (R.FRand() < 0.5)
			{
				Fx->Puff(PortalMouths[I] + PortalOut[I] * 6.0, PortalOut[I] * 8.0 + FVector(0, 10, 0), 12.0, 5.0, 40.0, FLinearColor(G, G, G), 0.6 * Fade, false, 0.2, 1.5);
			}
		}
		if (T < 20.0) { Fx->Puff(VentCentre + FVector(0, 2, 0), FVector(0, R.FRandRange(20, 40), 0), 0.8, 3.0, 9.0, FLinearColor(1.0f, 0.4f, 0.1f) * 2.0f, 0.9 * (1.0 - T / 20.0), true, 0.5, 0.0); }
	}
}
