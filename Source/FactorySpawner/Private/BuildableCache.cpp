#include "BuildableCache.h"
#include "FGRecipeManager.h"
#include "FactorySpawner.h"
#include "FGBuildableConveyorBelt.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/StaticMesh.h"

namespace
{
	// Helper: get enum name as string
	FString GetEnumName(EBuildable Value)
	{
		const UEnum* EnumPtr = StaticEnum<EBuildable>();
		return EnumPtr->GetNameStringByValue(static_cast<int64>(Value));
	}

	// Helper: build class path for a machine type
	FString GetMachineClassPath(EBuildable Type)
	{
		switch (Type)
		{
		case EBuildable::Splitter:   return TEXT("/Game/FactoryGame/Buildable/Factory/CA_Splitter/Build_ConveyorAttachmentSplitter.Build_ConveyorAttachmentSplitter_C");
		case EBuildable::Merger:     return TEXT("/Game/FactoryGame/Buildable/Factory/CA_Merger/Build_ConveyorAttachmentMerger.Build_ConveyorAttachmentMerger_C");
		case EBuildable::PowerPole:  return TEXT("/Game/FactoryGame/Buildable/Factory/PowerPoleMk1/Build_PowerPoleMk1.Build_PowerPoleMk1_C");
		case EBuildable::PowerLine:  return TEXT("/Game/FactoryGame/Buildable/Factory/PowerLine/Build_PowerLine.Build_PowerLine_C");
		case EBuildable::Belt:       return TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorBeltMk1/Build_ConveyorBeltMk1.Build_ConveyorBeltMk1_C");
		case EBuildable::Lift:       return TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorLiftMk1/Build_ConveyorLiftMk1.Build_ConveyorLiftMk1_C");
		default:
			// Machines with Mk1 convention
			FString MachineType = GetEnumName(Type);
			return FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%sMk1/Build_%sMk1.Build_%sMk1_C"),
				*MachineType, *MachineType, *MachineType);
		}
	}

	// Helper: mesh paths per buildable type
	TArray<FString> GetMeshPathsForType(EBuildable Type)
	{
		switch (Type)
		{
		case EBuildable::Smelter:     return { "/Game/FactoryGame/Buildable/Factory/SmelterMk1/Mesh/SmelterMk1_static.SmelterMk1_static", "/Game/FactoryGame/Buildable/Factory/SmelterMk1/Mesh/SM_VAT_Smelter_01.SM_VAT_Smelter_01" };
		case EBuildable::Constructor: return { "/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Mesh/ConstructorMk1_static.ConstructorMk1_static", "/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Mesh/SM_VAT_Constructor_MK1.SM_VAT_Constructor_MK1" };
		case EBuildable::Assembler:   return { "/Game/FactoryGame/Buildable/Factory/AssemblerMk1/Mesh/AssemblerMk1_static.AssemblerMk1_static", "/Game/FactoryGame/Buildable/Factory/AssemblerMk1/Mesh/SM_Assembler_VAT.SM_Assembler_VAT" };
		case EBuildable::Foundry:     return { "/Game/FactoryGame/Buildable/Factory/FoundryMk1/Mesh/FoundryMk1_static.FoundryMk1_static", "/Game/FactoryGame/Buildable/Factory/FoundryMk1/Mesh/SM_VAT_Foundry.SM_VAT_Foundry" };
		case EBuildable::Manufacturer:return { "/Game/FactoryGame/Buildable/Factory/ManufacturerMk1/Mesh/SM_Manufacturer.SM_Manufacturer", "/Game/FactoryGame/Buildable/Factory/ManufacturerMk1/Mesh/SM_VAT_Manufacturer.SM_VAT_Manufacturer" };
		case EBuildable::Splitter:    return { "/Game/FactoryGame/Buildable/Factory/CA_Splitter/Mesh/ConveyorAttachmentSplitter_static.ConveyorAttachmentSplitter_static", "/Game/FactoryGame/Buildable/Factory/CA_Splitter/Mesh/SM_Splitter_01.SM_Splitter_01" };
		case EBuildable::Merger:      return { "/Game/FactoryGame/Buildable/Factory/CA_Merger/Mesh/ConveyorAttachmentMerger_static.ConveyorAttachmentMerger_static", "/Game/FactoryGame/Buildable/Factory/CA_Merger/Mesh/SM_Merger_01.SM_Merger_01" };
		case EBuildable::PowerPole:   return { "/Game/FactoryGame/Buildable/Factory/PowerPoleMk1/Mesh/SM_PowerPole_Mk1.SM_PowerPole_Mk1" };
		default: return {};
		}
	}
}

// Static cache definitions
TMap<EBuildable, TSubclassOf<AFGBuildable>> UBuildableCache::CachedClasses;
TMap<FString, TSubclassOf<UFGRecipe>> UBuildableCache::CachedRecipeClasses;
TMap<EBuildable, TArray<UStaticMesh*>> UBuildableCache::CachedMeshes;
TArray<FWrongRecipe> UBuildableCache::WrongRecipes = {};

//-------------------------------------------------
// Buildable class loader
//-------------------------------------------------
TSubclassOf<AFGBuildable> UBuildableCache::GetBuildableClass(EBuildable Type)
{
	if (TSubclassOf<AFGBuildable>* Found = CachedClasses.Find(Type))
		return *Found;

	FString Path = GetMachineClassPath(Type);
	TSoftClassPtr<AFGBuildable> SoftClass(Path);
	TSubclassOf<AFGBuildable> LoadedClass = SoftClass.LoadSynchronous();

	if (LoadedClass)
		CachedClasses.Add(Type, LoadedClass);
	else
		UE_LOG(LogFactorySpawner, Error, TEXT("Failed to load buildable %s at %s"), *GetEnumName(Type), *Path);

	return LoadedClass;
}

void UBuildableCache::SetBeltClass(int32 Tier)
{
	FString Path = FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorBeltMk%d/Build_ConveyorBeltMk%d.Build_ConveyorBeltMk%d_C"), Tier, Tier, Tier);
	TSoftClassPtr<AFGBuildableConveyorBelt> SoftClass(Path);
	TSubclassOf<AFGBuildableConveyorBelt> LoadedClass = SoftClass.LoadSynchronous();

	if (LoadedClass)
		CachedClasses.Add(EBuildable::Belt, LoadedClass);
	else
		UE_LOG(LogFactorySpawner, Error, TEXT("Failed to load belt buildable at %s"), *Path);
}

//-------------------------------------------------
// Recipe loader
//-------------------------------------------------
TSubclassOf<UFGRecipe> UBuildableCache::GetRecipeClass(FString& Recipe, TSubclassOf<AFGBuildableManufacturer> ProducedIn, UWorld* World)
{
	if (TSubclassOf<UFGRecipe>* Found = CachedRecipeClasses.Find(Recipe))
		return *Found;

	if (WrongRecipes.ContainsByPredicate([&](const FWrongRecipe& Item) { return Item.Name == Recipe && Item.ProducedIn == ProducedIn->GetName(); }))
		return nullptr;

	AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);
	TArray<TSubclassOf<UFGRecipe>> AvailableRecipes;
	RecipeManager->GetAvailableRecipesForProducer(ProducedIn, AvailableRecipes);

	TSubclassOf<UFGRecipe>* FoundRecipe = AvailableRecipes.FindByPredicate([&](const TSubclassOf<UFGRecipe>& RecipeClass)
		{
			return RecipeClass->GetName() == FString::Printf(TEXT("Recipe_%s_C"), *Recipe);
		});

	if (!FoundRecipe)
	{
		WrongRecipes.Add({ Recipe, ProducedIn->GetName() });

		TArray<FString> RecipeNames;
		for (auto& R : AvailableRecipes)
		{
			FString Name = R->GetName();
			Name.RemoveFromStart(TEXT("Recipe_"));
			Name.RemoveFromEnd(TEXT("_C"));
			RecipeNames.Add(Name);
		}

		FString Msg;
		if (RecipeNames.Num() == 0)
			Msg = FString::Printf(TEXT("The machine %s has not been unlocked yet!"), *ProducedIn->GetName());
		else
			Msg = FString::Printf(TEXT("Following recipes are available for machine %s: %s"), *ProducedIn->GetName(), *FString::Join(RecipeNames, TEXT(", ")));

		FFactorySpawnerModule::ChatLog(World, FString::Printf(TEXT("Recipe %s not found for machine %s. %s"), *Recipe, *ProducedIn->GetName(), *Msg));
		return nullptr;
	}

	CachedRecipeClasses.Add(Recipe, *FoundRecipe);
	return *FoundRecipe;
}

//-------------------------------------------------
// Mesh loader
//-------------------------------------------------
TArray<UStaticMesh*> UBuildableCache::GetStaticMesh(EBuildable Type)
{
	if (TArray<UStaticMesh*>* Found = CachedMeshes.Find(Type))
		return *Found;

	TArray<FString> MeshPaths = GetMeshPathsForType(Type);
	TArray<UStaticMesh*> LoadedMeshes;

	for (const FString& Path : MeshPaths)
	{
		TSoftObjectPtr<UStaticMesh> SoftMesh(Path);
		if (UStaticMesh* Mesh = SoftMesh.LoadSynchronous())
		{
			LoadedMeshes.Add(Mesh);
		}
		else
		{
			UE_LOG(LogFactorySpawner, Warning, TEXT("Failed to load mesh for buildable type %d at %s"), (int32)Type, *Path);
		}
	}

	CachedMeshes.Add(Type, LoadedMeshes);
	return LoadedMeshes;
}
