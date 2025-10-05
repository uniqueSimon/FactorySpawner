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
    AMyChatSubsystem* ChatSubsystemPointer = nullptr;

    UPROPERTY()
    UBuildPlanGenerator* GeneratorPointer = nullptr;

    UPROPERTY()
    UBuildableCache* CachePointer = nullptr;

    UPROPERTY()
    TArray<UStaticMeshComponent*> PreviewMeshes;

    void SpawnPreviewHologram();
};
