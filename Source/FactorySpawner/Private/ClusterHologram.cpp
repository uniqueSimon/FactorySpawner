#include "ClusterHologram.h"
#include "FGFactoryHologram.h"
#include "MyChatSubsystem.h"

#include "Hologram/FGBuildableHologram.h"
#include "Buildables/FGBuildableAttachmentSplitter.h"
#include "Buildables/FGBuildableAttachmentMerger.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "FGBuildableConveyorBelt.h"
#include "Tests/FGTestBlueprintFunctionLibrary.h"
#include "FGFactoryConnectionComponent.h"

#include "FGChatManager.h"
#include "Kismet/GameplayStatics.h"
#include "FactorySpawner.h"

#include "FGRecipe.h"
#include "FGBuildablePowerPole.h"
#include "FGBuildableWire.h"
#include "FGCircuitConnectionComponent.h"
#include "FGPowerConnectionComponent.h"
#include "ItemAmount.h"
#include "FGBuildingDescriptor.h"
#include "FGRecipeManager.h"
#include "FGBuildableConveyorLift.h"
#include "BuildPlanGenerator.h"

void AClusterHologram::BeginPlay()
{
    Super::BeginPlay();

    FBuildPlan BuildPlan = AMyChatSubsystem::CurrentBuildPlan;

    for (const FBuildableUnit& Unit : BuildPlan.BuildableUnits)
    {
        TArray<UStaticMesh*> StaticMeshes = UBuildableCache::GetStaticMesh(Unit.Buildable);
        for (UStaticMesh* StaticMesh : StaticMeshes)
        {
            if (StaticMesh)
            {
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
}

AActor* AClusterHologram::Construct(TArray<AActor*>& out_children, FNetConstructionID netConstructionID)
{
    AActor* Ret = Super::Construct(out_children, netConstructionID);

    FBuildPlan BuildPlan = AMyChatSubsystem::CurrentBuildPlan;

    UWorld* World = GetWorld();

    if (BuildPlan.BuildableUnits.Num() == 0)
    {
        FFactorySpawnerModule::ChatLog(World, "Nothing to build! Define first a factory with this command: "
                                              "/FactorySpawner <number> <machine type> <recipe>");
    }

    FTransform ActorTransform = GetActorTransform();

    SpawnBuildPlan(BuildPlan, World, ActorTransform);

    return Ret;
}

TArray<FItemAmount> AClusterHologram::GetBaseCost() const
{
    TArray<FItemAmount> TotalCost;

    FBuildPlan BuildPlan = AMyChatSubsystem::CurrentBuildPlan;

    UWorld* World = GetWorld();
    AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);

    for (const FBuildableUnit& Unit : BuildPlan.BuildableUnits)
    {
        TSubclassOf<AFGBuildable> BuildableClass = UBuildableCache::GetBuildableClass(Unit.Buildable);
        TSubclassOf<UFGBuildingDescriptor> DescriptorClass =
            RecipeManager->FindBuildingDescriptorByClass(BuildableClass);

        if (!DescriptorClass)
        {
            // If buildable has not been unlocked, the descriptor is null! I just skip that for now.
            continue;
        }
        TArray<TSubclassOf<UFGRecipe>> Recipes = RecipeManager->FindRecipesByProduct(DescriptorClass);

        TSubclassOf<UFGRecipe> Recipe = Recipes[0];
        UFGRecipe* RecipeCDO = Recipe->GetDefaultObject<UFGRecipe>();

        for (const FItemAmount& Item : RecipeCDO->GetIngredients())
        {
            FItemAmount* Existing =
                TotalCost.FindByPredicate([&](const FItemAmount& X) { return X.ItemClass == Item.ItemClass; });

            if (Existing)
            {
                Existing->Amount += Item.Amount;
            }
            else
            {
                TotalCost.Add(FItemAmount(Item.ItemClass, Item.Amount));
            }
        }
    }

    return TotalCost;
}