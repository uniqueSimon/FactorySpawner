#pragma once

#include "CoreMinimal.h"
#include "BuildPlanTypes.h"
#include "BuildPlanGenerator.generated.h"

class UBuildableCache;
class AMyChatSubsystem;
struct FFactoryCommandToken;

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

UCLASS()
class FACTORYSPAWNER_API UBuildPlanGenerator : public UObject
{
    GENERATED_BODY()

  public:
    /** Initialize generator with a valid world and cache reference */
    void Initialize(UWorld* InWorld, UBuildableCache* InCache);

    /** Generate a new plan from parsed tokens */
    void Generate(const TArray<FFactoryCommandToken>& ClusterConfig);

    /** Access current build plan */
    const FBuildPlan& GetCurrentBuildPlan() const
    {
        return CurrentBuildPlan;
    }

    void ResetCurrentBuildPlan();

  private:
    // --- Internal helper functions ---
    void ProcessRow(const FFactoryCommandToken& RowConfig, int32 RowIndex);
    void PlaceMachines(const FFactoryCommandToken& RowConfig, int32 RowIndex, const FMachineConfig& MachineConfig);

    /** The world context this generator operates in */
    UPROPERTY()
    UWorld* World = nullptr;

    /** The cache instance used to resolve buildables and meshes */
    UPROPERTY()
    UBuildableCache* BuildableCache = nullptr;

    /** The resulting plan after generation */
    FBuildPlan CurrentBuildPlan;

    /** Internal state */
    int32 YCursor = 0;
    int32 XCursor = 0;
    int32 FirstMachineWidth = 0;

    FCachedPowerConnections CachedPowerConnections;
};
