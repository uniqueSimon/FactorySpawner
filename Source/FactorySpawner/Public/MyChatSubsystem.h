#pragma once

#include "CoreMinimal.h"
#include "ChatCommandInstance.h"
#include "MyChatSubsystem.generated.h"

class FFactoryCommandParser;
class UBuildPlanGenerator;
class UBuildableCache;

UENUM(BlueprintType)
enum class EBuildable : uint8
{
    // Machines
    Smelter UMETA(DisplayName = "Smelter"),
    Constructor UMETA(DisplayName = "Constructor"),
    Assembler UMETA(DisplayName = "Assembler"),
    Foundry UMETA(DisplayName = "Foundry"),
    Manufacturer UMETA(DisplayName = "Manufacturer"),

    // Utility buildings
    Splitter UMETA(DisplayName = "Splitter"),
    Merger UMETA(DisplayName = "Merger"),
    PowerPole UMETA(DisplayName = "Power Pole"),

    // Transport
    Belt UMETA(DisplayName = "Conveyor Belt"),
    Lift UMETA(DisplayName = "Lift"),
    PowerLine UMETA(DisplayName = "Power Line"),

    Invalid UMETA(Hidden),
};

struct FFactoryCommandToken
{
    int32 Count = 0;
    EBuildable MachineType;
    TOptional<FString> Recipe;
    TOptional<float> ClockPercent; // percent value (e.g. 75.5)
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

    /** Cache for buildables and recipes (world-specific) */
    UPROPERTY()
    UBuildableCache* BuildableCache;

    UPROPERTY()
    UBuildPlanGenerator* BuildPlanGenerator;

  private:
    EExecutionStatus HandleBeltTierCommand(const FString& Input, UCommandSender* Sender);
};
