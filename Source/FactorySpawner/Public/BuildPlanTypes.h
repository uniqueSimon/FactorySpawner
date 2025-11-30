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

EBuildable LastMachineType = EBuildable::Blender;

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

struct FConnectionWithSocket
{
    FGuid FromUnit;
    int32 FromSocket;
    FGuid ToUnit;
    int32 ToSocket;
};

struct FBuildPlan
{
    TArray<FBuildableUnit> BuildableUnits;
    TArray<FWireConnection> WireConnections;
    TArray<FConnectionWithSocket> BeltConnections;
    TArray<FConnectionWithSocket> PipeConnections;
};
