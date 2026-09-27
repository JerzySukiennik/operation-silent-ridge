// Loads Content/Data/terrain.osrh (written by scripts/make_height_data.py) and samples it exactly like the tile meshes are triangulated.
#include "OsrTerrain.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FOsrTerrain& FOsrTerrain::Get()
{
	static FOsrTerrain Instance;
	if (!Instance.IsLoaded())
	{
		Instance.Load();
	}
	return Instance;
}

bool FOsrTerrain::Load()
{
	TArray<uint8> Bytes;
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/terrain.osrh");
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 20)
	{
		UE_LOG(LogTemp, Error, TEXT("OSR terrain data missing: %s"), *Path);
		return false;
	}
	if (FMemory::Memcmp(Bytes.GetData(), "OSRH", 4) != 0) { return false; }
	uint32 N = 0, T = 0;
	float Mn = 0.f, Rg = 0.f;
	FMemory::Memcpy(&N, Bytes.GetData() + 4, 4);
	FMemory::Memcpy(&T, Bytes.GetData() + 8, 4);
	FMemory::Memcpy(&Mn, Bytes.GetData() + 12, 4);
	FMemory::Memcpy(&Rg, Bytes.GetData() + 16, 4);
	const int64 Need = 20 + int64(T) * T + int64(N) * N * 2;
	if (Bytes.Num() < Need) { return false; }
	Size = int32(N);
	Tiles = int32(T);
	HMin = Mn;
	HRange = Rg;
	Cell = 2.0 * MapHalf / Size;
	Steps.SetNumUninitialized(Tiles * Tiles);
	FMemory::Memcpy(Steps.GetData(), Bytes.GetData() + 20, Tiles * Tiles);
	Samples.SetNumUninitialized(Size * Size);
	FMemory::Memcpy(Samples.GetData(), Bytes.GetData() + 20 + Tiles * Tiles, int64(Size) * Size * 2);
	UE_LOG(LogTemp, Display, TEXT("OSR terrain loaded %dx%d, %d tiles"), Size, Size, Tiles);
	return true;
}

double FOsrTerrain::Sample(int32 I, int32 J) const
{
	// the tile builder pads the grid by repeating the last row/column (index Size == Size-1)
	I = FMath::Clamp(I, 0, Size - 1);
	J = FMath::Clamp(J, 0, Size - 1);
	return HMin + HRange * double(Samples[J * Size + I]) / 65535.0;
}

double FOsrTerrain::HeightAt(double X, double Z) const
{
	if (!IsLoaded()) { return -1000.0; }
	const double U = (X + MapHalf) / Cell;
	const double V = (Z + MapHalf) / Cell;
	if (U < 0.0 || V < 0.0 || U >= Size || V >= Size) { return -1000.0; }
	const int32 TileSpan = Size / Tiles;
	const int32 Ti = FMath::Min(int32(U) / TileSpan, Tiles - 1);
	const int32 Tj = FMath::Min(int32(V) / TileSpan, Tiles - 1);
	int32 S = Steps[Tj * Tiles + Ti];
	if (S <= 0)
	{
		// no tile rendered here (open sea): report the data anyway (it is below sea level)
		S = 1;
	}
	const int32 I0 = int32(FMath::FloorToDouble(U / S)) * S;
	const int32 J0 = int32(FMath::FloorToDouble(V / S)) * S;
	const double Fx = (U - I0) / S;
	const double Fz = (V - J0) / S;
	const double H00 = Sample(I0, J0);
	const double H10 = Sample(I0 + S, J0);
	const double H01 = Sample(I0, J0 + S);
	const double H11 = Sample(I0 + S, J0 + S);
	// triangles (i,j)-(i,j+1)-(i+1,j) and (i+1,j)-(i,j+1)-(i+1,j+1), as in gen_terrain_tiles.py
	if (Fx + Fz <= 1.0)
	{
		return H00 + Fx * (H10 - H00) + Fz * (H01 - H00);
	}
	return H11 + (1.0 - Fx) * (H01 - H11) + (1.0 - Fz) * (H10 - H11);
}
