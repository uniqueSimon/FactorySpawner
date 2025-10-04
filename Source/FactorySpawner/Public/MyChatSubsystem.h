#pragma once

#include "CoreMinimal.h"
#include "ChatCommandInstance.h"
#include "FactorySpawner.h"
#include "MyChatSubsystem.generated.h"

class FFactoryCommandParser;
struct FFactoryCommandToken;
class UBuildableCache;

UENUM(BlueprintType)
enum class EMachineType : uint8
{
    Smelter,
    Constructor,
    Assembler,
    Foundry,
    Manufacturer,
    Invalid,
};

UENUM(BlueprintType)
enum class EBuildable : uint8
{
    Smelter = static_cast<uint8>(EMachineType::Smelter),
    Constructor = static_cast<uint8>(EMachineType::Constructor),
    Assembler = static_cast<uint8>(EMachineType::Assembler),
    Foundry = static_cast<uint8>(EMachineType::Foundry),
    Manufacturer = static_cast<uint8>(EMachineType::Manufacturer),

    Splitter,
    Merger,
    PowerPole,

    Belt,
    Lift,
    PowerLine,
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

UCLASS()
class FACTORYSPAWNER_API AMyChatSubsystem : public AChatCommandInstance
{
    GENERATED_BODY()

  public:
    AMyChatSubsystem();

    // Static helper to find the subsystem instance in the current world
    static AMyChatSubsystem* Get(UWorld* World);

    /** Called when this actor enters the world */
    void BeginPlay() override;

    /** Called when this actor is removed from the world */
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    EExecutionStatus ExecuteCommand_Implementation(class UCommandSender* Sender, const TArray<FString>& Arguments,
                                                   const FString& Label) override;

    /** Clears current data (useful when changing savegames) */
    void ResetSubsystemData();

    /** The current generated build plan for this world */
    FBuildPlan CurrentBuildPlan;

    /** Cache for buildables and recipes (world-specific) */
    UPROPERTY()
    UBuildableCache* BuildableCache;

  private:
    EExecutionStatus HandleBeltTierCommand(const FString& Input, UCommandSender* Sender);
};
