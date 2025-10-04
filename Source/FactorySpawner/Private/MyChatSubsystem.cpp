#include "MyChatSubsystem.h"
#include "FGRecipe.h"
#include "FGBuildGun.h"
#include "FGCharacterPlayer.h"
#include "CommandSender.h"
#include "BuildableCache.h"
#include "FactoryCommandParser.h"
#include "FactorySpawner.h"
#include "BuildPlanGenerator.h"
#include "EngineUtils.h"

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

AMyChatSubsystem* AMyChatSubsystem::Get(UWorld* World)
{
    for (TActorIterator<AMyChatSubsystem> It(World); It; ++It)
    {
        return *It;
    }
    return nullptr;
}

void AMyChatSubsystem::BeginPlay()
{
    Super::BeginPlay();

    UWorld* World = GetWorld();

    // Create BuildableCache if needed
    if (!BuildableCache)
    {
        BuildableCache = NewObject<UBuildableCache>(this);
    }

    // Create BuildPlanGenerator if not existing
    if (!BuildPlanGenerator)
    {
        BuildPlanGenerator = NewObject<UBuildPlanGenerator>(this);
        BuildPlanGenerator->Initialize(World, BuildableCache);
    }

    // Reset state for safety
    ResetSubsystemData();

    BuildPlanGenerator->Generate(DefaultClusterConfig);

    FFactorySpawnerModule::ChatLog(World,
                                   "Check out my Planner-Tool https://uniquesimon.github.io/satisfactory-planner/ for "
                                   "automatic chat command generation!");
}

void AMyChatSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UE_LOG(LogTemp, Log, TEXT("[ChatSubsystem] EndPlay — cleaning up cache and build plan."));

    ResetSubsystemData();
    BuildableCache = nullptr;

    Super::EndPlay(EndPlayReason);
}

void AMyChatSubsystem::ResetSubsystemData()
{
    if (BuildPlanGenerator)
    {
        BuildPlanGenerator->ResetCurrentBuildPlan();
    }

    if (BuildableCache)
    {
        BuildableCache->ClearCache();
    }
}

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
    if (!World || !BuildPlanGenerator)
    {
        UE_LOG(LogTemp, Error, TEXT("BuildPlanGenerator not ready in ExecuteCommand"));
        return EExecutionStatus::UNCOMPLETED;
    }

    BuildPlanGenerator->Generate(CommandTokens);

    SelectRecipeWithBuildGun(World);

    return EExecutionStatus::COMPLETED;
}

EExecutionStatus AMyChatSubsystem::HandleBeltTierCommand(const FString& Input, UCommandSender* Sender)
{
    int32 Tier = 0;
    if (LexTryParseString(Tier, *Input) && Tier > 0 && Tier <= 8)
    {
        BuildableCache->SetBeltClass(Tier);
        Sender->SendChatMessage(FString::Printf(TEXT("Set Belt Tier Mk%d"), Tier), FLinearColor::Green);
        return EExecutionStatus::COMPLETED;
    }
    FString ErrorMsg = TEXT("BeltTier must be a number between 1 and 8");
    UE_LOG(LogFactorySpawner, Warning, TEXT("%s"), *ErrorMsg);
    Sender->SendChatMessage(ErrorMsg);
    return EExecutionStatus::BAD_ARGUMENTS;
}
