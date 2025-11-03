#pragma once

#include "CoreMinimal.h"
#include "FGBuildableHologram.h"
#include "BuildPlanTypes.h"
#include "ClusterHologram.generated.h"

class UBuildPlanGenerator;
class UBuildableCache;

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
    AFactorySpawnerChat* ChatSubsystemPointer = nullptr;

    UPROPERTY()
    TArray<UStaticMeshComponent*> PreviewMeshes;

    void SpawnPreviewHologram();
};
