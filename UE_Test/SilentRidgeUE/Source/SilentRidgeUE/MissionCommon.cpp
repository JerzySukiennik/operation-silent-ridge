// Mission helpers: terrain LOS with 4/3-earth curvature (adaptive ray march on the OsrTerrain heightfield), ray casts, mesh builder.
#include "MissionCommon.h"
#include "OsrTerrain.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace OsrMission
{
double Ground(double X, double Z)
{
	return FOsrTerrain::Get().SurfaceAt(X, Z);
}

bool LineOfSight(const FVector& A, const FVector& B, double Clear)
{
	const FVector D = B - A;
	const double Hd = FVector2D(D.X, D.Z).Size();
	if (Hd < 1.0) { return true; }
	const FOsrTerrain& Ter = FOsrTerrain::Get();
	// adaptive march: terrain slopes stay below ~70 deg, so a vertical margin M allows a horizontal step of ~0.35 M
	double S = 8.0;
	while (S < Hd - 8.0)
	{
		const double F = S / Hd;
		const double Y = A.Y + D.Y * F;
		const double Drop = S * (Hd - S) / (2.0 * EARTH_R_EFF);      // curvature bulge of the earth under the chord
		const double G = Ter.SurfaceAt(A.X + D.X * F, A.Z + D.Z * F) + Drop;
		const double M = Y - G;
		if (M < Clear) { return false; }
		S += FMath::Clamp(M * 0.35, 6.0, 250.0);
	}
	return true;
}

bool RayTerrain(const FVector& Origin, const FVector& Dir, double MaxDist, FVector& Hit)
{
	const FVector U = Dir.GetSafeNormal();
	const FOsrTerrain& Ter = FOsrTerrain::Get();
	double S = 0.0, PrevS = 0.0;
	double PrevM = Origin.Y - Ter.SurfaceAt(Origin.X, Origin.Z);
	if (PrevM < 0.0) { Hit = Origin; return true; }
	while (S < MaxDist)
	{
		S += FMath::Clamp(PrevM * 0.35, 1.0, 200.0);
		const FVector P = Origin + U * S;
		const double M = P.Y - Ter.SurfaceAt(P.X, P.Z);
		if (M <= 0.0)
		{
			// bisect between PrevS and S
			double Lo = PrevS, Hi = S;
			for (int32 K = 0; K < 18; ++K)
			{
				const double Mid = 0.5 * (Lo + Hi);
				const FVector Q = Origin + U * Mid;
				if (Q.Y - Ter.SurfaceAt(Q.X, Q.Z) > 0.0) { Lo = Mid; } else { Hi = Mid; }
			}
			Hit = Origin + U * Hi;
			return true;
		}
		PrevS = S;
		PrevM = M;
	}
	return false;
}

void ClearVegetation(UWorld* W, const TArray<FVector4>& Spheres)
{
	if (!W || Spheres.Num() == 0) { return; }
	int32 Removed = 0;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		TInlineComponentArray<UInstancedStaticMeshComponent*> Isms;
		It->GetComponents(Isms);
		for (UInstancedStaticMeshComponent* C : Isms)
		{
			if (!C || C->GetInstanceCount() == 0 || !C->GetStaticMesh()) { continue; }
			const FString Mesh = C->GetStaticMesh()->GetName().ToLower();
			if (!Mesh.Contains(TEXT("fir")) && !Mesh.Contains(TEXT("rock")) && !Mesh.Contains(TEXT("boulder")) && !Mesh.Contains(TEXT("cliff"))) { continue; }
			TArray<int32> Kill;
			for (const FVector4& S : Spheres)
			{
				const FVector Cu = ToUE(FVector(S.X, S.Y, S.Z));
				if (!C->Bounds.GetBox().ExpandBy(S.W * 100.0).IsInside(Cu)) { continue; }
				Kill.Append(C->GetInstancesOverlappingSphere(Cu, float(S.W * 100.0), true));
			}
			if (Kill.Num() == 0) { continue; }
			Kill.Sort();
			for (int32 K = Kill.Num() - 1; K > 0; --K) { if (Kill[K] == Kill[K - 1]) { Kill.RemoveAt(K); } }
			C->RemoveInstances(Kill);
			Removed += Kill.Num();
		}
	}
	UE_LOG(LogTemp, Display, TEXT("OSR mission: cleared %d tree/rock instances in %d zones"), Removed, Spheres.Num());
}

UMaterialInterface* LitMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Cached;
	if (!Cached.IsValid())
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mission/M_MissionLit.M_MissionLit"));
		if (!M) { M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")); }
		Cached = M;
	}
	return Cached.Get();
}

UMaterialInterface* PuffMaterial(bool bAdditive)
{
	static TWeakObjectPtr<UMaterialInterface> Cached[2];
	TWeakObjectPtr<UMaterialInterface>& C = Cached[bAdditive ? 1 : 0];
	if (!C.IsValid())
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, bAdditive ? TEXT("/Game/Mission/M_MissionPuffAdd.M_MissionPuffAdd") : TEXT("/Game/Mission/M_MissionPuffTr.M_MissionPuffTr"));
		if (!M) { M = LoadObject<UMaterialInterface>(nullptr, bAdditive ? TEXT("/Game/VFX/M_VfxAdditive.M_VfxAdditive") : TEXT("/Game/VFX/M_VfxTranslucent.M_VfxTranslucent")); }
		C = M;
	}
	return C.Get();
}

// ---------------------------------------------------------------- mesh builder

int32 FMeshBuilder::Vert(const FVector& P, const FVector& Nrm, const FVector2D& Uv, const FLinearColor& Col)
{
	const int32 K = V.Add(P);
	N.Add(Nrm);
	UV.Add(Uv);
	C.Add(Col);
	T.Add(FProcMeshTangent(1, 0, 0));
	return K;
}

void FMeshBuilder::Quad(const FVector& A, const FVector& B, const FVector& Cc, const FVector& D, const FLinearColor& Col)
{
	const FVector Nrm = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
	const double Sz = FMath::Max((B - A).Size(), (D - A).Size());
	const int32 I0 = Vert(A, Nrm, FVector2D(0, 0), Col);
	const int32 I1 = Vert(B, Nrm, FVector2D((B - A).Size() / 4.0, 0), Col);
	const int32 I2 = Vert(Cc, Nrm, FVector2D((B - A).Size() / 4.0, (D - A).Size() / 4.0), Col);
	const int32 I3 = Vert(D, Nrm, FVector2D(0, (D - A).Size() / 4.0), Col);
	(void)Sz;
	// Godot frame is right-handed; after the (x,z,y) swap to UE the winding flips, so emit the UE-front-facing order here
	Tri(I0, I1, I2);
	Tri(I0, I2, I3);
}

void FMeshBuilder::Box(const FVector& Ctr, const FVector& Ax, const FVector& Ay, const FVector& Az, const FVector& Half, const FLinearColor& Col, bool bBottom)
{
	const FVector X = Ax * Half.X, Y = Ay * Half.Y, Z = Az * Half.Z;
	auto P = [&](double Sx, double Sy, double Sz) { return Ctr + X * Sx + Y * Sy + Z * Sz; };
	// faces (outward normals via right-hand rule on (B-A) x (D-A))
	Quad(P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), Col);      // top (+Y)
	if (bBottom) { Quad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), Col); }
	Quad(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), Col);      // +X
	Quad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), Col);  // -X
	Quad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), Col);      // +Z
	Quad(P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), Col);  // -Z
}

void FMeshBuilder::BoxYaw(const FVector& Ctr, double Yaw, const FVector& Half, const FLinearColor& Col, bool bBottom)
{
	const FVector Ax(FMath::Cos(Yaw), 0.0, -FMath::Sin(Yaw));
	const FVector Az(FMath::Sin(Yaw), 0.0, FMath::Cos(Yaw));
	Box(Ctr, Ax, FVector(0, 1, 0), Az, Half, Col, bBottom);
}

void FMeshBuilder::Cylinder(const FVector& Base, const FVector& Axis, double Len, double R0, double R1, int32 Seg, const FLinearColor& Col, bool bCaps)
{
	const FVector A = Axis.GetSafeNormal();
	const FVector Ref = FMath::Abs(A.Y) < 0.9 ? FVector(0, 1, 0) : FVector(1, 0, 0);
	const FVector U = FVector::CrossProduct(A, Ref).GetSafeNormal();
	const FVector W = FVector::CrossProduct(U, A);
	const FVector Top = Base + A * Len;
	const double Slope = (R0 - R1) / FMath::Max(Len, 1e-3);
	for (int32 S = 0; S < Seg; ++S)
	{
		const double A0 = UE_DOUBLE_TWO_PI * S / Seg, A1 = UE_DOUBLE_TWO_PI * (S + 1) / Seg, Am = 0.5 * (A0 + A1);
		const FVector D0 = U * FMath::Cos(A0) + W * FMath::Sin(A0);
		const FVector D1 = U * FMath::Cos(A1) + W * FMath::Sin(A1);
		const FVector Nm = ((U * FMath::Cos(Am) + W * FMath::Sin(Am)) + A * Slope).GetSafeNormal();
		const int32 K0 = Vert(Base + D0 * R0, Nm, FVector2D(double(S) / Seg, 0), Col);
		const int32 K1 = Vert(Base + D1 * R0, Nm, FVector2D(double(S + 1) / Seg, 0), Col);
		const int32 K2 = Vert(Top + D1 * R1, Nm, FVector2D(double(S + 1) / Seg, 1), Col);
		const int32 K3 = Vert(Top + D0 * R1, Nm, FVector2D(double(S) / Seg, 1), Col);
		Tri(K0, K2, K1);
		Tri(K0, K3, K2);
		if (bCaps)
		{
			if (R1 > 1e-3)
			{
				const int32 C0 = Vert(Top, A, FVector2D(0.5, 0.5), Col);
				const int32 C1 = Vert(Top + D0 * R1, A, FVector2D(0, 0), Col);
				const int32 C2 = Vert(Top + D1 * R1, A, FVector2D(1, 0), Col);
				Tri(C0, C2, C1);
			}
			if (R0 > 1e-3)
			{
				const int32 C0 = Vert(Base, -A, FVector2D(0.5, 0.5), Col);
				const int32 C1 = Vert(Base + D0 * R0, -A, FVector2D(0, 0), Col);
				const int32 C2 = Vert(Base + D1 * R0, -A, FVector2D(1, 0), Col);
				Tri(C0, C1, C2);
			}
		}
	}
}

void FMeshBuilder::Arch(const FVector& Ctr, double Yaw, double R, double Len, double Squash, int32 Seg, const FLinearColor& Col, bool bEndWalls)
{
	// axis along the yawed X; the arch spans the yawed Z (width 2R) and Y (height R*Squash)
	const FVector Ax(FMath::Cos(Yaw), 0.0, -FMath::Sin(Yaw));
	const FVector Az(FMath::Sin(Yaw), 0.0, FMath::Cos(Yaw));
	const FVector Up(0, 1, 0);
	const FVector H = Ax * (Len * 0.5);
	for (int32 S = 0; S < Seg; ++S)
	{
		const double A0 = UE_DOUBLE_PI * S / Seg, A1 = UE_DOUBLE_PI * (S + 1) / Seg, Am = 0.5 * (A0 + A1);
		const FVector P0 = Az * (R * FMath::Cos(A0)) + Up * (R * Squash * FMath::Sin(A0));
		const FVector P1 = Az * (R * FMath::Cos(A1)) + Up * (R * Squash * FMath::Sin(A1));
		const FVector Nm = (Az * FMath::Cos(Am) * Squash + Up * FMath::Sin(Am)).GetSafeNormal();
		const int32 K0 = Vert(Ctr - H + P0, Nm, FVector2D(0, 0), Col);
		const int32 K1 = Vert(Ctr + H + P0, Nm, FVector2D(1, 0), Col);
		const int32 K2 = Vert(Ctr + H + P1, Nm, FVector2D(1, 1), Col);
		const int32 K3 = Vert(Ctr - H + P1, Nm, FVector2D(0, 1), Col);
		Tri(K0, K1, K2);
		Tri(K0, K2, K3);
		if (bEndWalls)
		{
			for (double Sd : {-1.0, 1.0})
			{
				const FVector E = Ctr + H * Sd;
				const FVector En = Ax * Sd;
				const int32 C0 = Vert(E, En, FVector2D(0.5, 0), Col);
				const int32 C1 = Vert(E + P0, En, FVector2D(0, 1), Col);
				const int32 C2 = Vert(E + P1, En, FVector2D(1, 1), Col);
				if (Sd > 0) { Tri(C0, C2, C1); } else { Tri(C0, C1, C2); }
			}
		}
	}
}

void FMeshBuilder::Sphere(const FVector& Ctr, double R, int32 Seg, const FLinearColor& Col)
{
	const int32 Rings = FMath::Max(Seg / 2, 3);
	for (int32 J = 0; J < Rings; ++J)
	{
		const double T0 = UE_DOUBLE_PI * J / Rings - UE_DOUBLE_HALF_PI, T1 = UE_DOUBLE_PI * (J + 1) / Rings - UE_DOUBLE_HALF_PI;
		for (int32 S = 0; S < Seg; ++S)
		{
			const double A0 = UE_DOUBLE_TWO_PI * S / Seg, A1 = UE_DOUBLE_TWO_PI * (S + 1) / Seg;
			auto Pt = [&](double Th, double Ph) { return FVector(FMath::Cos(Th) * FMath::Cos(Ph), FMath::Sin(Th), FMath::Cos(Th) * FMath::Sin(Ph)); };
			const FVector D00 = Pt(T0, A0), D01 = Pt(T0, A1), D11 = Pt(T1, A1), D10 = Pt(T1, A0);
			const int32 K0 = Vert(Ctr + D00 * R, D00, FVector2D(0, 0), Col);
			const int32 K1 = Vert(Ctr + D01 * R, D01, FVector2D(1, 0), Col);
			const int32 K2 = Vert(Ctr + D11 * R, D11, FVector2D(1, 1), Col);
			const int32 K3 = Vert(Ctr + D10 * R, D10, FVector2D(0, 1), Col);
			Tri(K0, K2, K1);
			Tri(K0, K3, K2);
		}
	}
}

void FMeshBuilder::Commit(UProceduralMeshComponent* Pmc, int32 Section, UMaterialInterface* Mat, bool bCollision) const
{
	if (!Pmc) { return; }
	TArray<FVector> Vu; Vu.Reserve(V.Num());
	TArray<FVector> Nu; Nu.Reserve(N.Num());
	for (const FVector& P : V) { Vu.Add(ToUE(P)); }
	for (const FVector& Q : N) { Nu.Add(DirToUE(Q)); }
	Pmc->CreateMeshSection_LinearColor(Section, Vu, I, Nu, UV, C, T, bCollision);
	if (Mat) { Pmc->SetMaterial(Section, Mat); }
}
}
