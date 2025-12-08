#pragma once

#include "CoreMinimal.h"

class FFactoryCommandParser
{
  public:
    // Parses a command like: "2 Smelter IngotIron 75, 3 Constructor IronPlate, beltTier 3"
    // The beltTier parameter is optional and applies to all machines in the command
    static bool ParseCommand(const FString& Input, TArray<FFactoryCommandToken>& OutTokens, FString& OutError);
};
