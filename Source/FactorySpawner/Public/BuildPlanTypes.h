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

    // Utility
    Splitter,
    Merger,
    PowerPole,

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

struct FBuildableUnit
{
    FGuid Id;
    EBuildable Buildable;
    FVector Location;
    TOptional<FString> Recipe;
    TOptional<float> Underclock;
};

struct FWireConnection
{
    FGuid FromUnit;
    FGuid ToUnit;
};

struct FBeltConnection
{
    FGuid FromUnit;
    int32 FromSocket;
    FGuid ToUnit;
    int32 ToSocket;
};

struct FBuildPlan
{
    TArray<FBuildableUnit> BuildableUnits;
    TArray<FBeltConnection> BeltConnections;
    TArray<FWireConnection> WireConnections;
};
