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

    AMyChatSubsystem* Subsystem = AMyChatSubsystem::Get(GetWorld());
    if (Subsystem)
    {
        const FBuildPlan& BuildPlan = Subsystem->CurrentBuildPlan;
        UBuildableCache* BuildableCache = Subsystem->BuildableCache;

        for (const FBuildableUnit& Unit : BuildPlan.BuildableUnits)
        {
            TArray<UStaticMesh*> StaticMeshes = BuildableCache->GetStaticMesh(Unit.Buildable);
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
}

AActor* AClusterHologram::Construct(TArray<AActor*>& out_children, FNetConstructionID netConstructionID)
{
    AActor* Ret = Super::Construct(out_children, netConstructionID);

    SpawnBuildPlan();

    return Ret;
}

TArray<FItemAmount> AClusterHologram::GetBaseCost() const
{
    TArray<FItemAmount> TotalCost;

    UWorld* World = GetWorld();

    AMyChatSubsystem* Subsystem = AMyChatSubsystem::Get(World);
    const FBuildPlan& BuildPlan = Subsystem->CurrentBuildPlan;
    UBuildableCache* BuildableCache = Subsystem->BuildableCache;

    AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);

    for (const FBuildableUnit& Unit : BuildPlan.BuildableUnits)
    {
        TSubclassOf<AFGBuildable> BuildableClass = BuildableCache->GetBuildableClass(Unit.Buildable);
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