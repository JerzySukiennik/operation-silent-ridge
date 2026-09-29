// Mission & combat shared helpers: Godot-frame <-> UE conversion, terrain line of sight (radar), ray/terrain intersection,
// a small procedural-mesh builder (vertex-coloured boxes/cylinders/cones in local metres) and the shared mission materials.
// Frame: Godot (x east, y up, z south, metres) like the flight model and OsrTerrain; UE = (x, z, y) * 100.
#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

class UMaterialInterface;
class UProceduralMeshComponent;

namespace OsrMission
{
	inline FVector ToUE(const FVector& G) { return FVector(G.X, G.Z, G.Y) * 100.0; }
	inline FVector DirToUE(const FVector& G) { return FVector(G.X, G.Z, G.Y); }
	inline FVector FromUE(const FVector& U) { return FVector(U.X, U.Z, U.Y) * 0.01; }
	inline FVector DirFromUE(const FVector& U) { return FVector(U.X, U.Z, U.Y); }
	inline FVector Flat(const FVector& V) { return FVector(V.X, 0.0, V.Z); }
	inline double Hdist(const FVector& A, const FVector& B) { return FVector2D(A.X - B.X, A.Z - B.Z).Size(); }

	constexpr double EARTH_R_EFF = 8.5e6;   // 4/3 Earth radius (standard refraction), REFERENCES §5.3

	/** Terrain surface height (sea = 0). */
	double Ground(double X, double Z);
	/** Terrain + earth curvature line of sight between two points (Godot frame). Clear = required margin over the terrain (m). */
	bool LineOfSight(const FVector& A, const FVector& B, double Clear = 2.0);
	/** Smooth-earth radar horizon, REFERENCES §5.3: D(km) = 4.12 (sqrt(h_radar) + sqrt(h_target)), heights in metres. */
	inline double RadarHorizonM(double HRadar, double HTarget) { return 4120.0 * (FMath::Sqrt(FMath::Max(HRadar, 0.0)) + FMath::Sqrt(FMath::Max(HTarget, 0.0))); }
	/** First terrain hit along a ray (Godot frame); false when none within MaxDist. */
	bool RayTerrain(const FVector& Origin, const FVector& Dir, double MaxDist, FVector& Hit);
	/** Angle (rad) between two directions. */
	inline double AngleBetween(const FVector& A, const FVector& B) { return FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0)); }

	UMaterialInterface* LitMaterial();        // /Game/Mission/M_MissionLit (vertex colour x Color, snow on top faces)
	UMaterialInterface* PuffMaterial(bool bAdditive);   // /Game/Mission/M_MissionPuffAdd / Tr (soft round sprites, ribbons)

	/** Vertex-coloured triangle soup in local metres (Godot axes relative to the owner actor), converted to UE cm on Commit. */
	struct FMeshBuilder
	{
		TArray<FVector> V; TArray<int32> I; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C; TArray<FProcMeshTangent> T;

		int32 Num() const { return V.Num(); }
		void Reset() { V.Reset(); I.Reset(); N.Reset(); UV.Reset(); C.Reset(); T.Reset(); }
		int32 Vert(const FVector& P, const FVector& Nrm, const FVector2D& Uv, const FLinearColor& Col);
		void Tri(int32 A, int32 B, int32 Cc) { I.Append({A, B, Cc}); }
		/** Flat-shaded quad (A,B,C,D counter-clockwise seen from outside in Godot's right-handed frame). */
		void Quad(const FVector& A, const FVector& B, const FVector& Cc, const FVector& D, const FLinearColor& Col);
		/** Oriented box: centre, axes (unit, Godot frame) and half extents. */
		void Box(const FVector& Ctr, const FVector& Ax, const FVector& Ay, const FVector& Az, const FVector& Half, const FLinearColor& Col, bool bBottom = true);
		/** Box yawed about +Y (Yaw rad, 0 = axes aligned; X along east). */
		void BoxYaw(const FVector& Ctr, double Yaw, const FVector& Half, const FLinearColor& Col, bool bBottom = true);
		/** Cylinder / cone frustum along Axis from Base, radii R0 -> R1. */
		void Cylinder(const FVector& Base, const FVector& Axis, double Len, double R0, double R1, int32 Seg, const FLinearColor& Col, bool bCaps = true);
		/** Half cylinder (Quonset hut / hardened shelter): axis along Dir on the ground, arch radius R, length Len. */
		void Arch(const FVector& Ctr, double Yaw, double R, double Len, double Squash, int32 Seg, const FLinearColor& Col, bool bEndWalls = true);
		/** Low-poly sphere. */
		void Sphere(const FVector& Ctr, double R, int32 Seg, const FLinearColor& Col);
		/** Uploads as mesh section Section (converted to UE local cm). */
		void Commit(UProceduralMeshComponent* Pmc, int32 Section, UMaterialInterface* Mat, bool bCollision = false) const;
	};

	/** Stable colour helpers (sRGB-ish base colours in linear). */
	inline FLinearColor Rgb(int32 R, int32 G, int32 B) { return FLinearColor(FColor(R, G, B)); }
}
