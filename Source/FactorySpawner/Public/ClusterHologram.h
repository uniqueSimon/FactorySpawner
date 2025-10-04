#pragma once

#include "CoreMinimal.h"
#include "FGBuildableHologram.h"
#include "BuildPlanTypes.h"
#include "ClusterHologram.generated.h"

class UBuildPlanGenerator;
class UBuildableCache;

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
    void BeginPlay() override;
    AActor* Construct(TArray<AActor*>& out_children, FNetConstructionID netConstructionID) override;
    TArray<FItemAmount> GetBaseCost() const override;

  private:
    UPROPERTY()
    AMyChatSubsystem* ChatSubsystemPointer = nullptr;

    UPROPERTY()
    UBuildPlanGenerator* GeneratorPointer = nullptr;

    UPROPERTY()
    UBuildableCache* CachePointer = nullptr;

    UPROPERTY()
    TArray<UStaticMeshComponent*> PreviewMeshes;

    void SpawnPreviewHologram();

    void SpawnBuildPlan();

    //helper functions
    void SpawnWires(UWorld* World, const TArray<FWireConnection>& WireConnections,
                    const TMap<FGuid, FBuiltThing>& SpawnedActors);
    void SpawnBelts(UWorld* World, const TArray<FBeltConnection>& BeltConnections,
                    TMap<FGuid, FBuiltThing>& SpawnedActors);
    TMap<FGuid, FBuiltThing> SpawnBuildables(const TArray<FBuildableUnit>& Units, UWorld* World,
                                             const FTransform& BaseTransform);
};
