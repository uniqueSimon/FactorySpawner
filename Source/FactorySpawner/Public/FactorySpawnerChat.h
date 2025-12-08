#pragma once

#include "CoreMinimal.h"
#include "ChatCommandInstance.h"
#include "BuildPlanTypes.h"
#include "FactorySpawnerChat.generated.h"

class UBuildPlanGenerator;
class UBuildableCache;

UCLASS()
class FACTORYSPAWNER_API AFactorySpawnerChat : public AChatCommandInstance
{
    GENERATED_BODY()

  public:
    AFactorySpawnerChat();

    // Static helper to find the subsystem instance in the current world
    static AFactorySpawnerChat* Get(UWorld* World);

    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    EExecutionStatus ExecuteCommand_Implementation(class UCommandSender* Sender, const TArray<FString>& Arguments,
                                                   const FString& Label) override;

    /** Clears current data (useful when changing savegames) */
    void ResetSubsystemData();

  private:
    EExecutionStatus HandleBeltTierCommand(const FString& Input, UCommandSender* Sender);

    /** Cache for buildables and recipes (world-specific) */
    UPROPERTY()
    UBuildableCache* BuildableCache;
};
