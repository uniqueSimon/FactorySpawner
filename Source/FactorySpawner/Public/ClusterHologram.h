#pragma once

#include "CoreMinimal.h"
#include "FGBuildableHologram.h"
#include "MyChatSubsystem.h"
#include "BuildableCache.h"
#include "ClusterHologram.generated.h"

struct FBuiltThing {
	AFGBuildable* Spawned = nullptr;
	EBuildable Buildable;
	FVector Position;
};

UCLASS()
class FACTORYSPAWNER_API AClusterHologram : public AFGBuildableHologram
{
	GENERATED_BODY()

public:
	virtual AActor* Construct(TArray< AActor* >& out_children, FNetConstructionID netConstructionID) override;

	UPROPERTY()
	TArray<AFGHologram*> SubHolograms;

	TArray< FItemAmount > GetBaseCost() const override;

	void SpawnBuildPlan(const FBuildPlan& Plan, UWorld* World, FTransform& ActorTransform);

	TMap<FGuid, FBuiltThing> SpawnBuildables(const TArray<FBuildableUnit>& BuildableUnits, UWorld* World, FTransform& ActorTransform);

protected:
	virtual void BeginPlay() override;

	UPROPERTY()
	TArray<UStaticMeshComponent*> PreviewMeshes;

};
