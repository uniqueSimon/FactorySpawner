#include "BuildableCache.h"
#include "FGRecipeManager.h"
#include "FactorySpawner.h"
#include "FGBuildableConveyorBelt.h"
#include "UObject/SoftObjectPtr.h"

namespace {
	FString GetEnumName(EBuildable Value)
	{
		const UEnum* EnumPtr = StaticEnum<EBuildable>();
		return EnumPtr->GetNameStringByValue(static_cast<int64>(Value));
	}
}

// Caches
TMap<EBuildable, TSubclassOf<AFGBuildable>> UBuildableCache::CachedClasses;
TMap<FString, TSubclassOf<UFGRecipe>> UBuildableCache::CachedRecipeClasses;
TMap<EBuildable, TArray<UStaticMesh*>> UBuildableCache::CachedMeshes;

TSubclassOf<AFGBuildable> UBuildableCache::GetBuildableClass(EBuildable Type)
{
	if (TSubclassOf<AFGBuildable>* Found = CachedClasses.Find(Type))
	{
		return *Found;
	}

	FString Path;
	switch (Type)
	{
	case EBuildable::Splitter:
		Path = TEXT("/Game/FactoryGame/Buildable/Factory/CA_Splitter/Build_ConveyorAttachmentSplitter.Build_ConveyorAttachmentSplitter_C");
		break;

	case EBuildable::Merger:
		Path = TEXT("/Game/FactoryGame/Buildable/Factory/CA_Merger/Build_ConveyorAttachmentMerger.Build_ConveyorAttachmentMerger_C");
		break;

	case EBuildable::PowerPole:
		Path = TEXT("/Game/FactoryGame/Buildable/Factory/PowerPoleMk1/Build_PowerPoleMk1.Build_PowerPoleMk1_C");
		break;

	case EBuildable::PowerLine:
		Path = TEXT("/Game/FactoryGame/Buildable/Factory/PowerLine/Build_PowerLine.Build_PowerLine_C");
		break;

	case EBuildable::Belt:
		Path = TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorBeltMk1/Build_ConveyorBeltMk1.Build_ConveyorBeltMk1_C");
		break;
	case EBuildable::Lift:
		Path = TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorLiftMk1/Build_ConveyorLiftMk1.Build_ConveyorLiftMk1_C");
		break;

	case EBuildable::Smelter:
	case EBuildable::Constructor:
	case EBuildable::Assembler:
	case EBuildable::Foundry:
	case EBuildable::Manufacturer:
	{
		FString MachineType = GetEnumName(Type);

		Path = FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%sMk1/Build_%sMk1.Build_%sMk1_C"), *MachineType, *MachineType, *MachineType);
		break;
	}
	default:
		return nullptr;
	}

	TSoftClassPtr<AFGBuildable> SoftClassPtr(Path);
	TSubclassOf<AFGBuildable> LoadedClass = SoftClassPtr.LoadSynchronous();

	if (LoadedClass)
	{
		CachedClasses.Add(Type, LoadedClass);
	}
	else
	{
		UE_LOG(LogFactorySpawner, Error, TEXT("Failed to load buildable %s at %s"), *GetEnumName(Type), *Path);
	}

	return LoadedClass;
}

void UBuildableCache::SetBeltClass(int32 Tier)
{
	FString Path = FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorBeltMk%d/Build_ConveyorBeltMk%d.Build_ConveyorBeltMk%d_C"), Tier, Tier, Tier);
	TSoftClassPtr<AFGBuildableConveyorBelt> SoftClassPtr(Path);
	TSubclassOf<AFGBuildableConveyorBelt> LoadedClass = SoftClassPtr.LoadSynchronous();

	if (LoadedClass)
	{
		CachedClasses.Add(EBuildable::Belt, LoadedClass);
	}
	else
	{
		UE_LOG(LogFactorySpawner, Error, TEXT("Failed to load belt buildable at %s"), *Path);
	}
}

TArray<FWrongRecipe> UBuildableCache::WrongRecipes = {};

TSubclassOf<UFGRecipe> UBuildableCache::GetRecipeClass(FString& Recipe, TSubclassOf<AFGBuildableManufacturer> ProducedIn, UWorld* World)
{
	// Check if it already in the cache
	if (TSubclassOf<UFGRecipe>* Found = CachedRecipeClasses.Find(Recipe))
	{
		return *Found;
	}
	// Check if we already determined that the recipe is invalid
	bool Exists = WrongRecipes.ContainsByPredicate(
		[&](const FWrongRecipe& Item)
		{
			return Item.Name == Recipe && Item.ProducedIn == ProducedIn->GetName();
		}
	);
	if (Exists) {
		return nullptr;
	}
	AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);

	TArray<TSubclassOf<UFGRecipe>> AvailableRecipes;
	RecipeManager->GetAvailableRecipesForProducer(ProducedIn, AvailableRecipes);

	TSubclassOf<UFGRecipe>* FoundRecipe = AvailableRecipes.FindByPredicate([&](const TSubclassOf<UFGRecipe>& RecipeClass)
		{
			return RecipeClass->GetName() == FString::Printf(TEXT("Recipe_%s_C"), *Recipe);
		});
	if (!FoundRecipe)
	{
		UBuildableCache::WrongRecipes.Add({ Recipe, ProducedIn->GetName() });
		FFactorySpawnerModule::ChatLog(World, FString::Printf(TEXT("Recipe %s not found for machine %s"), *Recipe, *ProducedIn->GetName()));
		TArray<FString> RecipeNames;
		for (TSubclassOf<UFGRecipe> Recipe : AvailableRecipes) {
			FString RecipeName = Recipe->GetName();
			RecipeName.RemoveFromStart(TEXT("Recipe_"));
			RecipeName.RemoveFromEnd(TEXT("_C"));
			RecipeNames.Add(RecipeName);
		}
		if (RecipeNames.Num() == 0) {
			FFactorySpawnerModule::ChatLog(World, FString::Printf(TEXT("The machine %s has not been unlocked yet!"), *ProducedIn->GetName()));
		}
		else {
			FString RecipeNamesJoined = FString::Join(RecipeNames, TEXT(", "));
			FFactorySpawnerModule::ChatLog(World, FString::Printf(TEXT("Following recipes are available for machine %s: %s"), *ProducedIn->GetName(), *RecipeNamesJoined));
		}
		return nullptr;
	}

	CachedRecipeClasses.Add(Recipe, *FoundRecipe);
	return *FoundRecipe;
};

struct FFilePos {
	FString Folder;
	FString File;
};

// Helper function: return mesh paths for each buildable type
static TArray<FFilePos> GetMeshPathsForType(EBuildable Type)
{
	TArray<FFilePos> FilePositions;
	switch (Type)
	{
	case EBuildable::Smelter:
		FilePositions.Add({ TEXT("SmelterMk1"),TEXT("SmelterMk1_static") });
		FilePositions.Add({ TEXT("SmelterMk1"),TEXT("SM_VAT_Smelter_01") });
		break;
	case EBuildable::Constructor:
		FilePositions.Add({ TEXT("ConstructorMk1"),TEXT("ConstructorMk1_static") });
		FilePositions.Add({ TEXT("ConstructorMk1"),TEXT("SM_VAT_Constructor_MK1") });
		break;
	case EBuildable::Assembler:
		FilePositions.Add({ TEXT("AssemblerMk1"),TEXT("AssemblerMk1_static") });
		FilePositions.Add({ TEXT("AssemblerMk1"),TEXT("SM_Assembler_VAT") });
		break;
	case EBuildable::Foundry:
		FilePositions.Add({ TEXT("FoundryMk1"),TEXT("FoundryMk1_static") });
		FilePositions.Add({ TEXT("FoundryMk1"),TEXT("SM_VAT_Foundry") });
		break;
	case EBuildable::Manufacturer:
		FilePositions.Add({ TEXT("ManufacturerMk1"),TEXT("SM_Manufacturer") });
		FilePositions.Add({ TEXT("ManufacturerMk1"),TEXT("SM_VAT_Manufacturer") });
		break;
	case EBuildable::Splitter:
		FilePositions.Add({ TEXT("CA_Splitter"),TEXT("ConveyorAttachmentSplitter_static") });
		FilePositions.Add({ TEXT("CA_Splitter"),TEXT("SM_Splitter_01") });
		break;
	case EBuildable::Merger:
		FilePositions.Add({ TEXT("CA_Merger"),TEXT("ConveyorAttachmentMerger_static") });
		FilePositions.Add({ TEXT("CA_Merger"),TEXT("SM_Merger_01") });
		break;
	case EBuildable::PowerPole:
		FilePositions.Add({ TEXT("PowerPoleMk1"),TEXT("SM_PowerPole_Mk1") });
		break;
	default:
		UE_LOG(LogFactorySpawner, Warning, TEXT("No mesh path defined for buildable type %d"), static_cast<int32>(Type));
		return {};
	};
	return FilePositions;
}

TArray<UStaticMesh*> UBuildableCache::GetStaticMesh(EBuildable Type)
{
	if (TArray<UStaticMesh*>* Found = CachedMeshes.Find(Type))
	{
		return *Found;
	}

	TArray<FFilePos>FilePositions = GetMeshPathsForType(Type);

	TArray<UStaticMesh*> StaticMeshes;
	for (FFilePos FilePos : FilePositions) {
		FString Path = FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%s/Mesh/%s.%s"), *FilePos.Folder, *FilePos.File, *FilePos.File);

		TSoftObjectPtr<UStaticMesh> SoftMeshPtr(Path);
		UStaticMesh* Mesh = SoftMeshPtr.LoadSynchronous();

		if (Mesh) {
			StaticMeshes.Add(Mesh);
		}
		else {
			UE_LOG(LogFactorySpawner, Warning, TEXT("Failed to load mesh at %s"), *Path);
		}
	}
	CachedMeshes.Add(Type, StaticMeshes);
	return StaticMeshes;
}
