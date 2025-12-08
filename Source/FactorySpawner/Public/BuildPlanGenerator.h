#pragma once

#include "CoreMinimal.h"
#include "BuildPlanTypes.h"

class UBuildableCache;
class AFGBuildable;
class UFGPowerConnectionComponent;
class UFGPipeConnectionComponent;
class UFGFactoryConnectionComponent;

struct FCachedPowerConnections
{
    UFGPowerConnectionComponent* LastMachine = nullptr;
    UFGPowerConnectionComponent* LastPole = nullptr;
    UFGPowerConnectionComponent* FirstPole = nullptr;
};

struct FConnectionQueue
{
    TQueue<UFGFactoryConnectionComponent*> Input;
    TQueue<UFGFactoryConnectionComponent*> Output;
    TQueue<UFGPipeConnectionComponent*> PipeInput;
    TQueue<UFGPipeConnectionComponent*> PipeOutput;
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

inline FMachineConnections MakeMachineConnections(int32 Length, std::initializer_list<FConnector> Belt = {},
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

inline FMachineConfig MakeMachineConfig(int32 Width, std::initializer_list<FMachineConnections> Inputs = {},
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

    void Generate(const TArray<FFactoryCommandToken>& ClusterConfig);

  private:
    void ProcessRow(const FFactoryCommandToken& RowConfig, int32 RowIndex);
    void PlaceMachines(const FFactoryCommandToken& RowConfig, int32 Width, const FMachineConnections& InputConnections,
                       const FMachineConnections& OutputConnections);
    void CalculateMachineSetup(EBuildable MachineType, const TOptional<FString>& Recipe,
                               const TOptional<float>& Underclock, int32 Width,
                               const FMachineConnections& InputConnections,
                               const FMachineConnections& OutputConnections, bool bFirstUnitInRow, bool bEvenIndex,
                               bool bLastIndex);
    void SpawnWireAndConnect(UFGPowerConnectionComponent* A, UFGPowerConnectionComponent* B);
    UFGPowerConnectionComponent* SpawnPowerPole(FVector Location);
    void SpawnMachine(FVector Location, EBuildable MachineType, const TOptional<FString>& Recipe,
                      const TOptional<float>& Underclock, UFGPowerConnectionComponent*& outPowerConn,
                      TArray<UFGFactoryConnectionComponent*>& outBeltConn,
                      TArray<UFGPipeConnectionComponent*>& outPipeConn);
    TArray<UFGFactoryConnectionComponent*> SpawnSplitterOrMerger(FVector Location, EBuildable SplitterOrMerger);
    void SpawnBeltAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To);
    TArray<UFGPipeConnectionComponent*> SpawnPipeCross(FVector Location);
    void SpawnPipeAndConnect(UFGPipeConnectionComponent* From, UFGPipeConnectionComponent* To);

  private:
    UWorld* World;
    UBuildableCache* Cache;
    TArray<AFGBuildable*> BuildablesForBlueprint;

    int32 YCursor = 0;
    int32 XCursor = 0;
    int32 FirstMachineWidth = 0;

    FCachedPowerConnections CachedPowerConnections;
    FConnectionQueue ConnectionQueue;

    AFGCharacterPlayer* Player = nullptr;
    UFGManufacturerClipboardRCO* RCO = nullptr;
};
