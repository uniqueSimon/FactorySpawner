#include "ClusterHologram.h"
#include "BuildPlanGenerator.h"
#include "MyChatSubsystem.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "FGBuildableConveyorBelt.h"
#include "FGRecipeManager.h"
#include "FGRecipe.h"
#include "FGBuildablePowerPole.h"
#include "FGBuildableWire.h"
#include "Kismet/GameplayStatics.h"
#include "Components/StaticMeshComponent.h"
#include "FGBuildingDescriptor.h"

void AClusterHologram::BeginPlay()
{
    Super::BeginPlay();

    // Cache subsystem and build plan once
    AMyChatSubsystem* Subsystem = AMyChatSubsystem::Get(GetWorld());
    if (!Subsystem || !Subsystem->BuildableCache)
    {
        UE_LOG(LogTemp, Warning, TEXT("ClusterHologram: No chat subsystem or buildable cache found!"));
        return;
    }

    const FBuildPlan& BuildPlan = Subsystem->CurrentBuildPlan;
    UBuildableCache* BuildableCache = Subsystem->BuildableCache;

    for (const FBuildableUnit& Unit : BuildPlan.BuildableUnits)
    {
        const TArray<UStaticMesh*>& StaticMeshes = BuildableCache->GetStaticMesh(Unit.Buildable);
        for (UStaticMesh* StaticMesh : StaticMeshes)
        {
            if (!StaticMesh)
                continue;

            // Create mesh component
            UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this);
            MeshComp->SetStaticMesh(StaticMesh);
            MeshComp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
            MeshComp->SetRelativeLocation(Unit.Location);
            MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            MeshComp->RegisterComponent();

            PreviewMeshes.Add(MeshComp);
        }
    }
}

AActor* AClusterHologram::Construct(TArray<AActor*>& OutChildren, FNetConstructionID NetConstructionID)
{
    AActor* Ret = Super::Construct(OutChildren, NetConstructionID);
    SpawnBuildPlan();
    return Ret;
}

TArray<FItemAmount> AClusterHologram::GetBaseCost() const
{
    TArray<FItemAmount> TotalCost;

    UWorld* World = GetWorld();
    if (!World)
        return TotalCost;

    AMyChatSubsystem* Subsystem = AMyChatSubsystem::Get(World);
    if (!Subsystem || !Subsystem->BuildableCache)
    {
        UE_LOG(LogTemp, Warning, TEXT("ClusterHologram: No chat subsystem or buildable cache found!"));
        return TotalCost;
    }

    const FBuildPlan& BuildPlan = Subsystem->CurrentBuildPlan;
    UBuildableCache* BuildableCache = Subsystem->BuildableCache;
    AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);
    if (!RecipeManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("ClusterHologram: No RecipeManager found!"));
        return TotalCost;
    }

    for (const FBuildableUnit& Unit : BuildPlan.BuildableUnits)
    {
        TSubclassOf<AFGBuildable> BuildableClass = BuildableCache->GetBuildableClass(Unit.Buildable);
        if (!BuildableClass)
            continue;

        TSubclassOf<UFGBuildingDescriptor> DescriptorClass =
            RecipeManager->FindBuildingDescriptorByClass(BuildableClass);
        if (!DescriptorClass)
            continue;

        TArray<TSubclassOf<UFGRecipe>> Recipes = RecipeManager->FindRecipesByProduct(DescriptorClass);
        if (Recipes.Num() == 0)
            continue;

        TSubclassOf<UFGRecipe> Recipe = Recipes[0];
        if (!Recipe)
            continue;

        UFGRecipe* RecipeCDO = Recipe->GetDefaultObject<UFGRecipe>();
        if (!RecipeCDO)
            continue;

        // Aggregate items
        for (const FItemAmount& Item : RecipeCDO->GetIngredients())
        {
            if (FItemAmount* Existing =
                    TotalCost.FindByPredicate([&](const FItemAmount& X) { return X.ItemClass == Item.ItemClass; }))
            {
                Existing->Amount += Item.Amount;
            }
            else
            {
                TotalCost.Add(Item);
            }
        }
    }

    return TotalCost;
}
