#include "FactorySpawnerChat.h"
#include "FGRecipe.h"
#include "FGBuildGun.h"
#include "FGCharacterPlayer.h"
#include "CommandSender.h"
#include "BuildableCache.h"
#include "FactoryCommandParser.h"
#include "FactorySpawner.h"
#include "BuildPlanGenerator.h"
#include "EngineUtils.h"

AFactorySpawnerChat* AFactorySpawnerChat::Get(UWorld* World)
{
    for (TActorIterator<AFactorySpawnerChat> It(World); It; ++It)
    {
        return *It;
    }
    return nullptr;
}

void AFactorySpawnerChat::BeginPlay()
{
    Super::BeginPlay();

    if (!BuildableCache)
        BuildableCache = NewObject<UBuildableCache>(this);

    ResetSubsystemData();
    FFactorySpawnerModule::ChatLog(
        GetWorld(),
        TEXT("FactorySpawner loaded! Tool to generate commands: https://uniquesimon.github.io/satisfactory-planner/"));
}

void AFactorySpawnerChat::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ResetSubsystemData();
    BuildableCache = nullptr;

    Super::EndPlay(EndPlayReason);
}

void AFactorySpawnerChat::ResetSubsystemData()
{
    if (BuildableCache)
    {
        BuildableCache->ClearCache();
    }
}

AFactorySpawnerChat::AFactorySpawnerChat()
{
    CommandName = TEXT("FactorySpawner");
    MinNumberOfArguments = 2;
    Usage = FText::FromString("Usage: /FactorySpawner <number> <machine type 1> <recipe 1>, <number> <machine type 2> "
                              "<recipe 2>, beltTier <number>");
}

EExecutionStatus AFactorySpawnerChat::ExecuteCommand_Implementation(UCommandSender* Sender,
                                                                    const TArray<FString>& Arguments,
                                                                    const FString& Label)
{
    FString Joined = FString::Join(Arguments, TEXT(" "));
    Sender->SendChatMessage(FString::Printf(TEXT("/FactorySpawner %s"), *Joined), FLinearColor::Green);

    TArray<FFactoryCommandToken> CommandTokens;
    FString Error;
    if (!FFactoryCommandParser::ParseCommand(Joined, CommandTokens, Error))
    {
        UE_LOG(LogFactorySpawner, Warning, TEXT("%s"), *Error);
        Sender->SendChatMessage(Error);
        return EExecutionStatus::BAD_ARGUMENTS;
    }

    UWorld* World = GetWorld();

    // Determine belt tier to use
    int32 BeltTier = 1;
    if (CommandTokens.Num() > 0 && CommandTokens[0].BeltTier.IsSet())
    {
        // Use explicitly provided belt tier from inline parameter
        BeltTier = CommandTokens[0].BeltTier.GetValue();
    }
    else
    {
        // Auto-detect highest unlocked belt tier
        BeltTier = BuildableCache->GetHighestUnlockedBeltTier(World);
    }

    BuildableCache->SetBeltClass(BeltTier);

    // Auto-detect pipeline tier
    int32 PipelineTier = BuildableCache->GetHighestUnlockedPipelineTier(World);
    BuildableCache->SetPipelineClass(PipelineTier);

    Sender->SendChatMessage(FString::Printf(TEXT("Using Belt Tier: Mk%d, Pipeline Tier: Mk%d"), BeltTier, PipelineTier),
                            FLinearColor::Gray);

    FBuildPlanGenerator(World, BuildableCache).Generate(CommandTokens);
    return EExecutionStatus::COMPLETED;
}
