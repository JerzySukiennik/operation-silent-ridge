// Small enemy airbase in the basin NE of the target valley: 2.4 km runway draped on the terrain (max-filtered so it never sinks),
// taxiway + apron, hardened aircraft shelters, Quonset hangars, tower, fuel farm; holds the Su-57 pair and scrambles it.
#pragma once

#include "CoreMinimal.h"
#include "MissionCombat.h"
#include "EnemyAirbase.generated.h"

class UProceduralMeshComponent;
class AOsrEnemyFighter;

UCLASS()
class SILENTRIDGEUE_API AOsrEnemyAirbase : public AOsrDamageable
{
	GENERATED_BODY()
public:
	AOsrEnemyAirbase();
	virtual void BeginPlay() override;
	virtual FVector PosG() const override { return Centre; }
	static AOsrEnemyAirbase* Find(const UObject* WorldContext);

	void Scramble();
	void ResetBase();
	bool bScrambled = false;
	FVector Centre = FVector::ZeroVector;
	FVector RwyStart = FVector::ZeroVector, RwyDir = FVector(1, 0, 0);
	UPROPERTY() TArray<TObjectPtr<AOsrEnemyFighter>> Fighters;

private:
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Pmc;
	void Build();
};
