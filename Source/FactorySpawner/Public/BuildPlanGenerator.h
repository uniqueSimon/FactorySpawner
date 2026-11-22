#pragma once

#include "CoreMinimal.h"
#include "BuildPlanTypes.h"

class UBuildableCache;
class AFGBuildable;
class AFGBuildableConveyorLift;
class UFGItemDescriptor;
class UFGRecipe;
struct FItemAmount;
struct FProductionPlan;
struct FProductionRow;
class UFGPowerConnectionComponent;
class UFGPipeConnectionComponent;
class UFGFactoryConnectionComponent;
enum class EFactoryConnectionDirection : uint8;

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

    bool Generate(const FFactoryCommand& Command, FString& OutError, const FProductionPlan* Plan = nullptr);

  private:
    static constexpr double FoundationSize = 800.0;
    static constexpr double FoundationThickness = 100.0;
    static constexpr double ProductionLiftStagger = 250.0;

    struct FFloorLayout
    {
        double YCursor = 0;
        FBox2D Footprint{ForceInit};
        double DeckZ = 0;
        double RequiredRise = 0;
        double Top = 0;
        bool bEquipment = false;
    };

    struct FPreparedRow
    {
        const FMachineConfig* Machine = nullptr;
        const FMachineConnections* Input = nullptr;
        const FMachineConnections* Output = nullptr;
        TSubclassOf<UFGRecipe> Recipe;
        FVector2D Position = FVector2D::ZeroVector;
        int32 Floor = 1;
        double InputAisleY = 0;
        double OutputAisleY = 0;
    };

    struct FItemPort
    {
        TSubclassOf<UFGItemDescriptor> Item;
        UFGFactoryConnectionComponent* Connection;
    };

    struct FFlowPort
    {
        UFGFactoryConnectionComponent* Belt = nullptr;
        UFGPipeConnectionComponent* Pipe = nullptr;
        int32 Floor = 1;
        TOptional<double> AisleY;
        FVector Location() const;
        explicit operator bool() const { return Belt || Pipe; }
    };

    struct FRowConveyors
    {
        int32 Floor;
        double InputAisleY = 0;
        double OutputAisleY = 0;
        TArray<FItemPort> Inputs;
        TArray<FItemPort> Outputs;
        TMap<FString, UFGPipeConnectionComponent*> PipeInputs;
        TMap<FString, UFGPipeConnectionComponent*> PipeOutputs;
    };

    bool PrepareRow(const FFactoryCommandToken& RowConfig, int32 RowIndex, FPreparedRow& Row, FString& OutError);
    bool PlanProductionLayout(const FFactoryCommand& Command, TArray<FPreparedRow>& Rows, FString& OutError);
    bool ProcessRow(const FFactoryCommandToken& RowConfig, const FPreparedRow& Row, FString& OutError);
    void PlaceMachines(const FFactoryCommandToken& RowConfig, TSubclassOf<UFGRecipe> Recipe, int32 Width, int32 Length,
                       const FMachineConnections& InputConnections, const FMachineConnections& OutputConnections);
    void CalculateMachineSetup(EBuildable MachineType, TSubclassOf<UFGRecipe> Recipe,
                               const TOptional<float>& Underclock, int32 Width, int32 Length,
                               const FMachineConnections& InputConnections,
                               const FMachineConnections& OutputConnections, bool bFirstUnitInRow, bool bEvenIndex,
                               bool bLastIndex);
    void SpawnWireAndConnect(UFGPowerConnectionComponent* A, UFGPowerConnectionComponent* B);
    UFGPowerConnectionComponent* SpawnPowerPole(const FVector& Location);
    AFGBuildable* SpawnMachine(const FVector& Location, EBuildable MachineType, TSubclassOf<UFGRecipe> Recipe,
                      const TOptional<float>& Underclock, UFGPowerConnectionComponent*& outPowerConn,
                      TArray<UFGFactoryConnectionComponent*>& outBeltConn,
                      TArray<UFGPipeConnectionComponent*>& outPipeConn);
    TArray<UFGFactoryConnectionComponent*> SpawnSplitterOrMerger(const FVector& Location, EBuildable SplitterOrMerger);
    void SpawnLiftOrBeltAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To);
    bool SpawnBeltAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To,
                             TOptional<double> BypassX = {}, TOptional<double> FromAisleY = {},
                             TOptional<double> ToAisleY = {});
    AFGBuildableConveyorLift* SpawnLift(const FVector& Location, double OutputZ, double InputYaw, double OutputYaw);
    void SpawnLiftAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To);
    TArray<UFGPipeConnectionComponent*> SpawnPipeCross(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);
    bool SpawnPipeAndConnect(UFGPipeConnectionComponent* From, UFGPipeConnectionComponent* To);
    bool BuildFloorPlatforms(FString& OutError);
    bool BuildProductionLogistics(const FProductionPlan& Plan, FString& OutError);
    bool ConnectFlow(FFlowPort From, FFlowPort To, TSubclassOf<UFGItemDescriptor> Item, double Rate);
    bool CanRunBeltDirectly(const UFGFactoryConnectionComponent* From, const UFGFactoryConnectionComponent* To) const;
    bool CanRunBeltViaAisle(const FVector& Start, const FVector& End, double TrackX, double StartY, double EndY) const;
    TArray<FFlowPort> ExpandFlow(FFlowPort Root, bool bSource, int32 Links, int32 OverflowLinks,
                                  TSubclassOf<UFGItemDescriptor> Item);
    FFlowPort SpawnFlowTerminal(const FVector& Location, bool bImport, TSubclassOf<UFGItemDescriptor> Item,
                                double Rate, int32 Lane);
    void AddPortLabel(const FVector& Location, const FString& Text);
    void RecordPipePort(UFGPipeConnectionComponent* MachinePort,
                        UFGPipeConnectionComponent* RowPort,
                        const TArray<FItemAmount>& RecipeItems, TMap<FString, UFGPipeConnectionComponent*>& Ports);
    void WirePowerRoute(UFGPowerConnectionComponent* From, UFGPowerConnectionComponent* To);
    bool ConnectRows(FString& OutError);
    bool ConnectRowConveyors(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To,
                             double ShaftX, bool bCrossFloor, TOptional<double> FromAisleY = {},
                             TOptional<double> ToAisleY = {});
    void RecordRowPort(UFGFactoryConnectionComponent* MachinePort,
                       const TArray<UFGFactoryConnectionComponent*>& ManifoldPorts, EFactoryConnectionDirection Direction,
                       const TArray<FItemAmount>& RecipeItems, TArray<FItemPort>& RowPorts);

  private:
    // Core references
    UWorld* World;
    UBuildableCache* Cache;
    AFGCharacterPlayer* Player = nullptr;
    UFGManufacturerClipboardRCO* RCO = nullptr;

    // Blueprint output
    TArray<AFGBuildable*> BuildablesForBlueprint;

    // Layout state
    TMap<int32, FFloorLayout> Floors;
    TArray<FRowConveyors> RowConveyors;
    FBox2D PlatformBounds{ForceInit};
    double FloorHeight = 0;
    double XCursor = 0;
    double YCursor = 0;
    double RowZ = 0;
    const FProductionPlan* Production = nullptr;
    UFGPowerConnectionComponent* FirstMachinePole = nullptr;
    TArray<TArray<FBox2D>> LeftConveyorLanes;
    TArray<TArray<FBox2D>> RightConveyorLanes;
    TArray<FBox> MachineClearances;
    int32 LogisticsJunction = 0;
    bool bMultiFloor = false;
    bool bBuildFailed = false;

    // Connection state
    FCachedPowerConnections CachedPowerConnections;
    FConnectionQueue ConnectionQueue;
};
