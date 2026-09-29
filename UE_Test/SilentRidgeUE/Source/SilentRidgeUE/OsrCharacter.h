// On-foot pilot (3rd person) on the carrier: UE template Manny + ABP_Unarmed locomotion, flight-suit colours + helmet, gamepad
// (XInput via Enhanced Input, DualShock/DualSense via FOsrPadInput) + keyboard/mouse, based movement on the moving/rolling deck,
// jet-blast push, interact (climb into the F-35C, elevator), pause menu + help.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "OsrCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UStaticMeshComponent;
class UInputAction;
class UInputMappingContext;

UCLASS()
class SILENTRIDGEUE_API AOsrCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	AOsrCharacter();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;

	// HUD state
	FString Prompt;
	bool bShowHelp = false;
	bool bPaused = false;
	int32 MenuIndex = 0;
	static constexpr int32 MenuCount = 3;
	FString MenuLabel(int32 I) const;
	double BlastLevel = 0.0;

	// autotest: drive the character
	bool bAutoDrive = false;
	FVector AutoMoveWorld = FVector::ZeroVector;   // desired direction (UE, unit or zero)
	bool bAutoInteract = false;

	UCameraComponent* GetCam() const { return Cam; }

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> Boom;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Cam;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Helmet;
	UPROPERTY() TObjectPtr<UInputMappingContext> Imc;
	UPROPERTY() TMap<FName, TObjectPtr<UInputAction>> Actions;

private:
	void CreateInput();
	double Val(FName N) const;
	void UpdateMenu();
	void Interact(double Dt);
	void JetBlast(double Dt);
	bool bStartPrev = false, bUpPrev = false, bDownPrev = false, bAPrev = false, bBPrev = false, bHelpPrev = false, bInteractPrev = false, bJumpPrev = false;
};
