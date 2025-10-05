#include "ClusterSpawner.h"
#include "FactorySpawner.h"

#include "FGBuildableManufacturer.h"
#include "FGBuildableConveyorBelt.h"
#include "FGBuildablePowerPole.h"
#include "FGBuildableWire.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPowerConnectionComponent.h"
#include "FGRecipeManager.h"
#include "FGPlayerController.h"
#include "FGCharacterPlayer.h"

// ------------------------------------------------------------
//                  Local Utility Functions
// ------------------------------------------------------------

namespace
{
    const FQuat Rot180 = FQuat(FVector::UpVector, PI);

    FTransform MoveTransform(const FTransform& Base, const FVector& Offset, bool bFlip = false)
    {
        const FVector NewLoc = Base.TransformPosition(Offset);
        const FQuat NewRot = bFlip ? Rot180 * Base.GetRotation() : Base.GetRotation();
        return FTransform(NewRot, NewLoc);
    }

    TArray<UFGFactoryConnectionComponent*> GetBeltConnections(AFGBuildable* Buildable)
    {
        TArray<UFGFactoryConnectionComponent*> Out;
        if (Buildable)
            Buildable->GetComponents<UFGFactoryConnectionComponent>(Out);
        return Out;
    }

    UFGPowerConnectionComponent* GetPowerConnection(AFGBuildable* Buildable)
    {
        if (!Buildable)
            return nullptr;

        TArray<UFGPowerConnectionComponent*> PowerConnections;
        Buildable->GetComponents<UFGPowerConnectionComponent>(PowerConnections);
        return PowerConnections.IsValidIndex(0) ? PowerConnections[0] : nullptr;
    }

    void ConnectWire(UWorld* World, TSubclassOf<AFGBuildableWire> WireClass, UFGPowerConnectionComponent* A,
                     UFGPowerConnectionComponent* B)
    {
        if (!World || !WireClass || !A || !B)
            return;

        if (AFGBuildableWire* Wire = World->SpawnActor<AFGBuildableWire>(WireClass, FTransform::Identity))
        {
            Wire->Connect(A, B);
        }
    }

    void ConnectBelt(AFGBuildableConveyorBelt* Belt, UFGFactoryConnectionComponent* From,
                     UFGFactoryConnectionComponent* To)
    {
        if (!Belt || !From || !To)
            return;

        const FVector FromLoc = From->GetComponentLocation();
        const FVector ToLoc = To->GetComponentLocation();
        const FVector Tangent = From->GetConnectorNormal() * FVector::Distance(FromLoc, ToLoc) * 1.5f;

        TArray<FSplinePointData> SplinePoints;
        SplinePoints.Add(FSplinePointData(FromLoc, Tangent));
        SplinePoints.Add(FSplinePointData(ToLoc, Tangent));

        From->SetConnection(Belt->GetConnection0());
        To->SetConnection(Belt->GetConnection1());

        AFGBuildableConveyorBelt::Respline(Belt, SplinePoints);
    }
} // namespace

// ------------------------------------------------------------
//                  FClusterSpawner Implementation
// ------------------------------------------------------------

FClusterSpawner::FClusterSpawner(UWorld* InWorld, UBuildableCache* InCache) : World(InWorld), Cache(InCache)
{
}

void FClusterSpawner::SpawnBuildPlan(const FBuildPlan& Plan, const FTransform& BaseTransform)
{
    if (!World || !Cache)
        return;

    if (Plan.BuildableUnits.IsEmpty())
    {
        FFactorySpawnerModule::ChatLog(World, TEXT("Nothing to build! Define a factory first."));
        return;
    }

    TMap<FGuid, FBuiltThing> Spawned = SpawnBuildables(Plan.BuildableUnits, BaseTransform);
    SpawnWires(Plan.WireConnections, Spawned);
    SpawnBelts(Plan.BeltConnections, Spawned);
}

TMap<FGuid, FBuiltThing> FClusterSpawner::SpawnBuildables(const TArray<FBuildableUnit>& Units,
                                                          const FTransform& BaseTransform)
{
    TMap<FGuid, FBuiltThing> Result;
    if (!World || !Cache)
        return Result;

    AFGPlayerController* PC = Cast<AFGPlayerController>(World->GetFirstPlayerController());
    if (!PC)
        return Result;

    AFGCharacterPlayer* Player = Cast<AFGCharacterPlayer>(PC->GetCharacter());
    UFGManufacturerClipboardRCO* RCO = PC->GetRemoteCallObjectOfClass<UFGManufacturerClipboardRCO>();

    for (const FBuildableUnit& Unit : Units)
    {
        const bool bFlip = Unit.Buildable == EBuildable::Merger;
        const FTransform UnitTransform = MoveTransform(BaseTransform, Unit.Location, bFlip);

        AFGBuildable* Spawned = nullptr;

        if (Unit.Buildable <= EBuildable::Manufacturer)
        {
            // Machine types
            TSubclassOf<AFGBuildableManufacturer> MachineClass =
                Cache->GetBuildableClass<AFGBuildableManufacturer>(Unit.Buildable);

            if (!MachineClass)
                continue;

            Spawned = World->SpawnActor<AFGBuildable>(MachineClass, UnitTransform);

            if (Spawned && Unit.Recipe.IsSet())
            {
                TSubclassOf<UFGRecipe> RecipeClass = Cache->GetRecipeClass(Unit.Recipe.GetValue(), MachineClass, World);
                if (RecipeClass)
                {
                    AFGBuildableManufacturer* M = Cast<AFGBuildableManufacturer>(Spawned);
                    if (M)
                    {
                        if (Unit.Underclock.IsSet() && RCO && Player)
                            RCO->Server_PasteSettings(M, Player, RecipeClass, Unit.Underclock.GetValue() / 100.0f, 1.0f,
                                                      nullptr, nullptr);
                        else
                            M->SetRecipe(RecipeClass);
                    }
                }
            }
        }
        else
        {
            // Non-machine buildables
            TSubclassOf<AFGBuildable> Class = Cache->GetBuildableClass<AFGBuildable>(Unit.Buildable);
            Spawned = World->SpawnActor<AFGBuildable>(Class, UnitTransform);
        }

        if (Spawned)
            Result.Add(Unit.Id, {Spawned, Unit.Buildable, Unit.Location});
    }

    return Result;
}

void FClusterSpawner::SpawnWires(const TArray<FWireConnection>& WireConnections,
                                 const TMap<FGuid, FBuiltThing>& SpawnedActors)
{
    TSubclassOf<AFGBuildableWire> WireClass = Cache->GetBuildableClass<AFGBuildableWire>(EBuildable::PowerLine);
    if (!WireClass)
        return;

    for (const FWireConnection& Wire : WireConnections)
    {
        const FBuiltThing* From = SpawnedActors.Find(Wire.FromUnit);
        const FBuiltThing* To = SpawnedActors.Find(Wire.ToUnit);
        if (!From || !To)
            continue;

        auto Resolve = [](const FBuiltThing& Thing)
        {
            if (Thing.Buildable <= EBuildable::Manufacturer)
                return GetPowerConnection(Thing.Spawned);

            if (auto* Pole = Cast<AFGBuildablePowerPole>(Thing.Spawned))
                return Pole->GetPowerConnection(0);

            return static_cast<UFGPowerConnectionComponent*>(nullptr);
        };

        ConnectWire(World, WireClass, Resolve(*From), Resolve(*To));
    }
}

void FClusterSpawner::SpawnBelts(const TArray<FBeltConnection>& BeltConnections,
                                 TMap<FGuid, FBuiltThing>& SpawnedActors)
{
    TSubclassOf<AFGBuildableConveyorBelt> BeltClass =
        Cache->GetBuildableClass<AFGBuildableConveyorBelt>(EBuildable::Belt);
    if (!BeltClass)
        return;

    for (const FBeltConnection& Belt : BeltConnections)
    {
        AFGBuildable* From = SpawnedActors.FindRef(Belt.FromUnit).Spawned;
        AFGBuildable* To = SpawnedActors.FindRef(Belt.ToUnit).Spawned;
        if (!From || !To)
            continue;

        const auto FromConns = GetBeltConnections(From);
        const auto ToConns = GetBeltConnections(To);

        if (!FromConns.IsValidIndex(Belt.FromSocket) || !ToConns.IsValidIndex(Belt.ToSocket))
            continue;

        if (AFGBuildableConveyorBelt* NewBelt = World->SpawnActor<AFGBuildableConveyorBelt>(BeltClass))
        {
            ConnectBelt(NewBelt, FromConns[Belt.FromSocket], ToConns[Belt.ToSocket]);
        }
    }
}
