// Mission VFX + positional audio (local presentation only, one per world): camera-facing puffs (fire, smoke, dust, flares, sparks),
// ribbon trails (missile smoke, flare trails), bullet tracers, explosion flashes (pooled point lights) and distance-delayed sounds.
// Everything is rebuilt each frame into two procedural-mesh sections (additive + translucent) — a few thousand vertices at most.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MissionFx.generated.h"

class UProceduralMeshComponent;
class UPointLightComponent;
class USoundBase;
class USoundAttenuation;
class UMaterialInstanceDynamic;

UENUM()
enum class EOsrBlast : uint8 { Small, Bomb, Missile, Aircraft, Secondary, Huge };

UCLASS()
class SILENTRIDGEUE_API AOsrMissionFx : public AActor
{
	GENERATED_BODY()
public:
	AOsrMissionFx();
	static AOsrMissionFx* Get(const UObject* WorldContext);

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	/** Godot-frame positions/velocities (m, m/s). */
	void Puff(const FVector& P, const FVector& V, double Life, double R0, double R1, const FLinearColor& Col, double Alpha, bool bAdditive, double Drag = 1.0, double Rise = 0.0);
	void Explosion(const FVector& P, EOsrBlast Kind, const FVector& Normal = FVector(0, 1, 0));
	void GroundImpact(const FVector& P, bool bSnow, double Scale = 1.0);
	void Tracer(const FVector& P, const FVector& V, double Life);
	/** Ribbon trail: returns an id; feed points while alive; EndTrail lets it fade out. */
	int32 BeginTrail(double Life, double W0, double W1, const FLinearColor& Col, double Alpha, bool bAdditive = false);
	void TrailPoint(int32 Id, const FVector& P);
	void EndTrail(int32 Id);
	/** Sound at a Godot-frame location, delayed by the distance to the listener (speed of sound). */
	void SoundAt(FName Sound, const FVector& P, double VolDb = 0.0, double Pitch = 1.0, bool bDelay = true);
	void Sound2D(FName Sound, double VolDb = 0.0, double Pitch = 1.0);
	/** Camera shake impulse for nearby blasts (read by the HUD/camera owner). */
	double Shake = 0.0;

	FVector CameraPosG() const;

private:
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Pmc;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lights;
	UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> Sounds;
	UPROPERTY() TObjectPtr<USoundAttenuation> Atten;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatAdd;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MatTr;

	struct FPuff { FVector P, V; double Age, Life, R0, R1, Alpha, Drag, Rise; FLinearColor C; bool bAdd; float Rot; };
	struct FTrail { int32 Id; TArray<FVector> P; TArray<double> T; double Life, W0, W1, Alpha; FLinearColor C; bool bAdd; bool bOpen; double LastT; };
	struct FTracer { FVector P, V; double Age, Life; };
	struct FFlash { int32 Light; double Age, Life, Peak; };
	struct FPendingSound { FName Sound; FVector P; double Delay, Vol, Pitch; };
	TArray<FPuff> Puffs;
	TArray<FTrail> Trails;
	TArray<FTracer> Tracers;
	TArray<FFlash> Flashes;
	TArray<FPendingSound> Pending;
	int32 NextTrail = 1;
	int32 NextLight = 0;
	double Now = 0.0;
	FRandomStream Rng;
	void LoadSounds();
	void Flash(const FVector& P, double Peak, double Life, const FLinearColor& Col);
	void PlayNow(FName Sound, const FVector& P, double Vol, double Pitch);
	void Rebuild();
};
