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
    int32 Index;
    int32 LocationX;
    int32 LocationY;

    FConnector(int32 InIndex, int32 InX, int32 InY = 0) : Index(InIndex), LocationX(InX), LocationY(InY)
    {
    }
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
    int32 Length = 0;
    TArray<FMachineConnections> InputConnections;
    TArray<FMachineConnections> OutputConnections;
};

#define MakeConnector FConnector

inline FMachineConnections MakeMachineConnections(int32 Length, std::initializer_list<FConnector> Belt = {},
                                                  std::initializer_list<FConnector> Pipe = {})
{
    return {Length, TArray<FConnector>(Belt), TArray<FConnector>(Pipe)};
}

inline FMachineConfig MakeMachineConfig(int32 Width, int32 Length,
                                        std::initializer_list<FMachineConnections> Inputs = {},
                                        std::initializer_list<FMachineConnections> Outputs = {})
{
    return {Width, Length, TArray<FMachineConnections>(Inputs), TArray<FMachineConnections>(Outputs)};
}

class FBuildPlanGenerator
{
  public:
    explicit FBuildPlanGenerator(UWorld* InWorld, UBuildableCache* InCache);

    void Generate(const TArray<FFactoryCommandToken>& ClusterConfig);

  private:
    void ProcessRow(const FFactoryCommandToken& RowConfig, int32 RowIndex);
    void PlaceMachines(const FFactoryCommandToken& RowConfig, int32 Width, int32 Length,
                       const FMachineConnections& InputConnections, const FMachineConnections& OutputConnections);
    void CalculateMachineSetup(EBuildable MachineType, const TOptional<FString>& Recipe,
                               const TOptional<float>& Underclock, int32 Width, int32 Length,
                               const FMachineConnections& InputConnections,
                               const FMachineConnections& OutputConnections, bool bFirstUnitInRow, bool bEvenIndex,
                               bool bLastIndex);
    void SpawnWireAndConnect(UFGPowerConnectionComponent* A, UFGPowerConnectionComponent* B);
    UFGPowerConnectionComponent* SpawnPowerPole(const FVector& Location);
    void SpawnMachine(const FVector& Location, EBuildable MachineType, const TOptional<FString>& Recipe,
                      const TOptional<float>& Underclock, UFGPowerConnectionComponent*& outPowerConn,
                      TArray<UFGFactoryConnectionComponent*>& outBeltConn,
                      TArray<UFGPipeConnectionComponent*>& outPipeConn);
    TArray<UFGFactoryConnectionComponent*> SpawnSplitterOrMerger(const FVector& Location, EBuildable SplitterOrMerger);
    void SpawnLiftOrBeltAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To);
    void SpawnBeltAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To);
    void SpawnLiftAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To);
    TArray<UFGPipeConnectionComponent*> SpawnPipeCross(const FVector& Location);
    void SpawnPipeAndConnect(UFGPipeConnectionComponent* From, UFGPipeConnectionComponent* To);

  private:
    // Core references
    UWorld* World;
    UBuildableCache* Cache;
    AFGCharacterPlayer* Player = nullptr;
    UFGManufacturerClipboardRCO* RCO = nullptr;

    // Blueprint output
    TArray<AFGBuildable*> BuildablesForBlueprint;

    // Layout state
    int32 YCursor = 0;
    int32 XCursor = 0;
    int32 FirstMachineWidth = 0;

    // Connection state
    FCachedPowerConnections CachedPowerConnections;
    FConnectionQueue ConnectionQueue;
};
