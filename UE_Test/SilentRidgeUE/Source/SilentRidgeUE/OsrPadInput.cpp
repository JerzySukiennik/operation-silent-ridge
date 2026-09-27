// HID reader for Sony pads: enumerates HID game controllers via the RawInput device list, opens the Sony one (VID 054C) and parses its input reports by known byte layout.
#include "OsrPadInput.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "Misc/ScopeLock.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	FCriticalSection GLock;
	FOsrPadState GState;

	FString SonyName(uint16 Pid)
	{
		switch (Pid)
		{
		case 0x05C4: return TEXT("DualShock 4 (v1)");
		case 0x09CC: return TEXT("DualShock 4 (v2)");
		case 0x0BA0: return TEXT("DualShock 4 (USB wireless adaptor)");
		case 0x0CE6: return TEXT("DualSense");
		case 0x0DF2: return TEXT("DualSense Edge");
		default: return FString::Printf(TEXT("Sony pad %04X"), Pid);
		}
	}

	double Stick(uint8 V) { return FMath::Clamp((double(V) - 127.5) / 127.5, -1.0, 1.0); }

	/** Parses one input report (Buf[0] = report id) into S. Returns false if the layout is unknown. */
	bool Parse(const uint8* Buf, int32 Len, uint16 Pid, FOsrPadState& S)
	{
		if (Len < 10) { return false; }
		const bool bDualSense = Pid == 0x0CE6 || Pid == 0x0DF2;
		int32 Sticks, Face, Shoulders, Sys, L2, R2;
		const uint8 Id = Buf[0];
		if (!bDualSense && Id == 0x01)
		{
			// DS4 USB report 1 and DS4 Bluetooth "simple" report 1
			Sticks = 1; Face = 5; Shoulders = 6; Sys = 7; L2 = 8; R2 = 9;
		}
		else if (!bDualSense && Id == 0x11 && Len >= 12)
		{
			// DS4 Bluetooth extended report (after a host enabled it, e.g. Steam/SDL): 2 extra header bytes
			Sticks = 3; Face = 7; Shoulders = 8; Sys = 9; L2 = 10; R2 = 11;
		}
		else if (bDualSense && Id == 0x01 && Len >= 11 && Len >= 30)
		{
			// DualSense USB report 1
			Sticks = 1; L2 = 5; R2 = 6; Face = 8; Shoulders = 9; Sys = 10;
		}
		else if (bDualSense && Id == 0x01)
		{
			// DualSense Bluetooth simple report 1 (DS4-like)
			Sticks = 1; Face = 5; Shoulders = 6; Sys = 7; L2 = 8; R2 = 9;
		}
		else if (bDualSense && Id == 0x31 && Len >= 12)
		{
			// DualSense Bluetooth extended report
			Sticks = 2; L2 = 6; R2 = 7; Face = 9; Shoulders = 10; Sys = 11;
		}
		else
		{
			return false;
		}
		S.LX = Stick(Buf[Sticks]);
		S.LY = Stick(Buf[Sticks + 1]);
		S.RX = Stick(Buf[Sticks + 2]);
		S.RY = Stick(Buf[Sticks + 3]);
		S.L2 = Buf[L2] / 255.0;
		S.R2 = Buf[R2] / 255.0;
		const uint8 F = Buf[Face];
		S.Hat = F & 0x0F;
		S.bSquare = (F & 0x10) != 0;
		S.bCross = (F & 0x20) != 0;
		S.bCircle = (F & 0x40) != 0;
		S.bTriangle = (F & 0x80) != 0;
		const uint8 Sh = Buf[Shoulders];
		S.bL1 = (Sh & 0x01) != 0;
		S.bR1 = (Sh & 0x02) != 0;
		S.bShare = (Sh & 0x10) != 0;
		S.bOptions = (Sh & 0x20) != 0;
		S.bL3 = (Sh & 0x40) != 0;
		S.bR3 = (Sh & 0x80) != 0;
		const uint8 Sy = Buf[Sys];
		S.bPS = (Sy & 0x01) != 0;
		S.bTouchpad = (Sy & 0x02) != 0;
		return true;
	}
}

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	class FPadThread : public FRunnable
	{
	public:
		FThreadSafeBool bStop = false;

		virtual uint32 Run() override
		{
			uint8 Buf[1024];
			HANDLE Dev = INVALID_HANDLE_VALUE;
			uint16 Pid = 0;
			OVERLAPPED Ov = {};
			Ov.hEvent = CreateEventW(nullptr, 1, 0, nullptr);
			bool bPending = false;
			double NextScan = 0.0;
			bool bLoggedFirst = false;
			while (!bStop)
			{
				if (Dev == INVALID_HANDLE_VALUE)
				{
					if (FPlatformTime::Seconds() < NextScan) { FPlatformProcess::Sleep(0.1f); continue; }
					NextScan = FPlatformTime::Seconds() + 1.5;
					FString Path;
					if (Find(Path, Pid))
					{
						Dev = CreateFileW(*Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
						if (Dev == INVALID_HANDLE_VALUE)
						{
							UE_LOG(LogTemp, Warning, TEXT("OSR pad: cannot open %s (error %d)"), *Path, int32(GetLastError()));
						}
						else
						{
							UE_LOG(LogTemp, Display, TEXT("OSR pad: opened %s  VID 054C PID %04X  %s"), *SonyName(Pid), Pid, *Path);
							FScopeLock L(&GLock);
							GState = FOsrPadState();
							GState.bConnected = true;
							GState.ProductId = Pid;
							GState.Name = SonyName(Pid);
							bLoggedFirst = false;
						}
					}
					continue;
				}
				if (!bPending)
				{
					ResetEvent(Ov.hEvent);
					if (!ReadFile(Dev, Buf, sizeof(Buf), nullptr, &Ov) && GetLastError() != ERROR_IO_PENDING)
					{
						Close(Dev, TEXT("read failed"));
						continue;
					}
					bPending = true;
				}
				const DWORD W = WaitForSingleObject(Ov.hEvent, 200);
				if (W != WAIT_OBJECT_0) { continue; }
				bPending = false;
				DWORD Got = 0;
				if (!GetOverlappedResult(Dev, &Ov, &Got, 0))
				{
					Close(Dev, TEXT("device removed"));
					continue;
				}
				FScopeLock L(&GLock);
				FOsrPadState S = GState;
				if (Parse(Buf, int32(Got), Pid, S))
				{
					S.ReportId = Buf[0];
					S.ReportLen = int32(Got);
					++S.Reports;
					FMemory::Memcpy(S.Raw, Buf, FMath::Min<int32>(16, int32(Got)));
					GState = S;
					if (!bLoggedFirst)
					{
						bLoggedFirst = true;
						UE_LOG(LogTemp, Display, TEXT("OSR pad: first report id 0x%02X len %d  LX %.2f LY %.2f RX %.2f RY %.2f L2 %.2f R2 %.2f hat %d"),
							Buf[0], int32(Got), S.LX, S.LY, S.RX, S.RY, S.L2, S.R2, S.Hat);
					}
				}
				else
				{
					GState.ReportId = Buf[0];
					GState.ReportLen = int32(Got);
					FMemory::Memcpy(GState.Raw, Buf, FMath::Min<int32>(16, int32(Got)));
				}
			}
			if (Dev != INVALID_HANDLE_VALUE) { CancelIo(Dev); CloseHandle(Dev); }
			CloseHandle(Ov.hEvent);
			return 0;
		}

		void Close(HANDLE& Dev, const TCHAR* Why)
		{
			UE_LOG(LogTemp, Display, TEXT("OSR pad: closed (%s)"), Why);
			CancelIo(Dev);
			CloseHandle(Dev);
			Dev = INVALID_HANDLE_VALUE;
			FScopeLock L(&GLock);
			GState = FOsrPadState();
		}

		/** Finds the first Sony game controller HID collection (usage page 1, usage 4 joystick / 5 gamepad). Logs every game controller seen. */
		static bool Find(FString& OutPath, uint16& OutPid)
		{
			UINT Count = 0;
			GetRawInputDeviceList(nullptr, &Count, sizeof(RAWINPUTDEVICELIST));
			if (Count == 0) { return false; }
			TArray<RAWINPUTDEVICELIST> List;
			List.SetNumZeroed(Count);
			if (GetRawInputDeviceList(List.GetData(), &Count, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) { return false; }
			static bool bLogged = false;
			bool bFound = false;
			for (UINT I = 0; I < Count; ++I)
			{
				if (List[I].dwType != RIM_TYPEHID) { continue; }
				RID_DEVICE_INFO Info = {};
				Info.cbSize = sizeof(Info);
				UINT Sz = sizeof(Info);
				if (GetRawInputDeviceInfoW(List[I].hDevice, RIDI_DEVICEINFO, &Info, &Sz) == (UINT)-1) { continue; }
				const bool bGameCtl = Info.hid.usUsagePage == 0x01 && (Info.hid.usUsage == 0x04 || Info.hid.usUsage == 0x05);
				if (!bGameCtl) { continue; }
				UINT NameLen = 0;
				GetRawInputDeviceInfoW(List[I].hDevice, RIDI_DEVICENAME, nullptr, &NameLen);
				TArray<WCHAR> Name;
				Name.SetNumZeroed(NameLen + 1);
				GetRawInputDeviceInfoW(List[I].hDevice, RIDI_DEVICENAME, Name.GetData(), &NameLen);
				const FString Path(Name.GetData());
				if (!bLogged)
				{
					UE_LOG(LogTemp, Display, TEXT("OSR pad: HID game controller VID %04X PID %04X usage %d  %s"), Info.hid.dwVendorId, Info.hid.dwProductId, Info.hid.usUsage, *Path);
				}
				if (!bFound && Info.hid.dwVendorId == 0x054C)
				{
					OutPath = Path;
					OutPid = uint16(Info.hid.dwProductId);
					bFound = true;
				}
			}
			bLogged = true;
			return bFound;
		}
	};

	FPadThread* GRunnable = nullptr;
	FRunnableThread* GThread = nullptr;
}

void FOsrPadInput::Start()
{
	if (GThread) { return; }
	GRunnable = new FPadThread();
	GThread = FRunnableThread::Create(GRunnable, TEXT("OsrPadInput"), 0, TPri_AboveNormal);
}

void FOsrPadInput::Stop()
{
	if (!GThread) { return; }
	GRunnable->bStop = true;
	GThread->WaitForCompletion();
	delete GThread;
	delete GRunnable;
	GThread = nullptr;
	GRunnable = nullptr;
}
#else
void FOsrPadInput::Start() {}
void FOsrPadInput::Stop() {}
#endif

FOsrPadState FOsrPadInput::Get()
{
	FScopeLock L(&GLock);
	return GState;
}

FString FOsrPadInput::DescribeActivePad()
{
	const FOsrPadState S = Get();
	if (S.bConnected) { return S.Name; }
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().IsGamepadAttached()) { return TEXT("Xbox / XInput pad"); }
	return FString();
}
