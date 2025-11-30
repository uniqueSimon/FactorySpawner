#include "BuildPlanGenerator.h"
#include "FactorySpawnerChat.h"
#include "FactorySpawner.h"
#include "FGRecipe.h"
#include "BuildableCache.h"
#include "FactoryCommandParser.h"

namespace
{
    // ---------- helper types ----------
    struct FBeltConnector
    {
        FGuid UnitId;
        int32 SocketNumber = 0;
    };

    // Simple FIFO queue implemented with array + read index
    struct FConnArrayQueue
    {
        TArray<FBeltConnector> Items;
        int32 ReadIndex = 0;

        void Reset()
        {
            Items.Reset();
            ReadIndex = 0;
        }
        void Enqueue(const FBeltConnector& Item)
        {
            Items.Add(Item);
        }
        bool Dequeue(FBeltConnector& Out)
        {
            if (ReadIndex < Items.Num())
            {
                Out = Items[ReadIndex++];
                // optionally shrink memory if ReadIndex grows large, but not necessary here
                return true;
            }
            return false;
        }
        bool IsEmpty() const
        {
            return ReadIndex >= Items.Num();
        }
    };

    struct FConnectionQueue
    {
        FConnArrayQueue Input;
        FConnArrayQueue Output;
        FConnArrayQueue PipeInput;
        FConnArrayQueue PipeOutput;
    };

    // Machine configuration map (use helper factories from header)
    static const TMap<EBuildable, FMachineConfig> MachineConfigList = {
        {EBuildable::Constructor, MakeMachineConfig(800, {MakeMachineConnections(900, {MakeConnector(1, 0)})},
                                                    {MakeMachineConnections(900, {MakeConnector(0, 0)})})},
        {EBuildable::Smelter, MakeMachineConfig(500, {MakeMachineConnections(900, {MakeConnector(0, 0)})},
                                                {MakeMachineConnections(800, {MakeConnector(1, 0)})})},
        {EBuildable::Foundry,
         MakeMachineConfig(1000, {MakeMachineConnections(1100, {MakeConnector(2, -200), MakeConnector(0, 200)})},
                           {MakeMachineConnections(800, {MakeConnector(1, -200)})})},
        {EBuildable::Assembler,
         MakeMachineConfig(900, {MakeMachineConnections(1400, {MakeConnector(1, -200), MakeConnector(2, 200)})},
                           {MakeMachineConnections(1100, {MakeConnector(0, 0)})})},
        {EBuildable::OilRefinery,
         MakeMachineConfig(1000,
                           {MakeMachineConnections(2000, {MakeConnector(0, -200)}, {MakeConnector(1, 200)}),
                            MakeMachineConnections(1500, {}, {MakeConnector(1, 200)}),
                            MakeMachineConnections(1500, {MakeConnector(0, -200)}, {})},
                           {MakeMachineConnections(2000, {MakeConnector(1, -200)}, {MakeConnector(0, 200)}),
                            MakeMachineConnections(1500, {MakeConnector(1, -200)}, {}),
                            MakeMachineConnections(1500, {}, {MakeConnector(0, 200)})})},
        {EBuildable::Blender,
         MakeMachineConfig(1800,
                           {MakeMachineConnections(2500, {MakeConnector(0, 200, 400), MakeConnector(1, 600, 600)},
                                                   {MakeConnector(0, -600), MakeConnector(1, -200, 200)})},
                           {MakeMachineConnections(2500, {MakeConnector(2, -200, 400)}, {MakeConnector(2, -600)})})},
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

    // ---------- main per-machine setup ----------
    void CalculateMachineSetup(FBuildPlan& BuildPlan, EBuildable MachineType, const TOptional<FString>& Recipe,
                               const TOptional<float>& Underclock, int32 XCursor, int32 YCursor, int32 Width,
                               const FMachineConnections& InputConnections,
                               const FMachineConnections& OutputConnections, FConnectionQueue& ConnectionQueue,
                               FCachedPowerConnections& CachedPowerConnections, bool bFirstUnitInRow, bool bEvenIndex,
                               bool bLastIndex)
    {
        // create machine unit
        FGuid MachineId = FGuid::NewGuid();
        const FVector MachineLocation = FVector((float) XCursor, (float) YCursor, 0.0f);

        AddBuildableUnit(BuildPlan, MachineId, MachineType, MachineLocation, Recipe, Underclock);

        // Power handling (even/odd indexing pattern)
        if (!bEvenIndex)
        {
            if (bLastIndex)
            {
                // link previous pole -> this machine
                if (CachedPowerConnections.LastPole.IsValid())
                    AddWire(BuildPlan, CachedPowerConnections.LastPole, MachineId);
            }
            else
            {
                // remember last machine for later connection
                CachedPowerConnections.LastMachine = MachineId;
            }
        }
        else
        {
            // place a power pole between machines
            FGuid PowerPoleId = FGuid::NewGuid();
            const FVector PowerPoleRel = FVector(-(Width / 2.0f), -(InputConnections.Length / 2.0f), 0.0f);
            AddBuildableUnit(BuildPlan, PowerPoleId, EBuildable::PowerPole, MachineLocation + PowerPoleRel);

            // connect pole -> machine
            AddWire(BuildPlan, PowerPoleId, MachineId);

            if (bFirstUnitInRow)
            {
                if (CachedPowerConnections.FirstPole.IsValid())
                {
                    // connect pole -> first pole of previous row
                    AddWire(BuildPlan, PowerPoleId, CachedPowerConnections.FirstPole);
                }
                CachedPowerConnections.FirstPole = PowerPoleId;
            }
            else
            {
                // connect pole -> last machine & last pole (previous on same row)
                if (CachedPowerConnections.LastMachine.IsValid())
                    AddWire(BuildPlan, PowerPoleId, CachedPowerConnections.LastMachine);

                if (CachedPowerConnections.LastPole.IsValid())
                    AddWire(BuildPlan, PowerPoleId, CachedPowerConnections.LastPole);
            }

            CachedPowerConnections.LastPole = PowerPoleId;
        }

        // Input splitters and belt wiring
        for (const FConnector& Conn : InputConnections.Belt)
        {
            FGuid SplitterId = FGuid::NewGuid();
            const FVector SplitterLocation =
                MachineLocation + FVector(Conn.LocationX, -InputConnections.Length + 200.0f, 100.0f + Conn.LocationY);
            AddBuildableUnit(BuildPlan, SplitterId, EBuildable::Splitter, SplitterLocation);

            // connect previous item -> splitter (if present)
            if (!bFirstUnitInRow)
            {
                FBeltConnector Prev;
                if (ConnectionQueue.Input.Dequeue(Prev))
                {
                    AddBelt(BuildPlan, Prev.UnitId, Prev.SocketNumber, SplitterId, 1);
                }
            }

            // enqueue this splitter for future machines
            ConnectionQueue.Input.Enqueue({SplitterId, 0});

            // connect splitter -> machine input socket
            AddBelt(BuildPlan, SplitterId, 3, MachineId, Conn.Index);
        }

        // Output mergers and belt wiring
        for (const FConnector& Conn : OutputConnections.Belt)
        {
            FGuid MergerId = FGuid::NewGuid();
            const FVector MergerLocation =
                MachineLocation + FVector(Conn.LocationX, OutputConnections.Length - 200.0f, 100.0f + Conn.LocationY);
            AddBuildableUnit(BuildPlan, MergerId, EBuildable::Merger, MergerLocation);

            // connect merger -> previously enqueued output (if present)
            if (!bFirstUnitInRow)
            {
                FBeltConnector Prev;
                if (ConnectionQueue.Output.Dequeue(Prev))
                {
                    AddBelt(BuildPlan, MergerId, 1, Prev.UnitId, Prev.SocketNumber);
                }
            }

            // enqueue this merger for future row's machines
            ConnectionQueue.Output.Enqueue({MergerId, 0});

            // connect machine output -> merger
            AddBelt(BuildPlan, MachineId, Conn.Index, MergerId, 2);
        }

        // Input pipes
        for (const FConnector& Conn : InputConnections.Pipe)
        {
            FGuid PipeCrossId = FGuid::NewGuid();
            const FVector PipeCrossLocation =
                MachineLocation + FVector(Conn.LocationX, -InputConnections.Length + 200.0f, 175.0f + Conn.LocationY);
            AddBuildableUnit(BuildPlan, PipeCrossId, EBuildable::PipeCross, PipeCrossLocation);

            // connect previous item -> pipe cross (if present)
            if (!bFirstUnitInRow)
            {
                FBeltConnector Prev;
                if (ConnectionQueue.PipeInput.Dequeue(Prev))
                {
                    AddPipe(BuildPlan, Prev.UnitId, Prev.SocketNumber, PipeCrossId, 3);
                }
            }

            // enqueue this pipe cross for future machines
            ConnectionQueue.PipeInput.Enqueue({PipeCrossId, 0});

            // connect pipe cross -> machine input socket
            AddPipe(BuildPlan, PipeCrossId, 1, MachineId, Conn.Index);
        }

        // Output pipes
        for (const FConnector& Conn : OutputConnections.Pipe)
        {
            FGuid PipeCrossId = FGuid::NewGuid();
            const FVector PipeCrossLocation =
                MachineLocation + FVector(Conn.LocationX, OutputConnections.Length - 200.0f, 175.0f + Conn.LocationY);
            AddBuildableUnit(BuildPlan, PipeCrossId, EBuildable::PipeCross, PipeCrossLocation);

            // connect pipe cross -> previously enqueued output (if present)
            if (!bFirstUnitInRow)
            {
                FBeltConnector Prev;
                if (ConnectionQueue.PipeOutput.Dequeue(Prev))
                {
                    AddPipe(BuildPlan, PipeCrossId, 3, Prev.UnitId, Prev.SocketNumber);
                }
            }

            // enqueue this pipe cross for future row's machines
            ConnectionQueue.PipeOutput.Enqueue({PipeCrossId, 0});

            // connect machine output -> pipe cross
            AddPipe(BuildPlan, MachineId, Conn.Index, PipeCrossId, 2);
        }
    }

} // namespace

FBuildPlanGenerator::FBuildPlanGenerator(UWorld* InWorld, UBuildableCache* InCache) : World(InWorld), Cache(InCache)
{
}

FBuildPlan FBuildPlanGenerator::Generate(const TArray<FFactoryCommandToken>& ClusterConfig)
{
    BuildPlan = FBuildPlan();
    YCursor = XCursor = FirstMachineWidth = 0;
    CachedPowerConnections = {};

    int32 RowIndex = 0;
    for (const FFactoryCommandToken& RowConfig : ClusterConfig)
    {
        ProcessRow(RowConfig, RowIndex++);
    }

    return BuildPlan;
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
            const int32 InputPorts = Ingredients.Num();
            const int32 OutputPorts = Products.Num();
            if (RowConfig.MachineType == EBuildable::Manufacturer && InputPorts == 3)
                InputVariant = 1;
            else if (RowConfig.MachineType == EBuildable::OilRefinery)
            {
                if (InputPorts == 1)
                {
                    EResourceForm Form = UFGItemDescriptor::GetForm(Ingredients[0].ItemClass);
                    if (Form == EResourceForm::RF_SOLID)
                    {
                        InputVariant = 1;
                    }
                    else if (Form == EResourceForm::RF_LIQUID)
                    {
                        InputVariant = 2;
                    }
                }
                if (OutputPorts == 1)
                {
                    EResourceForm Form = UFGItemDescriptor::GetForm(Products[0].ItemClass);
                    if (Form == EResourceForm::RF_SOLID)
                    {
                        OutputVariant = 1;
                    }
                    else if (Form == EResourceForm::RF_LIQUID)
                    {
                        OutputVariant = 2;
                    }
                }
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
    CachedPowerConnections.LastMachine.Invalidate();
    CachedPowerConnections.LastPole.Invalidate();
}

void FBuildPlanGenerator::PlaceMachines(const FFactoryCommandToken& RowConfig, int32 Width,
                                        const FMachineConnections& InputConnections,
                                        const FMachineConnections& OutputConnections)
{
    FConnectionQueue ConnectionQueue;

    for (int32 i = 0; i < RowConfig.Count; ++i)
    {
        const bool bFirstUnitInRow = (i == 0);
        const bool bEvenIndex = (i % 2 == 0);
        const bool bLastIndex = (i == RowConfig.Count - 1);

        CalculateMachineSetup(BuildPlan, RowConfig.MachineType, RowConfig.Recipe, RowConfig.ClockPercent, XCursor,
                              YCursor, Width, InputConnections, OutputConnections, ConnectionQueue,
                              CachedPowerConnections, bFirstUnitInRow, bEvenIndex, bLastIndex);

        XCursor += Width;
    }
}