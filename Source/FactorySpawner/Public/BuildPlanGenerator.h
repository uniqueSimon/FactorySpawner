#pragma once

#include "CoreMinimal.h"
#include "BuildPlanTypes.h"
#include <initializer_list>

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
    int32 LocationY = 0;
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

// Helper factory functions to make initializers shorter in cpp files.
inline FConnector MakeConnector(int32 Index, int32 LocationX, int32 LocationY = 0)
{
    FConnector C;
    C.Index = Index;
    C.LocationX = LocationX;
    C.LocationY = LocationY;
    return C;
}

inline FMachineConnections MakeMachineConnections(
    int32 Length,
    std::initializer_list<FConnector> Belt = {},
    std::initializer_list<FConnector> Pipe = {})
{
    FMachineConnections MC;
    MC.Length = Length;
    for (const FConnector& c : Belt)
        MC.Belt.Add(c);
    for (const FConnector& p : Pipe)
        MC.Pipe.Add(p);
    return MC;
}

inline FMachineConfig MakeMachineConfig(
    int32 Width,
    std::initializer_list<FMachineConnections> Inputs = {},
    std::initializer_list<FMachineConnections> Outputs = {})
{
    FMachineConfig Cfg;
    Cfg.Width = Width;
    for (const FMachineConnections& m : Inputs)
        Cfg.InputConnections.Add(m);
    for (const FMachineConnections& m : Outputs)
        Cfg.OutputConnections.Add(m);
    return Cfg;
}

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
