#include "BuildPlanGenerator.h"
#include "FactorySpawner.h"
#include "BuildableCache.h"
#include "ProductionPlan.h"
#include "Misc/ScopeExit.h"
#include "FGBlueprintSubsystem.h"
#include "FGObjectReference.h"
#include "HAL/IConsoleManager.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Tests/FGTestBlueprintFunctionLibrary.h"
#include "FGPlayerController.h"
#include "FGCharacterPlayer.h"
#include "FGPowerConnectionComponent.h"
#include "Buildables/FGBuildableWire.h"
#include "Buildables/FGBuildablePowerPole.h"
#include "FGFactoryConnectionComponent.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePipeline.h"
#include "FGPipeConnectionComponent.h"
#include "FGRecipe.h"

namespace
{
    constexpr double LiftSectionHeight = 4000.0;
    constexpr double LiftLaneSpacing = 1600.0;
    constexpr double ConveyorApproachMax = 220.0;

    using FConveyorRoute = TArray<FVector, TInlineAllocator<6>>;
    FConveyorRoute MakeConveyorRoute(const FVector& From, const FVector& To, double TrackX, double StartY, double EndY)
    {
        FConveyorRoute Route{From};
        const FVector Waypoints[] = {{From.X, StartY, From.Z}, {TrackX, StartY, From.Z},
            {TrackX, EndY, To.Z}, {To.X, EndY, To.Z}, To};
        for (const FVector& Point : Waypoints)
            if (!Point.Equals(Route.Last(), 1.0))
                Route.Add(Point);
        if (Route.Num() == 1)
            Route.Add(To);
        return Route;
    }

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
        {EBuildable::Constructor, MakeMachineConfig(8, 10, {MakeMachineConnections(9, {MakeConnector(1, 0)})},
                                                    {MakeMachineConnections(9, {MakeConnector(0, 0)})})},
        {EBuildable::Smelter, MakeMachineConfig(5, 10, {MakeMachineConnections(9, {MakeConnector(0, 0)})},
                                                {MakeMachineConnections(8, {MakeConnector(1, 0)})})},
        {EBuildable::Foundry,
         MakeMachineConfig(10, 10, {MakeMachineConnections(11, {MakeConnector(2, -2), MakeConnector(0, 2, 2)})},
                           {MakeMachineConnections(8, {MakeConnector(1, -2)})})},
        {EBuildable::Assembler,
         MakeMachineConfig(9, 16, {MakeMachineConnections(14, {MakeConnector(1, -2), MakeConnector(2, 2, 2)})},
                           {MakeMachineConnections(11, {MakeConnector(0, 0)})})},
        {EBuildable::OilRefinery,
         MakeMachineConfig(10, 30,
                           {MakeMachineConnections(17, {MakeConnector(0, -2, 4)}, {MakeConnector(1, 2)}),
                            MakeMachineConnections(15, {}, {MakeConnector(1, 2)}),
                            MakeMachineConnections(15, {MakeConnector(0, -2)}, {})},
                           {MakeMachineConnections(17, {MakeConnector(1, -2, 4)}, {MakeConnector(0, 2)}),
                            MakeMachineConnections(15, {}, {MakeConnector(0, 2)}),
                            MakeMachineConnections(15, {MakeConnector(1, -2)}, {})})},
        {EBuildable::Blender,
         MakeMachineConfig(
             18, 16,

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
             18, 20,
             {MakeMachineConnections(
                  17, {MakeConnector(4, -6), MakeConnector(2, -2, 2), MakeConnector(1, 2, 4), MakeConnector(0, 6, 6)}),
              MakeMachineConnections(17, {MakeConnector(4, -6), MakeConnector(2, -2, 2), MakeConnector(1, 2, 4)})},
             {MakeMachineConnections(13, {MakeConnector(3, 0)})})},
        {EBuildable::Converter,
         MakeMachineConfig(16, 16,
                           {/*2S*/ MakeMachineConnections(15, {MakeConnector(0, -2), MakeConnector(2, 2, 2)}),
                            /*1S*/ MakeMachineConnections(12, {MakeConnector(0, -2)}), MakeMachineConnections(8)},
                           {MakeMachineConnections(15, {MakeConnector(1, 2, 4)}, {MakeConnector(0, -2)}),
                            MakeMachineConnections(12, {}, {MakeConnector(0, -2)}),
                            MakeMachineConnections(12, {MakeConnector(1, 2)})})},
        {EBuildable::ParticleAccelerator,
         MakeMachineConfig(37, 35,
                           {/*2S,1L*/ MakeMachineConnections(19, {MakeConnector(0, 12, 4), MakeConnector(1, 16, 6)},
                                                             {MakeConnector(0, 8)}),
                            /*1S,1L*/ MakeMachineConnections(19, {MakeConnector(0, 12, 4)}, {MakeConnector(0, 8)}),
                            /*1L*/ MakeMachineConnections(16, {}, {MakeConnector(0, 8)}),
                            /*2S*/ MakeMachineConnections(19, {MakeConnector(0, 12, 0), MakeConnector(1, 16, 2)}),
                            /*1S*/ MakeMachineConnections(17, {MakeConnector(0, 12)})},
                           {MakeMachineConnections(17, {MakeConnector(2, 14)})})},
        {EBuildable::QuantumEncoder,
         MakeMachineConfig(
             22, 50,
             {MakeMachineConnections(33, {MakeConnector(2, -6, 4), MakeConnector(1, -2, 6), MakeConnector(3, 2, 8)},
                                     {MakeConnector(0, 6)})},
             {MakeMachineConnections(29, {MakeConnector(0, -2, 4)}, {MakeConnector(1, 2)})})},
        {EBuildable::CoalGenerator,
         MakeMachineConfig(10, 26, {MakeMachineConnections(20, {MakeConnector(0, -2, 4)}, {MakeConnector(0, 2)})},
                           {MakeMachineConnections(13)})},
        {EBuildable::FuelGenerator, MakeMachineConfig(20, 20, {MakeMachineConnections(14, {}, {MakeConnector(0, 0)})},
                                                      {MakeMachineConnections(10)})},
        {EBuildable::NuclearReactor, MakeMachineConfig(36, 43, {MakeMachineConnections(28, {MakeConnector(0, 2, 2)})},
                                                       {MakeMachineConnections(22, {MakeConnector(1, -2, 0)})})},
        {EBuildable::Packager,
         MakeMachineConfig(8, 8,
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
    if (PC)
    {
        Player = Cast<AFGCharacterPlayer>(PC->GetCharacter());
        RCO = PC->GetRemoteCallObjectOfClass<UFGManufacturerClipboardRCO>();
    }
}

bool FBuildPlanGenerator::Generate(const FFactoryCommand& Command, FString& OutError, const FProductionPlan* Plan)
{
    AFGBlueprintSubsystem* BlueprintSubsystem = AFGBlueprintSubsystem::Get(World);
    if (!BlueprintSubsystem || Command.Rows.IsEmpty())
    {
        OutError = TEXT("Blueprint subsystem or machine rows are unavailable");
        return false;
    }

    IConsoleVariable* Lightweight = nullptr;
    int32 PreviousLightweightMode = 0;
    ON_SCOPE_EXIT
    {
        if (Lightweight)
            Lightweight->SetWithCurrentPriority(PreviousLightweightMode);
    };

    ON_SCOPE_EXIT
    {
        for (AFGBuildable* Buildable : BuildablesForBlueprint)
        {
            if (IsValid(Buildable))
                Buildable->Destroy();
        }
    };

    Production = Plan;
    FloorHeight = Command.FloorHeightMeters * 100.0;
    bMultiFloor = Production || Command.Rows.ContainsByPredicate([](const FFactoryCommandToken& Row) { return Row.Floor > 1; });
    TArray<FPreparedRow> Rows;
    Rows.SetNum(Command.Rows.Num());
    for (int32 Index = 0; Index < Rows.Num(); ++Index)
        if (!PrepareRow(Command.Rows[Index], Index, Rows[Index], OutError))
            return false;
    if (Production && !PlanProductionLayout(Command, Rows, OutError))
        return false;
    for (int32 Index = 0; Index < Rows.Num(); ++Index)
        if (!ProcessRow(Command.Rows[Index], Rows[Index], OutError))
            return false;

    if (bMultiFloor)
    {
        Lightweight = IConsoleManager::Get().FindConsoleVariable(TEXT("FG.AllowLightweightManagement"));
        if (!Lightweight)
        {
            OutError = TEXT("Cannot retain foundation actors for blueprint serialization");
            return false;
        }
        // A foundation converted to a lightweight instance loses the actor path required by the blueprint archive.
        // EndPlay also reads this setting to decide whether to remove the actor from the buildable octree.
        PreviousLightweightMode = Lightweight->GetInt();
        Lightweight->SetWithCurrentPriority(0);
        if (!BuildFloorPlatforms(OutError))
            return false;
    }
    if (Production ? !BuildProductionLogistics(*Production, OutError) : (bMultiFloor && !ConnectRows(OutError)))
        return false;

    FTransform Origin = FTransform::Identity;
    FIntVector Dimensions(1, 1, 1);
    if (bMultiFloor)
    {
        FBox Bounds(ForceInit);
        for (AFGBuildable* Buildable : BuildablesForBlueprint)
        {
            Bounds += Buildable->GetActorLocation();
            Bounds += Buildable->GetComponentsBoundingBox(true);
        }
        const FVector Center = Bounds.GetCenter();
        Origin.SetTranslation(FVector(Center.X, Center.Y, -FoundationThickness));
        const FVector Size = Bounds.GetSize();
        Dimensions = FIntVector(FMath::Max(1, FMath::CeilToInt(Size.X / FoundationSize)),
                                FMath::Max(1, FMath::CeilToInt(Size.Y / FoundationSize)),
                                FMath::Max(1, FMath::CeilToInt((Bounds.Max.Z + FoundationThickness) / FoundationSize)));
    }

    UFGBlueprintDescriptor* ExistingDescriptor =
        BlueprintSubsystem->GetBlueprintDescriptorByNameString(TEXT("FactorySpawner"));
    if (ExistingDescriptor)
        BlueprintSubsystem->DeleteBlueprintDescriptor(ExistingDescriptor);

    FBlueprintRecord Record;
    Record.BlueprintName = TEXT("FactorySpawner");
    Record.BlueprintDescription = TEXT("Auto-generated blueprint");
    Record.Color = FLinearColor::White;

    BlueprintSubsystem->WriteBlueprintToArchive(Record, Origin, BuildablesForBlueprint, Dimensions);
    BlueprintSubsystem->RefreshBlueprintsAndDescriptors();
    if (!BlueprintSubsystem->GetBlueprintDescriptorByNameString(Record.BlueprintName))
    {
        OutError = TEXT("Generated blueprint was not found after saving");
        return false;
    }
    return true;
}

bool FBuildPlanGenerator::PrepareRow(const FFactoryCommandToken& RowConfig, int32 RowIndex, FPreparedRow& Row,
                                     FString& OutError)
{
    const FMachineConfig& Config = MachineConfigList[RowConfig.MachineType];
    int32 InputVariant = 0, OutputVariant = 0;
    TSubclassOf<UFGRecipe> RecipeClass;

    if (Production)
        RecipeClass = Production->Rows[RowIndex].Recipe;
    else if (RowConfig.Recipe.IsSet() && RowConfig.MachineType != EBuildable::CoalGenerator &&
        RowConfig.MachineType != EBuildable::FuelGenerator && RowConfig.MachineType != EBuildable::NuclearReactor)
    {
        TSubclassOf<AFGBuildableManufacturer> MachineClass =
            Cache->GetBuildableClass<AFGBuildableManufacturer>(RowConfig.MachineType);
        RecipeClass = Cache->GetRecipeClass(RowConfig.Recipe.GetValue(), MachineClass, World);
        if (!RecipeClass)
        {
            OutError = FString::Printf(TEXT("Recipe '%s' is not available on row %d"), **RowConfig.Recipe,
                                       RowIndex + 1);
            return false;
        }

    }

    if (RecipeClass)
    {
        const UFGRecipe* Recipe = RecipeClass.GetDefaultObject();
        int32 SolidIn = 0, LiquidIn = 0, SolidOut = 0, LiquidOut = 0;
        for (const FItemAmount& Item : Recipe->GetIngredients())
            UFGItemDescriptor::GetForm(Item.ItemClass) == EResourceForm::RF_SOLID ? ++SolidIn : ++LiquidIn;
        for (const FItemAmount& Item : Recipe->GetProducts())
            UFGItemDescriptor::GetForm(Item.ItemClass) == EResourceForm::RF_SOLID ? ++SolidOut : ++LiquidOut;

        const FMachineConnections& DefaultInput = Config.InputConnections[0];
        const FMachineConnections& DefaultOutput = Config.OutputConnections[0];
        InputVariant = GetPortVariantIndex(DefaultInput.Belt.Num(), DefaultInput.Pipe.Num(), SolidIn, LiquidIn);
        OutputVariant = GetPortVariantIndex(DefaultOutput.Belt.Num(), DefaultOutput.Pipe.Num(), SolidOut, LiquidOut);
        if (Production)
        {
            const auto SelectVariant = [](const TArray<FMachineConnections>& Variants, int32 Solid, int32 Fluid)
            {
                const int32 Exact = Variants.IndexOfByPredicate([=](const FMachineConnections& Variant)
                {
                    return Variant.Belt.Num() == Solid && Variant.Pipe.Num() == Fluid;
                });
                if (Exact != INDEX_NONE)
                    return Exact;
                return Variants.IndexOfByPredicate([=](const FMachineConnections& Variant)
                {
                    return Variant.Belt.Num() >= Solid && Variant.Pipe.Num() >= Fluid;
                });
            };
            InputVariant = SelectVariant(Config.InputConnections, SolidIn, LiquidIn);
            OutputVariant = SelectVariant(Config.OutputConnections, SolidOut, LiquidOut);
            if (InputVariant == INDEX_NONE || OutputVariant == INDEX_NONE)
            {
                OutError = FString::Printf(TEXT("No layout supports all ports on %s"), *RecipeClass->GetName());
                return false;
            }
        }
        else
        {
            if (!Config.InputConnections.IsValidIndex(InputVariant))
                InputVariant = 0;
            if (!Config.OutputConnections.IsValidIndex(OutputVariant))
                OutputVariant = 0;
        }
    }

    Row = {&Config, &Config.InputConnections[InputVariant], &Config.OutputConnections[OutputVariant],
           RecipeClass, FVector2D::ZeroVector, RowConfig.Floor};
    return true;
}

bool FBuildPlanGenerator::ProcessRow(const FFactoryCommandToken& RowConfig, const FPreparedRow& Row, FString& OutError)
{
    const FMachineConfig& Config = *Row.Machine;
    const FMachineConnections& InputConn = *Row.Input;
    const FMachineConnections& OutputConn = *Row.Output;
    FFloorLayout& Floor = Floors.FindOrAdd(Row.Floor);
    if (Production)
    {
        XCursor = Row.Position.X;
        YCursor = Row.Position.Y;
    }
    else
    {
        Floor.DeckZ = (Row.Floor - 1) * FloorHeight;
        YCursor = Floor.Footprint.bIsValid ? Floor.YCursor + InputConn.Length * 100.0 : 0;
        XCursor = Config.Width * 50.0;
        Floor.Footprint += FVector2D(-100, YCursor - FMath::Max(double(InputConn.Length), Config.Length / 2.0) * 100.0);
        Floor.Footprint += FVector2D(Config.Width * 100.0 * RowConfig.Count + 100,
                                    YCursor + FMath::Max(double(OutputConn.Length), Config.Length / 2.0) * 100.0);
        Floor.YCursor = YCursor + OutputConn.Length * 100.0;
    }
    RowZ = Floor.DeckZ;

    RowConveyors.Add({Row.Floor, Row.InputAisleY, Row.OutputAisleY});
    const int32 FirstBuildable = BuildablesForBlueprint.Num();
    PlaceMachines(RowConfig, Row.Recipe, Config.Width * 100, Config.Length * 100, InputConn, OutputConn);
    CachedPowerConnections.LastMachine = CachedPowerConnections.LastPole = nullptr;
    if (bBuildFailed)
    {
        OutError = FString::Printf(TEXT("Could not build machine row %d on floor %d"), RowConveyors.Num(), Row.Floor);
        return false;
    }
    if (bMultiFloor)
    {
        for (int32 Index = FirstBuildable; Index < BuildablesForBlueprint.Num(); ++Index)
        {
            const AFGBuildable* Buildable = BuildablesForBlueprint[Index];
            if (Buildable->GetActorLocation().Z == RowZ && Buildable->IsA<AFGBuildableFactory>())
            {
                const FBox Bounds = Buildable->GetComponentsBoundingBox(true);
                if (Bounds.IsValid)
                {
                    Floor.Top = FMath::Max(Floor.Top, Bounds.Max.Z);
                    if (Production)
                        MachineClearances.Add(Bounds.ExpandBy(FVector(65.0, 65.0, 60.0)));
                }
            }
        }
    }
    return true;
}

void FBuildPlanGenerator::PlaceMachines(const FFactoryCommandToken& RowConfig, TSubclassOf<UFGRecipe> Recipe,
                                        int32 Width, int32 Length, const FMachineConnections& InputConnections,
                                        const FMachineConnections& OutputConnections)
{
    ConnectionQueue.Input.Empty();
    ConnectionQueue.Output.Empty();
    ConnectionQueue.PipeInput.Empty();
    ConnectionQueue.PipeOutput.Empty();

    for (int32 i = 0; i < RowConfig.Count && !bBuildFailed; ++i)
    {
        CalculateMachineSetup(RowConfig.MachineType, Recipe, RowConfig.ClockPercent, Width, Length,
                              InputConnections, OutputConnections, i == 0, i % 2 == 0, i == RowConfig.Count - 1);
        XCursor += Width;
    }
}
void FBuildPlanGenerator::CalculateMachineSetup(EBuildable MachineType, TSubclassOf<UFGRecipe> Recipe,
                                                const TOptional<float>& Underclock, int32 Width, int32 Length,
                                                const FMachineConnections& InputConnections,
                                                const FMachineConnections& OutputConnections, bool bFirstUnitInRow,
                                                bool bEvenIndex, bool bLastIndex)
{
    UFGPowerConnectionComponent* MachinePowerConn;
    TArray<UFGFactoryConnectionComponent*> MachineBeltConn;
    TArray<UFGPipeConnectionComponent*> MachinePipeConn;
    const FVector MachineLocation(XCursor, YCursor, RowZ);
    if (!SpawnMachine(MachineLocation, MachineType, Recipe, Underclock, MachinePowerConn, MachineBeltConn,
                      MachinePipeConn) || !MachinePowerConn)
    {
        bBuildFailed = true;
        return;
    }

    if (!bEvenIndex)
    {
        if (bLastIndex && CachedPowerConnections.LastPole)
            SpawnWireAndConnect(CachedPowerConnections.LastPole, MachinePowerConn);
        else
            CachedPowerConnections.LastMachine = MachinePowerConn;
    }
    else
    {
        FVector PoleLocation = FVector(XCursor - Width / 2.0, YCursor - Length / 2.0, RowZ);
        UFGPowerConnectionComponent* Pole = SpawnPowerPole(PoleLocation);
        SpawnWireAndConnect(Pole, MachinePowerConn);

        if (bFirstUnitInRow)
        {
            if (!FirstMachinePole)
                FirstMachinePole = Pole;
            if (CachedPowerConnections.FirstPole)
                WirePowerRoute(CachedPowerConnections.FirstPole, Pole);
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

    for (const FConnector& Conn : InputConnections.Belt)
    {
        FVector Loc = MachineLocation +
                      FVector(Conn.LocationX * 100, -InputConnections.Length * 100 + 200, 100 + Conn.LocationY * 100);
        TArray<UFGFactoryConnectionComponent*> Splitter = SpawnSplitterOrMerger(Loc, EBuildable::Splitter);
        if (Splitter.Num() < 4 || !MachineBeltConn.IsValidIndex(Conn.Index))
        {
            bBuildFailed = true;
            return;
        }

        if (!bFirstUnitInRow)
        {
            UFGFactoryConnectionComponent* Prev;
            if (ConnectionQueue.Input.Dequeue(Prev))
                SpawnLiftOrBeltAndConnect(Prev, Splitter[1]);
        }
        ConnectionQueue.Input.Enqueue(Splitter[0]);
        SpawnLiftOrBeltAndConnect(Splitter[3], MachineBeltConn[Conn.Index]);
        if (bFirstUnitInRow && bMultiFloor && Recipe)
        {
            RecordRowPort(MachineBeltConn[Conn.Index], Splitter, EFactoryConnectionDirection::FCD_INPUT,
                          Recipe->GetDefaultObject<UFGRecipe>()->GetIngredients(), RowConveyors.Last().Inputs);
        }
    }

    // NuclearReactor outputs are at the input side
    for (const FConnector& Conn : OutputConnections.Belt)
    {
        int32 YOffset = MachineType == EBuildable::NuclearReactor ? -InputConnections.Length * 100 + 200
                                                                  : OutputConnections.Length * 100 - 200;
        FVector Loc = MachineLocation + FVector(Conn.LocationX * 100, YOffset, 100 + Conn.LocationY * 100);
        TArray<UFGFactoryConnectionComponent*> Merger = SpawnSplitterOrMerger(Loc, EBuildable::Merger);
        if (Merger.Num() < 4 || !MachineBeltConn.IsValidIndex(Conn.Index))
        {
            bBuildFailed = true;
            return;
        }

        if (!bFirstUnitInRow)
        {
            UFGFactoryConnectionComponent* Prev;
            if (ConnectionQueue.Output.Dequeue(Prev))
                SpawnLiftOrBeltAndConnect(Merger[1], Prev);
        }
        ConnectionQueue.Output.Enqueue(Merger[0]);
        SpawnLiftOrBeltAndConnect(MachineBeltConn[Conn.Index],
                                  MachineType == EBuildable::NuclearReactor ? Merger[3] : Merger[2]);
        if (bFirstUnitInRow && bMultiFloor && Recipe)
        {
            RecordRowPort(MachineBeltConn[Conn.Index], Merger, EFactoryConnectionDirection::FCD_OUTPUT,
                          Recipe->GetDefaultObject<UFGRecipe>()->GetProducts(), RowConveyors.Last().Outputs);
        }
    }

    for (const FConnector& Conn : InputConnections.Pipe)
    {
        FVector Loc = MachineLocation +
                      FVector(Conn.LocationX * 100, -InputConnections.Length * 100 + 200, 175 + Conn.LocationY * 100);
        TArray<UFGPipeConnectionComponent*> Cross = SpawnPipeCross(Loc);
        if (Cross.Num() < 4 || !MachinePipeConn.IsValidIndex(Conn.Index))
        {
            bBuildFailed = true;
            return;
        }

        if (!bFirstUnitInRow)
        {
            UFGPipeConnectionComponent* Prev;
            if (ConnectionQueue.PipeInput.Dequeue(Prev))
                SpawnPipeAndConnect(Prev, Cross[3]);
        }
        ConnectionQueue.PipeInput.Enqueue(Cross[0]);
        SpawnPipeAndConnect(MachinePipeConn[Conn.Index], Cross[1]);
        if (bFirstUnitInRow && Production && Recipe)
            RecordPipePort(MachinePipeConn[Conn.Index], Cross[3], Recipe.GetDefaultObject()->GetIngredients(),
                           RowConveyors.Last().PipeInputs);
    }

    for (const FConnector& Conn : OutputConnections.Pipe)
    {
        FVector Loc = MachineLocation +
                      FVector(Conn.LocationX * 100, OutputConnections.Length * 100 - 200, 175 + Conn.LocationY * 100);
        TArray<UFGPipeConnectionComponent*> Cross = SpawnPipeCross(Loc);
        if (Cross.Num() < 4 || !MachinePipeConn.IsValidIndex(Conn.Index))
        {
            bBuildFailed = true;
            return;
        }

        if (!bFirstUnitInRow)
        {
            UFGPipeConnectionComponent* Prev;
            if (ConnectionQueue.PipeOutput.Dequeue(Prev))
                SpawnPipeAndConnect(Prev, Cross[3]);
        }
        ConnectionQueue.PipeOutput.Enqueue(Cross[0]);
        SpawnPipeAndConnect(MachinePipeConn[Conn.Index], Cross[2]);
        if (bFirstUnitInRow && Production && Recipe)
            RecordPipePort(MachinePipeConn[Conn.Index], Cross[3], Recipe.GetDefaultObject()->GetProducts(),
                           RowConveyors.Last().PipeOutputs);
    }
}

void FBuildPlanGenerator::SpawnWireAndConnect(UFGPowerConnectionComponent* A, UFGPowerConnectionComponent* B)
{
    TSubclassOf<AFGBuildableWire> PowerLineClass = Cache->GetBuildableClass<AFGBuildableWire>(EBuildable::PowerLine);
    AFGBuildableWire* Wire = A && B ? World->SpawnActor<AFGBuildableWire>(PowerLineClass, FTransform::Identity) : nullptr;
    if (!Wire)
    {
        bBuildFailed = true;
        return;
    }
    BuildablesForBlueprint.Add(Wire);
    if (A->GetNumFreeConnections() <= 0 || B->GetNumFreeConnections() <= 0 || !Wire->Connect(A, B))
        bBuildFailed = true;
}

UFGPowerConnectionComponent* FBuildPlanGenerator::SpawnPowerPole(const FVector& Location)
{
    const FTransform UnitTransform = MoveTransform(Location);
    TSubclassOf<AFGBuildablePowerPole> PowerPoleClass =
        Cache->GetBuildableClass<AFGBuildablePowerPole>(EBuildable::PowerPole);
    AFGBuildablePowerPole* SpawnedPowerPole = World->SpawnActor<AFGBuildablePowerPole>(PowerPoleClass, UnitTransform);
    if (!SpawnedPowerPole)
        return nullptr;
    BuildablesForBlueprint.Add(SpawnedPowerPole);
    UFGPowerConnectionComponent* PolePowerConnection = SpawnedPowerPole->GetPowerConnection(0);
    return PolePowerConnection;
}

AFGBuildable* FBuildPlanGenerator::SpawnMachine(const FVector& Location, EBuildable MachineType,
                                                  TSubclassOf<UFGRecipe> Recipe, const TOptional<float>& Underclock,
                                       UFGPowerConnectionComponent*& outPowerConn,
                                       TArray<UFGFactoryConnectionComponent*>& outBeltConn,
                                       TArray<UFGPipeConnectionComponent*>& outPipeConn)
{
    FTransform Transform =
        MoveTransform(Location, MachineType == EBuildable::OilRefinery || MachineType == EBuildable::CoalGenerator ||
                                    MachineType == EBuildable::NuclearReactor);

    AFGBuildable* Spawned = nullptr;

    if (MachineType == EBuildable::CoalGenerator || MachineType == EBuildable::FuelGenerator ||
        MachineType == EBuildable::NuclearReactor)
    {
        TSubclassOf<AFGBuildableFactory> Class = Cache->GetBuildableClass<AFGBuildableFactory>(MachineType);
        Spawned = World->SpawnActor<AFGBuildableFactory>(Class, Transform);
    }
    else
    {
        TSubclassOf<AFGBuildableManufacturer> ManClass =
            Cache->GetBuildableClass<AFGBuildableManufacturer>(MachineType);
        AFGBuildableManufacturer* Man = World->SpawnActor<AFGBuildableManufacturer>(ManClass, Transform);
        if (Man && Recipe)
        {
            Man->SetRecipe(Recipe);
            if (Underclock.IsSet() && Production)
                Man->SetPendingPotential(Underclock.GetValue() / 100.0f);
            else if (Underclock.IsSet() && RCO && Player)
                RCO->Server_PasteSettings(Man, Player, Recipe, Underclock.GetValue() / 100.0f, 1.0f, nullptr, nullptr);
        }
        Spawned = Man;
    }

    if (!Spawned)
        return nullptr;

    BuildablesForBlueprint.Add(Spawned);

    TArray<UFGPowerConnectionComponent*> PowerConns = GetConnections<UFGPowerConnectionComponent>(Spawned);
    outPowerConn = PowerConns.IsEmpty() ? nullptr : PowerConns[0];
    outBeltConn = GetConnections<UFGFactoryConnectionComponent>(Spawned);
    outPipeConn = GetConnections<UFGPipeConnectionComponent>(Spawned);
    return Spawned;
}

TArray<UFGFactoryConnectionComponent*> FBuildPlanGenerator::SpawnSplitterOrMerger(const FVector& Location,
                                                                                  EBuildable SplitterOrMerger)
{
    const bool bFlip = SplitterOrMerger == EBuildable::Merger;
    TSubclassOf<AFGBuildable> Class = Cache->GetBuildableClass<AFGBuildable>(SplitterOrMerger);
    const FTransform UnitTransform = MoveTransform(Location, bFlip);
    AFGBuildable* Spawned = World->SpawnActor<AFGBuildable>(Class, UnitTransform);
    if (!Spawned)
        return {};
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
    const float MinVerticalForLift = 400.0f;
    const float MaxHorizontalForLift = 600.0f;

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

bool FBuildPlanGenerator::CanRunBeltDirectly(const UFGFactoryConnectionComponent* From,
                                             const UFGFactoryConnectionComponent* To) const
{
    const FVector Start = From->GetComponentLocation();
    const FVector End = To->GetComponentLocation();
    const double Distance = FVector::Distance(Start, End);
    if (Distance > 12000.0)
        return false;

    FBox Span(Start, Start);
    Span += End;
    Span = Span.ExpandBy(ConveyorApproachMax);
    TArray<const FBox*, TInlineAllocator<32>> Obstacles;
    for (const FBox& Box : MachineClearances)
        if (Box.Intersect(Span))
            Obstacles.Add(&Box);
    if (Obstacles.IsEmpty())
        return true;

    const double Approach = FMath::Min(Distance * 0.65, ConveyorApproachMax);
    const FVector StartTangent = From->GetConnectorNormal() * Approach;
    const FVector EndTangent = -To->GetConnectorNormal() * Approach;
    const int32 Steps = FMath::Max(3, FMath::CeilToInt(Distance / 200.0));
    FVector Previous = Start;
    for (int32 Step = 1; Step <= Steps; ++Step)
    {
        const FVector Point = FMath::CubicInterp(Start, StartTangent, End, EndTangent, double(Step) / Steps);
        const FVector Direction = Point - Previous;
        if (Obstacles.ContainsByPredicate([&](const FBox* Box)
        {
            return FMath::LineBoxIntersection(*Box, Previous, Point, Direction);
        }))
            return false;
        Previous = Point;
    }
    return true;
}

bool FBuildPlanGenerator::CanRunBeltViaAisle(const FVector& Start, const FVector& End, double TrackX,
                                                double StartY, double EndY) const
{
    const auto Route = MakeConveyorRoute(Start, End, TrackX, StartY, EndY);
    for (int32 Index = 1; Index < Route.Num(); ++Index)
    {
        if (MachineClearances.ContainsByPredicate([&](const FBox& Box)
        {
            return FMath::LineBoxIntersection(Box, Route[Index - 1], Route[Index], Route[Index] - Route[Index - 1]);
        }))
            return false;
    }
    return true;
}

bool FBuildPlanGenerator::SpawnBeltAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To,
                                                 TOptional<double> BypassX, TOptional<double> FromAisleY,
                                                 TOptional<double> ToAisleY)
{
    if (!From || !To || From->GetConnection() || To->GetConnection())
    {
        bBuildFailed = true;
        return false;
    }

    const TSubclassOf<AFGBuildableConveyorBelt> BeltClass =
        Cache->GetBuildableClass<AFGBuildableConveyorBelt>(EBuildable::Belt);
    const FVector FromLoc = From->GetComponentLocation();
    const FVector ToLoc = To->GetComponentLocation();
    const FTransform BeltTransform(From->GetConnectorNormal().Rotation(), FromLoc);
    auto* Belt = BeltClass ? World->SpawnActorDeferred<AFGBuildableConveyorBelt>(BeltClass, BeltTransform,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
    if (!Belt)
    {
        bBuildFailed = true;
        return false;
    }

    // Set the final shape before BeginPlay registers the belt with the buildable subsystem.
    TArray<FSplinePointData>& SplinePoints = *Belt->GetMutableSplinePointData();
    SplinePoints.Reset(4);
    const auto AddPoint = [&](const FVector& Location, const FVector& Tangent)
    {
        SplinePoints.Emplace(BeltTransform.InverseTransformPosition(Location),
                             BeltTransform.InverseTransformVectorNoScale(Tangent));
    };

    if (BypassX && (FromAisleY || ToAisleY || FMath::Abs(FromLoc.Y - ToLoc.Y) >= 300.0))
    {
        const double StartY = FromAisleY.Get(FromLoc.Y);
        const double EndY = ToAisleY.Get(ToLoc.Y);
        const auto Route = MakeConveyorRoute(FromLoc, ToLoc, *BypassX, StartY, EndY);
        AddPoint(FromLoc, From->GetConnectorNormal() * FMath::Min(220.0, FVector::Dist(Route[0], Route[1]) * 0.65));
        for (int32 Index = 1; Index + 1 < Route.Num(); ++Index)
        {
            const FVector Before = Route[Index] - Route[Index - 1];
            const FVector After = Route[Index + 1] - Route[Index];
            const double Radius = FMath::Min(220.0, 0.5 * FMath::Min(Before.Size(), After.Size()));
            AddPoint(Route[Index], (Before.GetSafeNormal() + After.GetSafeNormal()).GetSafeNormal() * Radius);
        }
        AddPoint(ToLoc, -To->GetConnectorNormal() * FMath::Min(220.0, FVector::Dist(Route.Last(), Route[Route.Num() - 2]) * 0.65));
    }
    else
    {
        const double TangentLength = Production
            ? FMath::Min(FVector::Distance(FromLoc, ToLoc) * 0.65, ConveyorApproachMax)
            : FMath::Min(FVector::Distance(FromLoc, ToLoc) * 1.5, 1600.0);
        AddPoint(FromLoc, From->GetConnectorNormal() * TangentLength);
        AddPoint(ToLoc, -To->GetConnectorNormal() * TangentLength);
    }

    Belt->FinishSpawning(BeltTransform);
    if (!IsValid(Belt))
    {
        bBuildFailed = true;
        return false;
    }
    BuildablesForBlueprint.Add(Belt);
    auto* Input = Belt->GetConnection0();
    auto* Output = Belt->GetConnection1();
    if (!Input || !Output || Input->GetConnection() || Output->GetConnection())
    {
        bBuildFailed = true;
        return false;
    }
    From->SetConnection(Input);
    Output->SetConnection(To);
    const bool bConnected = From->GetConnection() == Input && To->GetConnection() == Output;
    bBuildFailed |= !bConnected;
    return bConnected;
}

void FBuildPlanGenerator::SpawnLiftAndConnect(UFGFactoryConnectionComponent* From, UFGFactoryConnectionComponent* To)
{
    // Get the connection locations.
    const FVector FromLoc = From->GetComponentLocation();
    const FVector ToLoc = To->GetComponentLocation();
    AFGBuildableConveyorLift* Lift = SpawnLift(FromLoc + FVector(0, 300, 0), ToLoc.Z, 270.0, 90.0);
    if (!Lift || From->GetConnection() || To->GetConnection())
    {
        bBuildFailed = true;
        return;
    }
    From->SetConnection(Lift->GetConnection0());
    Lift->GetConnection1()->SetConnection(To);
    Lift->SetupConnections();
}

AFGBuildableConveyorLift* FBuildPlanGenerator::SpawnLift(const FVector& Location, double OutputZ,
                                                          double InputYaw, double OutputYaw)
{
    const TSubclassOf<AFGBuildableConveyorLift> LiftClass =
        Cache->GetBuildableClass<AFGBuildableConveyorLift>(EBuildable::Lift);
    if (!LiftClass)
        return nullptr;

    // Use Unreal's reflection system to set the private mTopTransform property.
    const FStructProperty* TopProperty = FindFProperty<FStructProperty>(LiftClass, TEXT("mTopTransform"));
    if (!TopProperty || TopProperty->Struct != TBaseStructure<FTransform>::Get())
        return nullptr;

    const FTransform InputTransform(FRotator(0, InputYaw, 0), Location);
    AFGBuildableConveyorLift* Lift = World->SpawnActorDeferred<AFGBuildableConveyorLift>(LiftClass, InputTransform);
    if (!Lift)
        return nullptr;

    // Calculate the output transform relative to the lift's base.
    const FTransform TopTransform(FRotator(0, OutputYaw - InputYaw, 0), FVector(0, 0, OutputZ - Location.Z));
    TopProperty->CopyCompleteValue(TopProperty->ContainerPtrToValuePtr<void>(Lift), &TopTransform);
    Lift->FinishSpawning(InputTransform);
    Lift->SetupConnections();
    BuildablesForBlueprint.Add(Lift);
    return Lift->GetConnection0() && Lift->GetConnection1() ? Lift : nullptr;
}

bool FBuildPlanGenerator::BuildFloorPlatforms(FString& OutError)
{
    const TSubclassOf<AFGBuildable> FoundationClass = Cache->GetBuildableClass<AFGBuildable>(EBuildable::Foundation);
    if (!FoundationClass)
    {
        OutError = TEXT("The 8m x 1m foundation class could not be loaded");
        return false;
    }

    TArray<int32> FloorNumbers;
    Floors.GetKeys(FloorNumbers);
    FloorNumbers.Sort();
    for (int32 Index = 0; Index < FloorNumbers.Num(); ++Index)
    {
        const int32 FloorNumber = FloorNumbers[Index];
        const FFloorLayout& Floor = Floors.FindChecked(FloorNumber);
        const double DeckZ = Floor.DeckZ;
        if (Index + 1 < FloorNumbers.Num() &&
            Floor.Top > Floors.FindChecked(FloorNumbers[Index + 1]).DeckZ - FoundationThickness + KINDA_SMALL_NUMBER)
        {
            OutError = FString::Printf(TEXT("Machines on floor %d reach the next platform. Increase floorHeight."),
                                       FloorNumber);
            return false;
        }

        const int32 MinX = FMath::FloorToInt(Floor.Footprint.Min.X / FoundationSize);
        const int32 MinY = FMath::FloorToInt(Floor.Footprint.Min.Y / FoundationSize);
        const int32 MaxX = FMath::CeilToInt(Floor.Footprint.Max.X / FoundationSize);
        const int32 MaxY = FMath::CeilToInt(Floor.Footprint.Max.Y / FoundationSize);
        if (Production)
            UE_LOG(LogFactorySpawner, Display,
                TEXT("Layout floor %d (%s): %dx%d tiles, deck %.1f m, machine top %.1f m from deck, spacing %.1f m"),
                Index + 1, Floor.bEquipment ? TEXT("equipment") : TEXT("production"), MaxX - MinX, MaxY - MinY,
                DeckZ / 100.0, (Floor.Top - DeckZ) / 100.0, Floor.RequiredRise / 100.0);
        PlatformBounds += FVector2D(MinX * FoundationSize, MinY * FoundationSize);
        PlatformBounds += FVector2D(MaxX * FoundationSize, MaxY * FoundationSize);
        for (int32 X = MinX; X < MaxX; ++X)
        {
            for (int32 Y = MinY; Y < MaxY; ++Y)
            {
                const FVector Location((X + 0.5) * FoundationSize, (Y + 0.5) * FoundationSize,
                                       DeckZ - FoundationThickness / 2.0);
                FActorSpawnParameters Spawn;
                Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                AFGBuildable* Foundation = World->SpawnActor<AFGBuildable>(FoundationClass, MoveTransform(Location), Spawn);
                if (!IsValid(Foundation) || !FObjectReferenceDisc(Foundation).IsInLevel())
                {
                    OutError = FString::Printf(TEXT("Could not build %s on floor %d at %s"),
                                               *FoundationClass->GetName(), FloorNumber, *Location.ToCompactString());
                    return false;
                }
                BuildablesForBlueprint.Add(Foundation);
            }
        }
    }
    return true;
}

void FBuildPlanGenerator::RecordRowPort(UFGFactoryConnectionComponent* MachinePort,
                                         const TArray<UFGFactoryConnectionComponent*>& ManifoldPorts,
                                         EFactoryConnectionDirection Direction, const TArray<FItemAmount>& RecipeItems,
                                         TArray<FItemPort>& RowPorts)
{
    TArray<TSubclassOf<UFGItemDescriptor>, TInlineAllocator<4>> SolidItems;
    for (const FItemAmount& Item : RecipeItems)
    {
        if (UFGItemDescriptor::GetForm(Item.ItemClass) == EResourceForm::RF_SOLID)
            SolidItems.AddUnique(Item.ItemClass);
    }

    TSubclassOf<UFGItemDescriptor> Item;
    if (const int32 Slot = MachinePort->GetInventoryAccessIndex(); Slot >= 0)
    {
        UFGInventoryComponent* Inventory = MachinePort->GetInventory();
        if (Inventory && Slot < Inventory->GetSizeLinear())
            Item = Inventory->GetAllowedItemOnIndex(Slot);
    }
    const auto AlreadyUsed = [&](TSubclassOf<UFGItemDescriptor> Candidate)
    {
        return RowPorts.ContainsByPredicate([&](const FItemPort& Port) { return Port.Item == Candidate; });
    };
    if (MachinePort->GetDirection() != Direction)
        return;
    if (Production && (!SolidItems.Contains(Item) || AlreadyUsed(Item)))
    {
        if (const auto* Unassigned = SolidItems.FindByPredicate([&](const auto& Candidate) { return !AlreadyUsed(Candidate); }))
            Item = *Unassigned;
    }
    else if (!Item && SolidItems.Num() == 1)
        Item = SolidItems[0];
    if (!Item || !SolidItems.Contains(Item) || (Production && AlreadyUsed(Item)))
        return;

    for (UFGFactoryConnectionComponent* Port : ManifoldPorts)
    {
        if (Port->GetDirection() == Direction && !Port->GetConnection())
        {
            RowPorts.Add({Item, Port});
            return;
        }
    }
}

bool FBuildPlanGenerator::ConnectRows(FString& OutError)
{
    // Only previous rows are eligible suppliers, so no connection creates a production loop.
    TMap<TSubclassOf<UFGItemDescriptor>, TArray<int32>> SupplierRows;
    int32 RouteCount = 0;
    for (int32 RowIndex = 0; RowIndex < RowConveyors.Num(); ++RowIndex)
    {
        const FRowConveyors& Row = RowConveyors[RowIndex];
        for (const FItemPort& Input : Row.Inputs)
        {
            TArray<int32>* Suppliers = SupplierRows.Find(Input.Item);
            if (!Suppliers || Suppliers->IsEmpty())
                continue;

            const int32 SourceIndex = Suppliers->Pop(EAllowShrinking::No);
            const FRowConveyors& Source = RowConveyors[SourceIndex];
            const FItemPort* Output = Source.Outputs.FindByPredicate([&](const FItemPort& Port)
            {
                return Port.Item == Input.Item && !Port.Connection->GetConnection();
            });
            const double ShaftX = PlatformBounds.Min.X - 1000.0 - RouteCount * LiftLaneSpacing;
            if (!Output || !ConnectRowConveyors(Output->Connection, Input.Connection, ShaftX, Source.Floor != Row.Floor))
            {
                OutError = FString::Printf(TEXT("Could not connect the conveyor from row %d to row %d"),
                                           SourceIndex + 1, RowIndex + 1);
                return false;
            }
            ++RouteCount;
        }

        for (const FItemPort& Output : Row.Outputs)
            SupplierRows.FindOrAdd(Output.Item).Add(RowIndex);
    }
    return true;
}

bool FBuildPlanGenerator::ConnectRowConveyors(UFGFactoryConnectionComponent* From,
                                               UFGFactoryConnectionComponent* To, double ShaftX, bool bCrossFloor,
                                               TOptional<double> FromAisleY, TOptional<double> ToAisleY)
{
    if (!bCrossFloor)
        return SpawnBeltAndConnect(From, To, ShaftX, FromAisleY, ToAisleY);

    const FVector FromLocation = From->GetComponentLocation();
    const FVector ToLocation = To->GetComponentLocation();
    const double ShaftY = (FromAisleY.Get(FromLocation.Y) + ToAisleY.Get(ToLocation.Y)) / 2.0;
    const int32 Sections = FMath::Max(1, FMath::CeilToInt(FMath::Abs(ToLocation.Z - FromLocation.Z) / LiftSectionHeight));
    const double SectionHeight = (ToLocation.Z - FromLocation.Z) / Sections;

    // Alternate the shafts to give the intermediate belts room to connect consecutive lifts.
    UFGFactoryConnectionComponent* Previous = From;
    double LastShaftX = ShaftX;
    for (int32 Section = 0; Section < Sections; ++Section)
    {
        const bool bFirstPosition = Section % 2 == 0;
        const bool bLeft = ShaftX < PlatformBounds.Min.X;
        const bool bStaggerAlongLane = Production != nullptr;
        const double X = ShaftX + (!bFirstPosition && !bStaggerAlongLane ? (bLeft ? -800.0 : 800.0) : 0.0);
        const double Y = ShaftY + (!bFirstPosition && bStaggerAlongLane ? ProductionLiftStagger : 0.0);
        const double InputZ = FromLocation.Z + Section * SectionHeight;
        const double Facing = bLeft ? 0.0 : 180.0;
        const double BetweenLifts = bStaggerAlongLane ? (bFirstPosition ? 90.0 : -90.0)
                                                     : Facing + (bFirstPosition ? 180.0 : 0.0);
        const double InputYaw = Section == 0 ? Facing : BetweenLifts;
        const double OutputYaw = Section == Sections - 1 ? Facing : BetweenLifts;
        AFGBuildableConveyorLift* Lift = SpawnLift(FVector(X, Y, InputZ), InputZ + SectionHeight, InputYaw, OutputYaw);
        if (!Lift)
            return false;
        const bool bLeaveAisle = Section == 0 && FromAisleY && !CanRunBeltDirectly(Previous, Lift->GetConnection0());
        if (!SpawnBeltAndConnect(Previous, Lift->GetConnection0(),
            bLeaveAisle ? TOptional<double>(X) : TOptional<double>(), bLeaveAisle ? FromAisleY : TOptional<double>()))
            return false;
        LastShaftX = X;
        Previous = Lift->GetConnection1();
    }
    const bool bEnterAisle = ToAisleY && !CanRunBeltDirectly(Previous, To);
    return bEnterAisle ? SpawnBeltAndConnect(Previous, To, LastShaftX, {}, ToAisleY)
                       : SpawnBeltAndConnect(Previous, To);
}

TArray<UFGPipeConnectionComponent*> FBuildPlanGenerator::SpawnPipeCross(const FVector& Location, const FRotator& Rotation)
{
    TSubclassOf<AFGBuildable> Class = Cache->GetBuildableClass<AFGBuildable>(EBuildable::PipeCross);
    const FTransform UnitTransform(Rotation, Location);
    AFGBuildable* Spawned = World->SpawnActor<AFGBuildable>(Class, UnitTransform);
    if (!Spawned)
        return {};
    BuildablesForBlueprint.Add(Spawned);
    return GetConnections<UFGPipeConnectionComponent>(Spawned);
}

bool FBuildPlanGenerator::SpawnPipeAndConnect(UFGPipeConnectionComponent* From, UFGPipeConnectionComponent* To)
{
    if (From && To && !From->GetConnection() && !To->GetConnection())
    {
        const auto PipeClass = Cache->GetBuildableClass<AFGBuildablePipeline>(EBuildable::Pipeline);
        auto* Pipe = PipeClass ? Cast<AFGBuildablePipeline>(
            UFGTestBlueprintFunctionLibrary::SpawnSplineBuildable(PipeClass, From, To)) : nullptr;
        if (Pipe)
        {
            BuildablesForBlueprint.Add(Pipe);
            if (From->GetConnection() && To->GetConnection() && Pipe->GetSplinePointData().Num() >= 2)
                return true;
        }
    }
    UE_LOG(LogFactorySpawner, Warning, TEXT("Pipe connection failed: %s (%s) to %s (%s)"),
        *GetNameSafe(From), *GetNameSafe(From ? From->GetOwner() : nullptr),
        *GetNameSafe(To), *GetNameSafe(To ? To->GetOwner() : nullptr));
    bBuildFailed = true;
    return false;
}