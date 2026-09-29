// LSO logic: calls, wave-off, grading (REFERENCES 4.8: ball call at 3/4 nmi, IFLOLS, OK / Fair / No grade / Cut, wires).
#include "LsoMonitor.h"

namespace
{
	const TCHAR* Lso_PH_NAME[4] = {TEXT("X"), TEXT("IM"), TEXT("IC"), TEXT("AR")};
}

void FLsoMonitor::Reset()
{
	bActive = false;
	bBallCalled = false;
	bWaveOff = false;
	WaveOffReason.Reset();
	BallDev = 0.0;
	for (FPhase& P : Ph) { P = FPhase(); }
}

int32 FLsoMonitor::PhaseOf(double R)
{
	if (R > 930.0) { return 0; }
	if (R > 465.0) { return 1; }
	if (R > 150.0) { return 2; }
	return 3;
}

void FLsoMonitor::Say(const FString& Who, const FString& Text, FName Clip, double Now)
{
	FLsoCall C;
	C.Who = Who;
	C.Text = Text;
	C.Clip = Clip;
	C.T = Now;
	Log.Add(C);
	if (Log.Num() > 40) { Log.RemoveAt(0); }
	UE_LOG(LogTemp, Display, TEXT("REPORT lso_call t=%.1f %s: %s"), Now, *Who, *Text);
}

void FLsoMonitor::Call(const FString& Key, const FString& Text, FName Clip, double Now, double Gap)
{
	if (Now - LastCallT < Gap) { return; }
	if (Key == LastCallKey && Now - LastCallT < Gap * 1.8) { return; }
	LastCallT = Now;
	LastCallKey = Key;
	Say(TEXT("LSO"), Text, Clip, Now);
}

bool FLsoMonitor::Update(const FLsoInput& In, double Now)
{
	const bool bWindow = In.bBehind && In.bAligned && In.RangeM < 2400.0 && In.RangeM > -30.0;
	if (!bWindow)
	{
		if (bActive && In.RangeM > 2600.0) { Reset(); }
		bActive = false;
		return false;
	}
	bActive = true;
	BallDev = In.GsDevDeg;
	const double R = In.RangeM;
	if (!bBallCalled && R < BallCallM && In.bGear)
	{
		bBallCalled = true;
		Say(TEXT("PILOT"), FString::Printf(TEXT("Lightning, ball, %.1f."), In.FuelKlb), FName(TEXT("pilot_ball")), Now);
		LastCallT = Now + 1.4;
		LastCallKey = TEXT("rogerball");
		Say(TEXT("LSO"), TEXT("Roger ball."), FName(TEXT("lso_roger_ball")), Now + 1.4);
	}
	if (R < BallCallM)
	{
		FPhase& P = Ph[PhaseOf(R)];
		P.MaxHi = FMath::Max(P.MaxHi, In.GsDevDeg);
		P.MaxLo = FMath::Max(P.MaxLo, -In.GsDevDeg);
		P.MaxR = FMath::Max(P.MaxR, In.LineupM);
		P.MaxL = FMath::Max(P.MaxL, -In.LineupM);
		P.MaxFast = FMath::Max(P.MaxFast, OnSpeedAoa - In.AoaDeg);
		P.MaxSlow = FMath::Max(P.MaxSlow, In.AoaDeg - OnSpeedAoa);
		++P.N;
	}
	// wave-off decisions (mandatory: flashing red lights + call)
	if (!bWaveOff && R < 950.0)
	{
		FString Why;
		if (!In.bGear && R < 750.0) { Why = TEXT("gear"); }
		else if (!In.bHook && R < 420.0) { Why = TEXT("hook"); }
		else if (In.GsDevDeg < -1.15 && R < 480.0 && R > 20.0) { Why = TEXT("low"); }
		else if (FMath::Abs(In.LineupM) > 8.5 && R < 380.0 && R > 20.0) { Why = TEXT("lineup"); }
		else if (In.AoaDeg > 16.5 && R < 320.0) { Why = TEXT("slow"); }
		if (!Why.IsEmpty())
		{
			bWaveOff = true;
			WaveOffT = Now;
			WaveOffReason = Why;
			++WaveOffs;
			LastCallT = -99.0;
			Call(TEXT("waveoff"), TEXT("Wave off, wave off!"), FName(TEXT("lso_waveoff")), Now, 0.0);
			return true;
		}
	}
	if (bWaveOff) { return true; }
	if (!In.bHook && R < 950.0 && R > 420.0) { Call(TEXT("hook"), TEXT("Hook down!"), FName(TEXT("lso_hook")), Now); }
	else if (bBallCalled && R > 40.0)
	{
		if (In.GsDevDeg < -0.75) { Call(TEXT("power"), TEXT("Power!"), FName(TEXT("lso_power")), Now, 1.6); }
		else if (In.GsDevDeg < -0.42) { Call(TEXT("low"), TEXT("You're low."), FName(TEXT("lso_low")), Now); }
		else if (In.LineupM > 4.0 + R * 0.004) { Call(TEXT("left"), TEXT("Come left."), FName(TEXT("lso_come_left")), Now); }
		else if (In.LineupM < -4.0 - R * 0.004) { Call(TEXT("right"), TEXT("Right for lineup."), FName(TEXT("lso_right")), Now); }
		else if (In.GsDevDeg > 0.5) { Call(TEXT("high"), TEXT("You're high."), FName(TEXT("lso_high")), Now); }
		else if (In.AoaDeg > OnSpeedAoa + 1.8) { Call(TEXT("slow"), TEXT("You're slow."), FName(TEXT("lso_slow")), Now); }
		else if (In.AoaDeg < OnSpeedAoa - 2.0) { Call(TEXT("fast"), TEXT("You're fast."), FName(TEXT("lso_fast")), Now); }
		else if (R < 300.0 && FMath::Abs(In.GsDevDeg) < 0.35) { Call(TEXT("easy"), TEXT("Easy with it."), FName(TEXT("lso_easy")), Now, 4.0); }
	}
	return true;
}

ELsoGrade FLsoMonitor::GradeTrap(int32 Wire)
{
	LastWire = Wire;
	FString C;
	double Dev = 0.0, Lu = 0.0, LoClose = 0.0;
	for (int32 I = 0; I < 4; ++I)
	{
		const FPhase& P = Ph[I];
		if (P.N == 0) { continue; }
		auto Tag = [&](double V, const TCHAR* Name, double Small, double Big)
		{
			if (V < Small) { return; }
			if (V < Big) { C += FString::Printf(TEXT("(%s)%s "), Name, Lso_PH_NAME[I]); }
			else { C += FString::Printf(TEXT("%s%s "), Name, Lso_PH_NAME[I]); }
		};
		Tag(P.MaxHi, TEXT("HI"), 0.35, 0.7);
		Tag(P.MaxLo, TEXT("LO"), 0.3, 0.6);
		Tag(P.MaxL, TEXT("LUL"), 2.5, 5.0);
		Tag(P.MaxR, TEXT("LUR"), 2.5, 5.0);
		Tag(P.MaxSlow, TEXT("SLO"), 1.5, 3.0);
		Tag(P.MaxFast, TEXT("F"), 1.5, 3.0);
		if (I >= 2)
		{
			Dev = FMath::Max(Dev, FMath::Max(P.MaxHi, P.MaxLo));
			Lu = FMath::Max(Lu, FMath::Max(P.MaxL, P.MaxR));
			LoClose = FMath::Max(LoClose, P.MaxLo);
		}
	}
	ELsoGrade G;
	if (bWaveOff || LoClose > 1.0) { G = ELsoGrade::Cut; }
	else if (Wire == 3 && Dev < 0.3 && Lu < 2.0) { G = ELsoGrade::Perfect; }
	else if (Dev < 0.55 && Lu < 3.5 && Wire >= 2) { G = ELsoGrade::Ok; }
	else if (Dev < 0.9 && Lu < 6.0) { G = ELsoGrade::Fair; }
	else { G = ELsoGrade::NoGrade; }
	if (Wire == 1 && (G == ELsoGrade::Perfect || G == ELsoGrade::Ok)) { G = ELsoGrade::Fair; }
	LastComment = C.TrimEnd();
	LastGrade = G;
	++Traps;
	Reset();
	return G;
}

void FLsoMonitor::Bolter(double Now)
{
	++Bolters;
	LastGrade = ELsoGrade::Bolter;
	LastWire = 0;
	LastCallT = -99.0;
	Call(TEXT("bolter"), TEXT("Bolter, bolter, bolter!"), FName(TEXT("lso_bolter")), Now, 0.0);
	Reset();
}
