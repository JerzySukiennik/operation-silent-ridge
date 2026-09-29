// Mission state machine (team-level, one per world, replication-ready actor — no per-player singletons): briefing, phases
// Deck -> Launched -> Ingress -> Target -> Egress -> Recovery -> Success / Fail, stats, radio messages, laser-spot registry,
// countermeasure decoys, and the integration points for the carrier workstream (catapult launch, trap, bolter, deck spawn).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MissionState.generated.h"

class AOsrMissionTarget;
class USoundBase;

UENUM()
enum class EOsrMissionPhase : uint8 { Deck, Launched, Ingress, Target, Egress, Recovery, Success, Fail };

USTRUCT()
struct FOsrMissionStats
{
	GENERATED_BODY()
	UPROPERTY() double LaunchTime = -1.0;
	UPROPERTY() double EndTime = -1.0;
	UPROPERTY() int32 Detections = 0;       // times an enemy radar established a track on a player
	UPROPERTY() int32 SamsLaunched = 0;
	UPROPERTY() int32 SamsEvaded = 0;
	UPROPERTY() int32 AamsLaunched = 0;
	UPROPERTY() int32 BombsReleased = 0;
	UPROPERTY() int32 BombsOnTarget = 0;
	UPROPERTY() double BestMissM = -1.0;
	UPROPERTY() int32 RoundsFired = 0;
	UPROPERTY() int32 FlaresUsed = 0;
	UPROPERTY() int32 ChaffUsed = 0;
	UPROPERTY() int32 AirKills = 0;
	UPROPERTY() int32 GroundKills = 0;
	UPROPERTY() int32 Deaths = 0;
	UPROPERTY() FString LsoGrade;
	UPROPERTY() int32 Wire = 0;
	UPROPERTY() int32 Bolters = 0;
};

/** A laser designator spot (any player — or later a JTAC on the ground — can own one). Codes as real PRF codes (1111..1788). */
struct FOsrLaserSpot
{
	TWeakObjectPtr<AActor> Owner;
	int32 Code = 1688;
	bool bOn = false;
	FVector Spot = FVector::ZeroVector;     // Godot frame
	FVector From = FVector::ZeroVector;     // designator position
	double LastUpdate = 0.0;
};

/** Flare / chaff decoy in the air. */
struct FOsrDecoy
{
	int32 Id = 0;
	bool bFlare = true;
	FVector P = FVector::ZeroVector, V = FVector::ZeroVector;
	double Age = 0.0, Life = 4.0;
	TWeakObjectPtr<AActor> Owner;
	int32 TrailId = 0;
};

struct FOsrRadioLine
{
	FString Who, Text;
	double Time = 0.0, Duration = 6.0;
};

UCLASS()
class SILENTRIDGEUE_API AOsrMissionState : public AActor
{
	GENERATED_BODY()
public:
	AOsrMissionState();
	static AOsrMissionState* Get(const UObject* WorldContext);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- state
	UPROPERTY() EOsrMissionPhase Phase = EOsrMissionPhase::Deck;
	UPROPERTY() double PhaseTime = 0.0;
	UPROPERTY() double MissionTime = 0.0;
	UPROPERTY() bool bTargetDestroyed = false;
	UPROPERTY() bool bFightersScrambled = false;
	UPROPERTY() FOsrMissionStats Stats;
	UPROPERTY() FString FailReason;
	double BriefingUntil = 25.0;
	bool bBriefingDismissed = false;

	static const TCHAR* PhaseName(EOsrMissionPhase P);
	FString ObjectiveText(const AActor* ForPawn) const;
	FString BriefingText() const;
	void SetPhase(EOsrMissionPhase P);
	void ResetMission();

	// ---- integration contract with the carrier workstream (call these from the carrier/jet events)
	void NotifySpawnedOnDeck(APawn* Pawn);
	void NotifyCatapultLaunched(APawn* Pawn);
	void NotifyTrap(APawn* Pawn, const FString& Grade, int32 Wire);
	void NotifyBolter(APawn* Pawn);
	void NotifyPlayerKilled(APawn* Pawn, const FString& Cause);

	// ---- combat events
	void NotifyTargetDestroyed();
	void NotifyBombImpact(bool bOnTarget, double MissM, const FString& What);
	void NotifyDetected(const AActor* Target, const FString& By);

	// ---- radio (subtitles + voice)
	void Radio(const FString& Who, const FString& Text, FName Voice = NAME_None, double Duration = 6.0);
	TArray<FOsrRadioLine> RadioLines;

	// ---- lasers
	TArray<FOsrLaserSpot> Lasers;
	void SetLaser(AActor* ByOwner, int32 Code, bool bOn, const FVector& From, const FVector& Spot);
	/** Seeker search: a spot with this code inside the cone (Axis, HalfFov rad) from Seeker, with terrain line of sight. */
	bool FindLaserSpot(int32 Code, const FVector& Seeker, const FVector& Axis, double HalfFov, FVector& OutSpot) const;

	// ---- decoys
	TArray<FOsrDecoy> Decoys;
	int32 DropDecoy(AActor* ByOwner, bool bFlare, const FVector& P, const FVector& V);

	/** Debug keys (keyboard): F5 cat launch, F6 trap OK 3-wire, F7 bolter, F8 reset mission, F9 jump to the canyon, F10 jump to the target area. */
	bool bDebugKeys = true;
	/** Strike autotest: hits are logged (REPORT would_be_killed) instead of killing the jet, so one run covers the whole sortie. */
	bool bAutotestInvulnerable = false;
	int32 WouldBeKills = 0;

private:
	int32 NextDecoy = 1;
	bool bIngressCall = false, bSpikeCall = false, bFeetWetCall = false;
	UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> Voices;
	void EnsureWorld();
	void UpdatePhase(double Dt);
	void UpdateDecoys(double Dt);
	void DebugKeys();
	void PlayVoice(FName Voice);
	APawn* LocalPawn() const;
	bool bWorldSpawned = false;
	bool bCarrierBound = false;
	void BindCarrier();
	/** The jet the mission follows: the local player's jet, else any manned (player / autopilot) jet in the air. */
	class AOsrJetPawn* PlayerJet() const;
};
