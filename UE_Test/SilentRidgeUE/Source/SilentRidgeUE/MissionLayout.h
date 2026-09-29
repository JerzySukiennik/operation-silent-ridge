// Mission 1 ground layout on terrain v2 (Godot frame x east, z south, metres; heights come from OsrTerrain at runtime).
// Picked offline from Content/Data/terrain.osrh (viewsheds along the canyon path, slopes, runway fit) — see Niepotrzebne/m3/mission_progress.txt.
#pragma once

#include "CoreMinimal.h"

namespace OsrLayout
{
	// underground plant: complex on the valley floor at the gully mouth, tunnel portals at the gully head, vent on the spur crest east of it
	inline const FVector2D PlantCentre(11350.0, -2560.0);
	inline const FVector2D PortalA(11500.0, -3330.0);      // faces south (+z)
	inline const FVector2D PortalB(11600.0, -3120.0);      // faces west (-x), cut into the spur flank
	inline const FVector2D Vent(12030.0, -3050.0);         // ventilation shaft on a levelled terrace on the spur crest (~140 m above the floor)
	constexpr double VentHalf = 2.0;                       // shaft opening 4 x 4 m (film: "under ~3 m"; slightly wider for a playable LGB)

	// air defences
	inline const FVector2D SearchRadar(10738.0, -5410.0);  // 91N6-class long-range search radar on the peak north of the canyon exit (1633 m)
	inline const FVector2D S400(14156.0, -7109.0);         // S-400 fire unit (92N6-class engagement radar on a mast + 4 TELs), 1801 m
	inline const FVector2D PantsirA(11250.0, -2540.0);     // point defence of the plant (valley floor)
	inline const FVector2D PantsirB(12090.0, -2930.0);     // point defence of the vent (spur)
	inline const FVector2D TorAirbase(18200.0, -8300.0);   // SHORAD at the airbase

	// enemy airbase: runway fitted to the basin NE of the target valley (max deviation 6 m over 2.4 km)
	inline const FVector2D RunwayCentre(18500.0, -7900.0);
	constexpr double RunwayHeadingRad = 155.0 * UE_DOUBLE_PI / 180.0;   // direction (sin h, cos h) in (x, z)
	constexpr double RunwayLength = 2400.0;
	constexpr double RunwayWidth = 45.0;
}
