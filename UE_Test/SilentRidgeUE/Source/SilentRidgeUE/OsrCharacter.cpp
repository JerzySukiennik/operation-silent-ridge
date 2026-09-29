// On-foot pilot: movement on the carrier deck, camera, input, jet blast, interactions.
#include "OsrCharacter.h"
#include "CarrierActor.h"
#include "CarrierLayout.h"
#include "CarrierJetOps.h"
#include "CarrierSubsystem.h"
#include "OsrJetPawn.h"
#include "OsrPadInput.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/LocalPlayer.h"
#include "Animation/AnimInstance.h"
#include "AnimationRuntime.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/KismetSystemLibrary.h"
#include "EngineUtils.h"

AOsrCharacter::AOsrCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
	UCharacterMovementComponent* M = GetCharacterMovement();
	M->bOrientRotationToMovement = true;
	M->RotationRate = FRotator(0.f, 540.f, 0.f);
	M->MaxWalkSpeed = 420.f;
	M->MinAnalogWalkSpeed = 60.f;
	M->BrakingDecelerationWalking = 1600.f;
	M->JumpZVelocity = 420.f;
	M->AirControl = 0.25f;
	M->bIgnoreBaseRotation = false;
	M->PerchRadiusThreshold = 10.f;
	M->MaxStepHeight = 35.f;
	M->SetWalkableFloorAngle(50.f);
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -92.f), FRotator(0, -90.f, 0));
	GetMesh()->bReceivesDecals = false;
	Boom = CreateDefaultSubobject<USpringArmComponent>(TEXT("Boom"));
	Boom->SetupAttachment(GetRootComponent());
	Boom->TargetArmLength = 360.f;
	Boom->SocketOffset = FVector(0, 45.f, 55.f);
	Boom->bUsePawnControlRotation = true;
	Boom->bEnableCameraLag = true;
	Boom->CameraLagSpeed = 12.f;
	Boom->ProbeSize = 14.f;
	Cam = CreateDefaultSubobject<UCameraComponent>(TEXT("Cam"));
	Cam->SetupAttachment(Boom, USpringArmComponent::SocketName);
	Cam->bUsePawnControlRotation = false;
	Cam->SetFieldOfView(80.f);
	Helmet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Helmet"));
	Helmet->SetupAttachment(GetMesh(), TEXT("head"));
	Helmet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AOsrCharacter::BeginPlay()
{
	Super::BeginPlay();
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (Mesh)
	{
		GetMesh()->SetSkeletalMesh(Mesh);
		if (UClass* Abp = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C")))
		{
			GetMesh()->SetAnimInstanceClass(Abp);
		}
		else { UE_LOG(LogTemp, Warning, TEXT("OSR character: ABP_Unarmed missing")); }
		if (UMaterialInterface* Suit = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carrier/Materials/M_CrewSuit.M_CrewSuit")))
		{
			// F-35 pilot: sage-green flight suit, G-suit, harness, dark gloves
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Suit, this);
			Mid->SetVectorParameterValue(TEXT("Torso"), FLinearColor(0.13f, 0.15f, 0.09f));
			Mid->SetVectorParameterValue(TEXT("Pants"), FLinearColor(0.13f, 0.15f, 0.09f));
			Mid->SetVectorParameterValue(TEXT("Gloves"), FLinearColor(0.04f, 0.035f, 0.03f));
			Mid->SetScalarParameterValue(TEXT("Stripes"), 0.0f);
			Mid->SetScalarParameterValue(TEXT("GSuit"), 1.0f);
			Mid->SetScalarParameterValue(TEXT("Harness"), 1.0f);
			for (int32 I = 0; I < Mesh->GetMaterials().Num(); ++I) { GetMesh()->SetMaterial(I, Mid); }
		}
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		const int32 Hi = Ref.FindBoneIndex(TEXT("head"));
		if (Hi != INDEX_NONE)
		{
			const FTransform Head = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Hi);
			const FVector Fwd(0, 1, 0), Up(0, 0, 1);
			const FTransform Want(FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat(), Head.GetLocation() + Fwd * 2.5 + Up * 9.5, FVector(1.12));
			Helmet->SetRelativeTransform(Want.GetRelativeTransform(Head));
		}
	}
	if (UStaticMesh* H = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Carrier/Meshes/SM_Helmet.SM_Helmet"))) { Helmet->SetStaticMesh(H); }
	if (UMaterialInterface* HM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Carrier/Materials/M_HelmetShell.M_HelmetShell")))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(HM, this);
		Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.09f, 0.095f, 0.1f));
		Helmet->SetMaterial(0, Mid);
	}
	FOsrPadInput::Start();
}

// ---------------------------------------------------------------- input

void AOsrCharacter::CreateInput()
{
	if (Imc) { return; }
	Imc = NewObject<UInputMappingContext>(this, TEXT("OsrFootIMC"));
	auto Make = [this](const TCHAR* Name, std::initializer_list<FKey> Keys)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = EInputActionValueType::Axis1D;
		Actions.Add(FName(Name), A);
		for (const FKey& K : Keys) { Imc->MapKey(A, K); }
	};
	Make(TEXT("FMoveX"), {EKeys::Gamepad_LeftX});
	Make(TEXT("FMoveY"), {EKeys::Gamepad_LeftY});
	Make(TEXT("FKW"), {EKeys::W, EKeys::Up});
	Make(TEXT("FKS"), {EKeys::S, EKeys::Down});
	Make(TEXT("FKA"), {EKeys::A, EKeys::Left});
	Make(TEXT("FKD"), {EKeys::D, EKeys::Right});
	Make(TEXT("FLookX"), {EKeys::Gamepad_RightX});
	Make(TEXT("FLookY"), {EKeys::Gamepad_RightY});
	Make(TEXT("FMouseX"), {EKeys::MouseX});
	Make(TEXT("FMouseY"), {EKeys::MouseY});
	Make(TEXT("FKLookL"), {EKeys::J});
	Make(TEXT("FKLookR"), {EKeys::L});
	Make(TEXT("FKLookU"), {EKeys::I});
	Make(TEXT("FKLookD"), {EKeys::K});
	Make(TEXT("FJump"), {EKeys::Gamepad_FaceButton_Bottom, EKeys::SpaceBar});
	Make(TEXT("FInteract"), {EKeys::Gamepad_FaceButton_Left, EKeys::F, EKeys::E});
	Make(TEXT("FSprint"), {EKeys::Gamepad_LeftThumbstick, EKeys::LeftShift});
	Make(TEXT("FStart"), {EKeys::Gamepad_Special_Right, EKeys::Escape, EKeys::P});
	Make(TEXT("FHelp"), {EKeys::Gamepad_Special_Left, EKeys::F1});
	Make(TEXT("FAccept"), {EKeys::Enter});
	Make(TEXT("FBack"), {EKeys::Gamepad_FaceButton_Right, EKeys::BackSpace});
	Make(TEXT("FMenuUp"), {EKeys::Gamepad_DPad_Up});
	Make(TEXT("FMenuDown"), {EKeys::Gamepad_DPad_Down});
}

void AOsrCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	CreateInput();
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		for (const auto& It : Actions) { EIC->BindActionValue(It.Value); }
	}
}

void AOsrCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	CreateInput();
	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Sub->AddMappingContext(Imc, 1);   // above the jet's context (same keys, different meaning)
		}
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
	bPaused = false;
}

void AOsrCharacter::UnPossessed()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Sub->RemoveMappingContext(Imc);
		}
	}
	Super::UnPossessed();
}

double AOsrCharacter::Val(FName N) const
{
	const UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	const TObjectPtr<UInputAction>* A = Actions.Find(N);
	if (!EIC || !A) { return 0.0; }
	return EIC->GetBoundActionValue(*A).Get<float>();
}

FString AOsrCharacter::MenuLabel(int32 I) const
{
	switch (I)
	{
	case 0: return TEXT("Resume");
	case 1: return TEXT("Respawn at the island door");
	default: return TEXT("Quit");
	}
}

void AOsrCharacter::UpdateMenu()
{
	const FOsrPadState P = FOsrPadInput::Get();
	const bool bStart = Val("FStart") > 0.5 || P.bOptions;
	if (bStart && !bStartPrev) { bPaused = !bPaused; MenuIndex = 0; }
	bStartPrev = bStart;
	const bool bHelp = Val("FHelp") > 0.5 || P.bShare || P.bTouchpad;
	if (bHelp && !bHelpPrev) { bShowHelp = !bShowHelp; }
	bHelpPrev = bHelp;
	const double Ly = -Val("FMoveY") + P.LY;
	const bool bUp = Val("FMenuUp") > 0.5 || Ly < -0.6 || Val("FKW") > 0.5 || P.DPadUp();
	const bool bDown = Val("FMenuDown") > 0.5 || Ly > 0.6 || Val("FKS") > 0.5 || P.DPadDown();
	const bool bA = Val("FJump") > 0.5 || Val("FAccept") > 0.5 || P.bCross;
	const bool bB = Val("FBack") > 0.5 || P.bCircle;
	if (bPaused)
	{
		if (bUp && !bUpPrev) { MenuIndex = (MenuIndex + MenuCount - 1) % MenuCount; }
		if (bDown && !bDownPrev) { MenuIndex = (MenuIndex + 1) % MenuCount; }
		if (bA && !bAPrev)
		{
			bPaused = false;
			if (MenuIndex == 1)
			{
				if (UCarrierSubsystem* S = GetWorld()->GetSubsystem<UCarrierSubsystem>()) { S->RespawnOnDeck(); }
			}
			else if (MenuIndex == 2) { UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); }
		}
		if (bB && !bBPrev) { bPaused = false; }
	}
	bUpPrev = bUp; bDownPrev = bDown; bAPrev = bA; bBPrev = bB;
}

// ---------------------------------------------------------------- tick

void AOsrCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FMath::Min<double>(DeltaSeconds, 0.1);
	if (!IsLocallyControlled() && !bAutoDrive) { return; }
	const FOsrPadState P = FOsrPadInput::Get();
	auto Merge = [](double A, double B) { return FMath::Abs(A) >= FMath::Abs(B) ? A : B; };
	if (IsLocallyControlled()) { UpdateMenu(); }
	FVector2D Move(Merge(Val("FMoveX"), P.LX) + Val("FKD") - Val("FKA"), Merge(Val("FMoveY"), -P.LY) + Val("FKW") - Val("FKS"));
	FVector2D Look(Merge(Val("FLookX"), P.RX) + Val("FKLookR") - Val("FKLookL"), Merge(Val("FLookY"), -P.RY) + Val("FKLookU") - Val("FKLookD"));
	if (bPaused) { Move = Look = FVector2D::ZeroVector; }
	if (Move.Size() < 0.12) { Move = FVector2D::ZeroVector; }
	if (Look.Size() < 0.12) { Look = FVector2D::ZeroVector; }
	if (Move.Size() > 1.0) { Move.Normalize(); }
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->AddYawInput(float(Look.X * 150.0 * Dt + Val("FMouseX") * 1.2));
		PC->AddPitchInput(float(-Look.Y * 100.0 * Dt - Val("FMouseY") * 1.2));
	}
	const FRotator CamYaw(0.0, GetControlRotation().Yaw, 0.0);
	FVector Wish = CamYaw.RotateVector(FVector(Move.Y, Move.X, 0.0));
	if (bAutoDrive) { Wish = AutoMoveWorld; }
	if (!Wish.IsNearlyZero()) { AddMovementInput(Wish.GetSafeNormal(), float(FMath::Min(Wish.Size(), 1.0))); }
	const bool bSprint = Val("FSprint") > 0.5 || P.bL3 || bAutoDrive;
	GetCharacterMovement()->MaxWalkSpeed = bSprint ? 640.f : 420.f;
	const bool bJump = !bPaused && (Val("FJump") > 0.5 || P.bCross);
	if (bJump && !bJumpPrev) { Jump(); }
	bJumpPrev = bJump;
	Interact(Dt);
	JetBlast(Dt);
	// fell overboard -> back to the island door
	if (ACarrierActor* C = ACarrierActor::Get(this))
	{
		if (C->UEToLocal(GetActorLocation()).Z < 2.0)
		{
			if (UCarrierSubsystem* S = GetWorld()->GetSubsystem<UCarrierSubsystem>()) { S->RespawnOnDeck(); }
		}
	}
}

void AOsrCharacter::Interact(double Dt)
{
	Prompt.Reset();
	UCarrierSubsystem* S = GetWorld()->GetSubsystem<UCarrierSubsystem>();
	ACarrierActor* C = ACarrierActor::Get(this);
	if (!S || !C) { return; }
	const FOsrPadState P = FOsrPadInput::Get();
	const bool bI = !bPaused && (Val("FInteract") > 0.5 || P.bSquare || bAutoInteract);
	const bool bPress = bI && !bInteractPrev;
	bInteractPrev = bI;
	const FVector L = C->UEToLocal(GetActorLocation());
	// the player's F-35C
	if (UCarrierJetOps* Ops = S->PlayerOps())
	{
		if (Ops->OnDeck())
		{
			const FVector2D Lad = Ops->LadderPoint();
			const double D = FVector2D::Distance(Lad, FVector2D(L.X, L.Y));
			if (D < 4.5 && FMath::Abs(L.Z - (CarrierLayout::DeckZ + 0.9)) < 2.0)
			{
				Prompt = TEXT("Climb into your F-35C  (X / Square / F)");
				if (bPress) { S->EnterJet(); return; }
			}
		}
	}
	// working elevator: ride it between the flight deck and the hangar deck
	for (const CarrierLayout::FElevator& E : CarrierLayout::Elevators)
	{
		if (!E.bWorking) { continue; }
		const bool bOn = L.X > E.X0 - 1.0 && L.X < E.X1 + 1.0 && L.Y > E.Y0 - 3.0 && L.Y < E.Y1 + 1.0;
		const bool bNearHangar = L.Z < CarrierLayout::HangarCeil && L.X > E.X0 - 4.0 && L.X < E.X1 + 4.0 && L.Y > CarrierLayout::Hangar.Max.Y - 6.0;
		if ((bOn || bNearHangar) && !C->IsElevatorMoving())
		{
			const bool bDown = C->IsElevatorDown();
			Prompt = bDown ? TEXT("Elevator 2: UP to the flight deck  (X / Square / F)") : TEXT("Elevator 2: DOWN to the hangar deck  (X / Square / F)");
			if (bPress) { C->CommandElevator(!bDown); }
		}
		else if (C->IsElevatorMoving() && bOn) { Prompt = TEXT("Elevator moving..."); }
	}
}

void AOsrCharacter::JetBlast(double Dt)
{
	BlastLevel = 0.0;
	UCharacterMovementComponent* M = GetCharacterMovement();
	for (TActorIterator<AOsrJetPawn> It(GetWorld()); It; ++It)
	{
		UCarrierJetOps* Ops = It->FindComponentByClass<UCarrierJetOps>();
		if (!Ops || !Ops->OnDeck()) { continue; }
		FVector N, D; double S;
		Ops->Blast(N, D, S);
		const FVector R = GetActorLocation() - N;
		const double Along = FVector::DotProduct(R, D) * 0.01;
		if (Along <= 0.0 || Along > 75.0) { continue; }
		const double Lat = (R - D * Along * 100.0).Size() * 0.01;
		const double W = 1.4 + Along * 0.17;
		double F = S * FMath::Pow(1.0 - Along / 75.0, 1.3) * FMath::Exp(-(Lat / W) * (Lat / W));
		ACarrierActor* C = ACarrierActor::Get(this);
		if (C && Ops->CatIndex >= 0 && C->JbdAngle(Ops->CatIndex) > 30.0 && Along > 6.5) { F *= 0.06; }   // behind the raised JBD
		BlastLevel = FMath::Max(BlastLevel, F);
		if (F < 0.02) { continue; }
		const FVector Push = D * (F * 38.0 * 100.0);
		M->AddImpulse(Push * Dt, true);
		if (F > 0.55 && M->IsMovingOnGround())
		{
			LaunchCharacter(D * (F * 900.0) + FVector(0, 0, 250.0 * F), false, false);   // knocked off your feet
		}
	}
}
