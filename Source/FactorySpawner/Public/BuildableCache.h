#pragma once

#include "CoreMinimal.h"
#include "BuildPlanTypes.h"
#include "BuildableCache.generated.h"

class AFGBuildable;
class AFGBuildableManufacturer;
class UFGRecipe;

USTRUCT()
struct FWrongRecipe
{
    GENERATED_BODY()

    UPROPERTY()
    FString Name;

    UPROPERTY()
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
    template <typename T>
    TSubclassOf<T> GetBuildableClass(EBuildable Type);

    void SetBeltClass(int32 Tier);
    
    // Get the highest unlocked belt tier (1-6), defaults to 1 if none found
    int32 GetHighestUnlockedBeltTier(UWorld* World);
    
    void SetPipelineClass(int32 Tier);
    
    // Get the highest unlocked pipeline tier (1-2), defaults to 1 if none found
    int32 GetHighestUnlockedPipelineTier(UWorld* World);

    // Recipe loader
    TSubclassOf<UFGRecipe> GetRecipeClass(const FString& Recipe, TSubclassOf<AFGBuildableManufacturer> ProducedIn,
                                                 UWorld* World);

    void ClearCache();

  private:
    // Instance-level caches
    UPROPERTY()
    TMap<EBuildable, TSubclassOf<AFGBuildable>> CachedClasses;

    UPROPERTY()
    TMap<FString, TSubclassOf<UFGRecipe>> CachedRecipeClasses;

    UPROPERTY()
    TArray<FWrongRecipe> WrongRecipes;
};
