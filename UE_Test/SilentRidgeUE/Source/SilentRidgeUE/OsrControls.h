// Pilot input shaping ported from Godot controls.gd / control_input.gd: deadzones, expo curves, throttle lever with MIL detent (release + hold again for AB), rudder ramp.
#pragma once

#include "CoreMinimal.h"

/** One tick of shaped pilot input (also what the autotest autopilot writes). */
struct FOsrControlInput
{
	double Pitch = 0.0;        // -1 push .. +1 pull
	double Roll = 0.0;         // -1 left .. +1 right
	double Yaw = 0.0;          // -1 left .. +1 right
	double Throttle = 0.8;     // dry lever 0 idle .. 1 MIL
	double Afterburner = 0.0;  // 0 off, (0..1] min..max
	bool bBrake = false;
	bool bGearToggle = false;  // edge
	FVector2D Look = FVector2D::ZeroVector;
	bool bLookBack = false;
	bool bHelpToggle = false;  // edge

	double Lever() const { return Afterburner > 0.0 ? Throttle + Afterburner : Throttle; }
};

/** Raw device state sampled each tick (gamepad + keyboard merged, already signed). */
struct FOsrRawInput
{
	double StickX = 0.0;       // roll: + right
	double StickY = 0.0;       // pitch: + pull (stick back)
	double RudderRight = 0.0;
	double RudderLeft = 0.0;
	double ThrottleUp = 0.0;   // RT 0..1
	double ThrottleDown = 0.0; // LT 0..1
	bool bBrake = false;
	bool bGear = false;
	double LookX = 0.0;        // + right
	double LookY = 0.0;        // + up
	bool bLookBack = false;
	bool bHelp = false;
};

class SILENTRIDGEUE_API FOsrControls
{
public:
	static constexpr double STICK_DEADZONE = 0.07;
	static constexpr double PITCH_EXPO = 0.55;
	static constexpr double ROLL_EXPO = 0.45;
	static constexpr double LOOK_DEADZONE = 0.15;
	static constexpr double LEVER_RATE = 0.6;
	static constexpr double AB_RATE = 1.6;
	static constexpr double DETENT_HOLD = 0.25;
	static constexpr double RUDDER_RAMP = 4.0;

	FOsrControlInput Input;
	bool bInvertPitch = false;
	bool bEnabled = true;

	static FVector2D ShapeStick(const FVector2D& V, double Deadzone);
	static double Expo(double X, double K) { return (1.0 - K) * X + K * X * X * X; }

	void Update(const FOsrRawInput& Raw, double Dt);
	void UpdateLever(double Dt, double Up, double Down);
	void SetLever(double Value) { LeverValue = FMath::Clamp(Value, 0.0, 2.0); }
	double GetLever() const { return LeverValue; }
	double DetentProgress() const { return FMath::Clamp(DetentTimer / DETENT_HOLD, 0.0, 1.0); }

private:
	double LeverValue = 0.8;
	double DetentTimer = 0.0;
	bool bDetentArmed = false;
	double Rudder = 0.0;
	bool bGearPrev = false;
	bool bHelpPrev = false;
};

struct FOsrHelpRow { const TCHAR* Key; const TCHAR* Text; };
extern SILENTRIDGEUE_API const FOsrHelpRow GOsrLayoutHelp[];
extern SILENTRIDGEUE_API const int32 GOsrLayoutHelpCount;
