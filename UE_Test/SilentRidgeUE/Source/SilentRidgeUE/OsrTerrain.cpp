// Loads Content/Data/terrain.osrh (OSR2, written by scripts/terrain_v2/export_tiles.py) and samples it exactly like the tile meshes are triangulated.
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
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 24)
	{
		UE_LOG(LogTemp, Error, TEXT("OSR terrain data missing: %s"), *Path);
		return false;
	}
	if (FMemory::Memcmp(Bytes.GetData(), "OSR2", 4) != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("OSR terrain data: not OSR2"));
		return false;
	}
	uint32 N = 0;
	float Lc = 0.f, Fc = 0.f, Mn = 0.f, Rg = 0.f;
	FMemory::Memcpy(&N, Bytes.GetData() + 4, 4);
	FMemory::Memcpy(&Lc, Bytes.GetData() + 8, 4);
	FMemory::Memcpy(&Fc, Bytes.GetData() + 12, 4);
	FMemory::Memcpy(&Mn, Bytes.GetData() + 16, 4);
	FMemory::Memcpy(&Rg, Bytes.GetData() + 20, 4);
	Lat = int32(N); LatCell = Lc; Fine = Fc; HMin = Mn; HRange = Rg;
	const int64 TableAt = 24, DataAt = TableAt + int64(Lat) * Lat * 12;
	if (Bytes.Num() < DataAt) { return false; }
	const int32 FineNodes = FMath::RoundToInt32(LatCell / Fine);
	Cells.SetNum(Lat * Lat);
	int64 MaxEnd = 0;
	for (int32 K = 0; K < Lat * Lat; ++K)
	{
		uint32 St = 0; uint64 Off = 0;
		FMemory::Memcpy(&St, Bytes.GetData() + TableAt + K * 12, 4);
		FMemory::Memcpy(&Off, Bytes.GetData() + TableAt + K * 12 + 4, 8);
		FCell& C = Cells[K];
		C.Step = int32(St);
		C.N = St > 0 ? FineNodes / int32(St) + 1 : 0;
		C.Offset = int64(Off) / 2;
		MaxEnd = FMath::Max<int64>(MaxEnd, int64(Off) + int64(C.N) * C.N * 2);
	}
	if (Bytes.Num() < DataAt + MaxEnd) { return false; }
	Samples.SetNumUninitialized(int32(MaxEnd / 2));
	FMemory::Memcpy(Samples.GetData(), Bytes.GetData() + DataAt, MaxEnd);
	Loaded = true;
	UE_LOG(LogTemp, Display, TEXT("OSR terrain v2 loaded: %dx%d lattice, %.1f MB samples"), Lat, Lat, MaxEnd / 1048576.0);
	return true;
}

double FOsrTerrain::HeightAt(double X, double Z) const
{
	if (!Loaded) { return -1000.0; }
	const double Lx = (X + MapHalf) / LatCell, Lz = (Z + MapHalf) / LatCell;
	if (Lx < 0.0 || Lz < 0.0 || Lx >= Lat || Lz >= Lat) { return -1000.0; }
	const int32 Ci = FMath::Min(int32(Lx), Lat - 1), Cj = FMath::Min(int32(Lz), Lat - 1);
	const FCell& C = Cells[Cj * Lat + Ci];
	if (C.Step <= 0) { return -1000.0; }
	const double Cell = Fine * C.Step;
	const double U = FMath::Clamp((Lx - Ci) * LatCell / Cell, 0.0, double(C.N - 1));
	const double V = FMath::Clamp((Lz - Cj) * LatCell / Cell, 0.0, double(C.N - 1));
	const int32 I0 = FMath::Min(int32(U), C.N - 2), J0 = FMath::Min(int32(V), C.N - 2);
	const double Fx = U - I0, Fz = V - J0;
	auto S = [&](int32 I, int32 J) { return HMin + HRange * double(Samples[int32(C.Offset + int64(J) * C.N + I)]) / 65535.0; };
	const double H00 = S(I0, J0), H10 = S(I0 + 1, J0), H01 = S(I0, J0 + 1), H11 = S(I0 + 1, J0 + 1);
	// triangles (i,j)-(i,j+1)-(i+1,j) and (i+1,j)-(i,j+1)-(i+1,j+1), as in export_tiles.py
	if (Fx + Fz <= 1.0)
	{
		return H00 + Fx * (H10 - H00) + Fz * (H01 - H00);
	}
	return H11 + (1.0 - Fx) * (H01 - H11) + (1.0 - Fz) * (H10 - H11);
}
