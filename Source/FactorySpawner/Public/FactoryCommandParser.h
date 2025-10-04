#pragma once

#include "CoreMinimal.h"

class FFactoryCommandParser
{
  public:
    // Parses a command like: "2 Smelter IngotIron 75, 3 Constructor IronPlate"
    static bool ParseCommand(const FString& Input, TArray<FFactoryCommandToken>& OutTokens, FString& OutError);
};
