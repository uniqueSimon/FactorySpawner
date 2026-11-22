#pragma once

#include "BuildPlanTypes.h"

class FFactoryCommandParser
{
  public:
    // Parses a command like: "2 Smelter IngotIron 75 floor=1, 3 Constructor IronPlate floor=2, beltTier 3"
    // The beltTier and floorHeight parameters are optional and apply to the whole blueprint.
    static bool ParseCommand(const FString& Input, FFactoryCommand& OutCommand, FString& OutError);
};
