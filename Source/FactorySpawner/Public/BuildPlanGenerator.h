#pragma once

#include "CoreMinimal.h"
#include "BuildPlanTypes.h"

class UBuildableCache;

struct FCachedPowerConnections
{
    FGuid LastMachine;
    FGuid LastPole;
    FGuid FirstPole;
};

struct FConnector
{
    int32 Index = 0;
    int32 LocationX = 0;
};

struct FMachineConnections
{
    int32 Length = 0;
    TArray<FConnector> Belt;
    TArray<FConnector> Pipe;
};

struct FMachineConfig
{
    int32 Width = 0;
    TArray<FMachineConnections> InputConnections;
    TArray<FMachineConnections> OutputConnections;
};

class FBuildPlanGenerator
{
  public:
    explicit FBuildPlanGenerator(UWorld* InWorld, UBuildableCache* InCache);

    FBuildPlan Generate(const TArray<FFactoryCommandToken>& ClusterConfig);

  private:
    void ProcessRow(const FFactoryCommandToken& RowConfig, int32 RowIndex);
    void PlaceMachines(const FFactoryCommandToken& RowConfig, int32 Width, const FMachineConnections& InputConnections,
                       const FMachineConnections& OutputConnections);

  private:
    UWorld* World;
    UBuildableCache* Cache;
    FBuildPlan BuildPlan;

    int32 YCursor = 0;
    int32 XCursor = 0;
    int32 FirstMachineWidth = 0;

    FCachedPowerConnections CachedPowerConnections;
};
