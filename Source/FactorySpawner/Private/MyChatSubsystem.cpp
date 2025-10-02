#include "MyChatSubsystem.h"
#include "FGRecipe.h"
#include "FGBuildGun.h"
#include "FGCharacterPlayer.h"
#include "CommandSender.h"
#include "BuildableCache.h"

namespace {
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

	static TArray<FClusterConfig> DefaultClusterConfig = {
		{ 3, EMachineType::Smelter, TOptional<FString>(TEXT("IngotIron"))},
		{ 3, EMachineType::Constructor, TOptional<FString>(TEXT("IronPlate")) },
		{ 2, EMachineType::Constructor, TOptional<FString>(TEXT("Wire")) },
	};

	void SelectRecipeWithBuildGun(UWorld* World) {
		FString RecipePath = TEXT("/FactorySpawner/Recipe_ConstructorGroup.Recipe_ConstructorGroup_C");
		TSubclassOf<UFGRecipe> LoadedRecipe = StaticLoadClass(UFGRecipe::StaticClass(), nullptr, *RecipePath);
		APlayerController* PlayerController = World->GetFirstPlayerController();
		AFGCharacterPlayer* PlayerCasted = Cast<AFGCharacterPlayer>(PlayerController->GetCharacter());
		AFGBuildGun* BuildGun = PlayerCasted->GetBuildGun();
		BuildGun->GotoBuildState(LoadedRecipe);
	}
}

FBuildPlan AMyChatSubsystem::CurrentBuildPlan;

AMyChatSubsystem::AMyChatSubsystem()
{
	CommandName = TEXT("FactorySpawner");
	MinNumberOfArguments = 2;
	Usage = FText::FromString("Usage: /FactorySpawner <number> <machine type 1> <recipe 1>, <number> <machine type 2> <recipe 2>; /FactorySpawner BeltTier <number>");
}

EExecutionStatus AMyChatSubsystem::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) {
	FString Joined = FString::Join(Arguments, TEXT(" "));
	Sender->SendChatMessage(FString::Printf(TEXT("/FactorySpawner %s"), *Joined), FLinearColor::Green);

	if (Arguments[0] == TEXT("BeltTier")) {
		if (Arguments.Num() != 2) {
			Sender->SendChatMessage(TEXT("Invalid syntax! Usage: /FactorySpawner BeltTier <number>"));
			return EExecutionStatus::BAD_ARGUMENTS;
		}
		FString TierStr = Arguments[1];
		if (TierStr.IsNumeric()) {
			int32 Tier = FCString::Atoi(*TierStr);
			if (Tier > 0 && Tier <= 8) {
				UBuildableCache::SetBeltClass(Tier);
				Sender->SendChatMessage(FString::Printf(TEXT("Set Belt Tier Mk%d"), Tier));
				return EExecutionStatus::COMPLETED;
			}
		}
		Sender->SendChatMessage(TEXT("BeltTier must be between 1 and 8"));
		return EExecutionStatus::BAD_ARGUMENTS;

	}

	TArray<FString> Rows;
	Joined.ParseIntoArray(Rows, TEXT(", "));

	TArray<FClusterConfig> ClusterConfig;
	for (FString& Row : Rows)
	{
		TArray<FString> Splitted;
		Row.ParseIntoArray(Splitted, TEXT(" "));
		if (Splitted.Num() >= 2 && Splitted.Num() <= 4)
		{
			int32 Value;
			if (LexTryParseString(Value, *Splitted[0]) && Value > 0)
			{
				EMachineType PlantType = StringToMachineType(Splitted[1]);
				if (PlantType == EMachineType::Invalid) {
					Sender->SendChatMessage(FString::Printf(TEXT("This machine type does not exist: %s. Use one of those: Smelter, Constructor, Assembler, Foundry, Manufacturer"), *Splitted[1]));
					return EExecutionStatus::BAD_ARGUMENTS;
				}
				if (Splitted.Num() == 2) {
					ClusterConfig.Add(FClusterConfig{ Value,PlantType });
				}
				else if (Splitted.Num() == 3) {
					ClusterConfig.Add(FClusterConfig{ Value,PlantType, Splitted[2] });
				}
				else {
					float UnderclockPercentage;
					if (LexTryParseString(UnderclockPercentage, *Splitted[3]) && Value > 0 && Value < 100)
					{
						ClusterConfig.Add(FClusterConfig{ Value,PlantType, Splitted[2], UnderclockPercentage });
					}

				}
			}
			else {
				Sender->SendChatMessage(TEXT("The number of machines must be greater than 0! Usage: /FactorySpawner <number> <machine type> <recipe (optional)>"));
				return EExecutionStatus::BAD_ARGUMENTS;
			}
		}
		else {
			Sender->SendChatMessage(TEXT("Wrong number of arguments! Usage: /FactorySpawner <number> <machine type> <recipe (optional)>"));
			return EExecutionStatus::BAD_ARGUMENTS;
		}
	}

	UWorld* World = GetWorld();
	CurrentBuildPlan = CalculateClusterSetup(World, ClusterConfig);

	SelectRecipeWithBuildGun(World);

	return EExecutionStatus::COMPLETED;
}

void AMyChatSubsystem::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	CurrentBuildPlan = CalculateClusterSetup(World, DefaultClusterConfig);

	FFactorySpawnerModule::ChatLog(World, "Check out my Planner-Tool https://uniquesimon.github.io/satisfactory-planner/ for automatic chat command generation!");
}