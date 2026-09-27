// F-35C pawn: fixed-step flight model, v1 gamepad layout via runtime Enhanced Input, chase camera (ChaseCamera.gd port), procedural VFX, jet audio (jet_audio.gd local layers), crash + respawn.
#include "OsrJetPawn.h"
#include "OsrTerrain.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "ProceduralMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Sound/SoundBase.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"

using namespace OsrMath;

namespace
{
	constexpr double SIM_DT = 1.0 / 120.0;
	constexpr double SPAWN_SPEED = 210.0;
	constexpr double RESPAWN_DELAY = 4.0;
	constexpr double FT = 0.3048;
	constexpr double KT = 0.514444;
	// chase camera (chase_camera.gd)
	constexpr double CAM_DISTANCE = 19.0;
	constexpr double CAM_HEIGHT = 3.4;
	constexpr double AIM_HEIGHT = 1.6;
	constexpr double ORBIT_YAW_MAX = 150.0 * UE_DOUBLE_PI / 180.0;
	constexpr double ORBIT_PITCH_MAX = 55.0 * UE_DOUBLE_PI / 180.0;
	constexpr double RECENTRE_DELAY = 0.8;
	constexpr double FOV_SLOW = 62.0;
	constexpr double FOV_FAST = 84.0;
	constexpr double CLEARANCE = 2.5;
	// Godot visual markers (f35c_visual_real.tscn), Godot local: nose -Z
	const FVector NOZZLE(0.0, -0.33, 5.83);
	const FVector TIPS[2] = {FVector(-6.55, -0.2, 4.4), FVector(6.55, -0.2, 4.4)};
	const FVector CANOPY(0.0, 0.6, -5.1);
	const FVector COLLISION_POINTS[] = {
		FVector(0.0, 0.0, -8.0), FVector(0.0, 0.2, 7.2), FVector(-6.4, -0.1, 3.4), FVector(6.4, -0.1, 3.4),
		FVector(0.0, -0.8, 0.0), FVector(-2.0, 2.3, 5.6), FVector(2.0, 2.3, 5.6)};
	const FVector UPV(0, 1, 0);

	/** Godot body-local (x right, y up, z aft) metres -> UE component-local cm (X fwd, Y right, Z up). */
	FVector LocalToUE(const FVector& G) { return FVector(-G.Z, G.X, G.Y) * 100.0; }

	FVector VSlerp(const FVector& A, const FVector& B, double T)
	{
		const double D = FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0);
		const double Th = FMath::Acos(D);
		if (Th < 1e-6) { return A; }
		const FVector Axis = FVector::CrossProduct(A, B);
		if (Axis.SizeSquared() < 1e-12) { return (A * (1.0 - T) + B * T).GetSafeNormal(); }
		return FQuat(Axis.GetSafeNormal(), Th * T).RotateVector(A);
	}
	double ExpK(double Dt, double Rate) { return Dt > 0.0 ? 1.0 - FMath::Exp(-Dt * Rate) : 1.0; }
}

// ---------------------------------------------------------------- frames

FQuat AOsrJetPawn::BasisQuat(const FVector& Right, const FVector& Up, const FVector& Back)
{
	FMatrix M(FPlane(Right, 0.0), FPlane(Up, 0.0), FPlane(Back, 0.0), FPlane(0, 0, 0, 1));
	return FQuat(M).GetNormalized();
}

FQuat AOsrJetPawn::LookingAt(const FVector& Dir, const FVector& Up)
{
	const FVector Back = -Dir.GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Up, Back).GetSafeNormal();
	const FVector Up2 = FVector::CrossProduct(Back, Right);
	return BasisQuat(Right, Up2, Back);
}

FQuat AOsrJetPawn::RotToUE(const FQuat& G)
{
	const FVector F = DirToUE(-BasisZ(G));
	const FVector U = DirToUE(BasisY(G));
	return FRotationMatrix::MakeFromXZ(F, U).ToQuat();
}

FVector AOsrJetPawn::SpawnPosition()
{
	// world.gd spawn_points()[0]: 1.5 km from the carrier towards the canyon mouth, echelon slot 0, 600 m
	const FVector Carrier(-31000.0, 0.0, 9500.0);
	const FVector Mouth(-17500.0, 0.0, 9800.0);
	FVector ToCoast = Mouth - Carrier; ToCoast.Y = 0.0; ToCoast = ToCoast.GetSafeNormal();
	const FVector Right = FVector::CrossProduct(ToCoast, UPV).GetSafeNormal();
	FVector P = Carrier + ToCoast * 1500.0 + Right * (0.0 * 120.0 - 180.0);
	P.Y = 600.0;
	return P;
}

FQuat AOsrJetPawn::SpawnRotation()
{
	const FVector Carrier(-31000.0, 0.0, 9500.0);
	const FVector Mouth(-17500.0, 0.0, 9800.0);
	FVector ToCoast = Mouth - Carrier; ToCoast.Y = 0.0;
	return LookingAt(ToCoast.GetSafeNormal(), UPV);
}

// ---------------------------------------------------------------- lifecycle

AOsrJetPawn::AOsrJetPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("F35C"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> F35(TEXT("/Game/F35C/f35c_static/StaticMeshes/f35c_static.f35c_static"));
	if (F35.Succeeded()) { Mesh->SetStaticMesh(F35.Object); }
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	Camera->SetupAttachment(Root);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->bConstrainAspectRatio = false;
	// motion blur tuned for speed feel (ground/walls smear, the followed jet stays crisp)
	Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->PostProcessSettings.MotionBlurAmount = 0.55f;
	Camera->PostProcessSettings.bOverride_MotionBlurMax = true;
	Camera->PostProcessSettings.MotionBlurMax = 4.0f;
	Camera->PostProcessSettings.bOverride_MotionBlurPerObjectSize = true;
	Camera->PostProcessSettings.MotionBlurPerObjectSize = 0.0f;
	JetVfx = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("JetVfx"));
	JetVfx->SetupAttachment(Mesh);
	JetVfx->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	JetVfx->SetCastShadow(false);
	WorldVfx = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WorldVfx"));
	WorldVfx->SetupAttachment(Root);
	WorldVfx->SetUsingAbsoluteLocation(true);
	WorldVfx->SetUsingAbsoluteRotation(true);
	WorldVfx->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorldVfx->SetCastShadow(false);
	WorldVfx->bUseAsyncCooking = false;
	NozzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("NozzleLight"));
	NozzleLight->SetupAttachment(Mesh);
	NozzleLight->SetRelativeLocation(LocalToUE(NOZZLE + FVector(0, 0, 1.0)));
	NozzleLight->SetIntensityUnits(ELightUnits::Candelas);
	NozzleLight->SetIntensity(0.0f);
	NozzleLight->SetLightColor(FLinearColor(1.0f, 0.55f, 0.25f));
	NozzleLight->SetAttenuationRadius(2500.0f);
	NozzleLight->SetCastShadows(false);
	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AOsrJetPawn::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("nograde"))) { ApplyColorGrade(); }
	FOsrTerrain::Get();
	LoadSettings();
	BuildJetVfx();
	SetupAudio();
	Respawn();
}

void AOsrJetPawn::ApplyColorGrade()
{
	// Cinematic grade (Jurek, stage 2): richer blues/greens, crisp contrast, warm sun side / cool canyon shadows, white snow kept white.
	FPostProcessSettings& P = Camera->PostProcessSettings;
	P.bOverride_ColorSaturation = true;       P.ColorSaturation = FVector4(1.18, 1.18, 1.18, 1.0);
	P.bOverride_ColorContrast = true;         P.ColorContrast = FVector4(1.10, 1.10, 1.10, 1.0);
	P.bOverride_ColorSaturationShadows = true; P.ColorSaturationShadows = FVector4(1.0, 1.0, 1.0, 1.08);
	P.bOverride_ColorGainShadows = true;      P.ColorGainShadows = FVector4(0.95, 0.99, 1.07, 1.0);      // cool blue shadows in the canyon
	P.bOverride_ColorGainHighlights = true;   P.ColorGainHighlights = FVector4(1.03, 1.01, 0.97, 1.0);   // warm sunlit side (mild: snow stays white)
	P.bOverride_ColorGammaShadows = true;     P.ColorGammaShadows = FVector4(1.0, 1.0, 1.0, 0.96);       // slightly deeper blacks
	P.bOverride_FilmSlope = true;             P.FilmSlope = 0.92f;                                          // ACES filmic, punchier mid contrast
	P.bOverride_FilmToe = true;               P.FilmToe = 0.58f;
	P.bOverride_FilmShoulder = true;          P.FilmShoulder = 0.24f;
	P.bOverride_VignetteIntensity = true;     P.VignetteIntensity = 0.45f;
	P.bOverride_BloomIntensity = true;        P.BloomIntensity = 0.8f;
	P.bOverride_BloomThreshold = true;        P.BloomThreshold = -1.0f;
}

void AOsrJetPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	CreateInput();
	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Sub->AddMappingContext(Imc, 0);
		}
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}

void AOsrJetPawn::Respawn()
{
	RespawnAt(SpawnPosition(), SpawnRotation(), SPAWN_SPEED);
}

void AOsrJetPawn::RespawnAt(const FVector& Pos, const FQuat& Rot, double Speed)
{
	Model.Reset(Pos, Rot, Speed);
	bAlive = true;
	bGearDown = false;
	bBrake = false;
	TimeToImpact = 99.0;
	RespawnTimer = -1.0;
	CurPos = PrevPos = RenderPos = Model.Position;
	CurRot = PrevRot = RenderRot = Model.Orientation;
	Accum = 0.0;
	Controls.SetLever(0.8);
	AutopilotLever = 0.85;
	Mesh->SetVisibility(true, true);
	for (UStaticMeshComponent* B : Blast) { B->SetVisibility(false); }
	BlastT = -1.0;
	TrailPts[0].Reset();
	TrailPts[1].Reset();
	EngineFade = 1.0;
	SetActorLocationAndRotation(ToUE(RenderPos), RotToUE(RenderRot));
	SnapCamera();
	++RespawnCount;
}

// ---------------------------------------------------------------- input

void AOsrJetPawn::CreateInput()
{
	if (Imc) { return; }
	Imc = NewObject<UInputMappingContext>(this, TEXT("OsrIMC"));
	auto Make = [this](const TCHAR* Name, std::initializer_list<FKey> Keys)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = EInputActionValueType::Axis1D;
		Actions.Add(FName(Name), A);
		for (const FKey& K : Keys) { Imc->MapKey(A, K); }
	};
	// gamepad axes (signed, raw) and keyboard fallbacks as separate actions; combined in ReadRaw()
	Make(TEXT("PadLX"), {EKeys::Gamepad_LeftX});
	Make(TEXT("PadLY"), {EKeys::Gamepad_LeftY});
	Make(TEXT("PadRX"), {EKeys::Gamepad_RightX});
	Make(TEXT("PadRY"), {EKeys::Gamepad_RightY});
	Make(TEXT("ThrUp"), {EKeys::Gamepad_RightTriggerAxis, EKeys::LeftShift});
	Make(TEXT("ThrDown"), {EKeys::Gamepad_LeftTriggerAxis, EKeys::LeftControl});
	Make(TEXT("RudL"), {EKeys::Gamepad_LeftShoulder, EKeys::Q});
	Make(TEXT("RudR"), {EKeys::Gamepad_RightShoulder, EKeys::E});
	Make(TEXT("Brake"), {EKeys::Gamepad_FaceButton_Right, EKeys::B});
	Make(TEXT("Gear"), {EKeys::Gamepad_FaceButton_Top, EKeys::G});
	Make(TEXT("LookBack"), {EKeys::Gamepad_RightThumbstick, EKeys::C});
	Make(TEXT("Help"), {EKeys::Gamepad_Special_Left, EKeys::F1});
	Make(TEXT("Start"), {EKeys::Gamepad_Special_Right, EKeys::Escape, EKeys::P});
	Make(TEXT("Accept"), {EKeys::Gamepad_FaceButton_Bottom, EKeys::Enter, EKeys::SpaceBar});
	Make(TEXT("MenuUp"), {EKeys::Gamepad_DPad_Up});
	Make(TEXT("MenuDown"), {EKeys::Gamepad_DPad_Down});
	Make(TEXT("KPitchUp"), {EKeys::S, EKeys::Down});
	Make(TEXT("KPitchDown"), {EKeys::W, EKeys::Up});
	Make(TEXT("KRollL"), {EKeys::A, EKeys::Left});
	Make(TEXT("KRollR"), {EKeys::D, EKeys::Right});
	Make(TEXT("KLookL"), {EKeys::J});
	Make(TEXT("KLookR"), {EKeys::L});
	Make(TEXT("KLookU"), {EKeys::I});
	Make(TEXT("KLookD"), {EKeys::K});
}

void AOsrJetPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	CreateInput();
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		for (const auto& It : Actions) { EIC->BindActionValue(It.Value); }
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("OSR: input component is not an EnhancedInputComponent"));
	}
}

double AOsrJetPawn::ActionValue(FName Name) const
{
	const UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	const TObjectPtr<UInputAction>* A = Actions.Find(Name);
	if (!EIC || !A) { return 0.0; }
	return EIC->GetBoundActionValue(*A).Get<float>();
}

FOsrRawInput AOsrJetPawn::ReadRaw() const
{
	FOsrRawInput R;
	R.StickX = FMath::Clamp(ActionValue("PadLX") + ActionValue("KRollR") - ActionValue("KRollL"), -1.0, 1.0);
	R.StickY = FMath::Clamp(-ActionValue("PadLY") + ActionValue("KPitchUp") - ActionValue("KPitchDown"), -1.0, 1.0);
	R.RudderLeft = ActionValue("RudL");
	R.RudderRight = ActionValue("RudR");
	R.ThrottleUp = FMath::Clamp(ActionValue("ThrUp"), 0.0, 1.0);
	R.ThrottleDown = FMath::Clamp(ActionValue("ThrDown"), 0.0, 1.0);
	R.bBrake = ActionValue("Brake") > 0.5;
	R.bGear = ActionValue("Gear") > 0.5;
	R.LookX = FMath::Clamp(ActionValue("PadRX") + ActionValue("KLookR") - ActionValue("KLookL"), -1.0, 1.0);
	R.LookY = FMath::Clamp(ActionValue("PadRY") + ActionValue("KLookU") - ActionValue("KLookD"), -1.0, 1.0);
	R.bLookBack = ActionValue("LookBack") > 0.5;
	R.bHelp = ActionValue("Help") > 0.5;
	return R;
}

FString AOsrJetPawn::MenuLabel(int32 Index) const
{
	switch (Index)
	{
	case 0: return TEXT("Resume");
	case 1: return TEXT("Respawn");
	case 2: return Controls.bInvertPitch ? TEXT("Stick up = nose UP (inverted)") : TEXT("Stick up = nose DOWN (flight sim)");
	default: return TEXT("Quit");
	}
}

void AOsrJetPawn::UpdateMenu()
{
	const bool bStart = ActionValue("Start") > 0.5;
	if (bStart && !bStartPrev)
	{
		bPaused = !bPaused;
		MenuIndex = 0;
	}
	bStartPrev = bStart;
	const double Ly = ActionValue("PadLY");
	const bool bUp = ActionValue("MenuUp") > 0.5 || Ly > 0.6 || ActionValue("KPitchDown") > 0.5;
	const bool bDown = ActionValue("MenuDown") > 0.5 || Ly < -0.6 || ActionValue("KPitchUp") > 0.5;
	const bool bA = ActionValue("Accept") > 0.5;
	const bool bB = ActionValue("Brake") > 0.5;
	if (bPaused)
	{
		if (bUp && !bUpPrev) { MenuIndex = (MenuIndex + MenuCount - 1) % MenuCount; }
		if (bDown && !bDownPrev) { MenuIndex = (MenuIndex + 1) % MenuCount; }
		if (bA && !bAPrev) { ActivateMenu(MenuIndex); }
		if (bB && !bBPrev) { bPaused = false; }
	}
	bUpPrev = bUp; bDownPrev = bDown; bAPrev = bA; bBPrev = bB;
	// the mission keeps running while paused (co-op game); only the stick is released (game.gd)
	Controls.bEnabled = !bPaused;
}

void AOsrJetPawn::ActivateMenu(int32 Index)
{
	switch (Index)
	{
	case 0: bPaused = false; break;
	case 1: bPaused = false; Respawn(); break;
	case 2: Controls.bInvertPitch = !Controls.bInvertPitch; SaveSettings(); break;
	default: UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); break;
	}
}

void AOsrJetPawn::LoadSettings()
{
	bool bInv = false;
	if (GConfig) { GConfig->GetBool(TEXT("OSR.Controls"), TEXT("InvertPitch"), bInv, GGameUserSettingsIni); }
	Controls.bInvertPitch = bInv;
}

void AOsrJetPawn::SaveSettings()
{
	if (!GConfig) { return; }
	GConfig->SetBool(TEXT("OSR.Controls"), TEXT("InvertPitch"), Controls.bInvertPitch, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

// ---------------------------------------------------------------- simulation

double AOsrJetPawn::SurfaceHeight(double X, double Z) const
{
	return FOsrTerrain::Get().SurfaceAt(X, Z);
}

void AOsrJetPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FMath::Min<double>(DeltaSeconds, 0.1);
	UpdateMenu();
	if (bAutopilot)
	{
		Controls.Input = AutopilotInput;
		AutopilotInput.bGearToggle = false;
		AutopilotInput.bHelpToggle = false;
	}
	else
	{
		Controls.Update(ReadRaw(), Dt);
	}
	const FOsrControlInput& In = Controls.Input;
	if (In.bHelpToggle) { bShowHelp = !bShowHelp; }
	if (bAlive)
	{
		if (In.bGearToggle) { bGearDown = !bGearDown; }
		bBrake = In.bBrake;
		Model.SetControls(In.Pitch, In.Roll, In.Yaw, In.Throttle, In.Afterburner, In.bBrake);
		Model.bGearDown = bGearDown;
		Accum = FMath::Min(Accum + Dt, 0.25);
		while (Accum >= SIM_DT && bAlive)
		{
			SimStep(SIM_DT);
			Accum -= SIM_DT;
		}
		if (bAlive)
		{
			const double A = FMath::Clamp(Accum / SIM_DT, 0.0, 1.0);
			RenderPos = FMath::Lerp(PrevPos, CurPos, A);
			RenderRot = FQuat::Slerp(PrevRot, CurRot, A).GetNormalized();
			SetActorLocationAndRotation(ToUE(RenderPos), RotToUE(RenderRot));
		}
		WarnTimer -= Dt;
		if (WarnTimer <= 0.0)
		{
			WarnTimer = 0.1;
			TimeToImpact = PredictImpact();
		}
	}
	else if (RespawnTimer >= 0.0)
	{
		RespawnTimer -= Dt;
		if (RespawnTimer < 0.0) { Respawn(); }
	}
	PlaceCamera(Dt);
	VfxTime += Dt;
	UpdateJetVfx(Dt);
	UpdateWorldVfx(Dt);
	UpdateBlast(Dt);
	UpdateAudio(Dt);
}

void AOsrJetPawn::SimStep(double Dt)
{
	PrevPos = CurPos;
	PrevRot = CurRot;
	Model.Step(Dt);
	CurPos = Model.Position;
	CurRot = Model.Orientation;
	CheckCollision();
}

void AOsrJetPawn::CheckCollision()
{
	const FVector P = Model.Position;
	const double Ground = SurfaceHeight(P.X, P.Z);
	if (P.Y - Ground > 40.0) { return; }
	for (const FVector& Lp : COLLISION_POINTS)
	{
		const FVector Wp = CurPos + CurRot.RotateVector(Lp);
		if (Wp.Y < SurfaceHeight(Wp.X, Wp.Z))
		{
			Crash(Wp);
			return;
		}
	}
}

double AOsrJetPawn::PredictImpact() const
{
	const FVector P = Model.Position;
	const FVector V = Model.Velocity;
	double T = 0.0;
	while (T < 10.0)
	{
		T += T < 4.0 ? 0.25 : 1.0;
		const FVector Q = P + V * T;
		const double G = SurfaceHeight(Q.X, Q.Z);
		if (Q.Y < G + 5.0)
		{
			if (V.Y < -3.0 || G - Q.Y < 150.0) { return T; }
			return 99.0;
		}
	}
	return 99.0;
}

void AOsrJetPawn::Crash(const FVector& At)
{
	if (!bAlive) { return; }
	bAlive = false;
	++CrashCount;
	RenderPos = CurPos;
	RenderRot = CurRot;
	Mesh->SetVisibility(false, true);
	StartBlast(At);
	PlayOneShot("crash_explosion", 6.0);
	RespawnTimer = RESPAWN_DELAY;
}

FOsrTelemetry AOsrJetPawn::Telemetry() const
{
	const FF35FlightModel& M = Model;
	FOsrTelemetry T;
	const double Ground = SurfaceHeight(M.Position.X, M.Position.Z);
	T.IasKt = M.Cas / KT;
	T.TasKt = M.Tas / KT;
	T.Mach = M.Mach;
	T.AltFt = M.Position.Y / FT;
	T.RadarAltFt = (M.Position.Y - Ground) / FT;
	T.AoaDeg = Deg(M.Alpha);
	T.G = M.Nz;
	T.GMax = M.NzMax;
	T.HeadingDeg = M.HeadingDeg();
	T.PitchDeg = M.PitchDeg();
	T.RollDeg = M.RollDeg();
	T.Throttle = M.Engine;
	T.Lever = M.InAb > 0.0 ? 1.0 + M.InAb : M.InThrottle;
	T.bAb = M.Ab > 0.0;
	T.AbLevel = M.Ab;
	T.FuelKg = M.Fuel;
	T.VsFpm = M.Velocity.Y / FT * 60.0;
	T.bGear = bGearDown;
	T.bBrake = bBrake;
	T.bAlive = bAlive;
	T.TimeToImpact = TimeToImpact;
	T.SpeedMs = M.Tas;
	return T;
}

// ---------------------------------------------------------------- chase camera (chase_camera.gd)

FQuat AOsrJetPawn::FollowRotation() const
{
	const FVector Nose = -BasisZ(RenderRot);
	FVector Fwd = Nose;
	if (Model.Velocity.Size() > 30.0 && bAlive)
	{
		Fwd = VSlerp(Nose, Model.Velocity.GetSafeNormal(), 0.55).GetSafeNormal();
	}
	FVector Up = BasisY(RenderRot);
	FVector Right = FVector::CrossProduct(Fwd, Up);
	if (Right.SizeSquared() < 1e-6) { return RenderRot; }
	Right = Right.GetSafeNormal();
	Up = FVector::CrossProduct(Right, Fwd).GetSafeNormal();
	return BasisQuat(Right, Up, -Fwd);
}

void AOsrJetPawn::SnapCamera()
{
	Frame = FollowRotation();
	Orbit = OrbitTarget = FVector2D::ZeroVector;
	Boom = 1.0;
	CrashT = 0.0;
	PlaceCamera(0.0);
}

double AOsrJetPawn::ShakeLevel() const
{
	const FF35FlightModel& M = Model;
	double S = 0.0;
	S += Smoothstep(4.5, 7.5, M.Nz) * 0.35;
	S += Smoothstep(Rad(16.0), Rad(35.0), M.Alpha) * 0.45;
	S += (1.0 - Smoothstep(0.0, 0.07, FMath::Abs(M.Mach - 1.0))) * 0.3;
	const double Agl = M.Position.Y - SurfaceHeight(M.Position.X, M.Position.Z);
	S += (1.0 - Smoothstep(20.0, 300.0, Agl)) * Smoothstep(120.0, 300.0, M.Tas) * 0.4;
	return S;
}

FVector AOsrJetPawn::TerrainClear(const FVector& Pivot, const FVector& CamPos, double Dt)
{
	const FVector Dir = CamPos - Pivot;
	double Free = 1.0;
	for (int32 I = 1; I < 9; ++I)
	{
		const double F = double(I) / 8.0;
		const FVector Q = Pivot + Dir * F;
		if (Q.Y < SurfaceHeight(Q.X, Q.Z) + CLEARANCE)
		{
			Free = FMath::Max(double(I - 1) / 8.0, 0.25);
			break;
		}
	}
	if (Free < Boom) { Boom = Free; }
	else { Boom = Dt > 0.0 ? Lerp(Boom, Free, ExpK(Dt, 2.0)) : Free; }
	FVector P = Pivot + Dir * Boom;
	const double GEnd = SurfaceHeight(P.X, P.Z) + CLEARANCE;
	if (P.Y < GEnd) { P.Y = GEnd; }
	return P;
}

void AOsrJetPawn::ApplyCamera(const FVector& PosG, const FVector& FwdG, const FVector& UpG)
{
	CamPosUE = ToUE(PosG);
	CamRotUE = FRotationMatrix::MakeFromXZ(DirToUE(FwdG), DirToUE(UpG)).ToQuat();
	double Aspect = 16.0 / 9.0;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Sz;
		GEngine->GameViewport->GetViewportSize(Sz);
		if (Sz.Y > 1.0) { Aspect = Sz.X / Sz.Y; }
	}
	// Godot FOV is vertical; UE's is horizontal
	const double HFov = 2.0 * Deg(FMath::Atan(FMath::Tan(Rad(CamFovV) * 0.5) * Aspect));
	Camera->SetWorldLocationAndRotation(CamPosUE, CamRotUE);
	Camera->SetFieldOfView(float(HFov));
}

void AOsrJetPawn::CrashView(double Dt)
{
	if (CrashT == 0.0)
	{
		CrashPoint = RenderPos;
		// start the orbit on the approach side (open air along the flown path) instead of a fixed azimuth: in the canyon a fixed one often lands inside a wall
		FVector Back = -Model.Velocity;
		Back.Y = 0.0;
		CrashAzimuth = Back.SizeSquared() > 1.0 ? FMath::Atan2(Back.X, Back.Z) : 0.0;
	}
	CrashT += Dt;
	const double Ang = CrashAzimuth + CrashT * 0.15;
	const double D = 90.0 + CrashT * 4.0;
	FVector Pos = CrashPoint + FVector(FMath::Sin(Ang) * D, 35.0 + CrashT * 2.0, FMath::Cos(Ang) * D);
	Pos.Y = FMath::Max(Pos.Y, SurfaceHeight(Pos.X, Pos.Z) + 10.0);
	const FVector Fwd = (CrashPoint + FVector(0, 15, 0) - Pos).GetSafeNormal();
	const FQuat Q = LookingAt(Fwd, UPV);
	ApplyCamera(Pos, Fwd, BasisY(Q));
}

void AOsrJetPawn::PlaceCamera(double Dt)
{
	if (!bAlive)
	{
		CrashView(Dt);
		return;
	}
	CrashT = 0.0;
	const FQuat TargetRot = FollowRotation();
	const double Tas = Model.Tas;
	Frame = FQuat::Slerp(Frame, TargetRot, ExpK(Dt, 7.0)).GetNormalized();

	const FOsrControlInput& In = Controls.Input;
	if (In.bLookBack)
	{
		OrbitTarget = FVector2D(UE_DOUBLE_PI, Rad(8.0));
		LookIdle = 0.0;
	}
	else if (In.Look.Size() > 0.05)
	{
		OrbitTarget = FVector2D(-In.Look.X * ORBIT_YAW_MAX, In.Look.Y * ORBIT_PITCH_MAX);
		LookIdle = 0.0;
	}
	else
	{
		LookIdle += Dt;
		if (LookIdle > RECENTRE_DELAY) { OrbitTarget = FVector2D::ZeroVector; }
	}
	const double Ko = ExpK(Dt, LookIdle == 0.0 ? 10.0 : 3.5);
	Orbit = Orbit + (OrbitTarget - Orbit) * Ko;

	const FQuat OrbitB = Frame * FQuat(FVector(0, 1, 0), Orbit.X) * FQuat(FVector(1, 0, 0), -Orbit.Y);
	const FVector Pivot = RenderPos;
	if (Dt > 0.0)
	{
		const double Accel = (Tas - PrevTas) / Dt;
		AccelPull = Lerp(AccelPull, FMath::Clamp(Accel * 0.12, -2.5, 4.0), ExpK(Dt, 1.5));
	}
	PrevTas = Tas;
	const double SpeedClose = Smoothstep(150.0, 330.0, Tas) * 2.5;
	const FVector Offset = OrbitB.RotateVector(FVector(0.0, CAM_HEIGHT - SpeedClose * 0.3, CAM_DISTANCE - SpeedClose + AccelPull));
	FVector CamPos = Pivot + Offset;
	const FVector Aim = Pivot + BasisY(Frame) * AIM_HEIGHT;
	CamPos = TerrainClear(Pivot, CamPos, Dt);
	FVector Up = BasisY(Frame);
	const FVector Fwd = (Aim - CamPos).GetSafeNormal();
	if (FMath::Abs(FVector::DotProduct(Fwd, Up)) > 0.98) { Up = BasisZ(Frame); }
	FQuat CamQ = LookingAt(Fwd, Up);

	ShakeAmp = Lerp(ShakeAmp, ShakeLevel(), Dt > 0.0 ? ExpK(Dt, 4.0) : 1.0);
	ShakeT += Dt;
	if (ShakeAmp > 0.001)
	{
		const double T = ShakeT;
		const double Sx = FMath::Sin(T * 71.0) * 0.5 + FMath::Sin(T * 113.0 + 1.3) * 0.3 + FMath::Sin(T * 23.0 + 0.4) * 0.2;
		const double Sy = FMath::Sin(T * 83.0 + 2.1) * 0.5 + FMath::Sin(T * 131.0 + 0.7) * 0.3 + FMath::Sin(T * 29.0 + 2.9) * 0.2;
		const double A = Rad(ShakeAmp);
		CamQ = CamQ * FQuat(FVector(1, 0, 0), Sy * A) * FQuat(FVector(0, 1, 0), Sx * A);
	}
	const double SpeedT = Smoothstep(90.0, 420.0, Tas);
	const double AbKick = 6.0 * Model.Ab;
	const double Target = Lerp(FOV_SLOW, FOV_FAST, SpeedT) + AbKick;
	CamFovV = Dt > 0.0 ? Lerp(CamFovV, Target, ExpK(Dt, 2.0)) : Target;
	ApplyCamera(CamPos, -BasisZ(CamQ), BasisY(CamQ));
}

// ---------------------------------------------------------------- VFX

namespace
{
	struct FMeshBuf
	{
		TArray<FVector> V; TArray<int32> I; TArray<FVector> N; TArray<FVector2D> UV; TArray<FLinearColor> C; TArray<FProcMeshTangent> T;
		void Add(const FVector& P, const FVector& Nrm, const FVector2D& Uv, const FLinearColor& Col)
		{
			V.Add(P); N.Add(Nrm); UV.Add(Uv); C.Add(Col); T.Add(FProcMeshTangent(1, 0, 0));
		}
		void Quad(int32 A, int32 B, int32 Cc, int32 D) { I.Append({A, B, Cc, A, Cc, D}); }
	};

	/** Cone along the Godot-local +Z (aft) from Origin: radius R0 -> R1 over Length; vertex alpha from AlphaFn(v). */
	void Cone(FMeshBuf& M, const FVector& Origin, const FVector& Axis, double R0, double R1, double Length, int32 Seg, int32 Rings, TFunctionRef<FLinearColor(double)> ColFn)
	{
		const FVector Ax = Axis.GetSafeNormal();
		FVector Ref = FMath::Abs(Ax.Y) < 0.9 ? FVector(0, 1, 0) : FVector(1, 0, 0);
		const FVector U = FVector::CrossProduct(Ax, Ref).GetSafeNormal();
		const FVector W = FVector::CrossProduct(Ax, U);
		const int32 Base = M.V.Num();
		for (int32 R = 0; R <= Rings; ++R)
		{
			const double Vv = double(R) / Rings;
			const double Rad_ = Lerp(R0, R1, Vv);
			const FLinearColor Col = ColFn(Vv);
			for (int32 S = 0; S <= Seg; ++S)
			{
				const double A = UE_DOUBLE_TWO_PI * S / Seg;
				const FVector Dir = U * FMath::Cos(A) + W * FMath::Sin(A);
				const FVector P = Origin + Ax * (Length * Vv) + Dir * Rad_;
				M.Add(LocalToUE(P), LocalToUE(Dir).GetSafeNormal(), FVector2D(double(S) / Seg, Vv), Col);
			}
		}
		for (int32 R = 0; R < Rings; ++R)
		{
			for (int32 S = 0; S < Seg; ++S)
			{
				const int32 A = Base + R * (Seg + 1) + S;
				M.Quad(A, A + Seg + 1, A + Seg + 2, A + 1);
			}
		}
	}

	void Sheet(FMeshBuf& M, const FVector& RootLe, const FVector& RootTe, const FVector& TipLe, const FVector& TipTe, double Gain)
	{
		const int32 Nu = 8, Nv = 6;
		const int32 Base = M.V.Num();
		for (int32 J = 0; J <= Nv; ++J)
		{
			const double Vv = double(J) / Nv;
			for (int32 I = 0; I <= Nu; ++I)
			{
				const double Uu = double(I) / Nu;
				const FVector Le = FMath::Lerp(RootLe, TipLe, Uu);
				const FVector Te = FMath::Lerp(RootTe, TipTe, Uu);
				const FVector P = FMath::Lerp(Le, Te, Vv);
				// dense over the wing, thinning to the tip and streaming off the trailing edge
				const double A = FMath::Pow(FMath::Sin(UE_DOUBLE_PI * FMath::Clamp(Uu * 0.9 + 0.05, 0.0, 1.0)), 0.7) * Smoothstep(0.0, 0.25, Vv) * (1.0 - Smoothstep(0.55, 1.0, Vv) * 0.85);
				M.Add(LocalToUE(P), FVector(0, 0, 1), FVector2D(Uu * 1.4, Vv * 0.45), FLinearColor(1, 1, 1, float(A * Gain)));
			}
		}
		for (int32 J = 0; J < Nv; ++J)
		{
			for (int32 I = 0; I < Nu; ++I)
			{
				const int32 A = Base + J * (Nu + 1) + I;
				M.Quad(A, A + Nu + 1, A + Nu + 2, A + 1);
			}
		}
	}

	enum ESection { S_PLUME_CORE = 0, S_PLUME_ENV, S_GLOW, S_WING, S_LEX, S_CONE, S_COUNT };
}

void AOsrJetPawn::BuildJetVfx()
{
	UMaterialInterface* Add = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/M_VfxAdditive.M_VfxAdditive"));
	UMaterialInterface* Tr = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/M_VfxTranslucent.M_VfxTranslucent"));
	if (!Add || !Tr)
	{
		UE_LOG(LogTemp, Error, TEXT("OSR: VFX materials missing"));
		return;
	}
	auto Mid = [this](UMaterialInterface* Base, FLinearColor Col, double Intensity, double Opacity, double NoiseAmt, double Tiling, double Scroll, double Edge)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
		M->SetVectorParameterValue("Color", Col);
		M->SetScalarParameterValue("Intensity", Intensity);
		M->SetScalarParameterValue("Opacity", Opacity);
		M->SetScalarParameterValue("NoiseAmt", NoiseAmt);
		M->SetScalarParameterValue("Tiling", Tiling);
		M->SetScalarParameterValue("Scroll", Scroll);
		M->SetScalarParameterValue("EdgeSoft", Edge);
		VfxMats.Add(M);
		return M;
	};
	const FVector Aft(0, 0, 1);
	auto Emit = [this](int32 Sec, const FMeshBuf& B, UMaterialInstanceDynamic* M)
	{
		JetVfx->CreateMeshSection_LinearColor(Sec, B.V, B.I, B.N, B.UV, B.C, B.T, false);
		JetVfx->SetMaterial(Sec, M);
		JetVfx->SetMeshSectionVisible(Sec, false);
	};
	{
		FMeshBuf B;
		Cone(B, NOZZLE, Aft, 0.42, 0.16, 4.4, 20, 16, [](double V) {
			const double Diamond = 0.75 + 0.25 * FMath::Cos(V * 4.0 * UE_DOUBLE_TWO_PI);
			return FLinearColor(1, 1, 1, float(Smoothstep(0.0, 0.06, V) * FMath::Pow(1.0 - V, 1.4) * Diamond)); });
		Emit(S_PLUME_CORE, B, Mid(Add, FLinearColor(1.0f, 0.62f, 0.32f), 0.0, 1.0, 0.35, 1.0, 3.0, 0.8));
	}
	{
		FMeshBuf B;
		Cone(B, NOZZLE, Aft, 0.56, 0.44, 8.0, 20, 12, [](double V) {
			return FLinearColor(1, 1, 1, float(Smoothstep(0.0, 0.1, V) * FMath::Pow(1.0 - V, 1.8))); });
		Emit(S_PLUME_ENV, B, Mid(Add, FLinearColor(1.0f, 0.42f, 0.18f), 0.0, 1.0, 0.6, 1.0, 2.0, 1.2));
	}
	{
		// nozzle glow: disc facing aft (triangle fan, alpha falls off to the rim)
		FMeshBuf B;
		const FVector C = NOZZLE + FVector(0, 0, -0.05);
		B.Add(LocalToUE(C), FVector(-1, 0, 0), FVector2D(0.5, 0.5), FLinearColor(1, 1, 1, 1));
		const int32 Seg = 24;
		for (int32 S = 0; S <= Seg; ++S)
		{
			const double A = UE_DOUBLE_TWO_PI * S / Seg;
			B.Add(LocalToUE(C + FVector(FMath::Cos(A), FMath::Sin(A), 0) * 0.55), FVector(-1, 0, 0), FVector2D(0.5 + 0.5 * FMath::Cos(A), 0.5 + 0.5 * FMath::Sin(A)), FLinearColor(1, 1, 1, 0));
		}
		for (int32 S = 0; S < Seg; ++S) { B.I.Append({0, 1 + S, 2 + S}); }
		Emit(S_GLOW, B, Mid(Add, FLinearColor(1.0f, 0.42f, 0.16f), 0.0, 1.0, 0.2, 1.0, 1.0, 0.0));
	}
	{
		FMeshBuf B;
		for (int32 I = 0; I < 2; ++I)
		{
			const FVector Tip = TIPS[I];
			for (int32 L = 0; L < 3; ++L)
			{
				const double H = 0.12 + L * 0.32;
				const double Trail = 1.2 + L * 0.9;
				Sheet(B, FVector(Tip.X * 0.28, Tip.Y + H + 0.1, Tip.Z - 5.3 + L * 0.3), FVector(Tip.X * 0.28, Tip.Y + H * 1.3 + 0.1, Tip.Z + 0.4 + Trail),
					FVector(Tip.X * (0.9 - L * 0.08), Tip.Y + H * 0.7, Tip.Z - 2.3 + L * 0.3), FVector(Tip.X * (0.9 - L * 0.08), Tip.Y + H * 0.9, Tip.Z + Trail * 0.8),
					L == 0 ? 1.0 : (L == 1 ? 0.7 : 0.45));
			}
		}
		Emit(S_WING, B, Mid(Tr, FLinearColor(0.93f, 0.95f, 1.0f), 2.6, 0.0, 0.85, 1.0, 2.8, 0.0));
	}
	{
		FMeshBuf B;
		for (int32 I = 0; I < 2; ++I)
		{
			const double S = I == 0 ? -1.0 : 1.0;
			const FVector A0(S * 1.05, CANOPY.Y - 0.5, CANOPY.Z + 1.2);
			const FVector A1(S * 2.7, CANOPY.Y - 0.1, TIPS[I].Z + 1.5);
			Cone(B, A0, A1 - A0, 0.1, 0.95, (A1 - A0).Size(), 14, 8, [](double V) {
				return FLinearColor(1, 1, 1, float(Smoothstep(0.0, 0.2, V) * (1.0 - Smoothstep(0.6, 1.0, V)))); });
		}
		Emit(S_LEX, B, Mid(Tr, FLinearColor(0.93f, 0.95f, 1.0f), 2.6, 0.0, 0.8, 2.0, 3.5, 1.0));
	}
	{
		FMeshBuf B;
		Cone(B, FVector(0, CANOPY.Y - 0.95, CANOPY.Z + 3.0), Aft, 1.7, 4.4, 3.6, 32, 6, [](double V) {
			return FLinearColor(1, 1, 1, float(Smoothstep(0.0, 0.15, V) * (1.0 - Smoothstep(0.35, 1.0, V)))); });
		Emit(S_CONE, B, Mid(Tr, FLinearColor(0.95f, 0.97f, 1.0f), 2.8, 0.0, 0.7, 6.0, 0.8, 0.0));
	}
	// explosion: fireballs (additive) + smoke (translucent) on engine spheres
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (int32 K = 0; K < 9; ++K)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Sphere);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetupAttachment(Root);
		C->RegisterComponent();
		const bool bFire = K < 5;
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Tr, this);   // fire is translucent-emissive so it reads against bright snow
		M->SetVectorParameterValue("Color", bFire ? FLinearColor(1.0f, 0.5f, 0.16f) : FLinearColor(0.09f, 0.085f, 0.08f));
		M->SetScalarParameterValue("NoiseAmt", 0.8);
		M->SetScalarParameterValue("Tiling", 2.0);
		M->SetScalarParameterValue("Scroll", bFire ? 1.5 : 0.3);
		M->SetScalarParameterValue("EdgeSoft", bFire ? 0.8 : 1.0);
		M->SetScalarParameterValue("Intensity", bFire ? 0.0 : 1.0);
		M->SetScalarParameterValue("Opacity", 0.0);
		C->SetMaterial(0, M);
		C->SetVisibility(false);
		Blast.Add(C);
		BlastMats.Add(M);
	}
	WorldVfx->SetMaterial(0, Mid(Tr, FLinearColor(0.95f, 0.97f, 1.0f), 2.6, 1.0, 0.3, 1.0, 1.0, 0.0));   // trails
	WorldVfx->SetMaterial(1, Mid(Tr, FLinearColor(1.0f, 1.0f, 1.0f), 3.0, 1.0, 0.0, 1.0, 0.0, 0.0));    // speed streaks
}

void AOsrJetPawn::UpdateJetVfx(double Dt)
{
	if (VfxMats.Num() < S_COUNT) { return; }
	const FF35FlightModel& M = Model;
	const double Humid = Lerp(1.0, 0.3, Smoothstep(0.0, 7000.0, M.Position.Y));
	const double Live = bAlive ? 1.0 : 0.0;
	const double AbVis = M.Ab * Live;
	const bool bPlume = AbVis > 0.01;
	JetVfx->SetMeshSectionVisible(S_PLUME_CORE, bPlume);
	JetVfx->SetMeshSectionVisible(S_PLUME_ENV, bPlume);
	if (bPlume)
	{
		// afterburner: stretch + brightness with AB level (plume length scales in the shader via vertex alpha falloff)
		const double Ab12 = FMath::Clamp(AbVis * 1.2, 0.0, 1.0);
		VfxMats[S_PLUME_CORE]->SetScalarParameterValue("Intensity", 28.0 * Ab12);
		VfxMats[S_PLUME_ENV]->SetScalarParameterValue("Intensity", 9.0 * Ab12);
	}
	JetVfx->SetMeshSectionVisible(S_GLOW, bAlive);
	VfxMats[S_GLOW]->SetScalarParameterValue("Intensity", 1.5 + 5.0 * M.Engine + 40.0 * AbVis);
	NozzleLight->SetIntensity(float(AbVis * 3000.0));

	const double GTerm = Smoothstep(4.5, 7.2, M.Nz);
	const double WingI = Humid * GTerm * Smoothstep(110.0, 200.0, M.Tas) * Live;
	JetVfx->SetMeshSectionVisible(S_WING, WingI > 0.01);
	VfxMats[S_WING]->SetScalarParameterValue("Opacity", 0.95 * WingI);
	const double LexI = Humid * FMath::Clamp(Smoothstep(Rad(9.0), Rad(22.0), M.Alpha) + GTerm * 0.6, 0.0, 1.0) * Smoothstep(55.0, 110.0, M.Tas) * Live;
	JetVfx->SetMeshSectionVisible(S_LEX, LexI > 0.01);
	VfxMats[S_LEX]->SetScalarParameterValue("Opacity", 0.9 * 0.8 * LexI);
	double ConeI = (1.0 - Smoothstep(0.0, 0.075, FMath::Abs(M.Mach - 0.98))) + Smoothstep(3.0, 6.0, M.Nz) * (1.0 - Smoothstep(0.0, 0.08, FMath::Abs(M.Mach - 0.9))) * 0.7;
	ConeI = FMath::Clamp(ConeI, 0.0, 1.0) * Humid * Live;
	JetVfx->SetMeshSectionVisible(S_CONE, ConeI > 0.01);
	VfxMats[S_CONE]->SetScalarParameterValue("Opacity", 0.7 * 0.85 * ConeI);
}

void AOsrJetPawn::UpdateWorldVfx(double Dt)
{
	if (VfxMats.Num() < S_COUNT) { return; }
	const FF35FlightModel& M = Model;
	const FVector CamG = FromUE(CamPosUE);
	const FVector AnchorG = RenderPos;
	WorldVfx->SetWorldLocationAndRotation(ToUE(AnchorG), FQuat::Identity);
	const double Humid = Lerp(1.0, 0.3, Smoothstep(0.0, 7000.0, M.Position.Y));
	double TrailI = Humid * FMath::Clamp(Smoothstep(3.0, 6.5, M.Nz) + Smoothstep(Rad(11.0), Rad(24.0), M.Alpha) * 0.8, 0.0, 1.0) * Smoothstep(60.0, 120.0, M.Tas);
	if (!bAlive) { TrailI = 0.0; }
	// ---- wingtip vortex trails (world points, 0.9 s life)
	constexpr double LIFE = 0.9;
	for (int32 K = 0; K < 2; ++K)
	{
		TArray<FTrailPoint>& Pts = TrailPts[K];
		Pts.RemoveAll([this, LIFE](const FTrailPoint& P) { return VfxTime - P.T > LIFE; });
		if (bAlive)
		{
			const FVector Tip = RenderPos + RenderRot.RotateVector(TIPS[K]);
			Pts.Add({Tip, VfxTime});
			if (Pts.Num() > 72) { Pts.RemoveAt(0); }
		}
	}
	FMeshBuf B;
	if (TrailI > 0.01)
	{
		for (int32 K = 0; K < 2; ++K)
		{
			const TArray<FTrailPoint>& Pts = TrailPts[K];
			if (Pts.Num() < 2) { continue; }
			const int32 Base = B.V.Num();
			for (int32 I = 0; I < Pts.Num(); ++I)
			{
				const FVector P = Pts[I].P;
				const FVector Seg = (Pts[FMath::Min(I + 1, Pts.Num() - 1)].P - Pts[FMath::Max(I - 1, 0)].P).GetSafeNormal();
				const FVector ToCam = (CamG - P).GetSafeNormal();
				const FVector Side = FVector::CrossProduct(Seg, ToCam).GetSafeNormal();
				const double Age = (VfxTime - Pts[I].T) / LIFE;
				const double W = 0.18 + 0.5 * Age;
				const float A = float(TrailI * (1.0 - Age) * Smoothstep(0.0, 0.08, Age + 0.02) * 0.8);
				B.Add(ToUE(P + Side * W - AnchorG), FVector(0, 0, 1), FVector2D(0, Age), FLinearColor(1, 1, 1, A));
				B.Add(ToUE(P - Side * W - AnchorG), FVector(0, 0, 1), FVector2D(1, Age), FLinearColor(1, 1, 1, A));
			}
			for (int32 I = 0; I + 1 < Pts.Num(); ++I)
			{
				const int32 A = Base + I * 2;
				B.Quad(A, A + 1, A + 3, A + 2);
			}
		}
	}
	if (B.V.Num() > 0) { WorldVfx->CreateMeshSection_LinearColor(0, B.V, B.I, B.N, B.UV, B.C, B.T, false); WorldVfx->SetMeshSectionVisible(0, true); }
	else if (WorldVfx->GetNumSections() > 0) { WorldVfx->SetMeshSectionVisible(0, false); }

	// ---- speed streaks (speed_streaks.gd): sparse world-space air streaks around the camera, scaled by speed and AGL
	const FVector V = M.Velocity;
	const double Tas = V.Size();
	const double Agl = M.Position.Y - SurfaceHeight(M.Position.X, M.Position.Z);
	const double K = bAlive ? Smoothstep(140.0, 320.0, Tas) * Lerp(0.35, 1.0, 1.0 - Smoothstep(40.0, 600.0, Agl)) : 0.0;
	const FVector Dir = Tas > 1.0 ? V / Tas : -BasisZ(RenderRot);
	const int32 Want = int32(220 * FMath::Clamp(K, 0.05, 1.0));
	static FRandomStream Rng(1729);
	for (FStreak& S : Streaks) { S.Age += Dt; }
	Streaks.RemoveAll([](const FStreak& S) { return S.Age > S.Life; });
	const FVector C0 = CamG + Dir * 50.0;
	const FQuat Basis = LookingAt(Dir, FMath::Abs(Dir.Y) < 0.98 ? UPV : FVector(1, 0, 0));
	int32 Spawn = FMath::Min(Want - Streaks.Num(), int32(FMath::CeilToInt(Want * Dt / 0.9 * 1.5)) + 1);
	while (Spawn-- > 0 && K > 0.01)
	{
		const FVector Local(Rng.FRandRange(-22.0, 22.0), Rng.FRandRange(-12.0, 12.0), Rng.FRandRange(-45.0, 45.0));
		Streaks.Add({C0 + Basis.RotateVector(Local), 0.0, 0.9});
	}
	FMeshBuf S2;
	if (K > 0.01)
	{
		const float Alpha = float(0.16 * K);   // drawn after motion blur, so fainter than the Godot 0.32
		for (const FStreak& S : Streaks)
		{
			const double Life = S.Age / S.Life;
			const double Fade = Smoothstep(0.0, 0.25, Life) * (1.0 - Smoothstep(0.75, 1.0, Life));
			const double Sc = 0.6 + 0.8 * FMath::Frac(S.P.X * 0.137 + S.P.Z * 0.071);
			const FVector ToCam = (CamG - S.P).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Dir, ToCam).GetSafeNormal() * 0.02 * Sc;
			const FVector Along = Dir * 4.5 * Sc;
			const int32 Base = S2.V.Num();
			const FVector Pts[4] = {S.P - Along - Side, S.P - Along + Side, S.P + Along + Side, S.P + Along - Side};
			for (int32 I = 0; I < 4; ++I)
			{
				S2.Add(ToUE(Pts[I] - AnchorG), FVector(0, 0, 1), FVector2D(I < 2 ? 0 : 1, 0), FLinearColor(1, 1, 1, float(Alpha * Fade)));
			}
			S2.Quad(Base, Base + 1, Base + 2, Base + 3);
		}
	}
	if (S2.V.Num() > 0) { WorldVfx->CreateMeshSection_LinearColor(1, S2.V, S2.I, S2.N, S2.UV, S2.C, S2.T, false); WorldVfx->SetMeshSectionVisible(1, true); }
	else if (WorldVfx->GetNumSections() > 1) { WorldVfx->SetMeshSectionVisible(1, false); }
}

void AOsrJetPawn::StartBlast(const FVector& AtG)
{
	BlastT = 0.0;
	BlastAt = AtG;
	for (UStaticMeshComponent* C : Blast) { C->SetVisibility(true); }
}

void AOsrJetPawn::UpdateBlast(double Dt)
{
	if (BlastT < 0.0 || Blast.Num() == 0) { return; }
	BlastT += Dt;
	const double T = BlastT;
	for (int32 K = 0; K < Blast.Num(); ++K)
	{
		const bool bFire = K < 5;
		const double Seed = K * 1.618;
		const FVector Off(FMath::Sin(Seed * 3.1) * 9.0, 4.0 + K * 2.0, FMath::Cos(Seed * 2.3) * 9.0);
		if (bFire)
		{
			const double Life = 1.6 + K * 0.25;
			const double U = FMath::Clamp(T / Life, 0.0, 1.0);
			const double R = (14.0 + K * 5.0) * FMath::Pow(Smoothstep(0.0, 0.35, U), 0.5) + 10.0 * U;
			const FVector P = BlastAt + Off * U + FVector(0, 25.0 * U * U, 0);
			Blast[K]->SetWorldLocation(ToUE(P));
			Blast[K]->SetWorldScale3D(FVector(R * 2.0));   // engine sphere is 1 m across
			BlastMats[K]->SetScalarParameterValue("Intensity", 4.0 + 30.0 * FMath::Pow(1.0 - U, 2.0));
			BlastMats[K]->SetScalarParameterValue("Opacity", 0.95 * (1.0 - Smoothstep(0.55, 1.0, U)));
			Blast[K]->SetVisibility(U < 1.0);
		}
		else
		{
			const double Life = 7.0;
			const double U = FMath::Clamp((T - 0.3) / Life, 0.0, 1.0);
			const double R = 12.0 + 38.0 * FMath::Pow(U, 0.6) + K * 3.0;
			const FVector P = BlastAt + Off + FVector(0, 10.0 + 70.0 * U, 0);
			Blast[K]->SetWorldLocation(ToUE(P));
			Blast[K]->SetWorldScale3D(FVector(R * 2.0));
			BlastMats[K]->SetScalarParameterValue("Opacity", 0.85 * Smoothstep(0.0, 0.05, U) * (1.0 - Smoothstep(0.55, 1.0, U)));
			Blast[K]->SetVisibility(U < 1.0 && T > 0.3);
		}
	}
	if (T > 8.0 && bAlive) { BlastT = -1.0; }
}

// ---------------------------------------------------------------- audio (jet_audio.gd, local jet)

void AOsrJetPawn::SetupAudio()
{
	const TCHAR* Names[] = {TEXT("engine_idle_loop"), TEXT("engine_mil_loop"), TEXT("engine_ab_loop"), TEXT("wind_low_loop"), TEXT("wind_high_loop"),
		TEXT("buffet_loop"), TEXT("ab_lightup"), TEXT("crash_explosion"), TEXT("gear_clunk")};
	for (const TCHAR* N : Names)
	{
		const FString Path = FString::Printf(TEXT("/Game/Audio/%s.%s"), N, N);
		if (USoundBase* S = LoadObject<USoundBase>(nullptr, *Path)) { Sounds.Add(FName(N), S); }
		else { UE_LOG(LogTemp, Warning, TEXT("OSR: sound missing %s"), *Path); }
	}
	const TCHAR* LoopNames[] = {TEXT("engine_idle"), TEXT("engine_mil"), TEXT("engine_ab"), TEXT("wind_low"), TEXT("wind_high"), TEXT("buffet")};
	for (const TCHAR* N : LoopNames)
	{
		USoundBase* S = Sounds.FindRef(FName(FString(N) + TEXT("_loop")));
		if (!S) { continue; }
		UAudioComponent* A = NewObject<UAudioComponent>(this);
		A->SetSound(S);
		A->bAutoActivate = false;
		A->bAllowSpatialization = false;
		A->bIsUISound = false;
		A->SetupAttachment(Root);
		A->RegisterComponent();
		A->SetVolumeMultiplier(0.001f);
		A->Play(FMath::FRandRange(0.0f, 3.0f));
		AudioLayers.Add(FName(N), A);
	}
}

void AOsrJetPawn::SetLayer(FName N, double Gain, double Pitch)
{
	if (UAudioComponent* A = AudioLayers.FindRef(N))
	{
		A->SetVolumeMultiplier(float(FMath::Max(Gain, 0.0001)));
		A->SetPitchMultiplier(float(FMath::Clamp(Pitch, 0.4, 2.0)));
	}
}

void AOsrJetPawn::PlayOneShot(FName N, double VolDb)
{
	if (USoundBase* S = Sounds.FindRef(N))
	{
		UGameplayStatics::PlaySound2D(this, S, float(FMath::Pow(10.0, VolDb / 20.0)));
	}
}

void AOsrJetPawn::UpdateAudio(double Dt)
{
	if (AudioLayers.Num() == 0 || Dt <= 0.0) { return; }
	const FOsrTelemetry T = Telemetry();
	const double Thr = FMath::Clamp(T.Throttle, 0.0, 1.0);
	double AbTarget = T.AbLevel;
	if (T.bAb && AbTarget <= 0.0) { AbTarget = 1.0; }
	Spool = Lerp(Spool, Thr, ExpK(Dt, 4.0));
	AbA = MoveToward(AbA, FMath::Clamp(AbTarget, 0.0, 1.0), Dt * 3.0);
	const bool bLit = AbTarget > 0.02;
	if (bLit && !bAbWasLit && bAlive) { PlayOneShot("ab_lightup", 0.0); }
	bAbWasLit = bLit;
	const int32 G = bGearDown ? 1 : 0;
	if (GearState != -1 && G != GearState) { PlayOneShot("gear_clunk", -4.0); }
	GearState = G;
	EngineFade = MoveToward(EngineFade, bAlive ? 1.0 : 0.0, Dt * (bAlive ? 0.8 : 4.0));

	const double Ias = T.IasKt;
	const double Aoa = FMath::Abs(T.AoaDeg);
	const double N = Spool;
	const double Ab = AbA;
	const double WIdle = 1.0 - Smoothstep(0.2, 0.75, N) * 0.8;
	double WMil = Smoothstep(0.05, 0.8, N);
	const double WAb = Smoothstep(0.0, 0.35, Ab);
	WMil *= 1.0 - 0.45 * WAb;
	// camera aspect: ahead of the nose the roar drops and the inlet whine dominates
	double Aspect = 1.0;
	const FVector ToCam = FromUE(CamPosUE) - RenderPos;
	if (ToCam.SizeSquared() > 0.01)
	{
		Aspect = FMath::Clamp(0.5 - 0.5 * FVector::DotProduct(-BasisZ(RenderRot), ToCam.GetSafeNormal()), 0.0, 1.0);
	}
	const double Roar = Lerp(0.45, 1.0, Aspect);
	const double Fade = EngineFade;
	SetLayer("engine_idle", WIdle * Lerp(1.25, 0.8, Aspect) * Fade, 0.9 + 0.22 * N);
	SetLayer("engine_mil", WMil * Roar * Fade, 0.88 + 0.16 * N + 0.03 * Ab);
	SetLayer("engine_ab", WAb * 1.25 * Roar * Fade, 0.95 + 0.06 * Ab);
	const double WLo = Smoothstep(60.0, 260.0, Ias) * (1.0 - 0.5 * Smoothstep(350.0, 600.0, Ias));
	const double WHi = Smoothstep(220.0, 620.0, Ias);
	const double WindBus = 0.708;   // Godot "Wind" bus at -3 dB
	SetLayer("wind_low", WLo * 0.7 * WindBus * (bAlive ? 1.0 : 0.0), 0.85 + Ias / 1400.0);
	SetLayer("wind_high", WHi * 0.75 * WindBus * (bAlive ? 1.0 : 0.0), 0.8 + Ias / 1500.0);
	const double Buf = FMath::Max(Smoothstep(14.0, 28.0, Aoa), Smoothstep(5.0, 8.5, T.G)) * Smoothstep(120.0, 220.0, Ias);
	SetLayer("buffet", Buf * 1.1 * WindBus * (bAlive ? 1.0 : 0.0), 0.9 + 0.2 * Buf);
}
