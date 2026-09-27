// F-35C flight model implementation: ISA atmosphere, F135 thrust lapse, Mach/AoA aero tables and fly-by-wire control laws (port of f35_flight_model.gd).
#include "F35FlightModel.h"

using namespace OsrMath;

namespace
{
	// Low-speed lift curve (deg -> CL) [estimate]
	const double CL_ALPHA_DEG[] = {0.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 35.0, 40.0, 45.0, 50.0, 60.0, 70.0, 80.0, 90.0};
	const double CL_TABLE[] = {0.0, 0.28, 0.57, 0.86, 1.13, 1.37, 1.55, 1.63, 1.60, 1.50, 1.38, 1.10, 0.78, 0.40, 0.0};
	const double MACH_X[] = {0.0, 0.6, 0.8, 0.9, 1.0, 1.1, 1.3, 1.6, 2.0};
	const double CLA_FACTOR[] = {1.0, 1.08, 1.17, 1.23, 1.19, 1.06, 0.91, 0.77, 0.63};
	const double CLMAX_FACTOR[] = {1.0, 0.95, 0.82, 0.74, 0.70, 0.72, 0.76, 0.80, 0.80};
	const double CD0_MACH[] = {0.0, 0.75, 0.85, 0.92, 0.97, 1.02, 1.08, 1.15, 1.3, 1.6, 2.0};
	const double CD0_TABLE[] = {0.0205, 0.0205, 0.0218, 0.0262, 0.0360, 0.0460, 0.0520, 0.0530, 0.0505, 0.0445, 0.0395};
	const double K_MACH[] = {0.0, 0.8, 0.95, 1.2, 1.6, 2.0};
	const double K_TABLE[] = {0.170, 0.172, 0.190, 0.225, 0.280, 0.330};
	const FVector GRAVITY(0.0, -FF35FlightModel::G0, 0.0);
}

void FF35FlightModel::Reset(const FVector& Pos, const FQuat& Rot, double Speed, double FuelFraction)
{
	Position = Pos;
	Orientation = Rot.GetNormalized();
	Velocity = -BasisZ(Orientation).GetSafeNormal() * Speed;
	Rates = FVector::ZeroVector;
	Fuel = FUEL_MAX * FMath::Clamp(FuelFraction, 0.0, 1.0);
	Engine = 0.8;
	Ab = 0.0;
	AbLight = 0.0;
	InPitch = InRoll = InYaw = 0.0;
	InThrottle = 0.8;
	InAb = 0.0;
	bInBrake = false;
	bPathHold = false;
	Nz = 1.0;
	NzMax = 1.0;
	SimTime = 0.0;
	Mass = EMPTY_MASS + PAYLOAD_MASS + Fuel;
	UpdateAir();
}

void FF35FlightModel::SetControls(double Pitch, double Roll, double Yaw, double Throttle, double Afterburner, bool bBrake)
{
	InPitch = FMath::Clamp(Pitch, -1.0, 1.0);
	InRoll = FMath::Clamp(Roll, -1.0, 1.0);
	InYaw = FMath::Clamp(Yaw, -1.0, 1.0);
	InThrottle = FMath::Clamp(Throttle, 0.0, 1.0);
	InAb = FMath::Clamp(Afterburner, 0.0, 1.0);
	bInBrake = bBrake;
}

void FF35FlightModel::Step(double Dt)
{
	UpdateEngine(Dt);
	UpdateAir();
	ControlLaws(Dt);
	Rotate(Dt);
	UpdateAir();
	Integrate(Dt);
	SimTime += Dt;
}

double FF35FlightModel::HeadingDeg() const
{
	return FPosMod(Deg(FMath::Atan2(Fwd.X, -Fwd.Z)), 360.0);
}

double FF35FlightModel::PitchDeg() const
{
	return Deg(FMath::Asin(FMath::Clamp(Fwd.Y, -1.0, 1.0)));
}

double FF35FlightModel::RollDeg() const
{
	const FVector Flat(Fwd.X, 0.0, Fwd.Z);
	if (Flat.SizeSquared() < 1e-6) { return 0.0; }
	const FVector LevelRight = FVector::CrossProduct(Flat.GetSafeNormal(), FVector(0, 1, 0));
	return Deg(FMath::Atan2(-Right.Y, FVector::DotProduct(Right, LevelRight)));
}

// ---------------------------------------------------------------- engine

void FF35FlightModel::UpdateEngine(double Dt)
{
	double Target = InThrottle;
	const bool bWantAb = InAb > 0.0 && Fuel > 0.0;
	if (bWantAb) { Target = 1.0; }
	if (Fuel <= 0.0) { Target = 0.0; }
	if (Target > Engine)
	{
		// idle -> MIL in ~3 s (F135-class spool-up) [estimate]
		Engine = FMath::Min(Target, Engine + (0.18 + 0.35 * Engine) * Dt);
	}
	else
	{
		Engine = FMath::Max(Target, Engine - 0.45 * Dt);
	}
	if (bWantAb && Engine > 0.95)
	{
		AbLight += Dt;
		if (AbLight > 0.25)
		{
			const double AbTarget = 0.15 + 0.85 * InAb;
			Ab = MoveToward(Ab, AbTarget, 1.2 * Dt);
		}
	}
	else
	{
		AbLight = 0.0;
		Ab = MoveToward(Ab, 0.0, 2.5 * Dt);
	}
	Fuel = FMath::Max(0.0, Fuel - FuelFlow * Dt);
	Mass = EMPTY_MASS + PAYLOAD_MASS + Fuel;
}

FAtmosphere FF35FlightModel::Isa(double H)
{
	const double HH = FMath::Clamp(H, -500.0, 20000.0);
	FAtmosphere A;
	if (HH <= 11000.0)
	{
		A.Temperature = 288.15 - 0.0065 * HH;
		A.Pressure = 101325.0 * FMath::Pow(A.Temperature / 288.15, 5.25588);
	}
	else
	{
		A.Temperature = 216.65;
		A.Pressure = 22632.06 * FMath::Exp(-G0 / (R_AIR * A.Temperature) * (HH - 11000.0));
	}
	A.Density = A.Pressure / (R_AIR * A.Temperature);
	return A;
}

double FF35FlightModel::NormalShockRecovery(double M)
{
	if (M <= 1.0) { return 1.0; }
	const double M2 = M * M;
	const double G = GAMMA_AIR;
	const double A = FMath::Pow((G + 1.0) * M2 / ((G - 1.0) * M2 + 2.0), G / (G - 1.0));
	const double B = FMath::Pow((G + 1.0) / (2.0 * G * M2 - (G - 1.0)), 1.0 / (G - 1.0));
	return A * B;
}

FThrustLimits FF35FlightModel::ThrustLimits(double H, double M)
{
	const FAtmosphere Atm = Isa(H);
	const double Theta = Atm.Temperature / 288.15;
	const double Delta = Atm.Pressure / 101325.0;
	const double Tr = 1.0 + 0.2 * M * M;
	const double Theta0 = Theta * Tr;
	const double Delta0 = Delta * FMath::Pow(Tr, 3.5);
	double Dry = Delta0;
	double Wet = Delta0;
	if (Theta0 > THROTTLE_RATIO)
	{
		Dry = Delta0 * (1.0 - DRY_LAPSE * (Theta0 - THROTTLE_RATIO) / Theta0);
		Wet = Delta0 * (1.0 - WET_LAPSE * (Theta0 - THROTTLE_RATIO) / Theta0);
	}
	const double Rec = FMath::Pow(NormalShockRecovery(M), INLET_LOSS_EXP);
	FThrustLimits L;
	L.Idle = THRUST_IDLE_SL * Delta0;
	L.Mil = THRUST_MIL_SL * FMath::Max(Dry, 0.0) * Rec;
	L.Max = THRUST_AB_SL * FMath::Max(Wet, 0.0) * Rec;
	return L;
}

void FF35FlightModel::ComputeThrust()
{
	if (Fuel <= 0.0)
	{
		Thrust = 0.0;
		FuelFlow = 0.0;
		return;
	}
	const FThrustLimits Lim = ThrustLimits(Position.Y, Mach);
	const double Dry = Lerp(Lim.Idle, Lim.Mil, FMath::Pow(Engine, THRUST_CURVE));
	Thrust = Dry;
	double AbPart = 0.0;
	if (Ab > 0.0)
	{
		AbPart = FMath::Max(0.0, Lim.Max - Lim.Mil) * Ab;
		Thrust = Dry + AbPart;
	}
	FuelFlow = FMath::Max(FUEL_IDLE_FLOW, (Dry * TSFC_MIL + AbPart * TSFC_AB_INC) / 3600.0);
}

// ---------------------------------------------------------------- aero

double FF35FlightModel::Table(const double* Xs, const double* Ys, int32 N, double X)
{
	if (X <= Xs[0]) { return Ys[0]; }
	if (X >= Xs[N - 1]) { return Ys[N - 1]; }
	for (int32 I = 1; I < N; ++I)
	{
		if (X <= Xs[I])
		{
			const double T = (X - Xs[I - 1]) / (Xs[I] - Xs[I - 1]);
			return Lerp(Ys[I - 1], Ys[I], T);
		}
	}
	return Ys[N - 1];
}

double FF35FlightModel::LiftCoefficient(double A, double M, bool bFlaps)
{
	const double Sgn = A >= 0.0 ? 1.0 : -1.0;
	double Ad = FMath::Abs(Deg(A));
	double Reverse = 1.0;
	if (Ad > 90.0)
	{
		Ad = 180.0 - Ad;
		Reverse = -0.6;
	}
	const double Base = Table(CL_ALPHA_DEG, CL_TABLE, 15, Ad);
	double X = Base * Table(MACH_X, CLA_FACTOR, 9, M);
	const double Cap = CL_TABLE[7] * Table(MACH_X, CLMAX_FACTOR, 9, M);
	if (X > 0.7 * Cap)
	{
		X = Cap * (0.7 + 0.3 * FMath::Tanh((X / Cap - 0.7) / 0.3));
	}
	if (Sgn < 0.0) { X *= 0.75; }
	double C = Sgn * Reverse * X;
	if (bFlaps && Ad < 30.0) { C += CL_FLAPS; }
	return C;
}

double FF35FlightModel::DragCoefficient(double A, double M, double CL, bool bGear, bool bBrake)
{
	double Cd0 = Table(CD0_MACH, CD0_TABLE, 11, M);
	if (bGear) { Cd0 += CD_GEAR; }
	if (bBrake) { Cd0 += CD_BRAKE; }
	const double K = Table(K_MACH, K_TABLE, 6, M);
	double Aa = FMath::Abs(A);
	if (Aa > UE_DOUBLE_PI * 0.5) { Aa = UE_DOUBLE_PI - Aa; }
	const double Induced = K * CL * CL;
	const double Separated = FMath::Max(FMath::Abs(CL) * FMath::Tan(FMath::Min(Aa, Rad(60.0))), 1.85 * FMath::Sin(Aa) * FMath::Sin(Aa));
	const double S = Smoothstep(Rad(12.0), Rad(30.0), Aa);
	return Cd0 + Lerp(Induced, Separated, S);
}

double FF35FlightModel::AlphaLimit(double M)
{
	return Rad(Lerp(ALPHA_MAX_DEG, 25.0, Smoothstep(0.6, 1.0, M)));
}

double FF35FlightModel::AlphaForCl(double CL, double M, bool bFlaps) const
{
	const double ALim = AlphaLimit(M);
	const double APeak = FMath::Min(Rad(ALPHA_PEAK_DEG), ALim);
	const double AMin = Rad(ALPHA_MIN_DEG);
	const double ClPeak = LiftCoefficient(APeak, M, bFlaps);
	const double ClMin = LiftCoefficient(AMin, M, bFlaps);
	if (CL >= ClPeak)
	{
		const double Over = FMath::Clamp((CL / FMath::Max(ClPeak, 0.05) - 1.0) / 0.5, 0.0, 1.0);
		return APeak + Over * (ALim - APeak);
	}
	if (CL <= ClMin) { return AMin; }
	double Lo = AMin;
	double Hi = APeak;
	for (int32 I = 0; I < 14; ++I)
	{
		const double Mid = 0.5 * (Lo + Hi);
		if (LiftCoefficient(Mid, M, bFlaps) < CL) { Lo = Mid; }
		else { Hi = Mid; }
	}
	return 0.5 * (Lo + Hi);
}

double FF35FlightModel::CasFrom(double M, double P)
{
	double Qc;
	if (M <= 1.0)
	{
		Qc = P * (FMath::Pow(1.0 + 0.2 * M * M, 3.5) - 1.0);
	}
	else
	{
		Qc = P * (166.92158 * FMath::Pow(M, 7.0) / FMath::Pow(7.0 * M * M - 1.0, 2.5) - 1.0);
	}
	const double R = Qc / 101325.0 + 1.0;
	const double X = 5.0 * (FMath::Pow(R, 2.0 / 7.0) - 1.0);
	if (X <= 1.0) { return 340.294 * FMath::Sqrt(FMath::Max(X, 0.0)); }
	double V = 340.294 * FMath::Sqrt(X);
	for (int32 I = 0; I < 6; ++I)
	{
		const double Mm = V / 340.294;
		const double Ratio = 166.92158 * FMath::Pow(Mm, 7.0) / FMath::Pow(7.0 * Mm * Mm - 1.0, 2.5);
		V *= FMath::Pow(R / Ratio, 0.5);
	}
	return V;
}

// ---------------------------------------------------------------- core

void FF35FlightModel::UpdateAir()
{
	Fwd = -BasisZ(Orientation);
	UpV = BasisY(Orientation);
	Right = BasisX(Orientation);
	const FAtmosphere Atm = Isa(Position.Y);
	Temperature = Atm.Temperature;
	Pressure = Atm.Pressure;
	Rho = Atm.Density;
	SoundSpeed = FMath::Sqrt(GAMMA_AIR * R_AIR * Temperature);
	Tas = Velocity.Size();
	Mach = Tas / SoundSpeed;
	Qbar = 0.5 * Rho * Tas * Tas;
	Cas = CasFrom(Mach, Pressure);
	if (Tas > 1.0)
	{
		VHat = Velocity / Tas;
		Alpha = FMath::Atan2(-FVector::DotProduct(Velocity, UpV), FVector::DotProduct(Velocity, Fwd));
		Beta = FMath::Asin(FMath::Clamp(FVector::DotProduct(Velocity, Right) / Tas, -1.0, 1.0));
	}
	else
	{
		VHat = Fwd;
		Alpha = 0.0;
		Beta = 0.0;
	}
	const FVector Ld = UpV - VHat * FVector::DotProduct(UpV, VHat);
	LiftDir = Ld.SizeSquared() > 1e-8 ? Ld.GetSafeNormal() : UpV;
	PathAngle = FMath::Asin(FMath::Clamp(VHat.Y, -1.0, 1.0));
	FVector Vert = FVector(0, 1, 0) - VHat * VHat.Y;
	if (Vert.SizeSquared() > 1e-6)
	{
		Vert = Vert.GetSafeNormal();
		const FVector Side = FVector::CrossProduct(VHat, Vert);
		Bank = FMath::Atan2(FVector::DotProduct(LiftDir, Side), FVector::DotProduct(LiftDir, Vert));
	}
	else
	{
		Bank = 0.0;
	}
}

void FF35FlightModel::ControlLaws(double Dt)
{
	const double Qs = FMath::Max(Qbar * WING_AREA, 1.0);
	const double Weight = Mass * G0;
	Authority = FMath::Clamp(Qbar / 1800.0, 0.0, 1.0);
	const double V = FMath::Max(Tas, 40.0);
	const bool bFlaps = bGearDown;

	// Pitch: neutral stick holds the flight path (bank-compensated 1 g), stick commands load factor to +7.5/-3 g
	const double Cb = FMath::Cos(Bank);
	double Comp;
	if (Cb >= 0.5) { Comp = 1.0 / Cb; }
	else if (Cb >= 0.0) { Comp = 4.0 * Cb; }
	else { Comp = Cb; }
	const double N0 = FMath::Cos(PathAngle) * Comp;
	double NHi = N_MAX;
	double NLo = N_MIN;
	if (bGearDown)
	{
		NHi = 4.0;
		NLo = -1.0;
	}
	const double S = InPitch;
	double N = N0 + (S >= 0.0 ? S * (NHi - N0) : S * (N0 - NLo));
	if (FMath::Abs(S) < 0.03 && FMath::Abs(Bank) < Rad(70.0) && Authority > 0.5)
	{
		if (!bPathHold)
		{
			bPathHold = true;
			PathRef = PathAngle;
		}
		N += FMath::Clamp(Tas / G0 * K_PATH * (PathRef - PathAngle), -1.0, 1.0);
	}
	else
	{
		bPathHold = false;
	}
	N = FMath::Clamp(N, NLo, NHi);
	NCmd = N;
	double CL = (N * Weight - Thrust * FMath::Sin(Alpha)) / Qs;
	AlphaCmd = FMath::Clamp(AlphaForCl(CL, Mach, bFlaps), Rad(ALPHA_MIN_DEG), AlphaLimit(Mach));
	for (int32 I = 0; I < 2; ++I)
	{
		const double Cla = LiftCoefficient(AlphaCmd, Mach, bFlaps);
		const double Nb = (Cla * FMath::Cos(AlphaCmd) + DragCoefficient(AlphaCmd, Mach, Cla, bGearDown, bInBrake) * FMath::Sin(AlphaCmd)) * Qs / Weight;
		if (Nb > NHi) { CL = Cla * NHi / Nb; }
		else if (Nb < NLo) { CL = Cla * NLo / Nb; }
		else { break; }
		AlphaCmd = FMath::Clamp(AlphaForCl(CL, Mach, bFlaps), Rad(ALPHA_MIN_DEG), AlphaLimit(Mach));
	}
	const double NAch = (LiftCoefficient(AlphaCmd, Mach, bFlaps) * Qs + Thrust * FMath::Sin(AlphaCmd)) / Weight;
	const double QFf = (NAch * G0 + FVector::DotProduct(GRAVITY, LiftDir)) / V;
	const double Stiff = FMath::Clamp(Qbar / 8000.0, 0.0, 1.0);
	double QCmd = QFf + K_ALPHA * Lerp(0.4, 1.0, Stiff) * (AlphaCmd - Alpha);
	if (Nz > NHi) { QCmd -= 0.15 * (Nz - NHi); }
	else if (Nz < NLo) { QCmd += 0.15 * (NLo - Nz); }
	QCmd = Lerp(-1.5 * Alpha, QCmd, Authority);
	QCmd = FMath::Clamp(QCmd, -Rad(PITCH_RATE_DOWN_DEG), Rad(PITCH_RATE_UP_DEG));
	const double TauQ = 0.08 + 0.17 * (1.0 - Stiff);
	Rates.X += (QCmd - Rates.X) * (1.0 - FMath::Exp(-Dt / TauQ));

	// Roll: roll-rate command about the velocity vector, authority fades at low q and high AoA
	double PMax = Rad(ROLL_RATE_MAX_DEG) * FMath::Clamp(Qbar / 9000.0, 0.3, 1.0);
	PMax *= Lerp(1.0, 0.35, Smoothstep(Rad(15.0), Rad(45.0), Alpha));
	PMax *= 1.0 - 0.3 * Smoothstep(1.0, 1.6, Mach);
	if (bGearDown) { PMax *= 0.6; }
	const double PCmd = InRoll * PMax * FMath::Max(Authority, 0.15);
	Rates.Z += (PCmd - Rates.Z) * (1.0 - FMath::Exp(-Dt / TAU_ROLL));

	// Yaw: automatic sideslip suppression; pedals command a small sideslip
	const double BetaCmd = -InYaw * Rad(Lerp(8.0, 2.0, FMath::Clamp(Qbar / 40000.0, 0.0, 1.0)));
	double RCmd = SideAccel / V + K_BETA * (Beta - BetaCmd);
	RCmd = FMath::Clamp(RCmd, -0.6, 0.6);
	Rates.Y += (RCmd - Rates.Y) * (1.0 - FMath::Exp(-Dt / TAU_YAW));
}

FVector FF35FlightModel::LocalOmega() const
{
	const FVector VLoc = Tas > 20.0 ? Orientation.UnrotateVector(VHat) : FVector(0, 0, -1);
	return FVector(Rates.X, -Rates.Y, 0.0) + VLoc * Rates.Z;
}

void FF35FlightModel::Rotate(double Dt)
{
	const FVector W = LocalOmega();
	const double Ang = W.Size() * Dt;
	if (Ang > 1e-9)
	{
		Orientation = (Orientation * FQuat(W.GetSafeNormal(), Ang)).GetNormalized();
	}
}

void FF35FlightModel::Integrate(double Dt)
{
	ComputeThrust();
	const bool bFlaps = bGearDown;
	Cl = LiftCoefficient(Alpha, Mach, bFlaps);
	Cd = DragCoefficient(Alpha, Mach, Cl, bGearDown, bInBrake);
	const double Qs = Qbar * WING_AREA;
	const FVector SideDir = FVector::CrossProduct(VHat, LiftDir);
	const FVector FAero = (LiftDir * Cl - VHat * Cd + SideDir * (CY_BETA * Beta)) * Qs;
	const FVector FThrust = Fwd * Thrust;
	const FVector FNg = FAero + FThrust;
	Nz = FVector::DotProduct(FNg, UpV) / (Mass * G0);
	if (SimTime > 0.5) { NzMax = FMath::Max(NzMax, Nz); }
	const FVector Accel = FNg / Mass + GRAVITY;
	SideAccel = FVector::DotProduct(Accel, SideDir);
	Velocity += Accel * Dt;
	Position += Velocity * Dt;
}
