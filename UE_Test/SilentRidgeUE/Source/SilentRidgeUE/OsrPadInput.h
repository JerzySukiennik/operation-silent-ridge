// Sony pad support (DualShock 4 v1/v2, DualSense, DualSense Edge; USB and Bluetooth) read straight from HID input reports on a worker thread.
// UE on Windows only handles XInput pads natively; this fills the gap without any driver/tool on the player's side.
#pragma once

#include "CoreMinimal.h"

struct FOsrPadState
{
	bool bConnected = false;
	FString Name;             // "DualShock 4 (v2)" ...
	uint16 ProductId = 0;
	double LX = 0, LY = 0;    // -1..1, +X right, +Y down (stick pulled back)
	double RX = 0, RY = 0;
	double L2 = 0, R2 = 0;    // 0..1
	bool bSquare = false, bCross = false, bCircle = false, bTriangle = false;
	bool bL1 = false, bR1 = false, bL3 = false, bR3 = false;
	bool bShare = false, bOptions = false, bPS = false, bTouchpad = false;
	int32 Hat = 8;            // 0 N, 1 NE ... 7 NW, 8 released
	int32 ReportId = -1;
	int32 ReportLen = 0;
	int64 Reports = 0;
	uint8 Raw[16] = {};
	bool DPadUp() const { return Hat == 7 || Hat == 0 || Hat == 1; }
	bool DPadDown() const { return Hat == 3 || Hat == 4 || Hat == 5; }
};

class SILENTRIDGEUE_API FOsrPadInput
{
public:
	/** Starts the reader thread (Windows; no-op elsewhere). Safe to call repeatedly. */
	static void Start();
	static void Stop();
	/** Thread-safe copy of the latest state. */
	static FOsrPadState Get();
	/** Human-readable description of the active pad for the HUD ("DualShock 4 (v2)", "Xbox / XInput pad", or empty). */
	static FString DescribeActivePad();
};
