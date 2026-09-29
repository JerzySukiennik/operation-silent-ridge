// Air defence sites implementation (detection model, tracks, launches, guns) + kit-bashed procedural vehicles.
#include "RadarSite.h"
#include "SamMissile.h"
#include "MissionCommon.h"
#include "MissionFx.h"
#include "MissionState.h"
#include "OsrJetPawn.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"

using namespace OsrMission;

namespace
{
	const FLinearColor OLIVE = Rgb(84, 92, 66);
	const FLinearColor OLIVE_DK = Rgb(60, 66, 48);
	const FLinearColor TIRE = Rgb(22, 22, 22);
	const FLinearColor PANEL = Rgb(150, 156, 146);
	const FLinearColor GLASS = Rgb(30, 38, 44);
	const FLinearColor STEEL = Rgb(96, 98, 92);
	const FLinearColor EARTH = Rgb(120, 110, 96);
	double Sm(double A, double B, double X) { const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0); return T * T * (3.0 - 2.0 * T); }
	FVector YawX(double Yaw) { return FVector(FMath::Cos(Yaw), 0.0, -FMath::Sin(Yaw)); }
	FVector YawZ(double Yaw) { return FVector(FMath::Sin(Yaw), 0.0, FMath::Cos(Yaw)); }
	FLinearColor NoSnowC(FLinearColor C) { C.A = 0.05f; return C; }

	/** 8x8 military truck (chassis + cab + wheels) along +X of Yaw, rear at -X. Local coords relative to the site base. */
	void Truck(FMeshBuilder& M, const FVector& C, double Yaw, double Len, int32 Axles, const FLinearColor& Col)
	{
		const FVector Ax = YawX(Yaw), Az = YawZ(Yaw), Up(0, 1, 0);
		M.Box(C + Up * 1.35, Ax, Up, Az, FVector(Len * 0.5, 0.35, 1.3), Col);
		M.Box(C + Up * 2.2 + Ax * (Len * 0.5 - 1.3), Ax, Up, Az, FVector(1.3, 1.0, 1.35), Col);
		M.Box(C + Up * 2.6 + Ax * (Len * 0.5 - 0.05), Ax, Up, Az, FVector(0.06, 0.45, 1.1), NoSnowC(GLASS));
		for (int32 K = 0; K < Axles; ++K)
		{
			const double X = Len * 0.5 - 1.2 - K * (Len - 2.4) / FMath::Max(Axles - 1, 1);
			for (double Sd : {-1.0, 1.0}) { M.Cylinder(C + Up * 0.62 + Ax * X + Az * (Sd * 1.05), Az * Sd, 0.4, 0.62, 0.62, 10, TIRE); }
		}
	}
}

AOsrRadarSite::AOsrRadarSite()
{
	Pmc = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Site"));
	SetRootComponent(Pmc);
	Pmc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pmc->bUseAsyncCooking = false;
	Rotor = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Rotor"));
	Rotor->SetupAttachment(Pmc);
	Rotor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rotor->bUseAsyncCooking = false;
}

void AOsrRadarSite::Setup(EOsrSiteType InType, const FVector2D& At, double InFacingYaw)
{
	Type = InType;
	FacingYaw = InFacingYaw;
	Base = FVector(At.X, Ground(At.X, At.Y), At.Y);
	switch (Type)
	{
	case EOsrSiteType::SearchRadar:
		DisplayName = TEXT("SEARCH RADAR"); Symbol = TEXT("SR");
		Antenna = Base + FVector(0, 9.0, 0); DetectRange = 38000.0; ScanPeriod = 2.5; Missiles = 0; Health = 120.0;
		break;
	case EOsrSiteType::S400:
		DisplayName = TEXT("S-400"); Symbol = TEXT("21");
		Antenna = Base + FVector(0, 27.0, 0); DetectRange = 24000.0; ScanPeriod = 0.1; Missiles = 8; Health = 200.0;
		MaxRange = 32000.0; MinRange = 1500.0; MinAlt = 15.0; LaunchInterval = 14.0; MaxInFlight = 1;
		break;
	case EOsrSiteType::Pantsir:
		DisplayName = TEXT("PANTSIR"); Symbol = TEXT("22");
		Antenna = Base + FVector(0, 6.5, 0); DetectRange = 12000.0; ScanPeriod = 0.1; Missiles = 12; Health = 80.0;
		MaxRange = 15000.0; MinRange = 600.0; MinAlt = 5.0; LaunchInterval = 8.0; MaxInFlight = 1;
		break;
	case EOsrSiteType::Tor:
		DisplayName = TEXT("TOR"); Symbol = TEXT("15");
		Antenna = Base + FVector(0, 5.5, 0); DetectRange = 10000.0; ScanPeriod = 0.1; Missiles = 8; Health = 80.0;
		MaxRange = 12000.0; MinRange = 1000.0; MinAlt = 10.0; LaunchInterval = 9.0; MaxInFlight = 1;
		break;
	}
	SetActorLocation(ToUE(Base));
	BuildMesh();
}

void AOsrRadarSite::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<AOsrRadarSite> It(GetWorld()); It; ++It)
	{
		if (It->Type == EOsrSiteType::SearchRadar && *It != this) { Cue = *It; }
	}
}

void AOsrRadarSite::ResetSite()
{
	bDead = false;
	bEmitting = true;
	Tracks.Reset();
	switch (Type)
	{
	case EOsrSiteType::SearchRadar: Health = 120.0; break;
	case EOsrSiteType::S400: Health = 200.0; Missiles = 8; break;
	case EOsrSiteType::Pantsir: Health = 80.0; Missiles = 12; break;
	case EOsrSiteType::Tor: Health = 80.0; Missiles = 8; break;
	}
	Pmc->SetVisibility(true, true);
	SetActorHiddenInGame(false);
}

// ---------------------------------------------------------------- geometry

void AOsrRadarSite::BuildMesh()
{
	FMeshBuilder M, R;
	Boxes.Reset();
	Launchers.Reset();
	const FVector Up(0, 1, 0);
	auto GroundL = [this](const FVector& Local) { return Ground(Base.X + Local.X, Base.Z + Local.Z) - Base.Y; };
	auto Hit = [this](const FVector& Local, double Yaw, const FVector& Half, const TCHAR* Name)
	{
		FOsrHitBox B; B.C = Base + Local; B.Ax = YawX(Yaw); B.Ay = FVector(0, 1, 0); B.Az = YawZ(Yaw); B.Half = Half; B.Name = Name; B.Part = Boxes.Num();
		Boxes.Add(B);
	};
	auto Pad = [&](const FVector& Local, double Yaw, double Hw, double Hd)
	{
		// levelled gravel pad under a vehicle (hides slope gaps)
		double Lo = 1e9, Hi = -1e9;
		for (int32 I = -1; I <= 1; ++I) { for (int32 J = -1; J <= 1; ++J) { const double G = GroundL(Local + YawX(Yaw) * (Hw * I) + YawZ(Yaw) * (Hd * J)); Lo = FMath::Min(Lo, G); Hi = FMath::Max(Hi, G); } }
		M.BoxYaw(FVector(Local.X, (Lo - 1.0 + Hi + 0.15) * 0.5, Local.Z), Yaw, FVector(Hw, (Hi + 0.15 - Lo + 1.0) * 0.5, Hd), NoSnowC(EARTH), false);
		return Hi + 0.15;
	};
	const double Fy = FacingYaw;
	switch (Type)
	{
	case EOsrSiteType::SearchRadar:
	{
		const double Y = Pad(FVector::ZeroVector, Fy, 12.0, 6.0);
		Truck(M, FVector(0, Y, 0), Fy, 13.0, 4, OLIVE);
		// turntable + big rotating phased-array panel (rotor)
		M.Cylinder(FVector(0, Y + 2.6, 0), Up, 2.4, 1.2, 1.0, 12, STEEL);
		Rotor->SetRelativeLocation(ToUE(FVector(0, Y + 5.0, 0)));
		R.Box(FVector(0, 2.6, 0), FVector(1, 0, 0), FVector(0, 0.97, -0.26).GetSafeNormal(), FVector(0, 0.26, 0.97).GetSafeNormal(), FVector(4.5, 2.4, 0.3), PANEL);
		R.Box(FVector(0, 2.6, -0.5), FVector(1, 0, 0), FVector(0, 0.97, -0.26).GetSafeNormal(), FVector(0, 0.26, 0.97).GetSafeNormal(), FVector(4.3, 2.2, 0.15), STEEL);
		R.Cylinder(FVector(0, -2.0, 0), Up, 2.5, 0.5, 0.5, 8, STEEL);
		// crew shelter + generator
		M.BoxYaw(FVector(10, GroundL(FVector(10, 0, 8)) + 1.3, 8), Fy + 0.4, FVector(3.0, 1.4, 1.3), OLIVE_DK);
		M.BoxYaw(FVector(-8, GroundL(FVector(-8, 0, 9)) + 1.0, 9), Fy - 0.3, FVector(2.2, 1.0, 1.1), OLIVE);
		Antenna = Base + FVector(0, Y + 7.6, 0);
		Hit(FVector(0, Y + 2.0, 0), Fy, FVector(6.5, 2.2, 1.6), TEXT("RADAR"));
		Hit(FVector(0, Y + 7.6, 0), 0.0, FVector(4.5, 2.6, 4.5), TEXT("ANTENNA"));
		break;
	}
	case EOsrSiteType::S400:
	{
		// 40V6M-class tower with the engagement radar on top (rotor = trainable panel)
		const double Y = Pad(FVector::ZeroVector, Fy, 8.0, 8.0);
		for (int32 K = 0; K < 4; ++K)
		{
			const FVector Leg((K & 1) ? 2.5 : -2.5, Y, (K & 2) ? 2.5 : -2.5);
			const FVector Top(Leg.X * 0.45, Y + 24.0, Leg.Z * 0.45);
			M.Cylinder(Leg, (Top - Leg).GetSafeNormal(), (Top - Leg).Size(), 0.22, 0.18, 6, STEEL);
		}
		for (int32 Lv = 1; Lv < 6; ++Lv)
		{
			const double H = Y + Lv * 4.0, S = 2.5 * (1.0 - 0.55 * Lv * 4.0 / 24.0);
			M.BoxYaw(FVector(0, H, S), 0.0, FVector(S, 0.08, 0.08), STEEL, false);
			M.BoxYaw(FVector(0, H, -S), 0.0, FVector(S, 0.08, 0.08), STEEL, false);
			M.BoxYaw(FVector(S, H, 0), 0.0, FVector(0.08, 0.08, S), STEEL, false);
			M.BoxYaw(FVector(-S, H, 0), 0.0, FVector(0.08, 0.08, S), STEEL, false);
		}
		M.BoxYaw(FVector(0, Y + 24.3, 0), 0.0, FVector(1.8, 0.3, 1.8), STEEL);
		Rotor->SetRelativeLocation(ToUE(FVector(0, Y + 24.6, 0)));
		R.Box(FVector(0, 1.2, 0), FVector(1, 0, 0), Up, FVector(0, 0, 1), FVector(1.6, 1.2, 1.2), OLIVE);
		R.Box(FVector(0, 3.0, 1.0), FVector(1, 0, 0), FVector(0, 0.94, 0.34).GetSafeNormal(), FVector(0, -0.34, 0.94).GetSafeNormal(), FVector(2.2, 2.2, 0.25), PANEL);
		Antenna = Base + FVector(0, Y + 27.0, 0);
		Hit(FVector(0, Y + 13.0, 0), 0.0, FVector(2.6, 13.0, 2.6), TEXT("RADAR TOWER"));
		// four TELs in earth revetments, tubes raised vertical
		for (int32 K = 0; K < 4; ++K)
		{
			const double A = Fy + (K - 1.5) * 0.55;
			const FVector P = YawZ(A) * 70.0 + YawX(A) * ((K - 1.5) * 12.0);
			const double Yt = Pad(P, A + UE_DOUBLE_HALF_PI, 9.0, 4.0);
			Truck(M, FVector(P.X, Yt, P.Z), A + UE_DOUBLE_HALF_PI, 14.0, 4, OLIVE);
			for (int32 T = 0; T < 4; ++T)
			{
				const FVector Tb = FVector(P.X, Yt + 1.8, P.Z) + YawX(A + UE_DOUBLE_HALF_PI) * -5.5 + YawZ(A + UE_DOUBLE_HALF_PI) * ((T - 1.5) * 0.58);
				M.Cylinder(Tb, Up, 7.5, 0.3, 0.3, 10, OLIVE_DK);
				Launchers.Add(Base + Tb + Up * 7.6);
			}
			// berm on three sides
			for (int32 S = 0; S < 3; ++S)
			{
				const double Ba = A + UE_DOUBLE_HALF_PI + S * UE_DOUBLE_HALF_PI;
				const FVector Bc = P + YawZ(Ba) * (S == 1 ? 9.0 : 5.5);
				M.BoxYaw(FVector(Bc.X, GroundL(Bc) + 0.6, Bc.Z), Ba, FVector(S == 1 ? 4.5 : 9.5, 1.6, 1.3), EARTH);
			}
			Hit(FVector(P.X, Yt + 2.5, P.Z), A + UE_DOUBLE_HALF_PI, FVector(7.0, 2.5, 1.6), TEXT("TEL"));
		}
		// command post + generators
		for (int32 K = 0; K < 2; ++K)
		{
			const FVector P = YawZ(Fy + UE_DOUBLE_PI) * 30.0 + YawX(Fy) * (K * 14.0 - 7.0);
			const double Yc = Pad(P, Fy, 6.0, 2.0);
			Truck(M, FVector(P.X, Yc, P.Z), Fy, 11.0, 3, OLIVE_DK);
			M.BoxYaw(FVector(P.X, Yc + 2.8, P.Z), Fy, FVector(3.2, 1.2, 1.25), OLIVE);
		}
		break;
	}
	case EOsrSiteType::Pantsir:
	case EOsrSiteType::Tor:
	{
		const bool bTor = Type == EOsrSiteType::Tor;
		const double Y = Pad(FVector::ZeroVector, Fy, 7.0, 3.0);
		if (bTor)
		{
			// tracked chassis
			M.BoxYaw(FVector(0, Y + 1.3, 0), Fy, FVector(3.6, 0.9, 1.6), OLIVE);
			for (double Sd : {-1.0, 1.0}) { M.Box(FVector(0, Y + 0.6, 0) + YawZ(Fy) * (Sd * 1.55), YawX(Fy), Up, YawZ(Fy), FVector(3.7, 0.6, 0.35), TIRE); }
		}
		else
		{
			Truck(M, FVector(0, Y, 0), Fy, 11.5, 4, OLIVE);
		}
		// trainable turret (rotor): missile packs, guns, radars
		Rotor->SetRelativeLocation(ToUE(FVector(0, Y + (bTor ? 2.2 : 2.9), 0) + YawX(Fy) * (bTor ? 0.0 : -2.2)));
		R.Box(FVector(0, 0.9, 0), FVector(1, 0, 0), Up, FVector(0, 0, 1), FVector(1.6, 0.9, 1.3), OLIVE_DK);
		if (bTor)
		{
			R.Box(FVector(-0.4, 2.0, 0), FVector(1, 0, 0), Up, FVector(0, 0, 1), FVector(1.1, 0.4, 1.0), OLIVE);      // vertical launch cells
			R.Box(FVector(-0.2, 3.4, 0), FVector(1, 0, 0), Up, FVector(0, 0, 1), FVector(0.2, 0.9, 1.4), PANEL);    // search radar
			R.Box(FVector(1.7, 1.0, 0), FVector(1, 0, 0), Up, FVector(0, 0, 1), FVector(0.25, 0.8, 0.8), PANEL);    // tracking radar
		}
		else
		{
			for (double Sd : {-1.0, 1.0})
			{
				// 6-tube missile packs, elevated
				const FVector Ex = FVector(0.94, 0.34, 0).GetSafeNormal();
				R.Box(FVector(0.2, 1.4, Sd * 1.9), Ex, FVector(-0.34, 0.94, 0).GetSafeNormal(), FVector(0, 0, 1), FVector(1.8, 0.45, 0.55), OLIVE);
				R.Cylinder(FVector(0.5, 0.7, Sd * 1.45), FVector(1, 0, 0), 2.6, 0.08, 0.06, 6, STEEL);    // 30 mm guns
			}
			R.Box(FVector(-0.6, 2.4, 0), FVector(1, 0, 0), Up, FVector(0, 0, 1), FVector(0.25, 0.8, 1.2), PANEL);   // search radar
			R.Cylinder(FVector(1.55, 1.1, 0), FVector(1, 0, 0), 0.25, 0.75, 0.75, 14, PANEL);                        // tracking radar dish
		}
		Antenna = Base + FVector(0, Y + 5.5, 0);
		Hit(FVector(0, Y + 2.0, 0), Fy, FVector(bTor ? 3.6 : 5.7, 2.2, 1.6), bTor ? TEXT("TOR") : TEXT("PANTSIR"));
		// camouflage net frame + crew tent
		M.BoxYaw(YawZ(Fy) * 9.0 + FVector(0, GroundL(YawZ(Fy) * 9.0) + 1.2, 0), Fy, FVector(2.5, 1.2, 1.8), OLIVE_DK);
		break;
	}
	}
	ClearZones.Reset();
	ClearZones.Add(FVector4(Base.X, Base.Y, Base.Z, Type == EOsrSiteType::S400 ? 110.0 : 35.0));
	M.Commit(Pmc, 0, LitMaterial());
	R.Commit(Rotor, 0, LitMaterial());
	if (Launchers.Num() == 0) { Launchers.Add(Antenna + FVector(0, -2.0, 0)); }
}

// ---------------------------------------------------------------- detection

double AOsrRadarSite::DetectionChance(const FVector& P, const FVector& V, bool& bLos, FString* Why) const
{
	bLos = false;
	const FVector Rel = P - Antenna;
	const double R = Rel.Size();
	// stealth aspect: nose-on the F-35 is smallest (shorter burn-through range)
	const FVector ToRadar = -Rel / FMath::Max(R, 1.0);
	const double NoseOn = V.Size() > 1.0 ? FVector::DotProduct(V.GetSafeNormal(), ToRadar) : 0.0;
	const double R0 = DetectRange * (NoseOn > 0.8 ? 0.72 : 1.0);
	if (R > R0) { if (Why) { *Why = TEXT("range"); } return 0.0; }
	if (R > RadarHorizonM(Antenna.Y, P.Y)) { if (Why) { *Why = TEXT("horizon"); } return 0.0; }
	if (!LineOfSight(Antenna, P, 3.0)) { if (Why) { *Why = TEXT("terrain"); } return 0.0; }
	bLos = true;
	const double Rf = 1.0 - Sm(0.55 * R0, R0, R);
	const double Agl = P.Y - Ground(P.X, P.Z);
	const double Speed = V.Size();
	// pulse-Doppler notch: a beaming target (low radial speed) sinks into the clutter filter, only when there is clutter behind it
	const bool bClutter = Agl < 300.0 || P.Y < Antenna.Y;
	const double Vr = FMath::Abs(FVector::DotProduct(V, Rel / FMath::Max(R, 1.0)));
	const double Nf = bClutter ? FMath::Lerp(0.12, 1.0, Sm(18.0, 60.0, Vr)) : 1.0;
	// low-level clutter: very low + slow is hard to separate from the ground
	double Cf = FMath::Lerp(0.35, 1.0, Sm(8.0, 60.0, Agl));
	Cf *= FMath::Lerp(0.55, 1.0, Sm(90.0, 200.0, Speed)) * (Agl < 60.0 ? 1.0 : 1.0);
	if (Agl >= 60.0) { Cf = 1.0; }
	if (Why) { *Why = FString::Printf(TEXT("pd rf=%.2f notch=%.2f clutter=%.2f"), Rf, Nf, Cf); }
	return FMath::Clamp(Rf * Nf * Cf, 0.0, 1.0);
}

const AOsrRadarSite::FTrack* AOsrRadarSite::TrackOf(const AActor* A) const
{
	for (const FTrack& T : Tracks) { if (T.Target.Get() == A) { return &T; } }
	return nullptr;
}

AOsrRadarSite::FTrack* AOsrRadarSite::TrackOf(const AActor* A)
{
	for (FTrack& T : Tracks) { if (T.Target.Get() == A) { return &T; } }
	return nullptr;
}

bool AOsrRadarSite::IsGuiding(const AActor* Target) const
{
	const FTrack* T = TrackOf(Target);
	return !bDead && bEmitting && T && T->Q >= 0.55 && T->bLos;
}

void AOsrRadarSite::OnChaff(const AActor* Target, const FVector& TargetVel)
{
	FTrack* T = TrackOf(Target);
	if (!T || T->Q <= 0.0) { return; }
	const AOsrJetPawn* J = Cast<AOsrJetPawn>(Target);
	const FVector P = OsrCombat::JetPos(J);
	const FVector Rel = (P - Antenna).GetSafeNormal();
	const double Vr = FMath::Abs(FVector::DotProduct(TargetVel, Rel));
	const double Beam = 1.0 - Sm(40.0, 160.0, Vr);
	const double Low = 1.0 - Sm(30.0, 200.0, P.Y - Ground(P.X, P.Z));
	const double PBreak = FMath::Clamp(0.22 + 0.45 * Beam + 0.15 * Low, 0.0, 0.85);
	if (FMath::FRand() < PBreak)
	{
		// the tracker walks off onto the chaff cloud and needs a few seconds to re-acquire
		T->Q = FMath::Min(T->Q, 0.25);
		T->LockTime = 0.0;
		T->ChaffUntil = Now + 2.5;
	}
}

void AOsrRadarSite::UpdateTracks(double Dt)
{
	if (bDead || !bEmitting) { for (FTrack& T : Tracks) { T.Q = 0.0; T.LockTime = 0.0; } return; }
	const bool bSearch = Type == EOsrSiteType::SearchRadar;
	ScanAcc += Dt;
	const bool bLook = ScanAcc >= ScanPeriod;
	if (!bLook) { return; }
	const double Step = ScanAcc;
	ScanAcc = 0.0;
	for (AOsrJetPawn* J : OsrCombat::Jets(GetWorld()))
	{
		if (!OsrCombat::JetAirborne(J)) { continue; }
		FTrack* T = TrackOf(J);
		if (!T) { T = &Tracks.AddDefaulted_GetRef(); T->Target = J; }
		bool bLos = false;
		const double Pd = DetectionChance(OsrCombat::JetPos(J), OsrCombat::JetVel(J), bLos);
		T->bLos = bLos;
		if (bSearch)
		{
			if (FMath::FRand() < Pd) { T->Q = FMath::Min(1.0, T->Q + 0.5); T->LastSeen = Now; }
			else { T->Q = FMath::Max(0.0, T->Q - 0.2); }
		}
		else
		{
			double CueF = 1.0;
			if (const AOsrRadarSite* C = Cue.Get())
			{
				if (const FTrack* Ct = C->TrackOf(J)) { if (Ct->Q >= 0.5) { CueF = 1.7; } }
			}
			if (bLos && Now >= T->ChaffUntil) { T->Q = FMath::Clamp(T->Q + Step * (1.35 * Pd * CueF - 0.3), 0.0, 1.0); T->LastSeen = Now; }
			else if (bLos) { T->Q = FMath::Max(0.0, T->Q - Step * 0.2); }
			else { T->Q = FMath::Max(0.0, T->Q - Step * 0.45); }
			T->LockTime = T->Q >= 0.95 ? T->LockTime + Step : 0.0;
		}
		if (T->Q >= (bSearch ? 0.5 : 0.95) && !T->bCounted)
		{
			T->bCounted = true;
			if (AOsrMissionState* Ms = AOsrMissionState::Get(this)) { Ms->NotifyDetected(J, DisplayName); }
		}
		if (T->Q <= 0.05) { T->bCounted = false; }
	}
	Tracks.RemoveAll([](const FTrack& T) { return !T.Target.IsValid(); });
}

// ---------------------------------------------------------------- weapons

void AOsrRadarSite::UpdateLaunches()
{
	if (bDead || Missiles <= 0 || !OsrCombat::WeaponsFree(GetWorld())) { return; }
	for (FTrack& T : Tracks)
	{
		AOsrJetPawn* J = Cast<AOsrJetPawn>(T.Target.Get());
		if (!OsrCombat::JetAirborne(J)) { continue; }
		const double NeedLock = Type == EOsrSiteType::S400 ? 3.0 : 2.0;
		if (T.LockTime < NeedLock || !T.bLos) { continue; }
		T.InFlight = 0;
		for (AOsrMissile* M : AOsrMissile::HomingOn(GetWorld(), J)) { if (M->Site.Get() == this) { ++T.InFlight; } }
		if (T.InFlight >= MaxInFlight || Now - T.LastLaunch < LaunchInterval) { continue; }
		const FVector P = OsrCombat::JetPos(J), V = OsrCombat::JetVel(J);
		const double R = FVector::Dist(P, Antenna);
		const double Recede = FMath::Clamp(FVector::DotProduct(V, (P - Antenna).GetSafeNormal()) / 300.0, 0.0, 1.0);
		const double Reach = MaxRange * (1.0 - 0.45 * Recede);
		const double Agl = P.Y - Ground(P.X, P.Z);
		if (R > Reach || R < MinRange || Agl < MinAlt) { continue; }
		T.LastLaunch = Now;
		Launch(J);
	}
}

void AOsrRadarSite::Launch(AActor* Target)
{
	if (Launchers.Num() == 0) { return; }
	const FVector From = Launchers[NextTube++ % Launchers.Num()];
	const AOsrJetPawn* J = Cast<AOsrJetPawn>(Target);
	const FVector Tp = OsrCombat::JetPos(J);
	FVector Dir;
	double Speed;
	EOsrMissileKind Kind;
	switch (Type)
	{
	case EOsrSiteType::S400: Dir = FVector(0, 1, 0); Speed = 35.0; Kind = EOsrMissileKind::S400_9M96; break;          // cold vertical launch
	case EOsrSiteType::Tor:  Dir = FVector(0, 1, 0); Speed = 30.0; Kind = EOsrMissileKind::Tor_9M331; break;
	default:
	{
		FVector To = (Tp - From).GetSafeNormal();
		To.Y = FMath::Max(To.Y, 0.25);
		Dir = To.GetSafeNormal(); Speed = 60.0; Kind = EOsrMissileKind::Pantsir_57E6; break;
	}
	}
	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AOsrMissile* M = GetWorld()->SpawnActor<AOsrMissile>(AOsrMissile::StaticClass(), FTransform(ToUE(From)), Sp);
	if (!M) { return; }
	M->Launch(Kind, From, Dir, Speed, Target, this, this);
	--Missiles;
	if (AOsrMissionState* Ms = AOsrMissionState::Get(this)) { ++Ms->Stats.SamsLaunched; }
	if (AOsrMissionFx* Fx = AOsrMissionFx::Get(this))
	{
		Fx->SoundAt("sam_launch", From, 4.0);
		for (int32 K = 0; K < 10; ++K)
		{
			Fx->Puff(From, FVector(FMath::FRandRange(-12.0, 12.0), FMath::FRandRange(0.0, 8.0), FMath::FRandRange(-12.0, 12.0)), 5.0, 3.0, 16.0, FLinearColor(0.7f, 0.7f, 0.68f), 0.7, false, 0.8, 1.0);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("REPORT sam_launch site=%s range_km=%.1f"), *DisplayName, FVector::Dist(From, Tp) / 1000.0);
}

void AOsrRadarSite::UpdateGuns(double Dt)
{
	if (Type != EOsrSiteType::Pantsir || bDead || !OsrCombat::WeaponsFree(GetWorld())) { return; }
	AOsrMissionFx* Fx = AOsrMissionFx::Get(this);
	for (FTrack& T : Tracks)
	{
		AOsrJetPawn* J = Cast<AOsrJetPawn>(T.Target.Get());
		if (!OsrCombat::JetAirborne(J) || T.Q < 0.95 || !T.bLos) { continue; }
		const FVector P = OsrCombat::JetPos(J), V = OsrCombat::JetVel(J);
		const double R = FVector::Dist(P, Antenna);
		if (R > 4000.0) { continue; }
		GunAcc += Dt;
		// visual: tracer stream towards the lead point with dispersion (2 x 2A38M, 2,500 rds/min each)
		const FVector Muzzle = Antenna + FVector(0, -3.0, 0);
		const double Tof = R / 960.0;
		const FVector Lead = P + V * Tof + FVector(0, 0.5 * 9.81 * Tof * Tof, 0);
		static double TracerAcc = 0.0;
		TracerAcc += Dt;
		while (TracerAcc > 0.06)
		{
			TracerAcc -= 0.06;
			const double Disp = 0.012;
			const FVector D = ((Lead - Muzzle).GetSafeNormal() + FVector(FMath::FRandRange(-Disp, Disp), FMath::FRandRange(-Disp, Disp), FMath::FRandRange(-Disp, Disp))).GetSafeNormal();
			if (Fx) { Fx->Tracer(Muzzle, D * 960.0, FMath::Min(Tof * 1.4, 5.0)); }
		}
		if (GunAcc > 1.0)
		{
			GunAcc = 0.0;
			if (Fx) { Fx->SoundAt("gun_burst", Muzzle, 0.0, 0.8); }
			const double Speed = V.Size();
			const double P1 = 0.3 * FMath::Pow(1.0 - R / 4000.0, 1.5) * FMath::Lerp(1.0, 0.45, Sm(150.0, 320.0, Speed)) * (FMath::Abs(J->Model.Nz) > 4.0 ? 0.6 : 1.0);
			if (FMath::FRand() < P1)
			{
				if (Fx) { Fx->Explosion(P, EOsrBlast::Aircraft); }
				OsrCombat::KillJet(J, P, TEXT("30 mm AAA (Pantsir)"));
			}
		}
		return;   // one gun target at a time
	}
	GunAcc = 0.0;
}

void AOsrRadarSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FMath::Min<double>(DeltaSeconds, 0.1);
	Now += Dt;
	if (bDead) { return; }
	UpdateTracks(Dt);
	UpdateLaunches();
	UpdateGuns(Dt);
	// antenna / turret motion: search spins, trackers train onto their best track
	const FTrack* Best = nullptr;
	for (const FTrack& T : Tracks) { if (!Best || T.Q > Best->Q) { Best = &T; } }
	if (Type == EOsrSiteType::SearchRadar || !Best || Best->Q < 0.3)
	{
		RotorAngle += Dt * (Type == EOsrSiteType::SearchRadar ? 72.0 : 25.0);
	}
	else if (const AOsrJetPawn* J = Cast<AOsrJetPawn>(Best->Target.Get()))
	{
		const FVector D = OsrCombat::JetPos(J) - Base;
		const double Want = FMath::RadiansToDegrees(FMath::Atan2(D.Z, D.X));      // UE yaw: X east, Y(=Godot z) south
		RotorAngle = FMath::FixedTurn(RotorAngle, Want + (Type == EOsrSiteType::S400 ? -90.0 : 0.0), Dt * 90.0);
	}
	Rotor->SetRelativeRotation(FRotator(0.0, RotorAngle, 0.0));
}

void AOsrRadarSite::OnKilled(AActor* By)
{
	bEmitting = false;
	Tracks.Reset();
	if (AOsrMissionFx* Fx = AOsrMissionFx::Get(this)) { Fx->Explosion(Antenna, EOsrBlast::Secondary); }
	Rotor->SetVisibility(false);
	if (AOsrMissionState* Ms = AOsrMissionState::Get(this))
	{
		++Ms->Stats.GroundKills;
		Ms->Radio(TEXT("WATCHTOWER"), FString::Printf(TEXT("%s destroyed."), *DisplayName), NAME_None, 4.0);
	}
}

// ---------------------------------------------------------------- RWR

bool AOsrRadarSite::RwrFor(const AActor* Receiver, FOsrRwrContact& Out) const
{
	if (bDead || !bEmitting) { return false; }
	const AOsrJetPawn* J = Cast<AOsrJetPawn>(Receiver);
	if (!J) { return false; }
	const FVector P = OsrCombat::JetPos(J);
	const double R = FVector::Dist(P, Antenna);
	if (R > DetectRange * 1.8 || R > RadarHorizonM(Antenna.Y, P.Y) * 1.0) { return false; }
	const FTrack* T = TrackOf(Receiver);
	// the receiver hears the emitter whenever it is illuminated (LOS); cached from the last look, else a fresh check
	const bool bLos = T ? T->bLos : LineOfSight(Antenna, P, 3.0);
	if (!bLos) { return false; }
	Out.Symbol = Symbol;
	Out.From = Antenna;
	Out.State = EOsrRwr::Search;
	if (T && T->Q >= 0.6 && Type != EOsrSiteType::SearchRadar) { Out.State = EOsrRwr::Track; }
	for (AOsrMissile* M : AOsrMissile::HomingOn(GetWorld(), Receiver))
	{
		if (M->Site.Get() == this) { Out.State = EOsrRwr::Launch; }
	}
	const double Lethal = Type == EOsrSiteType::SearchRadar ? DetectRange : MaxRange;
	Out.Severity = FMath::Clamp(1.0 - R / (Lethal * 1.4), 0.0, 1.0);
	return true;
}

TArray<AOsrRadarSite*> AOsrRadarSite::All(UWorld* W)
{
	TArray<AOsrRadarSite*> Out;
	if (!W) { return Out; }
	for (TActorIterator<AOsrRadarSite> It(W); It; ++It) { Out.Add(*It); }
	return Out;
}

void AOsrRadarSite::RwrPicture(UWorld* W, const AActor* Receiver, TArray<FOsrRwrContact>& Out)
{
	Out.Reset();
	for (AOsrRadarSite* S : All(W))
	{
		FOsrRwrContact C;
		if (S->RwrFor(Receiver, C)) { Out.Add(C); }
	}
}
