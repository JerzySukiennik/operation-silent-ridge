// Binds the mission state machine to the carrier workstream's events (ACarrierActor: OnCatapultLaunched, OnTrap(Grade, Wire),
// OnBolter, OnWaveOff, OnSpawnedOnDeck). Compiles without the carrier sources too (then only the F5-F7 debug keys drive them).
#include "MissionState.h"
#if __has_include("CarrierActor.h")
#include "CarrierActor.h"
#define OSR_HAS_CARRIER 1
#else
#define OSR_HAS_CARRIER 0
#endif

void AOsrMissionState::BindCarrier()
{
#if OSR_HAS_CARRIER
	ACarrierActor* C = ACarrierActor::Get(this);
	if (!C) { return; }
	bCarrierBound = true;
	C->OnCatapultLaunched.AddWeakLambda(this, [this](AActor* Jet) { NotifyCatapultLaunched(Cast<APawn>(Jet)); });
	C->OnTrap.AddWeakLambda(this, [this](AActor* Jet, ELsoGrade G, int32 Wire) { NotifyTrap(Cast<APawn>(Jet), ACarrierActor::GradeText(G), Wire); });
	C->OnBolter.AddWeakLambda(this, [this](AActor* Jet) { NotifyBolter(Cast<APawn>(Jet)); });
	C->OnWaveOff.AddWeakLambda(this, [this](AActor* Jet) { Radio(TEXT("LSO"), TEXT("Wave off, wave off."), NAME_None, 3.0); });
	C->OnSpawnedOnDeck.AddWeakLambda(this, [this](APawn* P) { NotifySpawnedOnDeck(P); });
	UE_LOG(LogTemp, Display, TEXT("OSR mission: bound to the carrier events"));
#else
	bCarrierBound = true;
#endif
}
