// Landing Signal Officer: glideslope / lineup / AoA tracking on the approach (stabilized, ship-referenced), IFLOLS ball, radio calls
// (text + radio click, optional TTS clips), wave-off decisions and the debrief grade (OK / Fair / No grade / Cut + wire).
#pragma once

#include "CoreMinimal.h"
#include "CarrierActor.h"

struct FLsoCall
{
	FString Text;
	FString Who;        // "LSO" / "PILOT"
	FName Clip;         // /Game/Carrier/Audio/<Clip> if present
	double T = 0.0;
};

/** Per-approach snapshot fed by the jet ops every frame while the jet is airborne. */
struct FLsoInput
{
	double RangeM = 99999.0;      // hook distance to the touchdown point along the approach (stabilized frame), m
	double GsDevDeg = 0.0;        // hook elevation above the ship-referenced glideslope, deg (+ high)
	double LineupM = 0.0;         // lateral offset from the landing centreline, m (+ right of centreline, pilot's view)
	double AoaDeg = 12.3;
	double FuelKlb = 0.0;
	bool bGear = false, bHook = false, bBehind = false, bAligned = false;
};

class SILENTRIDGEUE_API FLsoMonitor
{
public:
	static constexpr double OnSpeedAoa = 12.3;
	static constexpr double BallCallM = 1390.0;   // 3/4 nmi

	void Reset();
	/** Returns true while the jet is "in the groove" (the IFLOLS should show the ball). */
	bool Update(const FLsoInput& In, double Now);
	/** Touchdown / trap / bolter results -> grade. */
	ELsoGrade GradeTrap(int32 Wire);
	void Bolter(double Now);
	const TArray<FLsoCall>& Calls() const { return Log; }
	void Say(const FString& Who, const FString& Text, FName Clip, double Now);

	bool bActive = false;         // in the approach window
	bool bBallCalled = false;
	bool bWaveOff = false;
	double WaveOffT = -99.0;
	FString WaveOffReason;
	double BallDev = 0.0;
	FString LastComment;          // debrief shorthand, e.g. "(LO)IC  LUL AR"
	ELsoGrade LastGrade = ELsoGrade::Ok;
	int32 LastWire = 0;
	int32 Traps = 0, Bolters = 0, WaveOffs = 0;

private:
	TArray<FLsoCall> Log;
	double LastCallT = -99.0;
	FString LastCallKey;
	// per phase stats: X (start), IM (middle), IC (in close), AR (at the ramp)
	struct FPhase { double MaxHi = 0, MaxLo = 0, MaxL = 0, MaxR = 0, MaxFast = 0, MaxSlow = 0; int32 N = 0; };
	FPhase Ph[4];
	static int32 PhaseOf(double RangeM);
	void Call(const FString& Key, const FString& Text, FName Clip, double Now, double Gap = 2.6);
};
