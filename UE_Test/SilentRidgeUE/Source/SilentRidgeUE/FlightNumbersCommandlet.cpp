// Flies the same scripted test-pilot manoeuvres as Godot flight_numbers.gd with FF35FlightModel and prints the table (FLIGHTNUMBERS lines).
#include "FlightNumbersCommandlet.h"
#include "F35FlightModel.h"
#include "OsrControls.h"

using namespace OsrMath;

namespace
{
	constexpr double DT = 1.0 / 120.0;
	constexpr double FT = 0.3048;
	constexpr double KT = 0.514444;
	using FM = FF35FlightModel;

	struct FRow { FString Name, Value, Ref; };
	TArray<FRow> Rows;
	int32 Failures = 0;

	void Row(const FString& N, const FString& V, const FString& R) { Rows.Add({N, V, R}); }
	void Check(bool bOk, const FString& What)
	{
		if (!bOk)
		{
			++Failures;
			UE_LOG(LogTemp, Display, TEXT("FLIGHTNUMBERS CHECK FAILED: %s"), *What);
		}
	}
	FM NewModel(double Alt, double Speed, double FuelFrac = 0.6)
	{
		FM M;
		M.Reset(FVector(0, Alt, 0), FQuat::Identity, Speed, FuelFrac);
		return M;
	}
	double MachSpeed(double Alt, double Mach) { return Mach * FMath::Sqrt(1.4 * 287.053 * FM::Isa(Alt).Temperature); }
	void SetPower(FM& M, double Pitch, double Roll, double Power)
	{
		M.SetControls(Pitch, Roll, 0.0, FMath::Clamp(Power, 0.0, 1.0), FMath::Clamp(Power - 1.0, 0.0, 1.0));
	}
	void Fly(FM& M, double Seconds, double Pitch, double Roll, double Power)
	{
		const int32 N = int32(Seconds / DT);
		for (int32 I = 0; I < N; ++I) { SetPower(M, Pitch, Roll, Power); M.Step(DT); }
	}
	double HoldLevelSpeed(FM& M, double VTarget, double Seconds, double MaxPower = 2.0)
	{
		double Power = 0.7, Integ = 0.0;
		const int32 N = int32(Seconds / DT);
		for (int32 I = 0; I < N; ++I)
		{
			const double Err = VTarget - M.Tas;
			Integ = FMath::Clamp(Integ + Err * DT * 0.02, -1.0, 2.0);
			Power = FMath::Clamp(0.15 * Err + Integ + 0.5, 0.0, MaxPower);
			SetPower(M, 0.0, 0.0, Power);
			M.Step(DT);
		}
		return Power;
	}
	FString PowerStr(double Pw) { return Pw <= 1.0 ? FString::Printf(TEXT("%d%%"), int32(Pw * 100.0)) : FString::Printf(TEXT("AB %d%%"), int32((Pw - 1.0) * 100.0)); }

	void TrimTable()
	{
		const double Cfg[][2] = {{0.0, 250.0}, {0.0, 350.0}, {0.0, 450.0}, {4572.0, 0.6}, {4572.0, 0.8}, {9144.0, 0.8}};
		for (const auto& C : Cfg)
		{
			const double Alt = C[0];
			double V; FString Label;
			if (C[1] > 2.0) { V = C[1] * KT; Label = FString::Printf(TEXT("level trim %d kt TAS, sea level"), int32(C[1])); }
			else { V = MachSpeed(Alt, C[1]); Label = FString::Printf(TEXT("level trim M %.1f, %d ft"), C[1], int32(Alt / FT)); }
			FM M = NewModel(FMath::Max(Alt, 150.0), V);
			const double Pw = HoldLevelSpeed(M, V, 45.0);
			Row(Label, FString::Printf(TEXT("AoA %.1f°  thr %s"), Deg(M.Alpha), *PowerStr(Pw)), TEXT("cruise AoA 2–6° typical"));
			Check(FMath::Abs(M.PathAngle) < Rad(0.5), Label + TEXT(" path hold"));
		}
	}
	void Approach()
	{
		FM M = NewModel(150.0, 140.0 * KT, 0.2);
		M.bGearDown = true;
		const double Pw = HoldLevelSpeed(M, 135.0 * KT, 45.0);
		Row(TEXT("approach, gear down, 135 KTAS, 20 % fuel"), FString::Printf(TEXT("AoA %.1f°  thr %s  %.0f kg"), Deg(M.Alpha), *PowerStr(Pw), M.Mass), TEXT("on-speed AoA 12.3° @ 135–140 kt (sec.)"));
		Check(FMath::Abs(Deg(M.Alpha) - 12.3) < 3.0, TEXT("approach AoA"));
	}
	void SustainedTurn()
	{
		const double Alt = 4572.0;
		const double V = MachSpeed(Alt, 0.8);
		FM M = NewModel(Alt, V, 0.5);
		Fly(M, 3.0, 0.0, 0.0, 2.0);
		double BankT = Rad(70.0);
		double Acc[4] = {0, 0, 0, 0};
		int32 N = 0;
		const int32 Steps = int32(70.0 / DT);
		for (int32 I = 0; I < Steps; ++I)
		{
			BankT = FMath::Clamp(BankT + (M.Tas - V) * 0.0006 * DT * 60.0, Rad(30.0), Rad(85.0));
			const double Roll = FMath::Clamp((BankT - M.Bank) * 3.0, -1.0, 1.0);
			const double NNeed = 1.0 / FMath::Max(FMath::Cos(M.Bank), 0.1);
			const double Pitch = FMath::Clamp((NNeed - 2.0) / (7.5 - 2.0) + (0.0 - M.PathAngle) * 6.0 + (4572.0 - M.Position.Y) * 0.002, -1.0, 1.0);
			SetPower(M, Pitch, Roll, 2.0);
			M.Step(DT);
			if (I * DT > 50.0)
			{
				Acc[0] += M.Nz; Acc[1] += M.Tas; Acc[2] += M.Position.Y;
				Acc[3] += FM::G0 * FMath::Sqrt(FMath::Max(M.Nz * M.Nz - 1.0, 0.0)) / M.Tas;
				++N;
			}
		}
		const double Nz = Acc[0] / N;
		const double Mach = (Acc[1] / N) / FMath::Sqrt(1.4 * 287.053 * FM::Isa(Alt).Temperature);
		Row(TEXT("sustained turn, M 0.8, 15,000 ft, max AB, 50 % fuel"), FString::Printf(TEXT("%.2f g  %.1f°/s  (M %.2f)"), Nz, Deg(Acc[3] / N), Mach), TEXT("5.0 g (DOT&E FY2012 C spec)"));
		Check(Nz > 4.3 && Nz < 5.7, TEXT("sustained turn"));
	}
	void Instantaneous()
	{
		struct FC { double Alt, V; const TCHAR* L; };
		const FC Cfg[] = {{4572.0, 0.8, TEXT("full aft stick, M 0.8, 15,000 ft")}, {150.0, 0.9, TEXT("full aft stick, M 0.9, sea level")}, {150.0, 170.0, TEXT("full aft stick, 330 kt, sea level")}};
		for (const FC& C : Cfg)
		{
			const double V = C.V < 2.0 ? MachSpeed(C.Alt, C.V) : C.V;
			FM M = NewModel(C.Alt, V);
			Fly(M, 1.0, 0.0, 0.0, 1.0);
			double MaxRate = 0.0, MaxA = 0.0;
			for (int32 I = 0; I < int32(4.0 / DT); ++I)
			{
				SetPower(M, 1.0, 0.0, 1.0);
				M.Step(DT);
				MaxRate = FMath::Max(MaxRate, M.Rates.X);
				MaxA = FMath::Max(MaxA, M.Alpha);
			}
			Row(C.L, FString::Printf(TEXT("max %.2f g, %.0f°/s, AoA %.0f°"), M.NzMax, Deg(MaxRate), Deg(MaxA)), TEXT("≤ 7.5 g (FBW limit)"));
			Check(M.NzMax < 7.8, FString(C.L) + TEXT(" g limit"));
		}
		double MinNz = 99.0;
		FM M2 = NewModel(4572.0, MachSpeed(4572.0, 0.8));
		Fly(M2, 1.0, 0.0, 0.0, 1.0);
		for (int32 I = 0; I < int32(3.0 / DT); ++I)
		{
			SetPower(M2, -1.0, 0.0, 1.0);
			M2.Step(DT);
			if (I * DT > 0.3) { MinNz = FMath::Min(MinNz, M2.Nz); }
		}
		Row(TEXT("full forward stick, M 0.8, 15,000 ft"), FString::Printf(TEXT("min %.2f g"), MinNz), TEXT("≥ -3 g"));
		Check(MinNz > -3.4, TEXT("negative g limit"));
	}
	void RollRate()
	{
		FM M = NewModel(150.0, 400.0 * KT);
		double Mx = 0.0, T360 = -1.0, Rolled = 0.0;
		for (int32 I = 0; I < int32(4.0 / DT); ++I)
		{
			SetPower(M, 0.0, 1.0, 1.0);
			M.Step(DT);
			Mx = FMath::Max(Mx, M.Rates.Z);
			Rolled += M.Rates.Z * DT;
			if (T360 < 0.0 && Rolled >= UE_DOUBLE_TWO_PI) { T360 = I * DT; }
		}
		Row(TEXT("roll rate, 400 kt, sea level, full stick"), FString::Printf(TEXT("%.0f°/s, 360° in %.2f s"), Deg(Mx), T360), TEXT("~200°/s class [estimate]"));
	}
	void TopSpeed(double Alt, const TCHAR* Label, const TCHAR* Ref)
	{
		FM M = NewModel(Alt, MachSpeed(Alt, Alt < 1000.0 ? 1.0 : 1.45), 1.0);
		double Last = 0.0;
		for (int32 I = 0; I < int32(400.0 / DT); ++I)
		{
			SetPower(M, 0.0, 0.0, 2.0);
			M.Step(DT);
			if (I % 1200 == 0)
			{
				if (FMath::Abs(M.Tas - Last) < 0.05) { break; }
				Last = M.Tas;
			}
		}
		Row(Label, FString::Printf(TEXT("M %.2f  %d KCAS  %d KTAS"), M.Mach, int32(M.Cas / KT), int32(M.Tas / KT)), Ref);
		if (Alt < 1000.0) { Check(M.Mach > 1.0 && M.Mach < 1.25, TEXT("top speed SL")); }
		else { Check(M.Mach > 1.45 && M.Mach < 1.75, TEXT("top speed alt")); }
	}
	void TopSpeedMil(double Alt)
	{
		FM M = NewModel(Alt, MachSpeed(Alt, 0.8));
		Fly(M, 300.0, 0.0, 0.0, 1.0);
		Row(FString::Printf(TEXT("top speed, MIL, %d ft"), int32(Alt / FT)), FString::Printf(TEXT("M %.2f  %d KCAS"), M.Mach, int32(M.Cas / KT)), TEXT("no supercruise (W)"));
	}
	void AccelTransonic()
	{
		const double Alt = 9144.0;
		FM M = NewModel(Alt, MachSpeed(Alt, 0.8), 0.5);
		double T = 0.0;
		while (M.Mach < 1.2 && T < 400.0) { SetPower(M, 0.0, 0.0, 2.0); M.Step(DT); T += DT; }
		Row(TEXT("accel M 0.8 → 1.2, 30,000 ft, max AB, 50 % fuel"), FString::Printf(TEXT("%.0f s"), T), TEXT("~100 s (C spec +43 s vs A, DOT&E; sec.)"));
	}
	void AccelSeaLevel()
	{
		FM M = NewModel(150.0, 250.0 * KT);
		double T = 0.0, T500 = -1.0, T600 = -1.0;
		while (T < 120.0)
		{
			SetPower(M, 0.0, 0.0, 2.0); M.Step(DT); T += DT;
			const double Kt = M.Cas / KT;
			if (T500 < 0.0 && Kt >= 500.0) { T500 = T; }
			if (T600 < 0.0 && Kt >= 600.0) { T600 = T; }
		}
		Row(TEXT("accel 250 → 500 / 600 KCAS, sea level, max AB"), FString::Printf(TEXT("%.1f s / %.1f s"), T500, T600), TEXT("—"));
	}
	double SpecificExcessPower(const FM& M, double Alt, double V, double N, bool bAb)
	{
		const FAtmosphere Atm = FM::Isa(Alt);
		const double A = FMath::Sqrt(1.4 * 287.053 * Atm.Temperature);
		const double Mach = V / A;
		const double Q = 0.5 * Atm.Density * V * V;
		const double W = M.Mass * FM::G0;
		const double CL = N * W / (Q * FM::WING_AREA);
		const double Al = M.AlphaForCl(CL, Mach, false);
		const double CD = FM::DragCoefficient(Al, Mach, CL, false, false);
		const FThrustLimits Lim = FM::ThrustLimits(Alt, Mach);
		const double T = bAb ? Lim.Max : Lim.Mil;
		return (T - Q * FM::WING_AREA * CD) * V / W;
	}
	void Climb()
	{
		double Best = 0.0, BestV = 0.0, BestMil = 0.0;
		FM M = NewModel(150.0, 200.0);
		for (int32 Kt = 250; Kt < 700; Kt += 10)
		{
			const double Ps = SpecificExcessPower(M, 150.0, Kt * KT, 1.0, true);
			if (Ps > Best) { Best = Ps; BestV = Kt; }
		}
		for (int32 Kt = 250; Kt < 650; Kt += 10) { BestMil = FMath::Max(BestMil, SpecificExcessPower(M, 150.0, Kt * KT, 1.0, false)); }
		Row(TEXT("max rate of climb, sea level (Ps at 1 g)"), FString::Printf(TEXT("%d ft/min AB @ %d kt, %d MIL"), int32(Best / FT * 60.0), int32(BestV), int32(BestMil / FT * 60.0)), TEXT("~45,000 ft/min class [estimate]"));
	}
	void EnergyBleed()
	{
		FM M = NewModel(300.0, 450.0 * KT);
		Fly(M, 1.0, 0.0, 0.0, 1.0);
		const double V0 = M.Cas;
		double V3 = 0.0;
		for (int32 I = 0; I < int32(6.0 / DT); ++I)
		{
			const double Roll = FMath::Clamp((Rad(84.0) - M.Bank) * 3.0, -1.0, 1.0);
			SetPower(M, I * DT > 0.4 ? 1.0 : 0.0, Roll, 1.0);
			M.Step(DT);
			if (I == int32(3.0 / DT)) { V3 = M.Cas; }
		}
		Row(TEXT("7.5 g turn from 450 KCAS, sea level, MIL"), FString::Printf(TEXT("%d → %d → %d KCAS (0/3/6 s)"), int32(V0 / KT), int32(V3 / KT), int32(M.Cas / KT)), TEXT("big bleed: speed falls toward corner"));
		Check(M.Cas < V0 - 20.0 * KT, TEXT("high-g bleed"));
	}
	void ZoomClimb()
	{
		FM M = NewModel(150.0, 600.0 * KT);
		const double H0 = M.Position.Y;
		double MaxH = H0;
		int32 Phase = 0;
		for (int32 I = 0; I < int32(80.0 / DT); ++I)
		{
			double Pitch = 0.0;
			if (Phase == 0)
			{
				Pitch = 0.6;
				if (M.PathAngle > Rad(60.0)) { Phase = 1; }
			}
			SetPower(M, Pitch, 0.0, 1.0);
			M.Step(DT);
			MaxH = FMath::Max(MaxH, M.Position.Y);
			if (Phase == 1 && M.Cas < 150.0 * KT) { break; }
		}
		Row(TEXT("zoom 600 KCAS → 150 KCAS, 60° climb, MIL"), FString::Printf(TEXT("+%d ft"), int32((MaxH - H0) / FT)), TEXT("energy height 600 kt ≈ 15,900 ft + thrust"));
	}
	void Stall()
	{
		FM M = NewModel(4572.0, 250.0 * KT);
		double MinKt = 999.0, MaxA = 0.0;
		for (int32 I = 0; I < int32(40.0 / DT); ++I)
		{
			SetPower(M, 1.0, 0.0, 0.0);
			M.Step(DT);
			MinKt = FMath::Min(MinKt, M.Cas / KT);
			MaxA = FMath::Max(MaxA, M.Alpha);
		}
		Row(TEXT("idle + full aft stick 40 s, 15,000 ft"), FString::Printf(TEXT("AoA max %.1f°, min %d KCAS"), Deg(MaxA), int32(MinKt)), TEXT("AoA limiter 50°, no departure"));
		Check(Deg(MaxA) < 52.0, TEXT("AoA limit"));
		FM M2 = NewModel(3000.0, 200.0);
		M2.Orientation = FQuat(FVector(1, 0, 0), Rad(85.0));
		M2.Velocity = M2.Orientation.RotateVector(FVector(0, 0, -1)) * 120.0;
		double MinV = 999.0;
		for (int32 I = 0; I < int32(25.0 / DT); ++I)
		{
			SetPower(M2, 0.0, 0.0, 0.0);
			M2.Step(DT);
			MinV = FMath::Min(MinV, M2.Tas);
		}
		Row(TEXT("vertical zoom to zero speed, idle, neutral"), FString::Printf(TEXT("min %d m/s → recovers at %d kt, pitch %d°"), int32(MinV), int32(M2.Cas / KT), int32(M2.PitchDeg())), TEXT("nose falls through, no lock-up"));
		Check(M2.Cas > 80.0 * KT, TEXT("tail slide recovery"));
	}
	void Response()
	{
		FM M = NewModel(1500.0, 180.0);
		Fly(M, 1.0, 0.0, 0.0, 0.8);
		double T90 = -1.0, T = 0.0;
		while (T < 4.0 && T90 < 0.0)
		{
			SetPower(M, 0.0, 1.0, 0.8); M.Step(DT); T += DT;
			if (M.Bank >= Rad(90.0)) { T90 = T; }
		}
		Row(TEXT("time to 90° bank, full stick, 350 kt"), FString::Printf(TEXT("%.2f s"), T90), TEXT("F-16 class ~0.6–0.8 s [estimate]"));
		FM M2 = NewModel(1500.0, 206.0);
		Fly(M2, 1.0, 0.0, 0.0, 1.0);
		const double Target = 5.0;
		const double Stick = (Target - 1.0) / (7.5 - 1.0);
		double T63 = -1.0, T90g = -1.0, Over = 0.0;
		T = 0.0;
		while (T < 3.0)
		{
			SetPower(M2, Stick, 0.0, 1.0); M2.Step(DT); T += DT;
			if (T63 < 0.0 && M2.Nz >= 1.0 + 0.63 * (Target - 1.0)) { T63 = T; }
			if (T90g < 0.0 && M2.Nz >= 1.0 + 0.9 * (Target - 1.0)) { T90g = T; }
			Over = FMath::Max(Over, M2.Nz - Target);
		}
		Row(TEXT("5 g pull-up step, 400 kt: 63 % / 90 % / overshoot"), FString::Printf(TEXT("%.2f s / %.2f s / %+.2f g"), T63, T90g, Over), TEXT("crisp, <10 % overshoot"));
		Check(T90g > 0.0 && T90g < 1.5 && Over < 0.5, TEXT("pitch response"));
		FM M3 = NewModel(1500.0, 130.0);
		Fly(M3, 1.0, 0.0, 0.0, 0.8);
		const double H0 = M3.HeadingDeg();
		for (int32 I = 0; I < int32(3.0 / DT); ++I) { M3.SetControls(0.0, 0.0, 1.0, 0.8, 0.0); M3.Step(DT); }
		Row(TEXT("full right pedal 3 s, 250 kt"), FString::Printf(TEXT("β %.1f°, heading %+.1f°, bank %.1f°"), Deg(M3.Beta), M3.HeadingDeg() - H0, Deg(M3.Bank)), TEXT("nose right (β<0), small flat turn"));
		Check(M3.HeadingDeg() - H0 > 0.5 && M3.Beta < 0.0, TEXT("rudder sign"));
	}
	void ControlsLogic()
	{
		FOsrControls C;
		C.SetLever(0.5);
		for (int32 I = 0; I < 240; ++I) { C.UpdateLever(DT, 1.0, 0.0); }
		const double AtMil = C.GetLever();
		C.SetLever(0.99);
		for (int32 I = 0; I < 3; ++I) { C.UpdateLever(DT, 1.0, 0.0); }
		const double ShortHold = C.GetLever();
		for (int32 I = 0; I < 60; ++I) { C.UpdateLever(DT, 1.0, 0.0); }
		const double NoRelease = C.GetLever();
		for (int32 I = 0; I < 5; ++I) { C.UpdateLever(DT, 0.0, 0.0); }
		for (int32 I = 0; I < 60; ++I) { C.UpdateLever(DT, 1.0, 0.0); }
		const double AfterHold = C.GetLever();
		for (int32 I = 0; I < 120; ++I) { C.UpdateLever(DT, 0.0, 1.0); }
		const double Back = C.GetLever();
		const bool bOk = FMath::Abs(AtMil - 1.0) < 1e-3 && ShortHold <= 1.0 && NoRelease <= 1.0 + 1e-3 && AfterHold > 1.3 && Back < 0.9;
		Row(TEXT("throttle detent: MIL stop / re-press+hold → AB / LT"), FString::Printf(TEXT("%.2f / %.2f / %.2f"), NoRelease, AfterHold, Back), TEXT("1.00 / >1 (AB) / <1"));
		Check(bOk, TEXT("throttle detent"));
		Row(TEXT("stick shaping: 10 % / 50 % pitch stick"), FString::Printf(TEXT("%.3f / %.3f of full command"), FOsrControls::Expo(0.1, 0.55), FOsrControls::Expo(0.5, 0.55)), TEXT("fine near centre"));
	}
	void Determinism()
	{
		FM A = NewModel(3000.0, 220.0);
		FM B = NewModel(3000.0, 220.0);
		for (int32 I = 0; I < int32(20.0 / DT); ++I)
		{
			const double P = FMath::Sin(I * 0.013) * 0.8;
			const double R = FMath::Cos(I * 0.007) * 0.9;
			A.SetControls(P, R, 0.1, 0.9, 0.0); B.SetControls(P, R, 0.1, 0.9, 0.0);
			A.Step(DT); B.Step(DT);
		}
		const bool bSame = A.Position == B.Position && A.Orientation == B.Orientation;
		Row(TEXT("determinism (two runs, 20 s random input)"), bSame ? TEXT("identical") : TEXT("DIFFERENT"), TEXT("bit-identical"));
		Check(bSame, TEXT("determinism"));
	}
	void Fuel()
	{
		FM M = NewModel(150.0, 250.0);
		Fly(M, 5.0, 0.0, 0.0, 1.0);
		const double Mil = M.FuelFlow;
		Fly(M, 5.0, 0.0, 0.0, 2.0);
		const double Abf = M.FuelFlow;
		Row(TEXT("fuel flow, sea level, MIL / max AB"), FString::Printf(TEXT("%.1f / %.1f kg/s"), Mil, Abf), TEXT("≈3 / ≈9 kg/s from TSFC [estimate]"));
		Row(TEXT("full-fuel AB endurance at SL"), FString::Printf(TEXT("%.1f min"), FM::FUEL_MAX / Abf / 60.0), TEXT("—"));
	}
}

int32 UFlightNumbersCommandlet::Main(const FString& Params)
{
	const double T0 = FPlatformTime::Seconds();
	Rows.Reset();
	Failures = 0;
	TrimTable();
	Approach();
	SustainedTurn();
	Instantaneous();
	RollRate();
	TopSpeed(100.0, TEXT("top speed, max AB, sea level"), TEXT("M 1.06 / 700 kt (W)"));
	TopSpeed(12200.0, TEXT("top speed, max AB, 40,000 ft"), TEXT("M 1.6 (LM)"));
	TopSpeedMil(100.0);
	TopSpeedMil(10668.0);
	AccelTransonic();
	AccelSeaLevel();
	Climb();
	EnergyBleed();
	ZoomClimb();
	Stall();
	Response();
	ControlsLogic();
	Determinism();
	Fuel();
	for (const FRow& R : Rows)
	{
		UE_LOG(LogTemp, Display, TEXT("FLIGHTNUMBERS | %-52s | %-34s | %s"), *R.Name, *R.Value, *R.Ref);
	}
	UE_LOG(LogTemp, Display, TEXT("FLIGHTNUMBERS REPORT failures=%d elapsed_ms=%d"), Failures, int32((FPlatformTime::Seconds() - T0) * 1000.0));
	return Failures > 0 ? 1 : 0;
}
