#include "BuildPlanGenerator.h"
#include "BuildableCache.h"
#include "Buildables/FGBuildable.h"
#include "Engine/World.h"
#include "ProductionPlan.h"

namespace
{
    constexpr double TallMachineHeight = 1200.0;
    constexpr double DeckHeightStep = 100.0;
    constexpr double OverheadClearance = 150.0;
    constexpr double RowSideMargin = 160.0;
    constexpr double AisleMargin = 250.0;
    constexpr double AisleInset = 100.0;

    struct FRowFootprint
    {
        int32 Index;
        double Width;
        double Front;
        double Back;
    };

    struct FRowBand
    {
        TArray<const FRowFootprint*> Rows;
        double Width = 0;
        double Front = 0;
        double Back = 0;
    };
}

bool FBuildPlanGenerator::PlanProductionLayout(const FFactoryCommand& Command, TArray<FPreparedRow>& Rows,
                                               FString& OutError)
{
    TMap<EBuildable, double> MachineHeights;
    TArray<int32> EquipmentStages;
    TSet<int32> ExplicitFloors;
    for (int32 Index = 0; Index < Rows.Num(); ++Index)
    {
        const auto Type = Command.Rows[Index].MachineType;
        if (!MachineHeights.Contains(Type))
        {
            const auto Class = Cache->GetBuildableClass<AFGBuildable>(Type);
            FActorSpawnParameters Spawn;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Probe = Class ? World->SpawnActor<AFGBuildable>(Class, FTransform::Identity, Spawn) : nullptr;
            if (!IsValid(Probe))
            {
                OutError = FString::Printf(TEXT("Cannot measure machine size for row %d"), Index + 1);
                return false;
            }
            const FBox Bounds = Probe->GetComponentsBoundingBox(true);
            Probe->Destroy();
            if (!Bounds.IsValid || !FMath::IsFinite(Bounds.Max.Z) || Bounds.Max.Z <= 0)
            {
                OutError = FString::Printf(TEXT("No valid machine height for row %d"), Index + 1);
                return false;
            }
            MachineHeights.Add(Type, Bounds.Max.Z);
        }
        if (Production->Rows[Index].bFixedFloor)
            ExplicitFloors.Add(Rows[Index].Floor);
        else if (MachineHeights.FindChecked(Type) > TallMachineHeight)
            EquipmentStages.Add(Rows[Index].Floor);
    }

    int32 EquipmentFloor = INDEX_NONE;
    if (!EquipmentStages.IsEmpty())
    {
        EquipmentStages.Sort();
        EquipmentFloor = EquipmentStages[EquipmentStages.Num() / 2];
        while (ExplicitFloors.Contains(EquipmentFloor))
            ++EquipmentFloor;
    }

    TMap<int32, TArray<FRowFootprint>> FloorRows;
    for (int32 Index = 0; Index < Rows.Num(); ++Index)
    {
        auto& Row = Rows[Index];
        const double Height = MachineHeights.FindChecked(Command.Rows[Index].MachineType);
        if (EquipmentFloor != INDEX_NONE && !Production->Rows[Index].bFixedFloor)
        {
            if (Height > TallMachineHeight)
                Row.Floor = EquipmentFloor;
            else if (Row.Floor >= EquipmentFloor)
                ++Row.Floor;
        }

        auto& Floor = Floors.FindOrAdd(Row.Floor);
        Floor.bEquipment = Row.Floor == EquipmentFloor;
        double Top = Height;
        for (const FMachineConnections* Ports : {Row.Input, Row.Output})
        {
            for (const FConnector& Connector : Ports->Belt)
                Top = FMath::Max(Top, 100.0 + Connector.LocationY * 100.0);
            for (const FConnector& Connector : Ports->Pipe)
                Top = FMath::Max(Top, 175.0 + Connector.LocationY * 100.0);
        }
        const double Rise = FMath::CeilToDouble((Top + OverheadClearance + FoundationThickness) / DeckHeightStep)
                            * DeckHeightStep;
        Floor.RequiredRise = FMath::Max3(Floor.RequiredRise, Rise, FloorHeight);

        const double HalfLength = Row.Machine->Length * 50.0;
        FloorRows.FindOrAdd(Row.Floor).Add({
            Index,
            Row.Machine->Width * 100.0 * Command.Rows[Index].Count + 2 * RowSideMargin,
            FMath::Max(HalfLength, Row.Input->Length * 100.0) + AisleMargin,
            FMath::Max(HalfLength, Row.Output->Length * 100.0) + AisleMargin
        });
    }

    TArray<int32> FloorNumbers;
    Floors.GetKeys(FloorNumbers);
    FloorNumbers.Sort();
    double DeckZ = 0;
    for (int32 Number : FloorNumbers)
    {
        auto& Floor = Floors.FindChecked(Number);
        Floor.DeckZ = DeckZ;
        DeckZ += Floor.RequiredRise;
        auto& Footprints = FloorRows.FindChecked(Number);
        Footprints.StableSort([](const FRowFootprint& A, const FRowFootprint& B)
        {
            return A.Front + A.Back > B.Front + B.Back;
        });

        double TotalArea = 0;
        double Widest = 0;
        double SumWidths = 0;
        for (const auto& Row : Footprints)
        {
            TotalArea += Row.Width * (Row.Front + Row.Back);
            Widest = FMath::Max(Widest, Row.Width);
            SumWidths += Row.Width;
        }
        const int32 MinWidth = FMath::CeilToInt(Widest / FoundationSize);
        const int32 MaxWidth = FMath::Max(MinWidth, FMath::CeilToInt(
            FMath::Min(SumWidths, 2.0 * FMath::Sqrt(TotalArea)) / FoundationSize));
        double BestCost = TNumericLimits<double>::Max();
        TArray<FRowBand> BestBands;
        for (int32 Width = MinWidth; Width <= MaxWidth; ++Width)
        {
            TArray<FRowBand> Bands;
            for (const auto& Row : Footprints)
            {
                auto* Band = Bands.FindByPredicate([&](const FRowBand& Candidate)
                {
                    return Candidate.Width + Row.Width <= Width * FoundationSize;
                });
                if (!Band)
                    Band = &Bands.AddDefaulted_GetRef();
                Band->Rows.Add(&Row);
                Band->Width += Row.Width;
                Band->Front = FMath::Max(Band->Front, Row.Front);
                Band->Back = FMath::Max(Band->Back, Row.Back);
            }

            double UsedWidth = 0;
            double UsedLength = 0;
            for (const auto& Band : Bands)
            {
                UsedWidth = FMath::Max(UsedWidth, Band.Width);
                UsedLength += Band.Front + Band.Back;
            }
            UsedWidth = FMath::CeilToDouble(UsedWidth / FoundationSize);
            UsedLength = FMath::CeilToDouble(UsedLength / FoundationSize);
            const double Cost = UsedWidth * UsedLength + FMath::Square(UsedWidth - UsedLength);
            if (Cost < BestCost)
            {
                BestCost = Cost;
                BestBands = MoveTemp(Bands);
            }
        }

        double Y = 0;
        for (const auto& Band : BestBands)
        {
            double X = 0;
            for (const FRowFootprint* Footprint : Band.Rows)
            {
                auto& Row = Rows[Footprint->Index];
                Row.Position = FVector2D(X + RowSideMargin + Row.Machine->Width * 50.0, Y + Band.Front);
                Row.InputAisleY = Y + AisleInset;
                Row.OutputAisleY = Y + Band.Front + Band.Back - AisleInset;
                X += Footprint->Width;
            }
            Floor.Footprint += FVector2D(0, Y);
            Y += Band.Front + Band.Back;
            Floor.Footprint += FVector2D(X, Y);
        }
    }
    return true;
}
