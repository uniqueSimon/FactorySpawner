#pragma once
#include "CoreMinimal.h"
#include "FactoryCommandParser.h" // for FFactoryCommandToken
#include "MyChatSubsystem.h"

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

struct FMachineConfig
{
    int32 Width = 0;
    int32 LengthFront = 0;
    int32 LengthBehind = 0;
    int32 PowerPoleY = 0;
    TArray<FConnector> Input;
    TArray<FConnector> Output;
};

class FBuildPlanGenerator
{
  public:
    FBuildPlanGenerator(UWorld* InWorld);

    FBuildPlan Generate(const TArray<FFactoryCommandToken>& ClusterConfig);

  private:
    // Internal helpers
    void ProcessRow(const FFactoryCommandToken& RowConfig, int32 RowIndex);
    void PlaceMachines(const FFactoryCommandToken& RowConfig, int32 RowIndex, const FMachineConfig& MachineConfig);

  private:
    UWorld* World;
    FBuildPlan BuildPlan;

    int32 YCursor = 0;
    int32 XCursor = 0;
    int32 FirstMachineWidth = 0;

    FCachedPowerConnections CachedPowerConnections;
};
