// Headless flight-model check (port of Godot scripts/test/flight_numbers.gd): UnrealEditor-Cmd <project> -run=FlightNumbers
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "FlightNumbersCommandlet.generated.h"

UCLASS()
class UFlightNumbersCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
