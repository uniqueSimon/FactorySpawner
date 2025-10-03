#include "MyChatSubsystem.h"
#include "FGRecipe.h"
#include "FGBuildGun.h"
#include "FGCharacterPlayer.h"
#include "CommandSender.h"
#include "BuildableCache.h"
#include "FactoryCommandParser.h"
#include "FactorySpawner.h"
#include "FactoryCommandParser.h"

namespace
{
    EMachineType StringToMachineType(const FString Value)
    {
        FName Name(*Value);
        UEnum* EnumPtr = StaticEnum<EMachineType>();

        int64 EnumValue = EnumPtr->GetValueByName(Name);
        if (EnumValue == INDEX_NONE)
        {
            return EMachineType::Invalid;
        }
        return static_cast<EMachineType>(EnumValue);
    }

    static TArray<FFactoryCommandToken> DefaultClusterConfig = {
        {3, EMachineType::Smelter, TOptional<FString>(TEXT("IngotIron"))},
        {3, EMachineType::Constructor, TOptional<FString>(TEXT("IronPlate"))},
        {2, EMachineType::Constructor, TOptional<FString>(TEXT("Wire"))},
    };

    void SelectRecipeWithBuildGun(UWorld* World)
    {
        FString RecipePath = TEXT("/FactorySpawner/Recipe_ConstructorGroup.Recipe_ConstructorGroup_C");
        TSoftClassPtr<AFGBuildable> SoftClassPtr(RecipePath);
        TSubclassOf<UFGRecipe> LoadedRecipe = SoftClassPtr.LoadSynchronous();
        APlayerController* PlayerController = World->GetFirstPlayerController();
        AFGCharacterPlayer* PlayerCasted = Cast<AFGCharacterPlayer>(PlayerController->GetCharacter());
        AFGBuildGun* BuildGun = PlayerCasted->GetBuildGun();
        BuildGun->GotoBuildState(LoadedRecipe);
    }
} // namespace

FBuildPlan AMyChatSubsystem::CurrentBuildPlan;

AMyChatSubsystem::AMyChatSubsystem()
{
    CommandName = TEXT("FactorySpawner");
    MinNumberOfArguments = 2;
    Usage = FText::FromString("Usage: /FactorySpawner <number> <machine type 1> <recipe 1>, <number> <machine type 2> "
                              "<recipe 2>; /FactorySpawner BeltTier <number>");
}

EExecutionStatus AMyChatSubsystem::ExecuteCommand_Implementation(UCommandSender* Sender,
                                                                 const TArray<FString>& Arguments, const FString& Label)
{
    FString Joined = FString::Join(Arguments, TEXT(" "));
    Sender->SendChatMessage(FString::Printf(TEXT("/FactorySpawner %s"), *Joined), FLinearColor::Green);

    if (Arguments[0].Equals(TEXT("BeltTier"), ESearchCase::IgnoreCase))
    {
        return HandleBeltTierCommand(Arguments[1], Sender);
    }

    TArray<FFactoryCommandToken> CommandTokens;
    FString OutError;

    if (!FFactoryCommandParser::ParseCommand(Joined, CommandTokens, OutError))
    {
        UE_LOG(LogFactorySpawner, Warning, TEXT("%s"), *OutError);
        Sender->SendChatMessage(OutError);
        return EExecutionStatus::BAD_ARGUMENTS;
    }

    UWorld* World = GetWorld();
    CurrentBuildPlan = CalculateClusterSetup(World, CommandTokens);

    SelectRecipeWithBuildGun(World);

    return EExecutionStatus::COMPLETED;
}

EExecutionStatus AMyChatSubsystem::HandleBeltTierCommand(const FString& Input, UCommandSender* Sender)
{
    int32 Tier = 0;
    if (LexTryParseString(Tier, *Input) && Tier > 0 && Tier <= 8)
    {
        UBuildableCache::SetBeltClass(Tier);
        Sender->SendChatMessage(FString::Printf(TEXT("Set Belt Tier Mk%d"), Tier), FLinearColor::Green);
        return EExecutionStatus::COMPLETED;
    }
    FString ErrorMsg = TEXT("BeltTier must be a number between 1 and 8");
    UE_LOG(LogFactorySpawner, Warning, TEXT("%s"), *ErrorMsg);
    Sender->SendChatMessage(ErrorMsg);
    return EExecutionStatus::BAD_ARGUMENTS;
}

void AMyChatSubsystem::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();
    CurrentBuildPlan = CalculateClusterSetup(World, DefaultClusterConfig);

    FFactorySpawnerModule::ChatLog(World,
                                   "Check out my Planner-Tool https://uniquesimon.github.io/satisfactory-planner/ for "
                                   "automatic chat command generation!");
}