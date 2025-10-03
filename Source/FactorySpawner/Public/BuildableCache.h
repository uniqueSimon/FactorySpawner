#pragma once

#include "CoreMinimal.h"
#include "MyChatSubsystem.h" //to get EBuildable
#include "BuildableCache.generated.h"

class AFGBuildable;
class AFGBuildableConveyorBelt;
class AFGBuildableManufacturer;
class UFGRecipe;
class UStaticMesh;

struct FWrongRecipe
{
	FString Name;
	FString ProducedIn;
};

/**
 * Helper class for lazy-loading and caching buildable classes, recipes, and meshes
 */
UCLASS()
class FACTORYSPAWNER_API UBuildableCache : public UObject
{
	GENERATED_BODY()

public:
	// Buildable class loader
	static TSubclassOf<AFGBuildable> GetBuildableClass(EBuildable Type);

	static void SetBeltClass(int32 Tier);

	// Recipe loader
	static TSubclassOf<UFGRecipe> GetRecipeClass(FString& Recipe, TSubclassOf<AFGBuildableManufacturer> ProducedIn, UWorld* World);

	// Mesh loader
	static TArray<UStaticMesh*> GetStaticMesh(EBuildable Type);

private:
	// Internal caches
	static TMap<EBuildable, TSubclassOf<AFGBuildable>> CachedClasses;
	static TMap<FString, TSubclassOf<UFGRecipe>> CachedRecipeClasses;
	static TMap<EBuildable, TArray<UStaticMesh*>> CachedMeshes;

	static TArray<FWrongRecipe> WrongRecipes;
};
