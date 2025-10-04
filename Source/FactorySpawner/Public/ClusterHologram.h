#pragma once

#include "CoreMinimal.h"
#include "FGBuildableHologram.h"
#include "MyChatSubsystem.h"
#include "BuildableCache.h"
#include "ClusterHologram.generated.h"

struct FBuiltThing
{
    AFGBuildable* Spawned = nullptr;
    EBuildable Buildable;
    FVector Position;
};

UCLASS()
class FACTORYSPAWNER_API AClusterHologram : public AFGBuildableHologram
{
    GENERATED_BODY()

  public:
    UPROPERTY()
    AMyChatSubsystem* ChatSubsystem;

    virtual AActor* Construct(TArray<AActor*>& out_children, FNetConstructionID netConstructionID) override;

    UPROPERTY()
    TArray<AFGHologram*> SubHolograms;

    TArray<FItemAmount> GetBaseCost() const override;

    void SpawnBuildPlan();

    TMap<FGuid, FBuiltThing> SpawnBuildables(const TArray<FBuildableUnit>& BuildableUnits, UWorld* World,
                                             FTransform& ActorTransform, UBuildableCache* BuildableCache);

  protected:
    virtual void BeginPlay() override;

    UPROPERTY()
    TArray<UStaticMeshComponent*> PreviewMeshes;
};
