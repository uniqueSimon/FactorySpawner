#include "ClusterHologram.h"
#include "FactorySpawner.h"
#include "BuildableCache.h"
#include "BuildPlanGenerator.h"
#include "MyChatSubsystem.h"

#include "FGBuildableManufacturer.h"
#include "FGBuildableAttachmentSplitter.h"
#include "FGBuildableAttachmentMerger.h"
#include "FGBuildablePowerPole.h"
#include "FGBuildableConveyorBelt.h"
#include "FGBuildableWire.h"
#include "FGPowerConnectionComponent.h"
#include "FGFactoryConnectionComponent.h"
#include "FGRecipeManager.h"
#include "FGPlayerController.h"
#include "FGCharacterPlayer.h"

#include "Kismet/GameplayStatics.h"

namespace
{
    /** Precomputed rotation for flipping merger/splitter direction. */
    const FQuat Rot180 = FQuat(FVector::UpVector, PI);

    FTransform MoveTransform(const FTransform& Original, const FVector& Offset, bool bRotate180 = false)
    {
        const FVector NewLocation = Original.TransformPosition(Offset);
        const FQuat Rotation = bRotate180 ? Rot180 * Original.GetRotation() : Original.GetRotation();
        return FTransform(Rotation, NewLocation);
    }

    TArray<UFGFactoryConnectionComponent*> GetBeltConnections(AFGBuildable* Buildable)
    {
        TArray<UFGFactoryConnectionComponent*> Connections;
        if (Buildable)
        {
            Buildable->GetComponents<UFGFactoryConnectionComponent>(Connections);
        }
        return Connections;
    }

    UFGPowerConnectionComponent* GetPowerConnection(AFGBuildable* Buildable)
    {
        if (!Buildable)
            return nullptr;

        TArray<UFGPowerConnectionComponent*> PowerConnections;
        Buildable->GetComponents<UFGPowerConnectionComponent>(PowerConnections);
        return PowerConnections.IsValidIndex(0) ? PowerConnections[0] : nullptr;
    }

    void ConnectWire(UWorld* World, TSubclassOf<AFGBuildableWire> WireClass, UFGPowerConnectionComponent* First,
                     UFGPowerConnectionComponent* Second)
    {
        if (!World || !WireClass || !First || !Second)
            return;

        if (AFGBuildableWire* Wire = World->SpawnActor<AFGBuildableWire>(WireClass, FTransform::Identity))
        {
            Wire->Connect(First, Second);
        }
    }

    void ConnectBelt(AFGBuildableConveyorBelt* Belt, UFGFactoryConnectionComponent* From,
                     UFGFactoryConnectionComponent* To)
    {
        if (!Belt || !From || !To)
            return;

        const FVector FromLoc = From->GetComponentLocation();
        const FVector FromDir = From->GetConnectorNormal();
        const FVector ToLoc = To->GetComponentLocation();

        const float Dist = FVector::Distance(FromLoc, ToLoc);
        const FVector Tangent = FromDir * Dist * 1.5f;

        TArray<FSplinePointData> SplinePoints;
        SplinePoints.Add(FSplinePointData(FromLoc, Tangent));
        SplinePoints.Add(FSplinePointData(ToLoc, Tangent));

        From->SetConnection(Belt->GetConnection0());
        To->SetConnection(Belt->GetConnection1());

        AFGBuildableConveyorBelt::Respline(Belt, SplinePoints);
    }
} // namespace

// ------------------------------------------------------------
//                      Main Logic
// ------------------------------------------------------------

void AClusterHologram::SpawnWires(UWorld* World, const TArray<FWireConnection>& WireConnections,
                                  const TMap<FGuid, FBuiltThing>& SpawnedActors)
{
    if (!World || !CachePointer)
        return;

    TSubclassOf<AFGBuildableWire> WireClass = CachePointer->GetBuildableClass<AFGBuildableWire>(EBuildable::PowerLine);

    for (const FWireConnection& Wire : WireConnections)
    {
        const FBuiltThing* From = SpawnedActors.Find(Wire.FromUnit);
        const FBuiltThing* To = SpawnedActors.Find(Wire.ToUnit);
        if (!From || !To)
            continue;

        auto ResolvePowerConn = [](const FBuiltThing& Thing) -> UFGPowerConnectionComponent*
        {
            if (static_cast<uint8>(Thing.Buildable) <= static_cast<uint8>(EMachineType::Manufacturer))
                return GetPowerConnection(Thing.Spawned);

            if (AFGBuildablePowerPole* Pole = Cast<AFGBuildablePowerPole>(Thing.Spawned))
                return Pole->GetPowerConnection(0);

            return nullptr;
        };

        ConnectWire(World, WireClass, ResolvePowerConn(*From), ResolvePowerConn(*To));
    }
}

void AClusterHologram::SpawnBelts(UWorld* World, const TArray<FBeltConnection>& BeltConnections,
                                  TMap<FGuid, FBuiltThing>& SpawnedActors)
{
    if (!World || !CachePointer)
        return;

    TSubclassOf<AFGBuildableConveyorBelt> BeltClass =
        CachePointer->GetBuildableClass<AFGBuildableConveyorBelt>(EBuildable::Belt);

    for (const FBeltConnection& Belt : BeltConnections)
    {
        AFGBuildable* From = SpawnedActors.FindRef(Belt.FromUnit).Spawned;
        AFGBuildable* To = SpawnedActors.FindRef(Belt.ToUnit).Spawned;
        if (!From || !To)
            continue;

        const auto ConnectionsFrom = GetBeltConnections(From);
        const auto ConnectionsTo = GetBeltConnections(To);

        if (!ConnectionsFrom.IsValidIndex(Belt.FromSocket) || !ConnectionsTo.IsValidIndex(Belt.ToSocket))
            continue;

        UFGFactoryConnectionComponent* FromConn = ConnectionsFrom[Belt.FromSocket];
        UFGFactoryConnectionComponent* ToConn = ConnectionsTo[Belt.ToSocket];

        if (AFGBuildableConveyorBelt* SpawnedBelt = World->SpawnActor<AFGBuildableConveyorBelt>(BeltClass))
        {
            ConnectBelt(SpawnedBelt, FromConn, ToConn);
        }
    }
}

TMap<FGuid, FBuiltThing> AClusterHologram::SpawnBuildables(const TArray<FBuildableUnit>& Units, UWorld* World,
                                                           const FTransform& BaseTransform)
{
    TMap<FGuid, FBuiltThing> Result;
    if (!CachePointer || !World)
        return Result;

    AFGPlayerController* PC = Cast<AFGPlayerController>(World->GetFirstPlayerController());
    if (!PC)
        return Result;

    AFGCharacterPlayer* Player = Cast<AFGCharacterPlayer>(PC->GetCharacter());
    UFGManufacturerClipboardRCO* RCO = PC->GetRemoteCallObjectOfClass<UFGManufacturerClipboardRCO>();

    for (const FBuildableUnit& Unit : Units)
    {
        const bool bRotate = Unit.Buildable == EBuildable::Merger;
        const FTransform UnitTransform = MoveTransform(BaseTransform, Unit.Location, bRotate);

        AFGBuildable* Spawned = nullptr;

        // Spawn logic based on type
        if (Unit.Buildable <= EBuildable::Manufacturer)
        {
            TSubclassOf<AFGBuildableManufacturer> MachineClass =
                CachePointer->GetBuildableClass<AFGBuildableManufacturer>(Unit.Buildable);
            if (!MachineClass)
                continue;

            Spawned = World->SpawnActor<AFGBuildable>(MachineClass, UnitTransform);

            if (Unit.Recipe.IsSet())
            {
                const FString RecipeName = Unit.Recipe.GetValue();
                TSubclassOf<UFGRecipe> RecipeClass = CachePointer->GetRecipeClass(RecipeName, MachineClass, World);
                if (RecipeClass)
                {
                    AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Spawned);
                    if (Manufacturer)
                    {
                        if (Unit.Underclock.IsSet() && RCO && Player)
                            RCO->Server_PasteSettings(Manufacturer, Player, RecipeClass,
                                                      Unit.Underclock.GetValue() / 100.0f, 1.0f, nullptr, nullptr);
                        else
                            Manufacturer->SetRecipe(RecipeClass);
                    }
                }
            }
        }
        else
        {
            TSubclassOf<AFGBuildable> BuildableClass = CachePointer->GetBuildableClass<AFGBuildable>(Unit.Buildable);
            Spawned = World->SpawnActor<AFGBuildable>(BuildableClass, UnitTransform);
        }

        if (Spawned)
        {
            Result.Add(Unit.Id, {Spawned, Unit.Buildable, Unit.Location});
        }
    }

    return Result;
}

void AClusterHologram::SpawnBuildPlan()
{
    if (!GeneratorPointer || !CachePointer)
        return;

    UWorld* World = GetWorld();
    if (!World)
        return;

    const FBuildPlan& Plan = GeneratorPointer->GetCurrentBuildPlan();

    if (Plan.BuildableUnits.IsEmpty())
    {
        FFactorySpawnerModule::ChatLog(
            World, TEXT("Nothing to build! Define a factory first with: /FactorySpawner <number> <machine> <recipe>"));
        return;
    }

    const FTransform ActorTransform = GetActorTransform();
    TMap<FGuid, FBuiltThing> SpawnedActors = SpawnBuildables(Plan.BuildableUnits, World, ActorTransform);

    SpawnWires(World, Plan.WireConnections, SpawnedActors);
    SpawnBelts(World, Plan.BeltConnections, SpawnedActors);
}
