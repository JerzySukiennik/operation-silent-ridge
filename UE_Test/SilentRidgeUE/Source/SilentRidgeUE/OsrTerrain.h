// Heightfield lookup matching the rendered Nanite terrain tiles (same R16 data, same per-tile grid step and triangle split); Godot frame (x east, z south, metres).
#pragma once

#include "CoreMinimal.h"

class SILENTRIDGEUE_API FOsrTerrain
{
public:
	static FOsrTerrain& Get();

	bool IsLoaded() const { return Samples.Num() > 0; }
	/** Terrain height (m), negative under the sea; -1000 outside the map. */
	double HeightAt(double X, double Z) const;
	/** max(height, sea level) */
	double SurfaceAt(double X, double Z) const { return FMath::Max(HeightAt(X, Z), 0.0); }

	static constexpr double MapHalf = 40000.0;

private:
	bool Load();
	double Sample(int32 I, int32 J) const;

	int32 Size = 0;
	int32 Tiles = 0;
	double HMin = -1024.0;
	double HRange = 5120.0;
	double Cell = 0.0;
	TArray<uint8> Steps;
	TArray<uint16> Samples;
};
