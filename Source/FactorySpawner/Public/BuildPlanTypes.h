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
    Converter,
    ParticleAccelerator,
    QuantumEncoder,
    Packager,

    // Generators
    CoalGenerator,
    FuelGenerator,
    NuclearReactor,

    // Utility
    Splitter,
    Merger,
    PowerPole,
    Pipeline,
    Pipeline2,
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
    TOptional<int32> BeltTier;     // optional belt tier override (1-6 for Mk1-Mk6)
};
