// Input shaping + throttle detent logic (port of controls.gd).
#include "OsrControls.h"
#include "F35FlightModel.h"

const FOsrHelpRow GOsrLayoutHelp[] = {
	{TEXT("Left stick"), TEXT("Pitch (G command) / roll (roll rate)")},
	{TEXT("RT / LT"), TEXT("Throttle up / down; at MIL release RT, then press and hold for AB")},
	{TEXT("LB / RB"), TEXT("Rudder left / right")},
	{TEXT("Right stick"), TEXT("Look around (auto-recentres)")},
	{TEXT("R3 (hold)"), TEXT("Look back")},
	{TEXT("B (hold)"), TEXT("Speedbrake")},
	{TEXT("Y"), TEXT("Landing gear")},
	{TEXT("Back / View"), TEXT("This help")},
	{TEXT("Start / Menu"), TEXT("Pause menu (respawn, invert pitch, quit)")},
};
const int32 GOsrLayoutHelpCount = UE_ARRAY_COUNT(GOsrLayoutHelp);

FVector2D FOsrControls::ShapeStick(const FVector2D& V, double Deadzone)
{
	const double L = V.Size();
	if (L <= Deadzone) { return FVector2D::ZeroVector; }
	const double Scaled = FMath::Min((L - Deadzone) / (1.0 - Deadzone), 1.0);
	return V / L * Scaled;
}

void FOsrControls::Update(const FOsrRawInput& Raw, double Dt)
{
	if (!bEnabled)
	{
		Input.Pitch = 0.0;
		Input.Roll = 0.0;
		Input.Yaw = 0.0;
		Input.Look = FVector2D::ZeroVector;
		Input.bGearToggle = false;
		Input.bHelpToggle = false;
		return;
	}
	const FVector2D S = ShapeStick(FVector2D(Raw.StickX, Raw.StickY), STICK_DEADZONE);
	Input.Roll = Expo(S.X, ROLL_EXPO);
	Input.Pitch = Expo(S.Y, PITCH_EXPO) * (bInvertPitch ? -1.0 : 1.0);

	const double RudderTarget = Raw.RudderRight - Raw.RudderLeft;
	Rudder = OsrMath::MoveToward(Rudder, RudderTarget, RUDDER_RAMP * Dt);
	Input.Yaw = Rudder;

	UpdateLever(Dt, Raw.ThrottleUp, Raw.ThrottleDown);
	Input.Throttle = FMath::Min(LeverValue, 1.0);
	Input.Afterburner = LeverValue > 1.0 ? FMath::Clamp(LeverValue - 1.0, 0.0, 1.0) : 0.0;
	if (LeverValue > 1.0 && Input.Afterburner < 0.02) { Input.Afterburner = 0.02; }
	Input.bBrake = Raw.bBrake;

	Input.bGearToggle = Raw.bGear && !bGearPrev;
	bGearPrev = Raw.bGear;
	Input.bHelpToggle = Raw.bHelp && !bHelpPrev;
	bHelpPrev = Raw.bHelp;

	Input.Look = ShapeStick(FVector2D(Raw.LookX, Raw.LookY), LOOK_DEADZONE);
	Input.bLookBack = Raw.bLookBack;
}

void FOsrControls::UpdateLever(double Dt, double Up, double Down)
{
	const double UpS = OsrMath::Smoothstep(0.05, 1.0, Up);
	const double DownS = OsrMath::Smoothstep(0.05, 1.0, Down);
	if (DownS > 0.0)
	{
		DetentTimer = 0.0;
		if (LeverValue > 1.0)
		{
			LeverValue = FMath::Max(1.0, LeverValue - AB_RATE * DownS * Dt);
			if (LeverValue <= 1.0 + 1e-4) { LeverValue = 0.999; }
		}
		else
		{
			LeverValue = FMath::Max(0.0, LeverValue - LEVER_RATE * DownS * Dt);
		}
		return;
	}
	if (Up < 0.5 && LeverValue >= 1.0 - 1e-4 && LeverValue <= 1.0 + 1e-4) { bDetentArmed = true; }
	if (UpS <= 0.0)
	{
		DetentTimer = 0.0;
		return;
	}
	if (LeverValue < 1.0)
	{
		LeverValue = FMath::Min(1.0, LeverValue + LEVER_RATE * UpS * Dt);
		DetentTimer = 0.0;
		bDetentArmed = false;
	}
	else if (LeverValue <= 1.0 + 1e-4)
	{
		if (Up > 0.9 && bDetentArmed)
		{
			DetentTimer += Dt;
			if (DetentTimer >= DETENT_HOLD)
			{
				LeverValue = 1.0 + 1e-3;
				bDetentArmed = false;
			}
		}
		else
		{
			DetentTimer = 0.0;
		}
	}
	else
	{
		LeverValue = FMath::Min(2.0, LeverValue + AB_RATE * UpS * Dt);
	}
}
