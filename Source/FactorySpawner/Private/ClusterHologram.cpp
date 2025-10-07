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
#include "BuildableCache.h"
#include "FGBuildingDescriptor.h"
#include "ClusterSpawner.h"

void AClusterHologram::BeginPlay()
{
    Super::BeginPlay();

    SpawnPreviewHologram();
}

void AClusterHologram::SpawnPreviewHologram()
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    ChatSubsystemPointer = AMyChatSubsystem::Get(World);
    if (!ChatSubsystemPointer)
    {
        UE_LOG(LogTemp, Warning, TEXT("No ChatSubsystem found!"));
        return;
    }

    UBuildableCache* CachePointer = ChatSubsystemPointer->GetCache();
    const FBuildPlan& BuildPlan = ChatSubsystemPointer->GetBuildPlan();

    if (!CachePointer)
    {
        UE_LOG(LogTemp, Warning, TEXT("BuildableCache not initialized."));
        return;
    }

    for (const FBuildableUnit& Unit : BuildPlan.BuildableUnits)
    {
        const TArray<UStaticMesh*>& StaticMeshes = CachePointer->GetStaticMesh(Unit.Buildable);
        for (UStaticMesh* StaticMesh : StaticMeshes)
        {
            if (!StaticMesh)
                continue;

            // Create mesh component
            UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this);
            MeshComp->SetStaticMesh(StaticMesh);
            MeshComp->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
            MeshComp->SetRelativeLocation(Unit.Location);
            if (Unit.Buildable == EBuildable::Smelter)
            {
                FRotator BaseRotation = RootComponent->GetComponentRotation();
                BaseRotation.Yaw += 180.0f;
                MeshComp->SetRelativeRotation(BaseRotation);
            }
            MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            MeshComp->RegisterComponent();

            PreviewMeshes.Add(MeshComp);
        }
    }
}

AActor* AClusterHologram::Construct(TArray<AActor*>& OutChildren, FNetConstructionID NetConstructionID)
{
    AActor* Ret = Super::Construct(OutChildren, NetConstructionID);

    if (!ChatSubsystemPointer)
        return Ret;

    UBuildableCache* CachePointer = ChatSubsystemPointer->GetCache();
    const FBuildPlan& BuildPlan = ChatSubsystemPointer->GetBuildPlan();

    UWorld* World = GetWorld();
    if (!World)
        return Ret;

    FClusterSpawner Spawner(World, CachePointer);
    Spawner.SpawnBuildPlan(BuildPlan, GetActorTransform());

    return Ret;
}

TArray<FItemAmount> AClusterHologram::GetBaseCost() const
{
    TArray<FItemAmount> TotalCost;

    if (!ChatSubsystemPointer)
        return TotalCost;

    UBuildableCache* CachePointer = ChatSubsystemPointer->GetCache();
    const FBuildPlan& BuildPlan = ChatSubsystemPointer->GetBuildPlan();

    UWorld* World = GetWorld();
    if (!World)
        return TotalCost;

    AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);
    if (!RecipeManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("ClusterHologram: No RecipeManager found!"));
        return TotalCost;
    }

    for (const FBuildableUnit& Unit : BuildPlan.BuildableUnits)
    {
        TSubclassOf<AFGBuildable> BuildableClass = CachePointer->GetBuildableClass<AFGBuildable>(Unit.Buildable);
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
