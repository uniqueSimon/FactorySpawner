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
    FFactorySpawnerModule::ChatLog(GetWorld(),
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
                              "<recipe 2>; /FactorySpawner BeltTier <number>");
}

EExecutionStatus AFactorySpawnerChat::ExecuteCommand_Implementation(UCommandSender* Sender,
                                                                 const TArray<FString>& Arguments, const FString& Label)
{
    FString Joined = FString::Join(Arguments, TEXT(" "));
    Sender->SendChatMessage(FString::Printf(TEXT("/FactorySpawner %s"), *Joined), FLinearColor::Green);

    if (Arguments[0].Equals(TEXT("BeltTier"), ESearchCase::IgnoreCase))
        return HandleBeltTierCommand(Arguments[1], Sender);

    TArray<FFactoryCommandToken> CommandTokens;
    FString Error;
    if (!FFactoryCommandParser::ParseCommand(Joined, CommandTokens, Error))
    {
        UE_LOG(LogFactorySpawner, Warning, TEXT("%s"), *Error);
        Sender->SendChatMessage(Error);
        return EExecutionStatus::BAD_ARGUMENTS;
    }

    FBuildPlanGenerator(GetWorld(), BuildableCache).Generate(CommandTokens);
    return EExecutionStatus::COMPLETED;
}

EExecutionStatus AFactorySpawnerChat::HandleBeltTierCommand(const FString& Input, UCommandSender* Sender)
{
    int32 Tier;
    if (LexTryParseString(Tier, *Input) && Tier >= 1 && Tier <= 8)
    {
        BuildableCache->SetBeltClass(Tier);
        Sender->SendChatMessage(FString::Printf(TEXT("Belt Tier: Mk%d"), Tier), FLinearColor::Green);
        return EExecutionStatus::COMPLETED;
    }
    Sender->SendChatMessage(TEXT("BeltTier must be 1-8"));
    return EExecutionStatus::BAD_ARGUMENTS;
}
