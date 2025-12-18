#include "BuildPlanGenerator.h"
#include "BuildableCache.h"
#include "FactoryCommandParser.h"
#include "FGBlueprintSubsystem.h"
#include "FGBuildableManufacturer.h"
#include "FGTestBlueprintFunctionLibrary.h"
#include "FGPlayerController.h"
#include "FGCharacterPlayer.h"
#include "FGPowerConnectionComponent.h"
#include "FGBuildableWire.h"
#include "FGBuildablePowerPole.h"
#include "FGFactoryConnectionComponent.h"
#include "FGBuildableConveyorBelt.h"
#include "FGBuildableConveyorLift.h"
#include "FGBuildablePipeline.h"
#include "FGPipeConnectionComponent.h"
#include "FGRecipe.h"

namespace
{
    const FQuat Rot180 = FQuat(FVector::UpVector, PI);

    FTransform MoveTransform(const FVector& Offset, bool bFlip = false)
    {
        FTransform Base = FTransform::Identity;
        const FVector NewLoc = Base.TransformPosition(Offset);
        const FQuat NewRot = bFlip ? Rot180 * Base.GetRotation() : Base.GetRotation();
        return FTransform(NewRot, NewLoc);
    }

    template <typename T> TArray<T*> GetConnections(AFGBuildable* Buildable)
    {
        TArray<T*> Out;
        Buildable->GetComponents<T>(Out);
        return Out;
    }

    // Machine configuration map (use helper factories from header)
    static const TMap<EBuildable, FMachineConfig> MachineConfigList = {
        {EBuildable::Constructor, MakeMachineConfig(800, {MakeMachineConnections(900, {MakeConnector(1, 0)})},
                                                    {MakeMachineConnections(900, {MakeConnector(0, 0)})})},
        {EBuildable::Smelter, MakeMachineConfig(500, {MakeMachineConnections(900, {MakeConnector(0, 0)})},
                                                {MakeMachineConnections(800, {MakeConnector(1, 0)})})},
        {EBuildable::Foundry,
         MakeMachineConfig(1000, {MakeMachineConnections(1100, {MakeConnector(2, -200), MakeConnector(0, 200, 200)})},
                           {MakeMachineConnections(800, {MakeConnector(1, -200)})})},
        {EBuildable::Assembler,
         MakeMachineConfig(900, {MakeMachineConnections(1400, {MakeConnector(1, -200), MakeConnector(2, 200, 200)})},
                           {MakeMachineConnections(1100, {MakeConnector(0, 0)})})},
        {EBuildable::OilRefinery,
         MakeMachineConfig(1000,
                           {MakeMachineConnections(1700, {MakeConnector(0, -200, 400)}, {MakeConnector(1, 200)}),
                            MakeMachineConnections(1500, {MakeConnector(0, -200)}, {}),
                            MakeMachineConnections(1500, {}, {MakeConnector(1, 200)})},
                           {MakeMachineConnections(1700, {MakeConnector(1, -200, 400)}, {MakeConnector(0, 200)}),
                            MakeMachineConnections(1500, {MakeConnector(1, -200)}, {}),
                            MakeMachineConnections(1500, {}, {MakeConnector(0, 200)})})},
        {EBuildable::Blender,
         MakeMachineConfig(
             1800,

             {/*2S,2L*/ MakeMachineConnections(1500, {MakeConnector(1, 200, 600), MakeConnector(2, 600, 800)},
                                               {MakeConnector(2, -600), MakeConnector(0, -200, 200)}),
              /*2S,1L*/
              MakeMachineConnections(1500, {MakeConnector(1, 200, 400), MakeConnector(2, 600, 600)},
                                     {MakeConnector(2, -600)}),
              /*1S,2L*/
              MakeMachineConnections(1500, {MakeConnector(1, 200, 600)},
                                     {MakeConnector(2, -600), MakeConnector(0, -200, 200)}),
              /*1S,1L*/
              MakeMachineConnections(1500, {MakeConnector(1, 200, 400)}, {MakeConnector(2, -600)}),
              /*0S,2L*/
              MakeMachineConnections(1400, {}, {MakeConnector(2, -600), MakeConnector(0, -200, 200)}),
              /*0S,1L*/
              MakeMachineConnections(1200, {}, {MakeConnector(2, -600)})},

             {MakeMachineConnections(1500, {MakeConnector(0, -200, 400)}, {MakeConnector(1, -600)}),
              MakeMachineConnections(1200, {MakeConnector(0, -200)}, {}),
              MakeMachineConnections(1200, {}, {MakeConnector(1, -600)})})},
        {EBuildable::Manufacturer,
         MakeMachineConfig(1800,
                           {MakeMachineConnections(1700, {MakeConnector(4, -600), MakeConnector(2, -200, 200),
                                                          MakeConnector(1, 200, 400), MakeConnector(0, 600, 600)}),
                            MakeMachineConnections(1700, {MakeConnector(4, -600), MakeConnector(2, -200, 200),
                                                          MakeConnector(1, 200, 400)})},
                           {MakeMachineConnections(1300, {MakeConnector(3, 0)})})},
    };

} // namespace

FBuildPlanGenerator::FBuildPlanGenerator(UWorld* InWorld, UBuildableCache* InCache)
{
    World = InWorld;
    Cache = InCache;
    AFGPlayerController* PC = Cast<AFGPlayerController>(World->GetFirstPlayerController());
    Player = Cast<AFGCharacterPlayer>(PC->GetCharacter());
    RCO = PC->GetRemoteCallObjectOfClass<UFGManufacturerClipboardRCO>();
}

void FBuildPlanGenerator::Generate(const TArray<FFactoryCommandToken>& ClusterConfig)
{
    YCursor = XCursor = FirstMachineWidth = 0;
    CachedPowerConnections = {};

    for (int32 i = 0; i < ClusterConfig.Num(); ++i)
        ProcessRow(ClusterConfig[i], i);

    AFGBlueprintSubsystem* BlueprintSubsystem = AFGBlueprintSubsystem::Get(World);
    UFGBlueprintDescriptor* ExistingDescriptor =
        BlueprintSubsystem->GetBlueprintDescriptorByNameString(TEXT("FactorySpawner"));
    if (ExistingDescriptor)
        BlueprintSubsystem->DeleteBlueprintDescriptor(ExistingDescriptor);

    FBlueprintRecord Record;
    Record.BlueprintName = TEXT("FactorySpawner");
    Record.BlueprintDescription = TEXT("Auto-generated blueprint");
    Record.Color = FLinearColor::White;

    BlueprintSubsystem->WriteBlueprintToArchive(Record, FTransform::Identity, BuildablesForBlueprint,
                                                FIntVector(1, 1, 1));
    BlueprintSubsystem->RefreshBlueprintsAndDescriptors();

    for (AFGBuildable* Buildable : BuildablesForBlueprint)
        Buildable->Destroy();
}

void FBuildPlanGenerator::ProcessRow(const FFactoryCommandToken& RowConfig, int32 RowIndex)
{
    const FMachineConfig& Config = MachineConfigList[RowConfig.MachineType];
    int32 InputVariant = 0, OutputVariant = 0;

    if (RowConfig.Recipe.IsSet())
    {
        TSubclassOf<AFGBuildableManufacturer> MachineClass =
            Cache->GetBuildableClass<AFGBuildableManufacturer>(RowConfig.MachineType);
        TSubclassOf<UFGRecipe> RecipeClass = Cache->GetRecipeClass(RowConfig.Recipe.GetValue(), MachineClass, World);

        if (RecipeClass)
        {

            int32 SolidIn = 0, LiquidIn = 0, SolidOut = 0, LiquidOut = 0;
            for (const FItemAmount& Item : RecipeClass->GetDefaultObject<UFGRecipe>()->GetIngredients())
                UFGItemDescriptor::GetForm(Item.ItemClass) == EResourceForm::RF_SOLID ? ++SolidIn : ++LiquidIn;
            for (const FItemAmount& Item : RecipeClass->GetDefaultObject<UFGRecipe>()->GetProducts())
                UFGItemDescriptor::GetForm(Item.ItemClass) == EResourceForm::RF_SOLID ? ++SolidOut : ++LiquidOut;

            if (RowConfig.MachineType == EBuildable::Manufacturer && SolidIn == 3)
                InputVariant = 1;
            else if (RowConfig.MachineType == EBuildable::OilRefinery)
            {
                InputVariant = (SolidIn == 1 && LiquidIn == 0) ? 1 : (SolidIn == 0 && LiquidIn == 1) ? 2 : 0;
                OutputVariant = (SolidOut == 1 && LiquidOut == 0) ? 1 : (SolidOut == 0 && LiquidOut == 1) ? 2 : 0;
            }
            else if (RowConfig.MachineType == EBuildable::Blender)
            {
                if (SolidIn == 2 && LiquidIn == 1)
                    InputVariant = 1;
                else if (SolidIn == 1 && LiquidIn == 2)
                    InputVariant = 2;
                else if (SolidIn == 1 && LiquidIn == 1)
                    InputVariant = 3;
                else if (SolidIn == 0 && LiquidIn == 2)
                    InputVariant = 4;
                else if (SolidIn == 0 && LiquidIn == 1)
                    InputVariant = 5;
                OutputVariant = (SolidOut == 1 && LiquidOut == 0) ? 1 : (SolidOut == 0 && LiquidOut == 1) ? 2 : 0;
            }
        }
    }

    const FMachineConnections& InputConn = Config.InputConnections[InputVariant];
    const FMachineConnections& OutputConn = Config.OutputConnections[OutputVariant];

    if (RowIndex == 0)
        FirstMachineWidth = Config.Width;
    else
    {
        YCursor += InputConn.Length;
        XCursor = FMath::CeilToInt((Config.Width - FirstMachineWidth) / 2.0f / 100) * 100;
    }

    PlaceMachines(RowConfig, Config.Width, InputConn, OutputConn);
    YCursor += OutputConn.Length;
    CachedPowerConnections.LastMachine = CachedPowerConnections.LastPole = nullptr;
}

void FBuildPlanGenerator::PlaceMachines(const FFactoryCommandToken& RowConfig, int32 Width,
                                        const FMachineConnections& InputConnections,
                                        const FMachineConnections& OutputConnections)
{
    ConnectionQueue.Input.Empty();
    ConnectionQueue.Output.Empty();
    ConnectionQueue.PipeInput.Empty();
    ConnectionQueue.PipeOutput.Empty();

    for (int32 i = 0; i < RowConfig.Count; ++i)
    {
        CalculateMachineSetup(RowConfig.MachineType, RowConfig.Recipe, RowConfig.ClockPercent, Width, InputConnections,
                              OutputConnections, i == 0, i % 2 == 0, i == RowConfig.Count - 1);
        XCursor += Width;
    }
}
void FBuildPlanGenerator::CalculateMachineSetup(EBuildable MachineType, const TOptional<FString>& Recipe,
                                                const TOptional<float>& Underclock, int32 Width,
                                                const FMachineConnections& InputConnections,
                                                const FMachineConnections& OutputConnections, bool bFirstUnitInRow,
                                                bool bEvenIndex, bool bLastIndex)
{
    UFGPowerConnectionComponent* MachinePowerConn;
    TArray<UFGFactoryConnectionComponent*> MachineBeltConn;
    TArray<UFGPipeConnectionComponent*> MachinePipeConn;
    SpawnMachine(FVector(XCursor, YCursor, 0), MachineType, Recipe, Underclock, MachinePowerConn, MachineBeltConn,
                 MachinePipeConn);

    if (!bEvenIndex)
    {
        if (bLastIndex && CachedPowerConnections.LastPole)
            SpawnWireAndConnect(CachedPowerConnections.LastPole, MachinePowerConn);
        else
            CachedPowerConnections.LastMachine = MachinePowerConn;
    }
    else
    {
        FVector PoleLocation = FVector(XCursor - Width / 2.0f, YCursor - InputConnections.Length / 2.0f, 0);
        UFGPowerConnectionComponent* Pole = SpawnPowerPole(PoleLocation);
        SpawnWireAndConnect(Pole, MachinePowerConn);

        if (bFirstUnitInRow)
        {
            if (CachedPowerConnections.FirstPole)
                SpawnWireAndConnect(Pole, CachedPowerConnections.FirstPole);
            CachedPowerConnections.FirstPole = Pole;
        }
        else
        {
            if (CachedPowerConnections.LastMachine)
                SpawnWireAndConnect(Pole, CachedPowerConnections.LastMachine);
            if (CachedPowerConnections.LastPole)
                SpawnWireAndConnect(Pole, CachedPowerConnections.LastPole);
        }
        CachedPowerConnections.LastPole = Pole;
    }

    FVector MachineLocation(XCursor, YCursor, 0);

    for (const FConnector& Conn : InputConnections.Belt)
    {
        FVector Loc = MachineLocation + FVector(Conn.LocationX, -InputConnections.Length + 200, 100 + Conn.LocationY);
        TArray<UFGFactoryConnectionComponent*> Splitter = SpawnSplitterOrMerger(Loc, EBuildable::Splitter);

        if (!bFirstUnitInRow)
        {
            UFGFactoryConnectionComponent* Prev;
            if (ConnectionQueue.Input.Dequeue(Prev))
                SpawnBeltAndConnect(Prev, Splitter[1]);
        }
        ConnectionQueue.Input.Enqueue(Splitter[0]);
        SpawnLiftOrBeltAndConnect(Conn.LocationY, Splitter[3], MachineBeltConn[Conn.Index]);
    }

    for (const FConnector& Conn : OutputConnections.Belt)
    {
        FVector Loc = MachineLocation + FVector(Conn.LocationX, OutputConnections.Length - 200, 100 + Conn.LocationY);
        TArray<UFGFactoryConnectionComponent*> Merger = SpawnSplitterOrMerger(Loc, EBuildable::Merger);

        if (!bFirstUnitInRow)
        {
            UFGFactoryConnectionComponent* Prev;
            if (ConnectionQueue.Output.Dequeue(Prev))
                SpawnBeltAndConnect(Merger[1], Prev);
        }
        ConnectionQueue.Output.Enqueue(Merger[0]);
        SpawnLiftOrBeltAndConnect(Conn.LocationY, MachineBeltConn[Conn.Index], Merger[2]);
    }

    for (const FConnector& Conn : InputConnections.Pipe)
    {
        FVector Loc = MachineLocation + FVector(Conn.LocationX, -InputConnections.Length + 200, 175 + Conn.LocationY);
        TArray<UFGPipeConnectionComponent*> Cross = SpawnPipeCross(Loc);

        if (!bFirstUnitInRow)
        {
            UFGPipeConnectionComponent* Prev;
            if (ConnectionQueue.PipeInput.Dequeue(Prev))
                SpawnPipeAndConnect(Prev, Cross[3]);
        }
        ConnectionQueue.PipeInput.Enqueue(Cross[0]);
        SpawnPipeAndConnect(MachinePipeConn[Conn.Index], Cross[1]);
    }

    for (const FConnector& Conn : OutputConnections.Pipe)
    {
        FVector Loc = MachineLocation + FVector(Conn.LocationX, OutputConnections.Length - 200, 175 + Conn.LocationY);
        TArray<UFGPipeConnectionComponent*> Cross = SpawnPipeCross(Loc);

        if (!bFirstUnitInRow)
        {
            UFGPipeConnectionComponent* Prev;
            if (ConnectionQueue.PipeOutput.Dequeue(Prev))
                SpawnPipeAndConnect(Prev, Cross[3]);
        }
        ConnectionQueue.PipeOutput.Enqueue(Cross[0]);
        SpawnPipeAndConnect(MachinePipeConn[Conn.Index], Cross[2]);
    }
}

void FBuildPlanGenerator::SpawnWireAndConnect(UFGPowerConnectionComponent* A, UFGPowerConnectionComponent* B)
{
    TSubclassOf<AFGBuildableWire> PowerLineClass = Cache->GetBuildableClass<AFGBuildableWire>(EBuildable::PowerLine);
    AFGBuildableWire* Wire = World->SpawnActor<AFGBuildableWire>(PowerLineClass, FTransform::Identity);
    Wire->Connect(A, B);
    BuildablesForBlueprint.Add(static_cast<AFGBuildable*>(Wire));
}

UFGPowerConnectionComponent* FBuildPlanGenerator::SpawnPowerPole(const FVector& Location)
{
    const FTransform UnitTransform = MoveTransform(Location);
    TSubclassOf<AFGBuildablePowerPole> PowerPoleClass =
        Cache->GetBuildableClass<AFGBuildablePowerPole>(EBuildable::PowerPole);
    AFGBuildablePowerPole* SpawnedPowerPole = World->SpawnActor<AFGBuildablePowerPole>(PowerPoleClass, UnitTransform);
    BuildablesForBlueprint.Add(SpawnedPowerPole);
    UFGPowerConnectionComponent* PolePowerConnection = SpawnedPowerPole->GetPowerConnection(0);
    return PolePowerConnection;
}

void FBuildPlanGenerator::SpawnMachine(const FVector& Location, EBuildable MachineType,
                                       const TOptional<FString>& Recipe, const TOptional<float>& Underclock,
                                       UFGPowerConnectionComponent*& outPowerConn,
                                       TArray<UFGFactoryConnectionComponent*>& outBeltConn,
                                       TArray<UFGPipeConnectionComponent*>& outPipeConn)
{
    TSubclassOf<AFGBuildableManufacturer> MachineClass =
        Cache->GetBuildableClass<AFGBuildableManufacturer>(MachineType);
    FTransform Transform = MoveTransform(Location, MachineType == EBuildable::OilRefinery);
    AFGBuildableManufacturer* Machine = World->SpawnActor<AFGBuildableManufacturer>(MachineClass, Transform);

    if (Recipe.IsSet())
    {
        TSubclassOf<UFGRecipe> RecipeClass = Cache->GetRecipeClass(Recipe.GetValue(), MachineClass, World);
        if (Underclock.IsSet() && RCO && Player)
            RCO->Server_PasteSettings(Machine, Player, RecipeClass, Underclock.GetValue() / 100.0f, 1.0f, nullptr,
                                      nullptr);
        else
            Machine->SetRecipe(RecipeClass);
    }

    BuildablesForBlueprint.Add(Machine);
    outPowerConn = GetConnections<UFGPowerConnectionComponent>(Machine)[0];
    outBeltConn = GetConnections<UFGFactoryConnectionComponent>(Machine);
    outPipeConn = GetConnections<UFGPipeConnectionComponent>(Machine);
}

TArray<UFGFactoryConnectionComponent*> FBuildPlanGenerator::SpawnSplitterOrMerger(const FVector& Location,
                                                                                  EBuildable SplitterOrMerger)
{
    const bool bFlip = SplitterOrMerger == EBuildable::Merger;
    TSubclassOf<AFGBuildable> Class = Cache->GetBuildableClass<AFGBuildable>(SplitterOrMerger);
    const FTransform UnitTransform = MoveTransform(Location, bFlip);
    AFGBuildable* Spawned = World->SpawnActor<AFGBuildable>(Class, UnitTransform);
    BuildablesForBlueprint.Add(Spawned);
    return GetConnections<UFGFactoryConnectionComponent>(Spawned);
}

void FBuildPlanGenerator::SpawnLiftOrBeltAndConnect(float height, UFGFactoryConnectionComponent* From,
                                                    UFGFactoryConnectionComponent* To)
{
    height < 400 ? SpawnBeltAndConnect(From, To) : SpawnLiftAndConnect(From, To);
}

void FBuildPlanGenerator::SpawnBeltAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To)
{
    TSubclassOf<AFGBuildableConveyorBelt> BeltClass =
        Cache->GetBuildableClass<AFGBuildableConveyorBelt>(EBuildable::Belt);
    AFGBuildableConveyorBelt* Belt =
        Cast<AFGBuildableConveyorBelt>(UFGTestBlueprintFunctionLibrary::SpawnSplineBuildable(BeltClass, From, To));

    FVector FromLoc = From->GetComponentLocation();
    FVector ToLoc = To->GetComponentLocation();
    FVector TangentWorld = From->GetConnectorNormal() * FVector::Distance(FromLoc, ToLoc) * 1.5f;
    FTransform BeltTransform = Belt->GetActorTransform();

    TArray<FSplinePointData> SplinePoints;
    SplinePoints.Add(FSplinePointData(BeltTransform.InverseTransformPosition(FromLoc),
                                      BeltTransform.InverseTransformVectorNoScale(TangentWorld)));
    SplinePoints.Add(FSplinePointData(BeltTransform.InverseTransformPosition(ToLoc),
                                      BeltTransform.InverseTransformVectorNoScale(TangentWorld)));

    BuildablesForBlueprint.Add(AFGBuildableConveyorBelt::Respline(Belt, SplinePoints));
}

void FBuildPlanGenerator::SpawnLiftAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To)
{
    TSubclassOf<AFGBuildableConveyorLift> LiftClass =
        Cache->GetBuildableClass<AFGBuildableConveyorLift>(EBuildable::Lift);

    // Get the connection locations
    FVector FromLoc = From->GetComponentLocation();
    FVector ToLoc = To->GetComponentLocation();

    FTransform InputTransform(FRotator(0, 270, 0), FromLoc + FVector(0, 300, 0));

    AFGBuildableConveyorLift* Lift = World->SpawnActor<AFGBuildableConveyorLift>(LiftClass, InputTransform);

    // Calculate the top transform relative to the lift's base
    FVector OutputHeightOffset(0, 0, ToLoc.Z - FromLoc.Z);
    FTransform TopTransform(FRotator(0.0f, 180.0f, 0.0f), OutputHeightOffset);

    // Use Unreal's reflection system to set the private mTopTransform property
    FProperty* TopTransformProp = Lift->GetClass()->FindPropertyByName(TEXT("mTopTransform"));
    FStructProperty* StructProp = CastField<FStructProperty>(TopTransformProp);
    if (StructProp && StructProp->Struct == TBaseStructure<FTransform>::Get())
    {
        void* PropertyAddress = StructProp->ContainerPtrToValuePtr<void>(Lift);
        StructProp->CopyCompleteValue(PropertyAddress, &TopTransform);
    }

    TArray<UFGFactoryConnectionComponent*> LiftConnections = GetConnections<UFGFactoryConnectionComponent>(Lift);
    From->SetConnection(LiftConnections[0]);
    LiftConnections[1]->SetConnection(To);
    Lift->SetupConnections();

    BuildablesForBlueprint.Add(Lift);
}

TArray<UFGPipeConnectionComponent*> FBuildPlanGenerator::SpawnPipeCross(const FVector& Location)
{
    TSubclassOf<AFGBuildable> Class = Cache->GetBuildableClass<AFGBuildable>(EBuildable::PipeCross);
    const FTransform UnitTransform = MoveTransform(Location);
    AFGBuildable* Spawned = World->SpawnActor<AFGBuildable>(Class, UnitTransform);
    BuildablesForBlueprint.Add(Spawned);
    return GetConnections<UFGPipeConnectionComponent>(Spawned);
}

void FBuildPlanGenerator::SpawnPipeAndConnect(UFGPipeConnectionComponent* From, UFGPipeConnectionComponent* To)
{
    TSubclassOf<AFGBuildablePipeline> PipeClass = Cache->GetBuildableClass<AFGBuildablePipeline>(EBuildable::Pipeline);
    AFGBuildable* Spawned = UFGTestBlueprintFunctionLibrary::SpawnSplineBuildable(PipeClass, From, To);
    BuildablesForBlueprint.Add(Spawned);
}