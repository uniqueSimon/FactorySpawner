#pragma once

#include "CoreMinimal.h"

struct FFactoryCommandToken
{
	int32 Count = 0;
	EMachineType MachineType;
	FString Recipe;
	float ClockPercent = 100.0f; // percent value (e.g. 75.5)
	bool bHasUnderclock = false;
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
