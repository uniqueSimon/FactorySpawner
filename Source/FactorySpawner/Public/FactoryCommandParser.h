#pragma once

#include "CoreMinimal.h"

struct FFactoryCommandToken
{
	int32 Count;
	EMachineType MachineType;
	TOptional<FString> Recipe;
	TOptional<float> ClockPercent; // percent value (e.g. 75.5)
};

class FFactoryCommandParser
{
public:
	// Parses a command like: "2 Smelter IngotIron, 3 Constructor IronPlate"
	static bool ParseCommand(
		const FString& Input,
		TArray<FFactoryCommandToken>& OutTokens,
		FString& OutError
	);
};
