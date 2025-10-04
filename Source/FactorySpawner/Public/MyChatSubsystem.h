#pragma once

#include "CoreMinimal.h"
#include "ChatCommandInstance.h"
#include "BuildPlanTypes.h"
#include "MyChatSubsystem.generated.h"

class UBuildPlanGenerator;
class UBuildableCache;

UCLASS()
class FACTORYSPAWNER_API AMyChatSubsystem : public AChatCommandInstance
{
    GENERATED_BODY()

  public:
    AMyChatSubsystem();

    // Static helper to find the subsystem instance in the current world
    static AMyChatSubsystem* Get(UWorld* World);

    void BeginPlay() override;
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
