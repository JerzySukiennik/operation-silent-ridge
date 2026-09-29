// Combat plumbing shared by weapons, SAMs and fighters: a damageable-actor base (segment hit tests on simple volumes),
// scene queries (trace a bullet/bomb segment through everything), and helpers to treat player jets as targets.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MissionCombat.generated.h"

class AOsrJetPawn;

UENUM()
enum class EOsrDamage : uint8 { Bullet, Bomb, Missile, Blast };

/** Oriented box hit volume (Godot frame). */
struct FOsrHitBox
{
	FVector C = FVector::ZeroVector;
	FVector Ax = FVector(1, 0, 0), Ay = FVector(0, 1, 0), Az = FVector(0, 0, 1);
	FVector Half = FVector(1, 1, 1);
	FString Name;
	int32 Part = 0;
	/** Segment A->B against the box; returns the entry fraction in [0,1]. */
	bool Segment(const FVector& A, const FVector& B, double& TOut) const;
};

UCLASS(Abstract)
class SILENTRIDGEUE_API AOsrDamageable : public AActor
{
	GENERATED_BODY()
public:
	AOsrDamageable();
	/** Hit volumes in the Godot frame (static structures fill these once; moving ones refresh them). */
	TArray<FOsrHitBox> Boxes;
	double Health = 100.0;
	bool bDead = false;
	bool bAirTarget = false;
	FString DisplayName;

	virtual FVector PosG() const;
	virtual FVector VelG() const { return FVector::ZeroVector; }
	/** Nearest hit along A->B; Part = which box. */
	virtual bool SegmentHit(const FVector& A, const FVector& B, FVector& Hit, int32& Part) const;
	virtual void ApplyHit(const FVector& P, double Damage, EOsrDamage Kind, AActor* By, int32 Part);
	virtual void OnKilled(AActor* By) {}
};

namespace OsrCombat
{
	struct FTraceResult
	{
		bool bHit = false;
		bool bTerrain = false;
		FVector P = FVector::ZeroVector;
		AOsrDamageable* Actor = nullptr;
		AOsrJetPawn* Jet = nullptr;
		int32 Part = 0;
		double T = 1.0;
	};
	/** Segment through terrain, damageables and (optionally) player jets (radius JetR). Ignore = the shooter. */
	FTraceResult Trace(UWorld* W, const FVector& A, const FVector& B, const AActor* Ignore, bool bJets, double JetR = 7.0);
	/** Apply blast damage to damageables and jets within Radius (linear falloff). */
	void Blast(UWorld* W, const FVector& P, double Radius, double Damage, AActor* By);

	// player jets as targets
	TArray<AOsrJetPawn*> Jets(UWorld* W, bool bAliveOnly = true);
	FVector JetPos(const AOsrJetPawn* J);
	FVector JetVel(const AOsrJetPawn* J);
	bool JetAlive(const AOsrJetPawn* J);
	bool JetAirborne(const AOsrJetPawn* J);
	/** Destroys a player jet (shot down); the mission state records the cause. */
	void KillJet(AOsrJetPawn* J, const FVector& At, const FString& Cause);
	/** Weapons free for AI (off in the plain canyon autotest). */
	bool WeaponsFree(UWorld* W);
}
