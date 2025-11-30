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
#include "FGBuildablePipelineJunction.h"
#include "FGPipeConnectionComponent.h"
#include "FGBuildablePipeline.h"
#include "Kismet/GameplayStatics.h"
#include "FGTestBlueprintFunctionLibrary.h"

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

    template<typename T>
    TArray<T*> GetConnections(AFGBuildable* Buildable)
    {
        TArray<T*> Out;
        if (Buildable)
            Buildable->GetComponents<T>(Out);
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

    void ConnectBelt(TSubclassOf<AFGBuildableConveyorBelt> BeltClass, UFGFactoryConnectionComponent* From,
                     UFGFactoryConnectionComponent* To)
    {
        if (!BeltClass || !From || !To)
            return;

        const FVector FromLoc = From->GetComponentLocation();
        const FVector ToLoc = To->GetComponentLocation();
        const FVector TangentWorld = From->GetConnectorNormal() * FVector::Distance(FromLoc, ToLoc) * 1.5f;

        // Spawn the initial belt using the test helper
        AFGBuildable* Spawned = UFGTestBlueprintFunctionLibrary::SpawnSplineBuildable(BeltClass, From, To);
        AFGBuildableConveyorBelt* Belt = Cast<AFGBuildableConveyorBelt>(Spawned);
        if (!Belt)
            return;

        // Convert world-space positions/tangents into belt local space before resplining
        const FTransform BeltTransform = Belt->GetActorTransform();
        const FVector LocalFrom = BeltTransform.InverseTransformPosition(FromLoc);
        const FVector LocalTo = BeltTransform.InverseTransformPosition(ToLoc);
        const FVector LocalTangent = BeltTransform.InverseTransformVectorNoScale(TangentWorld);

        TArray<FSplinePointData> SplinePoints;
        SplinePoints.Add(FSplinePointData(LocalFrom, LocalTangent));
        SplinePoints.Add(FSplinePointData(LocalTo, LocalTangent));

        AFGBuildableConveyorBelt::Respline(Belt, SplinePoints);
    }

    void ConnectPipe(TSubclassOf<AFGBuildablePipeline> PipeClass, UFGPipeConnectionComponent* From,
                     UFGPipeConnectionComponent* To)
    {
        if (!PipeClass || !From || !To)
            return;

        UFGTestBlueprintFunctionLibrary::SpawnSplineBuildable(PipeClass, From, To);
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
    SpawnPipes(Plan.PipeConnections, Spawned);
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
        // Decide flip for certain buildables
        const bool bFlip = Unit.Buildable == EBuildable::Merger || Unit.Buildable == EBuildable::OilRefinery;
        const FTransform UnitTransform = MoveTransform(BaseTransform, Unit.Location, bFlip);

        AFGBuildable* Spawned = nullptr;

        if (Unit.Buildable <= LastMachineType)
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
            if (Class)
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

        auto Resolve = [](const FBuiltThing& Thing) -> UFGPowerConnectionComponent*
        {
            if (Thing.Buildable <= LastMachineType)
                return GetPowerConnection(Thing.Spawned);

            if (auto* Pole = Cast<AFGBuildablePowerPole>(Thing.Spawned))
                return Pole->GetPowerConnection(0);

            return nullptr;
        };

        ConnectWire(World, WireClass, Resolve(*From), Resolve(*To));
    }
}

void FClusterSpawner::SpawnBelts(const TArray<FConnectionWithSocket>& BeltConnections,
                                 TMap<FGuid, FBuiltThing>& SpawnedActors)
{
    TSubclassOf<AFGBuildableConveyorBelt> BeltClass =
        Cache->GetBuildableClass<AFGBuildableConveyorBelt>(EBuildable::Belt);
    if (!BeltClass)
        return;

    for (const FConnectionWithSocket& Belt : BeltConnections)
    {
        AFGBuildable* From = SpawnedActors.FindRef(Belt.FromUnit).Spawned;
        AFGBuildable* To = SpawnedActors.FindRef(Belt.ToUnit).Spawned;
        if (!From || !To)
            continue;

        const auto FromConns = GetConnections<UFGFactoryConnectionComponent>(From);
        const auto ToConns = GetConnections<UFGFactoryConnectionComponent>(To);

        if (!FromConns.IsValidIndex(Belt.FromSocket) || !ToConns.IsValidIndex(Belt.ToSocket))
            continue;

        ConnectBelt(BeltClass, FromConns[Belt.FromSocket], ToConns[Belt.ToSocket]);
    }
}

void FClusterSpawner::SpawnPipes(const TArray<FConnectionWithSocket>& PipeConnections,
                                 TMap<FGuid, FBuiltThing>& SpawnedActors)
{
    TSubclassOf<AFGBuildablePipeline> PipeClass = Cache->GetBuildableClass<AFGBuildablePipeline>(EBuildable::Pipeline);

    for (const FConnectionWithSocket& P : PipeConnections)
    {
        AFGBuildable* From = SpawnedActors.FindRef(P.FromUnit).Spawned;
        AFGBuildable* To = SpawnedActors.FindRef(P.ToUnit).Spawned;
        if (!From || !To)
            continue;

        const auto FromConns = GetConnections<UFGPipeConnectionComponent>(From);
        const auto ToConns = GetConnections<UFGPipeConnectionComponent>(To);

        if (!FromConns.IsValidIndex(P.FromSocket) || !ToConns.IsValidIndex(P.ToSocket))
            continue;

        ConnectPipe(PipeClass, FromConns[P.FromSocket], ToConns[P.ToSocket]);
    }
}
