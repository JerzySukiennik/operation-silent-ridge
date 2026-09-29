// Combat plumbing implementation.
#include "MissionCombat.h"
#include "MissionCommon.h"
#include "MissionState.h"
#include "OsrJetPawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"

using namespace OsrMission;

bool FOsrHitBox::Segment(const FVector& A, const FVector& B, double& TOut) const
{
	// slab test in box space
	const FVector D = B - A;
	const FVector O = A - C;
	const FVector Axes[3] = {Ax, Ay, Az};
	double T0 = 0.0, T1 = 1.0;
	for (int32 K = 0; K < 3; ++K)
	{
		const double Od = FVector::DotProduct(O, Axes[K]);
		const double Dd = FVector::DotProduct(D, Axes[K]);
		const double H = Half[K];
		if (FMath::Abs(Dd) < 1e-9)
		{
			if (Od < -H || Od > H) { return false; }
			continue;
		}
		double Ta = (-H - Od) / Dd, Tb = (H - Od) / Dd;
		if (Ta > Tb) { Swap(Ta, Tb); }
		T0 = FMath::Max(T0, Ta);
		T1 = FMath::Min(T1, Tb);
		if (T0 > T1) { return false; }
	}
	TOut = T0;
	return true;
}

AOsrDamageable::AOsrDamageable()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
}

FVector AOsrDamageable::PosG() const
{
	return FromUE(GetActorLocation());
}

bool AOsrDamageable::SegmentHit(const FVector& A, const FVector& B, FVector& Hit, int32& Part) const
{
	double Best = 2.0;
	for (int32 K = 0; K < Boxes.Num(); ++K)
	{
		double T;
		if (Boxes[K].Segment(A, B, T) && T < Best) { Best = T; Part = K; }
	}
	if (Best > 1.0) { return false; }
	Hit = A + (B - A) * Best;
	return true;
}

void AOsrDamageable::ApplyHit(const FVector& P, double Damage, EOsrDamage Kind, AActor* By, int32 Part)
{
	if (bDead) { return; }
	Health -= Damage;
	if (Health <= 0.0)
	{
		bDead = true;
		OnKilled(By);
	}
}

namespace OsrCombat
{
TArray<AOsrJetPawn*> Jets(UWorld* W, bool bAliveOnly)
{
	TArray<AOsrJetPawn*> Out;
	if (!W) { return Out; }
	for (TActorIterator<AOsrJetPawn> It(W); It; ++It)
	{
		if (!bAliveOnly || It->bAlive) { Out.Add(*It); }
	}
	return Out;
}

FVector JetPos(const AOsrJetPawn* J) { return J ? J->Model.Position : FVector::ZeroVector; }
FVector JetVel(const AOsrJetPawn* J) { return J ? J->Model.Velocity : FVector::ZeroVector; }
bool JetAlive(const AOsrJetPawn* J) { return J && J->bAlive; }

bool JetAirborne(const AOsrJetPawn* J)
{
	if (!JetAlive(J)) { return false; }
	const FVector P = J->Model.Position;
	// jets parked / taxiing on the carrier deck (20 m above the sea, carrier at 13 m/s) are not air targets
	(void)P;
	return J->Model.Tas > 40.0;
}

void KillJet(AOsrJetPawn* J, const FVector& At, const FString& Cause)
{
	if (!JetAlive(J)) { return; }
	AOsrMissionState* M = AOsrMissionState::Get(J);
	if (M && M->bAutotestInvulnerable)
	{
		++M->WouldBeKills;
		UE_LOG(LogTemp, Display, TEXT("REPORT would_be_killed cause=%s"), *Cause);
		return;
	}
	if (M) { M->NotifyPlayerKilled(J, Cause); }
	J->CrashAt(At);
}

bool WeaponsFree(UWorld* W)
{
	// the plain canyon autotest (-autotest without a mode) measures flight + rendering only
	static const bool bCanyonTest = FParse::Param(FCommandLine::Get(), TEXT("autotest")) || FParse::Param(FCommandLine::Get(), TEXT("peaceful"));
	return !bCanyonTest;
}

FTraceResult Trace(UWorld* W, const FVector& A, const FVector& B, const AActor* Ignore, bool bJets, double JetR)
{
	FTraceResult R;
	if (!W) { return R; }
	// terrain: coarse march along the segment (segments are short: one frame of a bullet/bomb)
	const double Len = (B - A).Size();
	{
		const int32 N = FMath::Clamp(int32(Len / 3.0), 1, 64);
		FVector Prev = A;
		double PrevM = A.Y - Ground(A.X, A.Z);
		for (int32 K = 1; K <= N; ++K)
		{
			const double F = double(K) / N;
			const FVector Q = FMath::Lerp(A, B, F);
			const double M = Q.Y - Ground(Q.X, Q.Z);
			if (M <= 0.0)
			{
				const double Fr = PrevM / FMath::Max(PrevM - M, 1e-6);
				R.bHit = true;
				R.bTerrain = true;
				R.T = (double(K - 1) + Fr) / N;
				R.P = FMath::Lerp(A, B, R.T);
				R.P.Y = Ground(R.P.X, R.P.Z);
				break;
			}
			Prev = Q;
			PrevM = M;
		}
	}
	const FVector Mid = (A + B) * 0.5;
	for (TActorIterator<AOsrDamageable> It(W); It; ++It)
	{
		AOsrDamageable* D = *It;
		if (D == Ignore || D->bDead || D->Boxes.Num() == 0) { continue; }
		if (FVector::DistSquared(D->PosG(), Mid) > FMath::Square(Len * 0.5 + 400.0)) { continue; }
		FVector H; int32 Part = 0;
		if (D->SegmentHit(A, B, H, Part))
		{
			const double T = (H - A).Size() / FMath::Max(Len, 1e-6);
			if (T < R.T || !R.bHit)
			{
				R.bHit = true; R.bTerrain = false; R.T = T; R.P = H; R.Actor = D; R.Jet = nullptr; R.Part = Part;
			}
		}
	}
	if (bJets)
	{
		for (AOsrJetPawn* J : Jets(W))
		{
			if (J == Ignore) { continue; }
			const FVector C = J->Model.Position;
			const FVector D = B - A;
			const double T = FMath::Clamp(FVector::DotProduct(C - A, D) / FMath::Max(D.SizeSquared(), 1e-9), 0.0, 1.0);
			if (FVector::DistSquared(A + D * T, C) < JetR * JetR && (!R.bHit || T < R.T))
			{
				R.bHit = true; R.bTerrain = false; R.T = T; R.P = A + D * T; R.Actor = nullptr; R.Jet = J;
			}
		}
	}
	return R;
}

void Blast(UWorld* W, const FVector& P, double Radius, double Damage, AActor* By)
{
	if (!W) { return; }
	for (TActorIterator<AOsrDamageable> It(W); It; ++It)
	{
		AOsrDamageable* D = *It;
		if (D->bDead) { continue; }
		double Best = TNumericLimits<double>::Max();
		int32 Part = 0;
		for (int32 K = 0; K < D->Boxes.Num(); ++K)
		{
			const FOsrHitBox& B = D->Boxes[K];
			const FVector O = P - B.C;
			const FVector L(FMath::Clamp(FVector::DotProduct(O, B.Ax), -B.Half.X, B.Half.X), FMath::Clamp(FVector::DotProduct(O, B.Ay), -B.Half.Y, B.Half.Y), FMath::Clamp(FVector::DotProduct(O, B.Az), -B.Half.Z, B.Half.Z));
			const FVector Q = B.C + B.Ax * L.X + B.Ay * L.Y + B.Az * L.Z;
			const double Dd = FVector::Dist(Q, P);
			if (Dd < Best) { Best = Dd; Part = K; }
		}
		if (Best < Radius) { D->ApplyHit(P, Damage * (1.0 - Best / Radius), EOsrDamage::Blast, By, Part); }
	}
}
}
