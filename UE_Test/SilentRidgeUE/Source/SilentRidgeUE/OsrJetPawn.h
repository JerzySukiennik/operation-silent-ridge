// Local F-35C pawn: 120 Hz FF35FlightModel, Enhanced Input gamepad layout from v1, chase camera with speed cues, VFX, jet audio, crash/respawn and the pause menu state.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "F35FlightModel.h"
#include "OsrControls.h"
#include "OsrJetPawn.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UPointLightComponent;
class UAudioComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class USoundBase;

/** Telemetry for HUD/audio (same keys as Godot Aircraft.telemetry()). */
struct FOsrTelemetry
{
	double IasKt = 0, TasKt = 0, Mach = 0, AltFt = 0, RadarAltFt = 0, AoaDeg = 0, G = 1, GMax = 1;
	double HeadingDeg = 0, PitchDeg = 0, RollDeg = 0, Throttle = 0, Lever = 0, AbLevel = 0, FuelKg = 0, VsFpm = 0;
	double TimeToImpact = 99, SpeedMs = 0;
	bool bAb = false, bGear = false, bBrake = false, bAlive = true;
};

UCLASS()
class SILENTRIDGEUE_API AOsrJetPawn : public APawn
{
	GENERATED_BODY()
public:
	AOsrJetPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;

	// ---- Godot-frame helpers (x east, y up, z south, metres)
	static FVector ToUE(const FVector& G) { return FVector(G.X, G.Z, G.Y) * 100.0; }
	static FVector DirToUE(const FVector& G) { return FVector(G.X, G.Z, G.Y); }
	static FVector FromUE(const FVector& U) { return FVector(U.X, U.Z, U.Y) * 0.01; }
	static FQuat RotToUE(const FQuat& GodotRot);
	static FQuat BasisQuat(const FVector& Right, const FVector& Up, const FVector& Back);
	static FQuat LookingAt(const FVector& Dir, const FVector& Up);

	void Respawn();
	void ApplyColorGrade();
	FOsrTelemetry Telemetry() const;
	double SurfaceHeight(double X, double Z) const;

	FF35FlightModel Model;
	FOsrControls Controls;
	bool bAlive = true;
	bool bGearDown = false;
	bool bBrake = false;
	double TimeToImpact = 99.0;

	// autotest / autopilot: when set, replaces the pilot
	bool bAutopilot = false;
	FOsrControlInput AutopilotInput;
	double AutopilotLever = 0.85;
	int32 CrashCount = 0;
	int32 RespawnCount = 0;

	// UI state read by the HUD
	bool bShowHelp = false;
	bool bPaused = false;
	int32 MenuIndex = 0;
	static constexpr int32 MenuCount = 4;
	FString MenuLabel(int32 Index) const;

	/** Camera state in UE space for the HUD projection. */
	FVector CamPosUE = FVector::ZeroVector;
	FQuat CamRotUE = FQuat::Identity;
	double CamFovV = 62.0;         // vertical FOV, degrees (Godot convention)

	/** Render transform (interpolated) in the Godot frame. */
	FVector RenderPos = FVector::ZeroVector;
	FQuat RenderRot = FQuat::Identity;

	// spawn: Godot world.gd spawn_points()[0]
	static FVector SpawnPosition();
	static FQuat SpawnRotation();
	void RespawnAt(const FVector& Pos, const FQuat& Rot, double Speed);

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> JetVfx;      // plume, glow, vapour, LEX, cone (attached to the jet)
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> WorldVfx;    // wingtip trails + speed streaks (world-anchored)
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> NozzleLight;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Blast;                  // explosion fireballs + smoke
	UPROPERTY() TMap<FName, TObjectPtr<UAudioComponent>> AudioLayers;
	UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> Sounds;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> VfxMats;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> BlastMats;
	UPROPERTY() TObjectPtr<UInputMappingContext> Imc;
	UPROPERTY() TMap<FName, TObjectPtr<UInputAction>> Actions;

private:
	// simulation
	double Accum = 0.0;
	FVector PrevPos, CurPos;
	FQuat PrevRot, CurRot;
	double WarnTimer = 0.0;
	double RespawnTimer = -1.0;
	void SimStep(double Dt);
	void CheckCollision();
	double PredictImpact() const;
	void Crash(const FVector& At);

	// input
	void CreateInput();
	double ActionValue(FName Name) const;
	FOsrRawInput ReadRaw() const;
	bool bStartPrev = false, bUpPrev = false, bDownPrev = false, bAPrev = false, bBPrev = false;
	void UpdateMenu();
	void ActivateMenu(int32 Index);
	void LoadSettings();
	void SaveSettings();

	// camera
	FQuat Frame = FQuat::Identity;
	FVector2D Orbit = FVector2D::ZeroVector, OrbitTarget = FVector2D::ZeroVector;
	double LookIdle = 0.0, ShakeT = 0.0, ShakeAmp = 0.0, Boom = 1.0, CrashT = 0.0, PrevTas = 0.0, AccelPull = 0.0;
	FVector CrashPoint = FVector::ZeroVector;
	double CrashAzimuth = 0.0;
	FQuat FollowRotation() const;
	void PlaceCamera(double Dt);
	void SnapCamera();
	double ShakeLevel() const;
	FVector TerrainClear(const FVector& Pivot, const FVector& CamPos, double Dt);
	void CrashView(double Dt);
	void ApplyCamera(const FVector& PosG, const FVector& FwdG, const FVector& UpG);

	// vfx
	struct FTrailPoint { FVector P; double T; };
	TArray<FTrailPoint> TrailPts[2];
	double TrailSample = 0.0;
	struct FStreak { FVector P; double Age; double Life; };
	TArray<FStreak> Streaks;
	double VfxTime = 0.0;
	double BlastT = -1.0;
	FVector BlastAt = FVector::ZeroVector;
	void BuildJetVfx();
	void UpdateJetVfx(double Dt);
	void UpdateWorldVfx(double Dt);
	void UpdateBlast(double Dt);
	void StartBlast(const FVector& AtG);

	// audio
	double Spool = 0.0, AbA = 0.0, EngineFade = 1.0;
	bool bAbWasLit = false;
	int32 GearState = -1;
	void SetupAudio();
	void UpdateAudio(double Dt);
	void SetLayer(FName N, double Gain, double Pitch);
	void PlayOneShot(FName N, double VolDb);
};
