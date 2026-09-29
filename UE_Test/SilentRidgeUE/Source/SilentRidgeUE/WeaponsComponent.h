// Per-jet weapons + sensors (attached to every AOsrJetPawn by the mission state): GAU-22/A 25 mm pod (220 rds, 3,300 rpm, tracers),
// 2 laser-guided bombs with release cues (LAR), targeting pod (full-screen TV view, zoom, slew, area/point track, laser arm/fire,
// auto-lase, per-player laser code), flares/chaff, RWR picture + tones, missile warning. Pad (DS4 HID + XInput) and keyboard input
// through its own mapping context (non-consuming, so the jet's own bindings keep working).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RadarSite.h"
#include "WeaponsComponent.generated.h"

class AOsrJetPawn;
class ACameraActor;
class UInputAction;
class UInputMappingContext;
class UAudioComponent;
class USoundBase;
class AOsrLaserBomb;

UENUM()
enum class EOsrWeapon : uint8 { Gun, Lgb };

UCLASS()
class SILENTRIDGEUE_API UOsrWeaponsComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UOsrWeaponsComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	static UOsrWeaponsComponent* Of(const AActor* Jet);
	void Rearm();

	// ---- loadout
	EOsrWeapon Selected = EOsrWeapon::Lgb;
	int32 GunRounds = 220;
	int32 Bombs = 2;
	int32 Flares = 30;
	int32 Chaff = 30;
	static constexpr int32 GUN_MAX = 220;
	static constexpr double GUN_RPS = 55.0;          // 3,300 rpm
	static constexpr double MUZZLE_V = 1036.0;       // m/s, PGU-47/U class

	// ---- targeting pod + laser
	bool bTgpView = false;
	bool bPointTrack = false;
	bool bTgpValid = false;         // pod has a ground point
	bool bMasked = false;           // pod line of sight blocked by the airframe gimbal limits
	bool bLaserArm = false;
	bool bLasing = false;
	bool bAutoLase = false;
	int32 LaserCode = 1688;         // own designator code (per player)
	int32 BombCode = 1688;          // code the next bomb seeks (another player's code = buddy lasing)
	FVector TgpPoint = FVector::ZeroVector;   // designated point (Godot)
	FString TrackName;
	int32 ZoomIdx = 0;
	static constexpr int32 ZOOM_N = 4;
	double TgpFovDeg() const;
	FVector PodPos() const;
	FVector TgpCamPos, TgpCamFwd, TgpCamUp;   // current TGP camera (Godot) for the HUD projection
	void SetBombLaserCode(int32 Code) { BombCode = Code; }

	// ---- release cues (LGB)
	bool bInRange = false;
	double TimeToRelease = -1.0;    // s until the release point, -1 unknown / out of reach
	double PredMiss = 1e9, PredTof = 0.0;
	FVector PredImpact = FVector::ZeroVector;
	double BombTti() const;         // of the first own bomb in flight (-1 none)

	// ---- threats
	TArray<FOsrRwrContact> Rwr;
	int32 MissilesInbound = 0;
	int32 IrInbound = 0;
	FVector NearestMissileDir = FVector::ZeroVector;
	double NearestMissileDist = 0.0;
	bool bMasterArm() const;

	// ---- commands (pad/keyboard, autotest, later network)
	void CmdSelect();
	void CmdToggleTgp();
	void CmdTrack();
	void CmdZoom();
	void CmdLaserArm();
	void CmdFlare();
	void CmdChaff();
	bool CmdReleaseBomb();
	void SetGunTrigger(bool bDown) { bTrigger = bDown; }
	void SlewTgp(const FVector2D& Rate, double Dt);
	/** Autotest / AI: designate a world point directly (point track). */
	void DesignatePoint(const FVector& P, bool bPoint = true);

private:
	UPROPERTY() TObjectPtr<ACameraActor> TgpCam;
	UPROPERTY() TObjectPtr<UInputMappingContext> Imc;
	UPROPERTY() TMap<FName, TObjectPtr<UInputAction>> Actions;
	UPROPERTY() TObjectPtr<UAudioComponent> GunAudio;
	UPROPERTY() TObjectPtr<UAudioComponent> LockTone;
	UPROPERTY() TObjectPtr<UAudioComponent> LaunchTone;
	UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> Sounds;
	AOsrJetPawn* Jet() const;
	bool bInputBound = false;
	TMap<FName, bool> Prev;
	bool bTrigger = false;
	double GunAcc = 0.0;
	struct FBullet { FVector P, V; double Age; bool bTracer; };
	TArray<FBullet> Bullets;
	int32 RoundCounter = 0;
	double CueAcc = 0.0, RwrAcc = 0.0, SearchChirpAcc = 0.0;
	double CmHold = 0.0, CmProgAcc = 0.0;
	int32 FlareLeft = 0, ChaffLeft = 0; double SalvoAcc = 0.0;
	TSet<TWeakObjectPtr<AActor>> KnownMissiles;
	TSet<FString> KnownEmitters;
	bool bWasPaused = false;
	double TgpHold = 0.0;
	bool bTgpHoldUsed = false;
	int32 LastRespawn = -1;
	void BindInput();
	double Act(FName N) const;
	bool Edge(FName N, bool bNow);
	void ReadInput(double Dt);
	void UpdateGun(double Dt);
	void UpdateTgp(double Dt);
	void UpdateCues(double Dt);
	void UpdateThreats(double Dt);
	void UpdateCountermeasures(double Dt);
	void DropOne(bool bFlare);
	void PlayUi(FName N, double VolDb = 0.0);
	bool IsLocal() const;
};
