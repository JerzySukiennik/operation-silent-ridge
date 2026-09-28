// Heightfield lookup matching the rendered Nanite terrain tiles exactly (terrain v2: 2.5 km lattice, per-cell grid step, same
// quantised samples and triangle split as scripts/terrain_v2/export_tiles.py); Godot frame (x east, z south, metres).
#pragma once

#include "CoreMinimal.h"

class SILENTRIDGEUE_API FOsrTerrain
{
public:
	static FOsrTerrain& Get();

	bool IsLoaded() const { return Loaded; }
	/** Terrain height (m), negative under the sea; -1000 outside the map or over open sea without a tile. */
	double HeightAt(double X, double Z) const;
	/** max(height, sea level) */
	double SurfaceAt(double X, double Z) const { return FMath::Max(HeightAt(X, Z), 0.0); }

	static constexpr double MapHalf = 40000.0;

private:
	bool Load();
	struct FCell { int32 Step = 0; int32 N = 0; int64 Offset = 0; };

	bool Loaded = false;
	int32 Lat = 0;
	double LatCell = 2500.0;
	double Fine = 4.8828125;
	double HMin = -1024.0;
	double HRange = 5120.0;
	TArray<FCell> Cells;
	TArray<uint16> Samples;
};
