#include "BuildPlanGenerator.h"
#include "FactorySpawnerChat.h"
#include "FactorySpawner.h"
#include "FGRecipe.h"
#include "BuildableCache.h"
#include "FactoryCommandParser.h"
#include "FGBlueprintSubsystem.h"
#include "FGBuildableManufacturer.h"
#include "FGTestBlueprintFunctionLibrary.h"
#include "Containers/Queue.h"
#include "FGPlayerController.h"
#include "FGCharacterPlayer.h"

// electrical
#include "FGPowerConnectionComponent.h"
#include "FGBuildableWire.h"
#include "FGBuildablePowerPole.h"

// belts
#include "FGFactoryConnectionComponent.h"
#include "FGBuildableConveyorBelt.h"

// pipes
#include "FGBuildablePipeline.h"
#include "FGPipeConnectionComponent.h"
#include "FGBuildablePipelineJunction.h"

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
        if (Buildable)
            Buildable->GetComponents<T>(Out);
        return Out;
    }

    UFGPowerConnectionComponent* GetMachinePowerConn(AFGBuildable* Buildable)
    {
        TArray<UFGPowerConnectionComponent*> PowerConnections;
        Buildable->GetComponents<UFGPowerConnectionComponent>(PowerConnections);
        return PowerConnections[0];
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
                           {MakeMachineConnections(2000, {MakeConnector(0, -200, 400)}, {MakeConnector(1, 200)}),
                            MakeMachineConnections(1500, {MakeConnector(0, -200)}, {}),
                            MakeMachineConnections(1500, {}, {MakeConnector(1, 200)})},
                           {MakeMachineConnections(2000, {MakeConnector(1, -200, 400)}, {MakeConnector(0, 200)}),
                            MakeMachineConnections(1500, {MakeConnector(1, -200)}, {}),
                            MakeMachineConnections(1500, {}, {MakeConnector(0, 200)})})},
        {EBuildable::Blender,
         MakeMachineConfig(
             1800,

             {/*2S,2L*/ MakeMachineConnections(2400, {MakeConnector(1, 200, 600), MakeConnector(2, 600, 800)},
                                               {MakeConnector(2, -600), MakeConnector(0, -200, 200)}),
              /*2S,1L*/
              MakeMachineConnections(2100, {MakeConnector(1, 200, 400), MakeConnector(2, 600, 600)},
                                     {MakeConnector(2, -600)}),
              /*1S,2L*/
              MakeMachineConnections(2100, {MakeConnector(1, 200, 600)},
                                     {MakeConnector(2, -600), MakeConnector(0, -200, 200)}),
              /*1S,1L*/
              MakeMachineConnections(1800, {MakeConnector(1, 200, 400)}, {MakeConnector(2, -600)}),
              /*0S,2L*/
              MakeMachineConnections(1400, {}, {MakeConnector(2, -600), MakeConnector(0, -200, 200)}),
              /*0S,1L*/
              MakeMachineConnections(1200, {}, {MakeConnector(2, -600)})},

             {MakeMachineConnections(1800, {MakeConnector(0, -200, 400)}, {MakeConnector(1, -600)}),
              MakeMachineConnections(1200, {MakeConnector(0, -200)}, {}),
              MakeMachineConnections(1200, {}, {MakeConnector(1, -600)})})},
        {EBuildable::Manufacturer,
         MakeMachineConfig(1800,
                           {MakeMachineConnections(2300, {MakeConnector(4, -600), MakeConnector(2, -200, 200),
                                                          MakeConnector(1, 200, 400), MakeConnector(0, 600, 600)}),
                            MakeMachineConnections(2000, {MakeConnector(4, -600), MakeConnector(2, -200, 200),
                                                          MakeConnector(1, 200, 400)})},
                           {MakeMachineConnections(1300, {MakeConnector(3, 0)})})},
    };

    // ---------- helpers to add units/connections ----------
    inline void AddBuildableUnit(FBuildPlan& Plan, const FGuid& Id, EBuildable Type, const FVector& Location,
                                 const TOptional<FString>& Recipe = TOptional<FString>(),
                                 const TOptional<float>& Underclock = TOptional<float>())
    {
        FBuildableUnit Unit;
        Unit.Id = Id;
        Unit.Buildable = Type;
        Unit.Location = Location;
        Unit.Recipe = Recipe;
        Unit.Underclock = Underclock;
        Plan.BuildableUnits.Add(MoveTemp(Unit));
    }

    inline void AddWire(FBuildPlan& Plan, const FGuid& From, const FGuid& To)
    {
        Plan.WireConnections.Add({From, To});
    }

    inline void AddBelt(FBuildPlan& Plan, const FGuid& FromUnit, int32 FromSocket, const FGuid& ToUnit, int32 ToSocket)
    {
        Plan.BeltConnections.Add({FromUnit, FromSocket, ToUnit, ToSocket});
    }

    inline void AddPipe(FBuildPlan& Plan, const FGuid& FromUnit, int32 FromSocket, const FGuid& ToUnit, int32 ToSocket)
    {
        Plan.PipeConnections.Add({FromUnit, FromSocket, ToUnit, ToSocket});
    }

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

    int32 RowIndex = 0;
    for (const FFactoryCommandToken& RowConfig : ClusterConfig)
    {
        ProcessRow(RowConfig, RowIndex++);
    }

    FBlueprintRecord record;
    record.BlueprintName = TEXT("FactorySpawner");
    record.BlueprintDescription = TEXT("Auto-generated blueprint.");
    record.Color = FLinearColor::White;
    record.Priority = 0;

    // Dimensions: small default
    FIntVector Dimensions(1, 1, 1);

    if (AFGBlueprintSubsystem* BlueprintSubsystem = AFGBlueprintSubsystem::Get(World))
    {
        FTransform SpawnTransform = FTransform::Identity;
        if (UFGBlueprintDescriptor* BlueprintDescriptor =
                BlueprintSubsystem->GetBlueprintDescriptorByNameString(record.BlueprintName))
        {
            BlueprintSubsystem->DeleteBlueprintDescriptor(BlueprintDescriptor);
        }
        FBlueprintHeader Header =
            BlueprintSubsystem->WriteBlueprintToArchive(record, SpawnTransform, BuildablesForBlueprint, Dimensions);
        BlueprintSubsystem->RefreshBlueprintsAndDescriptors();

        UE_LOG(LogTemp, Warning, TEXT("Wrote test blueprint '%s'"), *Header.BlueprintName);
    }

    // Cleanup the spawned actors
    for (AFGBuildable* Buildable : BuildablesForBlueprint)
    {
        Buildable->Destroy();
    }
}

void FBuildPlanGenerator::ProcessRow(const FFactoryCommandToken& RowConfig, int32 RowIndex)
{
    // determine variant, recipe, machine config, etc.
    TSubclassOf<AFGBuildableManufacturer> MachineClass =
        Cache->GetBuildableClass<AFGBuildableManufacturer>(RowConfig.MachineType);

    int32 InputVariant = 0;
    int32 OutputVariant = 0;

    const FMachineConfig& MachineConfig = MachineConfigList[RowConfig.MachineType];

    if (RowConfig.Recipe.IsSet())
    {
        if (TSubclassOf<UFGRecipe> RecipeClass =
                Cache->GetRecipeClass(RowConfig.Recipe.GetValue(), MachineClass, World))
        {
            TArray<FItemAmount> Ingredients = RecipeClass->GetDefaultObject<UFGRecipe>()->GetIngredients();
            TArray<FItemAmount> Products = RecipeClass->GetDefaultObject<UFGRecipe>()->GetProducts();

            // Count solid/liquid inputs and outputs
            int32 SolidInputs = 0;
            int32 LiquidInputs = 0;
            int32 SolidOutputs = 0;
            int32 LiquidOutputs = 0;

            for (const FItemAmount& IA : Ingredients)
            {
                EResourceForm Form = UFGItemDescriptor::GetForm(IA.ItemClass);
                if (Form == EResourceForm::RF_SOLID)
                    ++SolidInputs;
                else if (Form == EResourceForm::RF_LIQUID)
                    ++LiquidInputs;
            }

            for (const FItemAmount& PA : Products)
            {
                EResourceForm Form = UFGItemDescriptor::GetForm(PA.ItemClass);
                if (Form == EResourceForm::RF_SOLID)
                    ++SolidOutputs;
                else if (Form == EResourceForm::RF_LIQUID)
                    ++LiquidOutputs;
            }

            if (RowConfig.MachineType == EBuildable::Manufacturer && SolidInputs == 3)
                InputVariant = 1;
            else if (RowConfig.MachineType == EBuildable::OilRefinery)
            {
                if (SolidInputs == 1 && LiquidInputs == 0)
                    InputVariant = 1;
                else if (SolidInputs == 0 && LiquidInputs == 1)
                    InputVariant = 2;

                if (SolidOutputs == 1 && LiquidOutputs == 0)
                    OutputVariant = 1;
                else if (SolidOutputs == 0 && LiquidOutputs == 1)
                    OutputVariant = 2;
            }
            else if (RowConfig.MachineType == EBuildable::Blender)
            {
                if (SolidInputs == 2 && LiquidInputs == 1)
                    InputVariant = 1;
                else if (SolidInputs == 1 && LiquidInputs == 2)
                    InputVariant = 2;
                else if (SolidInputs == 1 && LiquidInputs == 1)
                    InputVariant = 3;
                else if (SolidInputs == 0 && LiquidInputs == 2)
                    InputVariant = 4;
                else if (SolidInputs == 0 && LiquidInputs == 1)
                    InputVariant = 5;

                if (SolidOutputs == 1 && LiquidOutputs == 0)
                    OutputVariant = 1;
                else if (SolidOutputs == 0 && LiquidOutputs == 1)
                    OutputVariant = 2;
            }
        }
    }

    const FMachineConnections InputConnections = MachineConfig.InputConnections[InputVariant];
    const FMachineConnections OutputConnections = MachineConfig.OutputConnections[OutputVariant];

    if (RowIndex == 0)
        FirstMachineWidth = MachineConfig.Width;
    else
    {
        YCursor += InputConnections.Length;
        XCursor = FMath::CeilToInt((MachineConfig.Width - FirstMachineWidth) / 2.0f / 100) * 100;
    }

    PlaceMachines(RowConfig, MachineConfig.Width, InputConnections, OutputConnections);

    // advance Y cursor by the machine's output length
    YCursor += OutputConnections.Length;
    CachedPowerConnections.LastMachine = nullptr;
    CachedPowerConnections.LastPole = nullptr;
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
        const bool bFirstUnitInRow = (i == 0);
        const bool bEvenIndex = (i % 2 == 0);
        const bool bLastIndex = (i == RowConfig.Count - 1);

        CalculateMachineSetup(RowConfig.MachineType, RowConfig.Recipe, RowConfig.ClockPercent, Width, InputConnections,
                              OutputConnections, bFirstUnitInRow, bEvenIndex, bLastIndex);

        XCursor += Width;
    }
}
// ---------- main per-machine setup ----------
void FBuildPlanGenerator::CalculateMachineSetup(EBuildable MachineType, const TOptional<FString>& Recipe,
                                                const TOptional<float>& Underclock, int32 Width,
                                                const FMachineConnections& InputConnections,
                                                const FMachineConnections& OutputConnections, bool bFirstUnitInRow,
                                                bool bEvenIndex, bool bLastIndex)
{
    // create machine unit
    const FVector MachineLocation = FVector(XCursor, YCursor, 0.0f);

    UFGPowerConnectionComponent* MachinePowerConn;
    TArray<UFGFactoryConnectionComponent*> MachineBeltConn;
    TArray<UFGPipeConnectionComponent*> MachinePipeConn;
    SpawnMachine(MachineLocation, MachineType, Recipe, Underclock, MachinePowerConn, MachineBeltConn, MachinePipeConn);

    if (!bEvenIndex)
    {
        if (bLastIndex)
        {
            // link previous pole -> this machine
            if (CachedPowerConnections.LastPole != nullptr)
            {
                SpawnWireAndConnect(CachedPowerConnections.LastPole, MachinePowerConn);
            }
        }
        else
        {
            // remember last machine for later connection
            CachedPowerConnections.LastMachine = MachinePowerConn;
        }
    }
    else
    {
        // place a power pole between machines
        const FVector PowerPoleLocation =
            MachineLocation + FVector(-(Width / 2.0f), -(InputConnections.Length / 2.0f), 0.0f);
        UFGPowerConnectionComponent* PolePowerConnection = SpawnPowerPole(PowerPoleLocation);

        // connect pole -> machine
        SpawnWireAndConnect(PolePowerConnection, MachinePowerConn);

        if (bFirstUnitInRow)
        {
            if (CachedPowerConnections.FirstPole != nullptr)
            {
                // connect pole -> first pole of previous row
                SpawnWireAndConnect(PolePowerConnection, CachedPowerConnections.FirstPole);
            }
            CachedPowerConnections.FirstPole = PolePowerConnection;
        }
        else
        {
            // connect pole -> last machine & last pole (previous on same row)
            if (CachedPowerConnections.LastMachine != nullptr)
                SpawnWireAndConnect(PolePowerConnection, CachedPowerConnections.LastMachine);

            if (CachedPowerConnections.LastPole != nullptr)
                SpawnWireAndConnect(PolePowerConnection, CachedPowerConnections.LastPole);
        }

        CachedPowerConnections.LastPole = PolePowerConnection;
    }

    // Input splitters and belt wiring
    for (const FConnector& Conn : InputConnections.Belt)
    {
        const FVector SplitterLocation =
            MachineLocation + FVector(Conn.LocationX, -InputConnections.Length + 200.0f, 100.0f + Conn.LocationY);
        TArray<UFGFactoryConnectionComponent*> SplitterConn =
            SpawnSplitterOrMerger(SplitterLocation, EBuildable::Splitter);

        // connect previous item -> splitter (if present)
        if (!bFirstUnitInRow)
        {
            UFGFactoryConnectionComponent* Prev;
            if (ConnectionQueue.Input.Dequeue(Prev))
            {
                SpawnBeltAndConnect(Prev, SplitterConn[1]);
            }
        }

        // enqueue this splitter for future machines
        ConnectionQueue.Input.Enqueue(SplitterConn[0]);

        // connect splitter -> machine input socket
        SpawnBeltAndConnect(SplitterConn[3], MachineBeltConn[Conn.Index]);
    }

    // Output mergers and belt wiring
    for (const FConnector& Conn : OutputConnections.Belt)
    {
        const FVector MergerLocation =
            MachineLocation + FVector(Conn.LocationX, OutputConnections.Length - 200.0f, 100.0f + Conn.LocationY);
        TArray<UFGFactoryConnectionComponent*> MergerConn = SpawnSplitterOrMerger(MergerLocation, EBuildable::Merger);

        // connect merger -> previously enqueued output (if present)
        if (!bFirstUnitInRow)
        {
            UFGFactoryConnectionComponent* Prev;
            if (ConnectionQueue.Output.Dequeue(Prev))
            {
                SpawnBeltAndConnect(MergerConn[1], Prev);
            }
        }

        // enqueue this merger for future row's machines
        ConnectionQueue.Output.Enqueue(MergerConn[0]);

        // connect machine output -> merger
        SpawnBeltAndConnect(MachineBeltConn[Conn.Index], MergerConn[2]);
    }

    // Input pipes
    for (const FConnector& Conn : InputConnections.Pipe)
    {
        const FVector PipeCrossLocation =
            MachineLocation + FVector(Conn.LocationX, -InputConnections.Length + 200.0f, 175.0f + Conn.LocationY);

        TArray<UFGPipeConnectionComponent*> PipeCrossConn = SpawnPipeCross(PipeCrossLocation);

        // connect previous item -> pipe cross (if present)
        if (!bFirstUnitInRow)
        {
            UFGPipeConnectionComponent* Prev;
            if (ConnectionQueue.PipeInput.Dequeue(Prev))
            {
                SpawnPipeAndConnect(Prev, PipeCrossConn[3]);
            }
        }

        // enqueue this pipe cross for future machines
        ConnectionQueue.PipeInput.Enqueue(PipeCrossConn[0]);

        // connect pipe cross -> machine input socket
        SpawnPipeAndConnect(MachinePipeConn[Conn.Index], PipeCrossConn[1]);
    }

    // Output pipes
    for (const FConnector& Conn : OutputConnections.Pipe)
    {
        const FVector PipeCrossLocation =
            MachineLocation + FVector(Conn.LocationX, OutputConnections.Length - 200.0f, 175.0f + Conn.LocationY);
        TArray<UFGPipeConnectionComponent*> PipeCrossConn = SpawnPipeCross(PipeCrossLocation);

        // connect pipe cross -> previously enqueued output (if present)
        if (!bFirstUnitInRow)
        {
            UFGPipeConnectionComponent* Prev;
            if (ConnectionQueue.PipeOutput.Dequeue(Prev))
            {
                SpawnPipeAndConnect(Prev, PipeCrossConn[3]);
            }
        }

        // enqueue this pipe cross for future row's machines
        ConnectionQueue.PipeOutput.Enqueue(PipeCrossConn[0]);

        // connect machine output -> pipe cross
        SpawnPipeAndConnect(MachinePipeConn[Conn.Index], PipeCrossConn[2]);
    }
}

void FBuildPlanGenerator::SpawnWireAndConnect(UFGPowerConnectionComponent* A, UFGPowerConnectionComponent* B)
{
    TSubclassOf<AFGBuildableWire> PowerLineClass = Cache->GetBuildableClass<AFGBuildableWire>(EBuildable::PowerLine);
    AFGBuildableWire* Wire = World->SpawnActor<AFGBuildableWire>(PowerLineClass, FTransform::Identity);
    Wire->Connect(A, B);
    BuildablesForBlueprint.Add(static_cast<AFGBuildable*>(Wire));
}

UFGPowerConnectionComponent* FBuildPlanGenerator::SpawnPowerPole(FVector Location)
{
    const FTransform UnitTransform = MoveTransform(Location);
    TSubclassOf<AFGBuildablePowerPole> PowerPoleClass =
        Cache->GetBuildableClass<AFGBuildablePowerPole>(EBuildable::PowerPole);
    AFGBuildablePowerPole* SpawnedPowerPole = World->SpawnActor<AFGBuildablePowerPole>(PowerPoleClass, UnitTransform);
    BuildablesForBlueprint.Add(SpawnedPowerPole);
    UFGPowerConnectionComponent* PolePowerConnection = SpawnedPowerPole->GetPowerConnection(0);
    return PolePowerConnection;
}

void FBuildPlanGenerator::SpawnMachine(FVector Location, EBuildable MachineType, const TOptional<FString>& Recipe,
                                       const TOptional<float>& Underclock, UFGPowerConnectionComponent*& outPowerConn,
                                       TArray<UFGFactoryConnectionComponent*>& outBeltConn,
                                       TArray<UFGPipeConnectionComponent*>& outPipeConn)
{
    const bool bFlip = MachineType == EBuildable::OilRefinery;
    TSubclassOf<AFGBuildableManufacturer> MachineClass =
        Cache->GetBuildableClass<AFGBuildableManufacturer>(MachineType);

    // Decide flip for certain buildables
    const FTransform UnitTransform = MoveTransform(Location, bFlip);

    AFGBuildableManufacturer* SpawnedMachine = World->SpawnActor<AFGBuildableManufacturer>(MachineClass, UnitTransform);

    if (Recipe.IsSet())
    {
        TSubclassOf<UFGRecipe> RecipeClass = Cache->GetRecipeClass(Recipe.GetValue(), MachineClass, World);
        if (RecipeClass)
        {
            if (SpawnedMachine)
            {
                if (Underclock.IsSet() && RCO && Player)
                    RCO->Server_PasteSettings(SpawnedMachine, Player, RecipeClass, Underclock.GetValue() / 100.0f, 1.0f,
                                              nullptr, nullptr);
                else
                    SpawnedMachine->SetRecipe(RecipeClass);
            }
        }
    }

    BuildablesForBlueprint.Add(SpawnedMachine);

    UFGPowerConnectionComponent* MachinePowerConn = GetMachinePowerConn(SpawnedMachine);

    outPowerConn = MachinePowerConn;
    outBeltConn = GetConnections<UFGFactoryConnectionComponent>(SpawnedMachine);
    outPipeConn = GetConnections<UFGPipeConnectionComponent>(SpawnedMachine);
}

TArray<UFGFactoryConnectionComponent*> FBuildPlanGenerator::SpawnSplitterOrMerger(FVector Location,
                                                                                  EBuildable SplitterOrMerger)
{
    const bool bFlip = SplitterOrMerger == EBuildable::Merger;
    TSubclassOf<AFGBuildable> Class = Cache->GetBuildableClass<AFGBuildable>(SplitterOrMerger);
    const FTransform UnitTransform = MoveTransform(Location, bFlip);
    AFGBuildable* Spawned = World->SpawnActor<AFGBuildable>(Class, UnitTransform);
    BuildablesForBlueprint.Add(Spawned);
    return GetConnections<UFGFactoryConnectionComponent>(Spawned);
}

void FBuildPlanGenerator::SpawnBeltAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To)
{
    TSubclassOf<AFGBuildableConveyorBelt> BeltClass =
        Cache->GetBuildableClass<AFGBuildableConveyorBelt>(EBuildable::Belt);

    // Spawn the initial belt using the test helper
    AFGBuildable* Spawned = UFGTestBlueprintFunctionLibrary::SpawnSplineBuildable(BeltClass, From, To);
    AFGBuildableConveyorBelt* Belt = Cast<AFGBuildableConveyorBelt>(Spawned);

    // Convert world-space positions/tangents into belt local space before resplining
    const FVector FromLoc = From->GetComponentLocation();
    const FVector ToLoc = To->GetComponentLocation();
    const FVector TangentWorld = From->GetConnectorNormal() * FVector::Distance(FromLoc, ToLoc) * 1.5f;
    const FTransform BeltTransform = Belt->GetActorTransform();
    const FVector LocalFrom = BeltTransform.InverseTransformPosition(FromLoc);
    const FVector LocalTo = BeltTransform.InverseTransformPosition(ToLoc);
    const FVector LocalTangent = BeltTransform.InverseTransformVectorNoScale(TangentWorld);

    TArray<FSplinePointData> SplinePoints;
    SplinePoints.Add(FSplinePointData(LocalFrom, LocalTangent));
    SplinePoints.Add(FSplinePointData(LocalTo, LocalTangent));

    AFGBuildableConveyorBelt* ResplinedBelt = AFGBuildableConveyorBelt::Respline(Belt, SplinePoints);

    BuildablesForBlueprint.Add(ResplinedBelt);
}

TArray<UFGPipeConnectionComponent*> FBuildPlanGenerator::SpawnPipeCross(FVector Location)
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