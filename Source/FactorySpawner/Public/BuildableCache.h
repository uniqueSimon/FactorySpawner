#pragma once

#include "CoreMinimal.h"
#include "MyChatSubsystem.h"
#include "FGBuildable.h"
#include "FGBuildableManufacturer.h"
#include "BuildableCache.generated.h"

class AFGBuildableConveyorBelt;
class UFGRecipe;
class UStaticMesh;

struct FWrongRecipe
{
	FString Name;
	FString ProducedIn;
};

/**
 * Helper class for lazy-loading and caching buildable classes
 */
UCLASS()
class FACTORYSPAWNER_API UBuildableCache : public UObject
{
	GENERATED_BODY()

public:
	// Get (and cache) the buildable class for a given type
	static TSubclassOf<AFGBuildable> GetBuildableClass(EBuildable Type);

	static void SetBeltClass(int32 Tier);

	// Get (and cache) the recipe class for a given name and where it is produced
	static TSubclassOf<UFGRecipe> GetRecipeClass(FString& Recipe, TSubclassOf<AFGBuildableManufacturer> ProducedIn, UWorld* World);

	// Get (and cache) the recipe class for a given name and where it is produced
	static TArray<UStaticMesh*> GetStaticMesh(EBuildable Type);
private:
	// Internal cache
	static TMap<EBuildable, TSubclassOf<AFGBuildable>> CachedClasses;
	static TMap<FString, TSubclassOf<UFGRecipe>> CachedRecipeClasses;
	static TMap<EBuildable, TArray<UStaticMesh*>> CachedMeshes;

	static TArray<FWrongRecipe> WrongRecipes;
};
