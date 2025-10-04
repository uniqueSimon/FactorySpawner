#include "ClusterHologram.h"
#include "FactorySpawner.h"
#include "BuildableCache.h"

#include "FGBuildableManufacturer.h"
#include "FGBuildableAttachmentSplitter.h"
#include "FGBuildableAttachmentMerger.h"
#include "FGBuildablePowerPole.h"
#include "FGBuildableConveyorBelt.h"
#include "FGBuildableWire.h"

#include "FGPowerConnectionComponent.h"

#include "FGRecipeManager.h"
#include "FGTestBlueprintFunctionLibrary.h"
#include "FGPlayerController.h"
#include "BuildPlanGenerator.h"

namespace
{
    FQuat Rot180 = FQuat(FVector::UpVector, PI);

    FTransform MoveTransform(const FTransform& Original, const FVector& Offset, bool bRotate180 = false)
    {
        FVector NewLocation = Original.TransformPosition(Offset);
        FQuat Rotation = Original.GetRotation();
        return FTransform(bRotate180 ? Rot180 * Rotation : Rotation, NewLocation);
    }

    TArray<UFGFactoryConnectionComponent*> GetBeltConnections(AFGBuildable* Buildable)
    {
        TArray<UFGFactoryConnectionComponent*> Connections;
        Buildable->GetComponents<UFGFactoryConnectionComponent>(Connections);
        return Connections;
    }

    UFGPowerConnectionComponent* GetMachinePowerConnection(AFGBuildable* Buildable)
    {
        TArray<UFGPowerConnectionComponent*> PowerConnections;
        Buildable->GetComponents<UFGPowerConnectionComponent>(PowerConnections);
        return PowerConnections[0];
    }

    void ConnectWithWire(UWorld* World, TSubclassOf<AFGBuildableWire> PowerLine, UFGPowerConnectionComponent* First,
                         UFGPowerConnectionComponent* Second)
    {
        AFGBuildableWire* Wire = World->SpawnActor<AFGBuildableWire>(PowerLine, FTransform::Identity);
        if (First && Second)
        {
            Wire->Connect(First, Second);
        }
    }

    void SpawnWires(UWorld* World, const TArray<FWireConnection>& WireConnections,
                    const TMap<FGuid, FBuiltThing>& SpawnedActors, UBuildableCache* BuildableCache)
    {
        UClass* BaseClass = BuildableCache->GetBuildableClass(EBuildable::PowerLine);
        TSubclassOf<AFGBuildableWire> WireClass = BaseClass;

        for (const FWireConnection& Wire : WireConnections)
        {
            FBuiltThing From = SpawnedActors.FindRef(Wire.FromUnit);
            FBuiltThing To = SpawnedActors.FindRef(Wire.ToUnit);

            bool bIsMachine1 = static_cast<uint8>(From.Buildable) <= static_cast<uint8>(EMachineType::Manufacturer);

            UFGPowerConnectionComponent* PowerConnection1;

            if (bIsMachine1)
            {
                PowerConnection1 = GetMachinePowerConnection(From.Spawned);
            }
            else
            {
                AFGBuildablePowerPole* PowerPole = Cast<AFGBuildablePowerPole>(From.Spawned);
                PowerConnection1 = PowerPole->GetPowerConnection(0);
            }

            bool bIsMachine2 = static_cast<uint8>(To.Buildable) <= static_cast<uint8>(EMachineType::Manufacturer);

            UFGPowerConnectionComponent* PowerConnection2;

            if (bIsMachine2)
            {
                PowerConnection2 = GetMachinePowerConnection(To.Spawned);
            }
            else
            {
                AFGBuildablePowerPole* PowerPole = Cast<AFGBuildablePowerPole>(To.Spawned);
                PowerConnection2 = PowerPole->GetPowerConnection(0);
            }

            ConnectWithWire(World, WireClass, PowerConnection1, PowerConnection2);
        }
    }

    void ConnectWithBelts(UWorld* World, UFGFactoryConnectionComponent* FromConn, UFGFactoryConnectionComponent* ToConn,
                          UBuildableCache* BuildableCache)
    {
        UClass* BaseClassBelt = BuildableCache->GetBuildableClass(EBuildable::Belt);
        TSubclassOf<AFGBuildableConveyorBelt> BeltClass = BaseClassBelt;
        AFGBuildableConveyorBelt* SpawnedBelt = World->SpawnActor<AFGBuildableConveyorBelt>(BeltClass);

        FVector FromLoc = FromConn->GetComponentLocation();
        FVector FromDir = FromConn->GetConnectorNormal();
        FVector ToLoc = ToConn->GetComponentLocation();

        float Dist = FVector::Distance(FromLoc, ToLoc);
        FVector Tangent = FromDir * Dist * 1.5f;

        TArray<FSplinePointData> SplinePoints;
        SplinePoints.Add(FSplinePointData(FromLoc, Tangent));
        SplinePoints.Add(FSplinePointData(ToLoc, Tangent));

        UFGFactoryConnectionComponent* BeltConn0 = SpawnedBelt->GetConnection0();
        UFGFactoryConnectionComponent* BeltConn1 = SpawnedBelt->GetConnection1();

        FromConn->SetConnection(BeltConn0);
        ToConn->SetConnection(BeltConn1);

        AFGBuildableConveyorBelt::Respline(SpawnedBelt, SplinePoints);
    }

    void SpawnBelts(UWorld* World, const TArray<FBeltConnection>& BeltConnections,
                    TMap<FGuid, FBuiltThing>& SpawnedActors, UBuildableCache* BuildableCache)
    {
        for (const FBeltConnection& Belt : BeltConnections)
        {
            AFGBuildable* From = SpawnedActors.FindRef(Belt.FromUnit).Spawned;
            AFGBuildable* To = SpawnedActors.FindRef(Belt.ToUnit).Spawned;

            if (From && To)
            {
                UFGFactoryConnectionComponent* FromConn = GetBeltConnections(From)[Belt.FromSocket];
                UFGFactoryConnectionComponent* ToConn = GetBeltConnections(To)[Belt.ToSocket];

                if (FromConn && ToConn)
                {
                    ConnectWithBelts(World, FromConn, ToConn, BuildableCache);
                }
                else
                {
                    for (UFGFactoryConnectionComponent* Conn : GetBeltConnections(From))
                    {
                        EFactoryConnectionDirection Direction = Conn->GetDirection();
                        FString SocketName = Conn->GetName();
                        UE_LOG(LogFactorySpawner, Warning, TEXT("Belt connection failed! Direction: %d, name: %s"),
                               Direction, *SocketName);
                    }
                }
            }
        }
    }
} // namespace

TMap<FGuid, FBuiltThing> AClusterHologram::SpawnBuildables(const TArray<FBuildableUnit>& BuildableUnits, UWorld* World,
                                                           FTransform& ActorTransform, UBuildableCache* BuildableCache)
{
    // Map to track spawned actors for each unit
    TMap<FGuid, FBuiltThing> SpawnedActors;

    AFGPlayerController* PlayerController = Cast<AFGPlayerController>(World->GetFirstPlayerController());
    UFGManufacturerClipboardRCO* RCO = PlayerController->GetRemoteCallObjectOfClass<UFGManufacturerClipboardRCO>();
    AFGCharacterPlayer* Player = Cast<AFGCharacterPlayer>(PlayerController->GetCharacter());

    for (const FBuildableUnit& Unit : BuildableUnits)
    {
        FTransform Location = MoveTransform(ActorTransform, Unit.Location, Unit.Buildable == EBuildable::Merger);

        AFGBuildable* Spawned = nullptr;

        switch (Unit.Buildable)
        {
        case EBuildable::Smelter:
        case EBuildable::Constructor:
        case EBuildable::Assembler:
        case EBuildable::Foundry:
        case EBuildable::Manufacturer:
        {
            UClass* BaseClass = BuildableCache->GetBuildableClass(Unit.Buildable);
            TSubclassOf<AFGBuildableManufacturer> MachineClass = BaseClass;
            if (MachineClass)
            {
                Spawned = World->SpawnActor<AFGBuildable>(MachineClass, Location);
                if (Unit.Recipe.IsSet())
                {
                    FString RecipeName = Unit.Recipe.GetValue();
                    TSubclassOf<UFGRecipe> RecipeClass =
                        BuildableCache->GetRecipeClass(RecipeName, MachineClass, World);
                    if (RecipeClass)
                    {
                        AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Spawned);
                        if (Unit.Underclock)
                        {
                            RCO->Server_PasteSettings(Manufacturer, Player, RecipeClass,
                                                      Unit.Underclock.GetValue() / 100, 1.0f, nullptr, nullptr);
                        }
                        else
                        {
                            Manufacturer->SetRecipe(RecipeClass);
                        }
                    }
                }
            }
            break;
        }
        case EBuildable::Splitter:
        case EBuildable::Merger:
        case EBuildable::PowerPole:
        {
            TSubclassOf<AFGBuildable> BuildableClass = BuildableCache->GetBuildableClass(Unit.Buildable);
            Spawned = World->SpawnActor<AFGBuildable>(BuildableClass, Location);

            break;
        }
        default:
            break;
        }

        if (Spawned)
        {
            SpawnedActors.Add(Unit.Id, {Spawned, Unit.Buildable, Unit.Location});
        }
    }
    return SpawnedActors;
}

void AClusterHologram::SpawnBuildPlan()
{
    UWorld* World = GetWorld();
    AMyChatSubsystem* Subsystem = AMyChatSubsystem::Get(World);
    const FBuildPlan& BuildPlan = Subsystem->CurrentBuildPlan;
    UBuildableCache* BuildableCache = Subsystem->BuildableCache;

    if (BuildPlan.BuildableUnits.Num() == 0)
    {
        FFactorySpawnerModule::ChatLog(World, "Nothing to build! Define first a factory with this command: "
                                              "/FactorySpawner <number> <machine type> <recipe>");
    }
    else
    {
        FTransform ActorTransform = GetActorTransform();

        TMap<FGuid, FBuiltThing> SpawnedActors =
            SpawnBuildables(BuildPlan.BuildableUnits, World, ActorTransform, BuildableCache);

        SpawnWires(World, BuildPlan.WireConnections, SpawnedActors, BuildableCache);

        SpawnBelts(World, BuildPlan.BeltConnections, SpawnedActors, BuildableCache);
    }
}