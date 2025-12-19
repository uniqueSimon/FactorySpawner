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

    /**
     * Calculate the variant index based on port requirements.
     * The variants are ordered by pipe count (descending), then belt count (descending).
     *
     * @param MaxBelt Maximum available belt ports
     * @param MaxPipe Maximum available pipe ports
     * @param NeededBelt Required belt ports for the recipe
     * @param NeededPipe Required pipe ports for the recipe
     * @return The index of the variant in the configuration array
     *
     * Example: MaxBelt=2, MaxPipe=2
     *   Index 0: (2 pipe, 2 belt)
     *   Index 1: (2 pipe, 1 belt)
     *   Index 2: (2 pipe, 0 belt)
     *   Index 3: (1 pipe, 2 belt)
     *   Index 4: (1 pipe, 1 belt)
     *   Index 5: (1 pipe, 0 belt)
     */
    int32 GetPortVariantIndex(int32 MaxBelt, int32 MaxPipe, int32 NeededBelt, int32 NeededPipe)
    {
        const int32 NumBeltVariants = MaxBelt + 1;
        const int32 BeltOffset = MaxBelt - NeededBelt;
        const int32 PipeOffset = MaxPipe - NeededPipe;
        return PipeOffset * NumBeltVariants + BeltOffset;
    }

    // Machine configuration map (use helper factories from header)
    static const TMap<EBuildable, FMachineConfig> MachineConfigList = {
        {EBuildable::Constructor, MakeMachineConfig(8, {MakeMachineConnections(9, {MakeConnector(1, 0)})},
                                                    {MakeMachineConnections(9, {MakeConnector(0, 0)})})},
        {EBuildable::Smelter, MakeMachineConfig(5, {MakeMachineConnections(9, {MakeConnector(0, 0)})},
                                                {MakeMachineConnections(8, {MakeConnector(1, 0)})})},
        {EBuildable::Foundry,
         MakeMachineConfig(10, {MakeMachineConnections(11, {MakeConnector(2, -2), MakeConnector(0, 2, 2)})},
                           {MakeMachineConnections(8, {MakeConnector(1, -2)})})},
        {EBuildable::Assembler,
         MakeMachineConfig(9, {MakeMachineConnections(14, {MakeConnector(1, -2), MakeConnector(2, 2, 2)})},
                           {MakeMachineConnections(11, {MakeConnector(0, 0)})})},
        {EBuildable::OilRefinery,
         MakeMachineConfig(10,
                           {MakeMachineConnections(17, {MakeConnector(0, -2, 4)}, {MakeConnector(1, 2)}),
                            MakeMachineConnections(15, {}, {MakeConnector(1, 2)}),
                            MakeMachineConnections(15, {MakeConnector(0, -2)}, {})},
                           {MakeMachineConnections(17, {MakeConnector(1, -2, 4)}, {MakeConnector(0, 2)}),
                            MakeMachineConnections(15, {}, {MakeConnector(0, 2)}),
                            MakeMachineConnections(15, {MakeConnector(1, -2)}, {})})},
        {EBuildable::Blender,
         MakeMachineConfig(
             18,

             {/*2S,2L*/
              MakeMachineConnections(15, {MakeConnector(1, 2, 6), MakeConnector(2, 6, 8)},
                                     {MakeConnector(2, -6), MakeConnector(0, -2, 2)}),
              /*1S,2L*/
              MakeMachineConnections(15, {MakeConnector(1, 2, 6)}, {MakeConnector(2, -6), MakeConnector(0, -2, 2)}),
              /*0S,2L*/
              MakeMachineConnections(14, {}, {MakeConnector(2, -6), MakeConnector(0, -2, 2)}),
              /*2S,1L*/
              MakeMachineConnections(15, {MakeConnector(1, 2, 4), MakeConnector(2, 6, 6)}, {MakeConnector(2, -6)}),
              /*1S,1L*/
              MakeMachineConnections(15, {MakeConnector(1, 2, 4)}, {MakeConnector(2, -6)}),
              /*0S,1L*/
              MakeMachineConnections(12, {}, {MakeConnector(2, -6)})},

             {MakeMachineConnections(15, {MakeConnector(0, -2, 4)}, {MakeConnector(1, -6)}),
              MakeMachineConnections(12, {}, {MakeConnector(1, -6)}),
              MakeMachineConnections(12, {MakeConnector(0, -2)}, {})})},
        {EBuildable::Manufacturer,
         MakeMachineConfig(
             18,
             {MakeMachineConnections(
                  17, {MakeConnector(4, -6), MakeConnector(2, -2, 2), MakeConnector(1, 2, 4), MakeConnector(0, 6, 6)}),
              MakeMachineConnections(17, {MakeConnector(4, -6), MakeConnector(2, -2, 2), MakeConnector(1, 2, 4)})},
             {MakeMachineConnections(13, {MakeConnector(3, 0)})})},
        {EBuildable::Converter,
         MakeMachineConfig(16,
                           {/*2S*/ MakeMachineConnections(15, {MakeConnector(0, -2), MakeConnector(2, 2, 2)}),
                            /*1S*/ MakeMachineConnections(12, {MakeConnector(0, -2)}), MakeMachineConnections(8)},
                           {MakeMachineConnections(15, {MakeConnector(1, 2, 4)}, {MakeConnector(0, -2)}),
                            MakeMachineConnections(12, {}, {MakeConnector(0, -2)}),
                            MakeMachineConnections(12, {MakeConnector(1, 2)})})},
        {EBuildable::ParticleAccelerator,
         MakeMachineConfig(37,
                           {/*2S,1L*/ MakeMachineConnections(19, {MakeConnector(0, 12, 4), MakeConnector(1, 16, 6)},
                                                             {MakeConnector(0, 8)}),
                            /*1S,1L*/ MakeMachineConnections(19, {MakeConnector(0, 12, 4)}, {MakeConnector(0, 8)}),
                            /*1L*/ MakeMachineConnections(16, {}, {MakeConnector(0, 8)}),
                            /*2S*/ MakeMachineConnections(19, {MakeConnector(0, 12, 0), MakeConnector(1, 16, 2)}),
                            /*1S*/ MakeMachineConnections(17, {MakeConnector(0, 12)})},
                           {MakeMachineConnections(17, {MakeConnector(2, 14)})})},
        {EBuildable::QuantumEncoder,
         MakeMachineConfig(
             22,
             {MakeMachineConnections(33, {MakeConnector(2, -6, 4), MakeConnector(1, -2, 6), MakeConnector(3, 2, 8)},
                                     {MakeConnector(0, 6)})},
             {MakeMachineConnections(29, {MakeConnector(0, -2, 4)}, {MakeConnector(1, 2)})})},
        {EBuildable::Packager,
         MakeMachineConfig(8,
                           {MakeMachineConnections(9, {MakeConnector(1, 0)}, {MakeConnector(0, 0, 2)}),
                            MakeMachineConnections(9, {}, {MakeConnector(0, 0, 2)}),
                            MakeMachineConnections(9, {MakeConnector(1, 0)})},
                           {MakeMachineConnections(9, {MakeConnector(0, 0)}, {MakeConnector(1, 0, 2)}),
                            MakeMachineConnections(9, {}, {MakeConnector(1, 0, 2)}),
                            MakeMachineConnections(9, {MakeConnector(0, 0)})})}};

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

            int32 MaxBeltInput = Config.InputConnections[0].Belt.Num();
            int32 MaxPipeInput = Config.InputConnections[0].Pipe.Num();
            InputVariant = GetPortVariantIndex(MaxBeltInput, MaxPipeInput, SolidIn, LiquidIn);

            int32 MaxBeltOutput = Config.OutputConnections[0].Belt.Num();
            int32 MaxPipeOutput = Config.OutputConnections[0].Pipe.Num();
            OutputVariant = GetPortVariantIndex(MaxBeltOutput, MaxPipeOutput, SolidOut, LiquidOut);
        }
    }

    const FMachineConnections& InputConn = Config.InputConnections[InputVariant];
    const FMachineConnections& OutputConn = Config.OutputConnections[OutputVariant];

    if (RowIndex == 0)
        FirstMachineWidth = Config.Width * 100;
    else
    {
        YCursor += InputConn.Length * 100;
        XCursor = FMath::CeilToInt((Config.Width * 100 - FirstMachineWidth) / 2.0f / 100) * 100;
    }

    PlaceMachines(RowConfig, Config.Width * 100, InputConn, OutputConn);
    YCursor += OutputConn.Length * 100;
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
        FVector PoleLocation = FVector(XCursor - Width / 2.0f, YCursor - InputConnections.Length * 100 / 2.0f, 0);
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
        FVector Loc = MachineLocation +
                      FVector(Conn.LocationX * 100, -InputConnections.Length * 100 + 200, 100 + Conn.LocationY * 100);
        TArray<UFGFactoryConnectionComponent*> Splitter = SpawnSplitterOrMerger(Loc, EBuildable::Splitter);

        if (!bFirstUnitInRow)
        {
            UFGFactoryConnectionComponent* Prev;
            if (ConnectionQueue.Input.Dequeue(Prev))
                SpawnLiftOrBeltAndConnect(Prev, Splitter[1]);
        }
        ConnectionQueue.Input.Enqueue(Splitter[0]);
        SpawnLiftOrBeltAndConnect(Splitter[3], MachineBeltConn[Conn.Index]);
    }

    for (const FConnector& Conn : OutputConnections.Belt)
    {
        FVector Loc = MachineLocation +
                      FVector(Conn.LocationX * 100, OutputConnections.Length * 100 - 200, 100 + Conn.LocationY * 100);
        TArray<UFGFactoryConnectionComponent*> Merger = SpawnSplitterOrMerger(Loc, EBuildable::Merger);

        if (!bFirstUnitInRow)
        {
            UFGFactoryConnectionComponent* Prev;
            if (ConnectionQueue.Output.Dequeue(Prev))
                SpawnLiftOrBeltAndConnect(Merger[1], Prev);
        }
        ConnectionQueue.Output.Enqueue(Merger[0]);
        SpawnLiftOrBeltAndConnect(MachineBeltConn[Conn.Index], Merger[2]);
    }

    for (const FConnector& Conn : InputConnections.Pipe)
    {
        FVector Loc = MachineLocation +
                      FVector(Conn.LocationX * 100, -InputConnections.Length * 100 + 200, 175 + Conn.LocationY * 100);
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
        FVector Loc = MachineLocation +
                      FVector(Conn.LocationX * 100, OutputConnections.Length * 100 - 200, 175 + Conn.LocationY * 100);
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

void FBuildPlanGenerator::SpawnLiftOrBeltAndConnect(UFGFactoryConnectionComponent* From,
                                                    UFGFactoryConnectionComponent* To)
{
    FVector FromLoc = From->GetComponentLocation();
    FVector ToLoc = To->GetComponentLocation();
    FVector ConnDistance = FromLoc - ToLoc;

    // Calculate absolute component distances
    const float DZ = FMath::Abs(ConnDistance.Z);
    const float DX = FMath::Abs(ConnDistance.X);
    const float DY = FMath::Abs(ConnDistance.Y);

    // Quantum encoder has an input port that is further away and we need a belt instead of a lift
    // Lift needs some height difference and not too far apart horizontally
    const float MinVerticalForLift =400.0f;
    const float MaxHorizontalForLift =600.0f;

    const bool bUseLift = (DZ >= MinVerticalForLift) && (DX <= MaxHorizontalForLift) && (DY <= MaxHorizontalForLift);

    if (bUseLift)
    {
        SpawnLiftAndConnect(From, To);
    }
    else
    {
        SpawnBeltAndConnect(From, To);
    }
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