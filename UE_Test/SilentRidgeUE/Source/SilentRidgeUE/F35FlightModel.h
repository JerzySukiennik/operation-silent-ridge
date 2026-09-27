// Pure, deterministic F-35C flight model ported 1:1 from Godot game/scripts/flight/f35_flight_model.gd (no UObject dependency).
// Works in the Godot frame: metres, +Y up, +X east, +Z south, nose = -Z of the body. The pawn converts to UE space.
#pragma once

#include "CoreMinimal.h"

namespace OsrMath
{
	inline double Smoothstep(double E0, double E1, double X)
	{
		// Godot smoothstep (also valid for E0 > E1)
		if (E0 == E1) { return X < E0 ? 0.0 : 1.0; }
		const double T = FMath::Clamp((X - E0) / (E1 - E0), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
	inline double MoveToward(double From, double To, double Delta)
	{
		return FMath::Abs(To - From) <= Delta ? To : From + FMath::Sign(To - From) * Delta;
	}
	inline double Lerp(double A, double B, double T) { return A + (B - A) * T; }
	inline double Deg(double Rad) { return Rad * 180.0 / UE_DOUBLE_PI; }
	inline double Rad(double Deg) { return Deg * UE_DOUBLE_PI / 180.0; }
	inline double FPosMod(double X, double Y) { const double M = FMath::Fmod(X, Y); return M < 0.0 ? M + Y : M; }
	inline FVector BasisX(const FQuat& Q) { return Q.RotateVector(FVector(1, 0, 0)); }
	inline FVector BasisY(const FQuat& Q) { return Q.RotateVector(FVector(0, 1, 0)); }
	inline FVector BasisZ(const FQuat& Q) { return Q.RotateVector(FVector(0, 0, 1)); }
	inline FVector SafeNormal(const FVector& V, const FVector& Fallback)
	{
		const double L = V.Size();
		return L > 1e-12 ? V / L : Fallback;
	}
}

struct FAtmosphere
{
	double Temperature = 288.15;
	double Pressure = 101325.0;
	double Density = 1.225;
};

struct FThrustLimits
{
	double Idle = 0.0;
	double Mil = 0.0;
	double Max = 0.0;
};

class SILENTRIDGEUE_API FF35FlightModel
{
public:
	static constexpr double G0 = 9.80665;
	static constexpr double R_AIR = 287.053;
	static constexpr double GAMMA_AIR = 1.4;

	// LM F-35C fact sheet / Wikipedia specs (REFERENCES §1.2)
	static constexpr double WING_AREA = 62.1;
	static constexpr double EMPTY_MASS = 15686.0;
	static constexpr double FUEL_MAX = 8958.0;
	static constexpr double PAYLOAD_MASS = 450.0;
	static constexpr double SPAWN_FUEL = 0.6;

	// F135-PW-100, P&W product card 2022
	static constexpr double THRUST_MIL_SL = 124550.0;
	static constexpr double THRUST_AB_SL = 191270.0;
	static constexpr double THRUST_IDLE_SL = 7000.0;
	static constexpr double THROTTLE_RATIO = 1.07;
	static constexpr double WET_LAPSE = 2.8;
	static constexpr double DRY_LAPSE = 3.8;
	static constexpr double THRUST_CURVE = 1.8;
	static constexpr double TSFC_MIL = 0.0903;
	static constexpr double TSFC_AB_INC = 0.255;
	static constexpr double FUEL_IDLE_FLOW = 0.2;
	static constexpr double INLET_LOSS_EXP = 2.5;

	static constexpr double N_MAX = 7.5;
	static constexpr double N_MIN = -3.0;
	static constexpr double ALPHA_MAX_DEG = 50.0;
	static constexpr double ALPHA_MIN_DEG = -15.0;
	static constexpr double ALPHA_PEAK_DEG = 35.0;

	static constexpr double CY_BETA = -0.9;
	static constexpr double CD_BRAKE = 0.035;
	static constexpr double CD_GEAR = 0.028;
	static constexpr double CL_FLAPS = 0.22;

	static constexpr double K_ALPHA = 3.0;
	static constexpr double K_BETA = 3.0;
	static constexpr double K_PATH = 0.6;
	static constexpr double ROLL_RATE_MAX_DEG = 200.0;
	static constexpr double PITCH_RATE_UP_DEG = 45.0;
	static constexpr double PITCH_RATE_DOWN_DEG = 30.0;
	static constexpr double TAU_ROLL = 0.09;
	static constexpr double TAU_YAW = 0.15;

	// State
	FVector Position = FVector::ZeroVector;
	FQuat Orientation = FQuat::Identity;
	FVector Velocity = FVector::ZeroVector;
	FVector Rates = FVector::ZeroVector;   // x pitch q, y yaw r (right +), z roll p about the velocity vector (right wing down +), rad/s
	double Fuel = FUEL_MAX * SPAWN_FUEL;
	double Engine = 0.8;
	double Ab = 0.0;
	bool bGearDown = false;
	double SimTime = 0.0;

	// Pilot inputs (already shaped by the controls)
	double InPitch = 0.0;
	double InRoll = 0.0;
	double InYaw = 0.0;
	double InThrottle = 0.8;
	double InAb = 0.0;
	bool bInBrake = false;

	// Derived air data
	double Mass = EMPTY_MASS + PAYLOAD_MASS + FUEL_MAX * SPAWN_FUEL;
	double Rho = 1.225;
	double Pressure = 101325.0;
	double Temperature = 288.15;
	double SoundSpeed = 340.3;
	double Tas = 0.0;
	double Mach = 0.0;
	double Qbar = 0.0;
	double Cas = 0.0;
	double Alpha = 0.0;
	double Beta = 0.0;
	double Thrust = 0.0;
	double FuelFlow = 0.0;
	double Nz = 1.0;
	double NzMax = 1.0;
	double Cl = 0.0;
	double Cd = 0.0;
	double PathAngle = 0.0;
	double Bank = 0.0;
	double AlphaCmd = 0.0;
	double NCmd = 1.0;
	double Authority = 1.0;

	/** Resets at a Godot-frame transform (nose = -Z of Rotation) with a forward speed. */
	void Reset(const FVector& Pos, const FQuat& Rot, double Speed, double FuelFraction = SPAWN_FUEL);
	void SetControls(double Pitch, double Roll, double Yaw, double Throttle, double Afterburner, bool bBrake = false);
	void Step(double Dt);
	void RefreshAirData() { UpdateAir(); }

	FVector AngularVelocityWorld() const { return Orientation.RotateVector(LocalOmega()); }
	double HeadingDeg() const;
	double PitchDeg() const;
	double RollDeg() const;
	FVector Forward() const { return Fwd; }
	FVector Up() const { return UpV; }

	static FAtmosphere Isa(double H);
	static double NormalShockRecovery(double M);
	static FThrustLimits ThrustLimits(double H, double M);
	static double Table(const double* Xs, const double* Ys, int32 N, double X);
	static double LiftCoefficient(double A, double M, bool bFlaps = false);
	static double DragCoefficient(double A, double M, double CL, bool bGear, bool bBrake);
	static double AlphaLimit(double M);
	double AlphaForCl(double CL, double M, bool bFlaps) const;
	static double CasFrom(double M, double P);

private:
	double AbLight = 0.0;
	bool bPathHold = false;
	double PathRef = 0.0;
	double SideAccel = 0.0;
	FVector Fwd = FVector(0, 0, -1);
	FVector UpV = FVector(0, 1, 0);
	FVector Right = FVector(1, 0, 0);
	FVector VHat = FVector(0, 0, -1);
	FVector LiftDir = FVector(0, 1, 0);

	void UpdateEngine(double Dt);
	void ComputeThrust();
	void UpdateAir();
	void ControlLaws(double Dt);
	FVector LocalOmega() const;
	void Rotate(double Dt);
	void Integrate(double Dt);
};
