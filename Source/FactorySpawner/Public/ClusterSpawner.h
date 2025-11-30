#pragma once

#include "CoreMinimal.h"
#include "BuildPlanTypes.h"
#include "BuildableCache.h"

struct FBuiltThing
{
    AFGBuildable* Spawned = nullptr;
    EBuildable Buildable;
    FVector Position;
};

class FClusterSpawner
{
public:
    FClusterSpawner(UWorld* InWorld, UBuildableCache* InCache);

    /** Spawns everything from a BuildPlan */
    void SpawnBuildPlan(const FBuildPlan& Plan, const FTransform& BaseTransform);

private:
    /** Internal helpers */
    TMap<FGuid, FBuiltThing> SpawnBuildables(const TArray<FBuildableUnit>& Units, const FTransform& BaseTransform);
    void SpawnBelts(const TArray<FConnectionWithSocket>& BeltConnections, TMap<FGuid, FBuiltThing>& SpawnedActors);
    void SpawnWires(const TArray<FWireConnection>& WireConnections, const TMap<FGuid, FBuiltThing>& SpawnedActors);

private:
    UWorld* World;
    UBuildableCache* Cache;
};
