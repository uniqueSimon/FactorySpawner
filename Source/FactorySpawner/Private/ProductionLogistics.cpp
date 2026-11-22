#include "BuildPlanGenerator.h"
#include "FactorySpawner.h"
#include "Algo/Count.h"
#include "BuildableCache.h"
#include "ProductionPlan.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildableSplitterSmart.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGPowerConnectionComponent.h"
#include "FGRecipe.h"
#include "Resources/FGItemDescriptor.h"

namespace
{
    constexpr double MinRate = 0.0001;

    bool IsPipeItem(TSubclassOf<UFGItemDescriptor> Item)
    {
        const EResourceForm Form = UFGItemDescriptor::GetForm(Item);
        return Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS;
    }

    UFGFactoryConnectionComponent* FindBeltPort(const TArray<UFGFactoryConnectionComponent*>& Ports,
                                                EFactoryConnectionDirection Direction)
    {
        for (UFGFactoryConnectionComponent* Port : Ports)
            if (Port->GetDirection() == Direction && !Port->GetConnection())
                return Port;
        return nullptr;
    }
}

FVector FBuildPlanGenerator::FFlowPort::Location() const
{
    return Belt ? Belt->GetComponentLocation() : Pipe->GetComponentLocation();
}

void FBuildPlanGenerator::WirePowerRoute(UFGPowerConnectionComponent* From, UFGPowerConnectionComponent* To)
{
    if (!From || !To)
    {
        bBuildFailed = true;
        return;
    }
    constexpr double RelayDistance = 7500;
    const FVector Start = From->GetComponentLocation();
    const FVector End = To->GetComponentLocation();
    const int32 Sections = FMath::Max(1, FMath::CeilToInt(FVector::Distance(Start, End) / RelayDistance));
    for (int32 Section = 1; Section < Sections && !bBuildFailed; ++Section)
    {
        UFGPowerConnectionComponent* Relay = SpawnPowerPole(FMath::Lerp(Start, End, double(Section) / Sections));
        SpawnWireAndConnect(From, Relay);
        From = Relay;
    }
    if (!bBuildFailed)
        SpawnWireAndConnect(From, To);
}

void FBuildPlanGenerator::RecordPipePort(UFGPipeConnectionComponent* MachinePort,
                                            UFGPipeConnectionComponent* RowPort,
                                            const TArray<FItemAmount>& RecipeItems,
                                            TMap<FString, UFGPipeConnectionComponent*>& Ports)
{
    TArray<TSubclassOf<UFGItemDescriptor>, TInlineAllocator<4>> Items;
    for (const FItemAmount& Amount : RecipeItems)
        if (IsPipeItem(Amount.ItemClass))
            Items.AddUnique(Amount.ItemClass);

    TSubclassOf<UFGItemDescriptor> Item = MachinePort->GetFluidDescriptor();
    if (!Item)
    {
        const int32 Slot = MachinePort->GetInventoryAccessIndex();
        if (Items.Num() == 1)
            Item = Items[0];
        else if (Items.IsValidIndex(Slot))
            Item = Items[Slot];
    }
    if (!Items.Contains(Item) || Ports.Contains(Item->GetName()))
    {
        if (const auto* Unassigned = Items.FindByPredicate([&](const auto& Candidate) { return !Ports.Contains(Candidate->GetName()); }))
            Item = *Unassigned;
    }
    if (Item && Items.Contains(Item) && !Ports.Contains(Item->GetName()) && !RowPort->GetConnection())
        Ports.Add(Item->GetName(), RowPort);
}

void FBuildPlanGenerator::AddPortLabel(const FVector& Location, const FString& Text)
{
    const auto SignClass = Cache->GetBuildableClass<AFGBuildableWidgetSign>(EBuildable::PortSign);
    if (!SignClass)
        return;
    if (auto* Sign = World->SpawnActor<AFGBuildableWidgetSign>(SignClass, FTransform(FRotator(0, 180, 0), Location)))
    {
        BuildablesForBlueprint.Add(Sign);
        FPrefabSignData Data;
        Sign->GetSignPrefabData(Data);
        for (auto& Element : Data.TextElementData)
            Element.Value = Text;
        if (Data.TextElementData.IsEmpty())
            Data.TextElementData.Add(TEXT("Text"), Text);
        Sign->SetPrefabSignData(Data);
    }
}

FBuildPlanGenerator::FFlowPort FBuildPlanGenerator::SpawnFlowTerminal(const FVector& Location, bool bImport,
    TSubclassOf<UFGItemDescriptor> Item, double Rate, int32 Lane)
{
    const FString Label = FString::Printf(TEXT("%s %s %.2f/min [%d]"), bImport ? TEXT("IN") : TEXT("OUT"),
                                         *UFGItemDescriptor::GetItemName(Item).ToString(), Rate, Lane);
    AddPortLabel(Location + FVector(0, 0, 280), Label);
    if (IsPipeItem(Item))
    {
        auto Ports = SpawnPipeCross(Location);
        return Ports.IsValidIndex(2) ? FFlowPort{nullptr, Ports[bImport ? 2 : 0]} : FFlowPort{};
    }
    const auto Ports = SpawnSplitterOrMerger(Location, bImport ? EBuildable::Splitter : EBuildable::Merger);
    const auto Direction = bImport ? EFactoryConnectionDirection::FCD_OUTPUT : EFactoryConnectionDirection::FCD_INPUT;
    return {FindBeltPort(Ports, Direction), nullptr};
}

bool FBuildPlanGenerator::ConnectFlow(FFlowPort From, FFlowPort To, TSubclassOf<UFGItemDescriptor> Item, double Rate)
{
    if (!From || !To || bool(From.Pipe) != bool(To.Pipe))
        return false;
    const FVector Start = From.Location();
    const FVector End = To.Location();
    if (From.Belt)
    {
        const bool bCrossFloor = From.Floor != To.Floor;
        if (!bCrossFloor && CanRunBeltDirectly(From.Belt, To.Belt))
            return SpawnBeltAndConnect(From.Belt, To.Belt);

        const double StartY = From.AisleY.Get(Start.Y);
        const double EndY = To.AisleY.Get(End.Y);
        const double ShaftY = (StartY + EndY) / 2.0;
        if (!bCrossFloor && (From.AisleY || To.AisleY))
        {
            for (const double TrackX : {(Start.X + End.X) / 2.0, Start.X, End.X})
                if (CanRunBeltViaAisle(Start, End, TrackX, StartY, EndY))
                    return SpawnBeltAndConnect(From.Belt, To.Belt, TrackX, From.AisleY, To.AisleY);
        }

        const double SideClearance = bCrossFloor ? 130.0 : 100.0;
        const double LowY = bCrossFloor ? ShaftY : FMath::Min(StartY, EndY);
        const double HighY = bCrossFloor ? ShaftY + ProductionLiftStagger : FMath::Max(StartY, EndY);
        // Consecutive lifts share one side corridor and stagger along it.
        const FBox2D RouteSpan(FVector2D(LowY - SideClearance, FMath::Min(Start.Z, End.Z) - 130.0),
                              FVector2D(HighY + SideClearance, FMath::Max(Start.Z, End.Z) + 130.0));
        const auto FindLane = [&](const TArray<TArray<FBox2D>>& Lanes)
        {
            const int32 Reusable = Lanes.IndexOfByPredicate([&](const TArray<FBox2D>& Spans)
            {
                return !Spans.ContainsByPredicate([&](const FBox2D& Span) { return Span.Intersect(RouteSpan); });
            });
            return Reusable == INDEX_NONE ? Lanes.Num() : Reusable;
        };
        const int32 LeftLane = FindLane(LeftConveyorLanes);
        const int32 RightLane = FindLane(RightConveyorLanes);
        const double LeftX = PlatformBounds.Min.X - 520.0 - LeftLane * 300.0;
        const double RightX = PlatformBounds.Max.X + 520.0 + RightLane * 300.0;
        const bool bUseLeft = FMath::Abs(Start.X - LeftX) + FMath::Abs(End.X - LeftX) <=
                              FMath::Abs(Start.X - RightX) + FMath::Abs(End.X - RightX);
        auto& Lanes = bUseLeft ? LeftConveyorLanes : RightConveyorLanes;
        const int32 Lane = bUseLeft ? LeftLane : RightLane;
        if (Lane == Lanes.Num())
            Lanes.AddDefaulted();
        Lanes[Lane].Add(RouteSpan);
        return ConnectRowConveyors(From.Belt, To.Belt, bUseLeft ? LeftX : RightX, bCrossFloor, From.AisleY, To.AisleY);
    }

    UFGPipeConnectionComponent* Previous = From.Pipe;
    const auto ConnectLeg = [&](UFGPipeConnectionComponent* Destination)
    {
        // Leave room for the bends at each end within the vanilla 56 m pipe length.
        constexpr double PipeSectionLength = 4500.0;
        const FVector LegStart = Previous->GetComponentLocation();
        const FVector LegEnd = Destination->GetComponentLocation();
        const FVector Direction = (LegEnd - LegStart).GetSafeNormal();
        const int32 Sections = FMath::CeilToInt(FVector::Distance(LegStart, LegEnd) / PipeSectionLength);
        for (int32 Section = 1; Section < Sections; ++Section)
        {
            auto Ports = SpawnPipeCross(FMath::Lerp(LegStart, LegEnd, double(Section) / Sections), Direction.Rotation());
            if (Ports.Num() < 2)
                return false;
            Ports.Sort([&](const UFGPipeConnectionComponent& A, const UFGPipeConnectionComponent& B)
            {
                return FVector::DotProduct(A.GetConnectorNormal(), Direction) <
                       FVector::DotProduct(B.GetConnectorNormal(), Direction);
            });
            if (!SpawnPipeAndConnect(Previous, Ports[0]))
                return false;
            Previous = Ports.Last();
        }
        return SpawnPipeAndConnect(Previous, Destination);
    };
    const auto InsertDevice = [&](EBuildable Type, double At, double Limit)
    {
        const auto Class = Cache->GetBuildableClass<AFGBuildablePipelinePump>(Type);
        const FTransform Transform((End - Start).Rotation(), FMath::Lerp(Start, End, At));
        auto* Device = Class ? World->SpawnActor<AFGBuildablePipelinePump>(Class, Transform) : nullptr;
        if (!Device)
            return false;
        BuildablesForBlueprint.Add(Device);
        auto Ports = Device->GetPipeConnections();
        if (Ports.Num() != 2)
            return false;
        if (Ports[0]->GetPipeConnectionType() == EPipeConnectionType::PCT_PRODUCER ||
            Ports[1]->GetPipeConnectionType() == EPipeConnectionType::PCT_CONSUMER)
            Swap(Ports[0], Ports[1]);
        if (Limit > MinRate)
            Device->SetUserFlowLimit(Limit / 60.0);
        if (!ConnectLeg(Ports[0]))
            return false;
        Previous = Ports[1];
        if (Device->RunsOnPower())
        {
            auto* DevicePower = Device->FindComponentByClass<UFGPowerConnectionComponent>();
            auto* Pole = SpawnPowerPole(Device->GetActorLocation() + FVector(0, 300, -150));
            WirePowerRoute(CachedPowerConnections.FirstPole, Pole);
            WirePowerRoute(Pole, DevicePower);
            CachedPowerConnections.FirstPole = Pole;
        }
        return !bBuildFailed;
    };

    if (UFGItemDescriptor::GetForm(Item) == EResourceForm::RF_LIQUID && End.Z - Start.Z > 1200.0)
    {
        const int32 Pumps = FMath::CeilToInt((End.Z - Start.Z) / 1700.0);
        for (int32 Index = 0; Index < Pumps; ++Index)
        {
            const double At = (Index + 0.35) / Pumps;
            if (!InsertDevice(EBuildable::PipePump, At, Index == Pumps - 1 ? Rate : 0))
                return false;
        }
    }
    else if (Rate > MinRate && !InsertDevice(EBuildable::PipeValve, 0.65, Rate))
        return false;

    return ConnectLeg(To.Pipe) && !bBuildFailed;
}

TArray<FBuildPlanGenerator::FFlowPort> FBuildPlanGenerator::ExpandFlow(FFlowPort Root, bool bSource, int32 Links,
    int32 OverflowLinks, TSubclassOf<UFGItemDescriptor> Item)
{
    if (Links == 1)
        return {Root};

    TArray<FFlowPort> Ports;
    for (int32 Index = 0; Index < Links - 1; ++Index)
    {
        // Extend each branch toward a separate junction before routing to the other rows.
        FVector Position = Root.Location();
        if (Root.Belt)
        {
            // Keep splitters and mergers beside their port instead of sending the trunk to the outer corridor.
            Position.X -= 250.0;
            Position.Y = Root.AisleY.Get(Position.Y) + 100.0 * ((LogisticsJunction++ % 3) - 1);
        }
        else
        {
            Position.X = FMath::Min(Position.X - 500.0, PlatformBounds.Min.X - 600.0);
            Position.Y += 400.0 * ((LogisticsJunction++ % 5) + 1);
        }
        FFlowPort Trunk;
        FFlowPort First;
        FFlowPort Next;
        if (Root.Pipe)
        {
            auto Cross = SpawnPipeCross(Position);
            if (Cross.Num() < 3)
                return {};
            Trunk.Pipe = Cross[0];
            First.Pipe = Cross[1];
            Next.Pipe = Cross[2];
        }
        else
        {
            const bool bPriority = bSource && OverflowLinks > 0 && Index == 0;
            TArray<UFGFactoryConnectionComponent*> Connectors;
            AFGBuildableSplitterSmart* Smart = nullptr;
            if (bPriority)
            {
                const auto Class = Cache->GetBuildableClass<AFGBuildableSplitterSmart>(EBuildable::SmartSplitter);
                Smart = Class ? World->SpawnActor<AFGBuildableSplitterSmart>(Class, FTransform(Position)) : nullptr;
                if (!Smart)
                    return {};
                BuildablesForBlueprint.Add(Smart);
                Smart->GetComponents<UFGFactoryConnectionComponent>(Connectors);
            }
            else
                Connectors = SpawnSplitterOrMerger(Position, bSource ? EBuildable::Splitter : EBuildable::Merger);

            const auto BranchDirection = bSource ? EFactoryConnectionDirection::FCD_OUTPUT : EFactoryConnectionDirection::FCD_INPUT;
            const auto TrunkDirection = bSource ? EFactoryConnectionDirection::FCD_INPUT : EFactoryConnectionDirection::FCD_OUTPUT;
            Trunk.Belt = FindBeltPort(Connectors, TrunkDirection);
            TInlineComponentArray<UFGFactoryConnectionComponent*> Branches;
            for (auto* Connector : Connectors)
                if (Connector->GetDirection() == BranchDirection)
                    Branches.Add(Connector);
            if (Branches.Num() < 2)
                return {};
            UFGFactoryConnectionComponent::SortComponentList(Branches);
            First.Belt = Branches[0];
            Next.Belt = Branches[1];
            if (Smart)
            {
                const auto Overflow = TSoftClassPtr<UFGItemDescriptor>(
                    FSoftObjectPath(TEXT("/Game/FactoryGame/Resource/FilteringRules/Desc_Overflow.Desc_Overflow_C"))).LoadSynchronous();
                if (!Overflow)
                    return {};
                Smart->SetSortRules({FSplitterSortRule(Overflow, 0), FSplitterSortRule(Item, 1)});
            }
        }

        Trunk.Floor = First.Floor = Next.Floor = Root.Floor;
        Trunk.AisleY = First.AisleY = Next.AisleY = Root.AisleY;
        const bool bConnected = bSource ? ConnectFlow(Root, Trunk, Item, 0) : ConnectFlow(Trunk, Root, Item, 0);
        if (!bConnected)
            return {};
        if (OverflowLinks > 0 && Index == 0)
        {
            Ports = ExpandFlow(First, true, OverflowLinks, 0, Item);
            Ports.Append(ExpandFlow(Next, true, Links - OverflowLinks, 0, Item));
            return Ports;
        }
        Ports.Add(First);
        Root = Next;
    }
    Ports.Add(Root);
    return Ports;
}

bool FBuildPlanGenerator::BuildProductionLogistics(const FProductionPlan& Plan, FString& OutError)
{
    int32 GroundFloor = MAX_int32;
    for (const auto& Floor : Floors)
        GroundFloor = FMath::Min(GroundFloor, Floor.Key);
    const FVector Entry(PlatformBounds.Min.X - 300.0, PlatformBounds.Min.Y - 1800.0, 0.0);
    auto* PowerEntry = SpawnPowerPole(Entry);
    WirePowerRoute(PowerEntry, FirstMachinePole);
    if (bBuildFailed)
    {
        OutError = TEXT("Could not connect the factory to its external power pole");
        return false;
    }
    AddPortLabel(Entry + FVector(0, 0, 450), TEXT("FACTORY POWER IN"));

    // Use a chain of dedicated poles for logistics equipment so the row poles retain their connection slots.
    CachedPowerConnections.FirstPole = PowerEntry;
    struct FEndpoint
    {
        FFlowPort Port;
        int32 Row = INDEX_NONE;
        double Remaining = 0;
        bool bExportOnly = false;
        bool bExternal = false;
        TArray<int32> Edges;
    };
    struct FEdge
    {
        int32 Source;
        int32 Sink;
        double Rate;
        FFlowPort From;
        FFlowPort To;
    };

    TSet<FString> MaterialSet;
    for (const auto& Row : Plan.Rows)
    {
        for (const auto& Input : Row.Inputs)
            MaterialSet.Add(Input.Item);
        for (const auto& Output : Row.Outputs)
            MaterialSet.Add(Output.Item);
    }
    for (const auto& Import : Plan.Imports)
        MaterialSet.Add(Import.Key);
    for (const auto& Export : Plan.Exports)
        MaterialSet.Add(Export.Key);
    auto Materials = MaterialSet.Array();
    Materials.Sort();
    int32 PipeTerminal = 0;
    TSet<int32> BeltTerminalSlots[2];
    for (const FString& Material : Materials)
    {
        const auto Item = Plan.Items.FindChecked(Material);
        const FString ItemName = UFGItemDescriptor::GetItemName(Item).ToString();
        const bool bPipe = IsPipeItem(Item);
        TArray<FEndpoint> Sources;
        TArray<FEndpoint> Sinks;
        for (int32 RowIndex = 0; RowIndex < Plan.Rows.Num(); ++RowIndex)
        {
            const auto& Row = Plan.Rows[RowIndex];
            const auto& RowPorts = RowConveyors[RowIndex];
            const auto Append = [&](const TArray<FProductionAmount>& Amounts, bool bInput)
            {
                for (const FProductionAmount& Amount : Amounts)
                {
                    if (Amount.Item != Material)
                        continue;
                    FFlowPort Port;
                    Port.Floor = RowPorts.Floor;
                    Port.AisleY = bInput ? RowPorts.InputAisleY : RowPorts.OutputAisleY;
                    if (bPipe)
                        Port.Pipe = (bInput ? RowPorts.PipeInputs : RowPorts.PipeOutputs).FindRef(Material);
                    else
                    {
                        const auto& Belts = bInput ? RowPorts.Inputs : RowPorts.Outputs;
                        if (const FItemPort* Match = Belts.FindByPredicate([&](const FItemPort& Belt) { return Belt.Item == Item; }))
                            Port.Belt = Match->Connection;
                    }
                    if (!Port)
                        return false;
                    const auto& Products = Row.Recipe.GetDefaultObject()->GetProducts();
                    const bool bExportOnly = !bInput && Plan.ExportByproducts.Contains(Material) &&
                        Products[0].ItemClass != Item;
                    (bInput ? Sinks : Sources).Add({Port, RowIndex, Amount.PerMinute, bExportOnly});
                }
                return true;
            };
            if (!Append(Row.Inputs, true) || !Append(Row.Outputs, false))
            {
                OutError = FString::Printf(TEXT("Cannot identify %s port on %s (row %d)"), *ItemName,
                                           *Row.Recipe->GetName(), RowIndex + 1);
                return false;
            }
        }

        const auto Terminals = [&](double Remaining, bool bImport)
        {
            int32 PreferredSlot = 0;
            int32 Side = 0;
            constexpr double TerminalSpacing = 300.0;
            if (!bPipe)
            {
                // Keep ground-level ports alongside the row that handles the most material.
                const auto& Rows = bImport ? Sinks : Sources;
                double HeaviestRate = 0.0;
                for (const auto& Row : Rows)
                    if (!Row.bExternal && Row.Remaining > HeaviestRate)
                    {
                        const FVector RowPort = Row.Port.Location();
                        PreferredSlot = FMath::RoundToInt((RowPort.Y - Entry.Y) / TerminalSpacing);
                        Side = RowPort.X > PlatformBounds.GetCenter().X ? 1 : 0;
                        HeaviestRate = Row.Remaining;
                    }
            }
            int32 Lane = 0;
            while (Remaining > MinRate)
            {
                const double Rate = FMath::Min(Remaining, bPipe ? Plan.PipeCapacity : Plan.BeltCapacity);
                FVector Location(Entry.X, Entry.Y, 100);
                if (bPipe)
                {
                    Location.X += (PipeTerminal++ + 1) * 600.0;
                }
                else
                {
                    auto& Slots = BeltTerminalSlots[Side];
                    int32 Slot = PreferredSlot;
                    for (int32 Offset = 1; Slots.Contains(Slot); ++Offset)
                    {
                        const int32 Before = PreferredSlot - Offset;
                        Slot = Slots.Contains(Before) ? PreferredSlot + Offset : Before;
                    }
                    Slots.Add(Slot);
                    if (Side == 1)
                        Location.X = PlatformBounds.Max.X + 300.0;
                    Location.Y += Slot * TerminalSpacing;
                }
                FFlowPort Port = SpawnFlowTerminal(Location, bImport, Item, Rate, ++Lane);
                Port.Floor = GroundFloor;
                if (!Port)
                    return false;
                (bImport ? Sources : Sinks).Add({Port, INDEX_NONE, Rate, false, true});
                Remaining -= Rate;
            }
            return true;
        };
        if (!Terminals(Plan.Imports.FindRef(Material), true) || !Terminals(Plan.Exports.FindRef(Material), false))
        {
            OutError = FString::Printf(TEXT("Cannot create an external port for %s"), *ItemName);
            return false;
        }

        // Internal demand gets first call on each production port, then its unallocated capacity is exported.
        TArray<int32> SourceOrder;
        TSet<int32> ProducerRows;
        for (int32 Index = 0; Index < Sources.Num(); ++Index)
        {
            SourceOrder.Add(Index);
            if (!Sources[Index].bExternal)
                ProducerRows.Add(Sources[Index].Row);
        }
        const bool bLocalRouting = !bPipe && !Sinks.ContainsByPredicate([&](const FEndpoint& Sink)
        {
            return !Sink.bExternal && ProducerRows.Contains(Sink.Row);
        });
        TArray<FEdge> Edges;
        for (int32 SinkIndex = 0; SinkIndex < Sinks.Num(); ++SinkIndex)
        {
            auto& Sink = Sinks[SinkIndex];
            if (bLocalRouting)
            {
                const FVector Destination = Sink.Port.Location();
                const auto Distance = [&](const FEndpoint& Source)
                {
                    const FVector Delta = (Source.Port.Location() - Destination).GetAbs();
                    return Delta.X + Delta.Y + Delta.Z * 2.0;
                };
                SourceOrder.StableSort([&](int32 A, int32 B)
                {
                    if (Sources[A].bExternal != Sources[B].bExternal)
                        return !Sources[A].bExternal;
                    return Distance(Sources[A]) < Distance(Sources[B]);
                });
            }
            for (const int32 SourceIndex : SourceOrder)
            {
                if (Sink.Remaining <= MinRate)
                    break;
                auto& Source = Sources[SourceIndex];
                if (Source.Remaining <= MinRate || (!Sink.bExternal &&
                    (Source.bExportOnly || Source.Row == Sink.Row)))
                    continue;
                const double Rate = FMath::Min(Sink.Remaining, Source.Remaining);
                const int32 Edge = Edges.Add({SourceIndex, SinkIndex, Rate});
                Source.Edges.Add(Edge);
                Sink.Edges.Add(Edge);
                Source.Remaining -= Rate;
                Sink.Remaining -= Rate;
            }
            if (Sink.Remaining > MinRate)
            {
                OutError = FString::Printf(TEXT("Cannot route %.3f/min of %s to row %d"), Sink.Remaining, *ItemName, Sink.Row + 1);
                return false;
            }
        }
        for (auto& Source : Sources)
        {
            if (Source.Remaining > MinRate)
            {
                OutError = FString::Printf(TEXT("No outlet for %.3f/min of %s"), Source.Remaining, *ItemName);
                return false;
            }
            const auto IsExport = [&](int32 Edge) { return Sinks[Edges[Edge].Sink].bExternal; };
            const int32 Exports = Algo::CountIf(Source.Edges, IsExport);
            const int32 Overflow = Exports > 0 && Exports < Source.Edges.Num() && !bPipe ? Exports : 0;
            if (Overflow)
                Source.Edges.StableSort([&](int32 A, int32 B) { return IsExport(A) > IsExport(B); });
            const auto Ports = ExpandFlow(Source.Port, true, Source.Edges.Num(), Overflow, Item);
            if (Ports.Num() != Source.Edges.Num())
            {
                OutError = FString::Printf(TEXT("Cannot split the %s supply"), *ItemName);
                return false;
            }
            for (int32 Index = 0; Index < Ports.Num(); ++Index)
                Edges[Source.Edges[Index]].From = Ports[Index];
        }
        for (auto& Sink : Sinks)
        {
            const auto Ports = ExpandFlow(Sink.Port, false, Sink.Edges.Num(), 0, Item);
            if (Ports.Num() != Sink.Edges.Num())
            {
                OutError = FString::Printf(TEXT("Cannot merge the %s supply"), *ItemName);
                return false;
            }
            for (int32 Index = 0; Index < Ports.Num(); ++Index)
                Edges[Sink.Edges[Index]].To = Ports[Index];
        }
        for (const FEdge& Edge : Edges)
        {
            const bool bMeter = bPipe && Sources[Edge.Source].Edges.Num() > 1;
            if (!ConnectFlow(Edge.From, Edge.To, Item, bMeter ? Edge.Rate : 0))
            {
                UE_LOG(LogFactorySpawner, Warning, TEXT("Logistics link failed for %s: %s to %s, at %.2f/min"),
                    *ItemName, *Edge.From.Location().ToCompactString(), *Edge.To.Location().ToCompactString(), Edge.Rate);
                OutError = FString::Printf(TEXT("Cannot connect logistics for %s (%.2f/min)"), *ItemName, Edge.Rate);
                return false;
            }
        }
    }
    return true;
}
