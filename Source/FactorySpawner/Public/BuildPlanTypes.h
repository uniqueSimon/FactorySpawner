#pragma once

#include "CoreMinimal.h"

UENUM(BlueprintType)
enum class EBuildable : uint8
{
    // Machines
    Smelter,
    Constructor,
    Assembler,
    Foundry,
    Manufacturer,
    OilRefinery,
    Blender,

    // Utility
    Splitter,
    Merger,
    PowerPole,
    Pipeline,
    PipeCross,

    // Transport
    Belt,
    Lift,
    PowerLine,

    Invalid
};

struct FFactoryCommandToken
{
    int32 Count = 0;
    EBuildable MachineType;
    TOptional<FString> Recipe;
    TOptional<float> ClockPercent; // percent value (e.g. 75.5)
};
