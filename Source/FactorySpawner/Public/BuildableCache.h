#pragma once

#include "CoreMinimal.h"
#include "MyChatSubsystem.h" //to get EBuildable
#include "BuildableCache.generated.h"

class AMyChatSubsystem;
class AFGBuildable;
class AFGBuildableConveyorBelt;
class AFGBuildableManufacturer;
class UFGRecipe;
class UStaticMesh;

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

    // Recipe loader
    TSubclassOf<UFGRecipe> GetRecipeClass(const FString& Recipe, TSubclassOf<AFGBuildableManufacturer> ProducedIn,
                                                 UWorld* World);

    // Mesh loader
    TArray<UStaticMesh*> GetStaticMesh(EBuildable Type);

    void ClearCache();

  private:
    // Instance-level caches
    UPROPERTY()
    TMap<EBuildable, TSubclassOf<AFGBuildable>> CachedClasses;

    UPROPERTY()
    TMap<FString, TSubclassOf<UFGRecipe>> CachedRecipeClasses;

    TMap<EBuildable, TArray<UStaticMesh*>> CachedMeshes;

    UPROPERTY()
    TArray<FWrongRecipe> WrongRecipes;
};
