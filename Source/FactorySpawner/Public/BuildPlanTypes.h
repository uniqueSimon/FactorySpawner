#pragma once

#include "CoreMinimal.h"

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
