#include "ProductionPlan.h"
#include "BuildableCache.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableManufacturerVariablePower.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "Resources/FGResourceDescriptor.h"

namespace
{
    constexpr double Epsilon = 0.00001;

    FString Normalize(FString Value, const TCHAR* Prefix, bool bClassName)
    {
        if (bClassName)
        {
            Value.RemoveFromStart(Prefix);
            Value.RemoveFromEnd(TEXT("_C"));
        }
        FString Result;
        for (TCHAR Character : Value)
            if (FChar::IsAlnum(Character) || Character >= 128)
                Result.AppendChar(FChar::ToLower(Character));
        return Result;
    }

    bool Resolve(const TMap<FString, TArray<FString>>& Aliases, const FString& Input, const TCHAR* Kind,
                 const TCHAR* Prefix, FString& Out, FString& Error)
    {
        const TArray<FString>* Values = Aliases.Find(Normalize(Input, Prefix, true));
        if (!Values || Values->Num() != 1)
        {
            Error = FString::Printf(TEXT("%s '%s': %s"), Kind, *Input,
                                    Values ? *FString::Join(*Values, TEXT(", ")) : TEXT("unknown or unavailable"));
            return false;
        }
        Out = (*Values)[0];
        return true;
    }

    bool PositiveRate(const FString& Text, double& Value)
    {
        return LexTryParseString(Value, *Text) && FMath::IsFinite(Value) && Value > 0.0;
    }

    bool IsFluid(TSubclassOf<UFGItemDescriptor> Item)
    {
        const EResourceForm Form = UFGItemDescriptor::GetForm(Item);
        return Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS;
    }

    double RateOf(const TArray<FProductionAmount>& Rates, const FString& Item)
    {
        double Value = 0;
        for (const FProductionAmount& Rate : Rates)
            if (Rate.Item == Item)
                Value += Rate.PerMinute;
        return Value;
    }

    bool Reusable(const FProductionRecipe& Recipe, const FString& Item, const FProductionRequest& Request)
    {
        return !Request.ExportByproducts.Contains(Item) || Recipe.Outputs[0].Item == Item;
    }

    void ApplyRates(TMap<FString, double>& Map, const TArray<FProductionAmount>& Rates, double Factor)
    {
        for (const FProductionAmount& Rate : Rates)
            Map.FindOrAdd(Rate.Item) += Rate.PerMinute * Factor;
    }
}

bool ParseProductionRequest(const FString& Command, FProductionRequest& Out, FString& Error)
{
    Out = {};
    const int32 Split = Command.Find(TEXT(" "));
    if (Split < 0)
    {
        Error = TEXT("Specify a product and rate, e.g. plan IronPlate=20");
        return false;
    }
    Out.bPreview = Command.Left(Split).Equals(TEXT("plan"), ESearchCase::IgnoreCase);
    TArray<FString> Groups;
    Command.Mid(Split + 1).ParseIntoArray(Groups, TEXT(","), true);
    bool bHeightSet = false;
    for (FString Group : Groups)
    {
        Group.TrimStartAndEndInline();
        FString Key;
        FString Value;
        if (Group.RemoveFromStart(TEXT("import "), ESearchCase::IgnoreCase))
        {
            Out.Imports.Add(Group.TrimStartAndEnd());
        }
        else if (Group.RemoveFromStart(TEXT("export "), ESearchCase::IgnoreCase))
        {
            Out.ExportByproducts.Add(Group.TrimStartAndEnd());
        }
        else if (Group.StartsWith(TEXT("floorHeight ")) || Group.StartsWith(TEXT("beltTier ")))
        {
            Group.Split(TEXT(" "), &Key, &Value);
            int32 Number = 0;
            const bool bHeight = Key.Equals(TEXT("floorHeight"), ESearchCase::IgnoreCase);
            if (!LexTryParseString(Number, *Value.TrimStartAndEnd()) ||
                (bHeight ? Number < 16 || Number > 64 || bHeightSet : Number < 1 || Number > 6 || Out.BeltTier.IsSet()))
            {
                Error = TEXT("Set floorHeight once (16-64 metres) and beltTier once (1-6)");
                return false;
            }
            if (bHeight)
            {
                Out.FloorHeight = Number;
                bHeightSet = true;
            }
            else
                Out.BeltTier = Number;
        }
        else
        {
            const bool bRecipe = Group.RemoveFromStart(TEXT("recipe "), ESearchCase::IgnoreCase);
            const bool bFloor = Group.RemoveFromStart(TEXT("floor "), ESearchCase::IgnoreCase);
            Group.Split(TEXT("="), &Key, &Value);
            Key.TrimStartAndEndInline();
            Value.TrimStartAndEndInline();
            if (Key.IsEmpty() || Value.IsEmpty())
            {
                Error = FString::Printf(TEXT("Expected name=value in '%s'"), *Group);
                return false;
            }
            if (bRecipe && !Out.RecipeChoices.Contains(Key))
                Out.RecipeChoices.Add(Key, Value);
            else if (bFloor && !Out.Floors.Contains(Key))
            {
                int32 Floor = 0;
                if (!LexTryParseString(Floor, *Value) || Floor < 1 || Floor > 8)
                {
                    Error = TEXT("Floor must be 1-8");
                    return false;
                }
                Out.Floors.Add(Key, Floor);
            }
            else
            {
                double Rate;
                if (bFloor || bRecipe || Out.Targets.Contains(Key) || !PositiveRate(Value, Rate))
                {
                    Error = FString::Printf(TEXT("Duplicate setting or invalid rate: %s"), *Group);
                    return false;
                }
                Out.Targets.Add(Key, Rate);
            }
        }
    }
    if (!Out.Targets.IsEmpty())
        return true;
    Error = TEXT("Specify at least one product and rate");
    return false;
}

bool SolveProduction(const TArray<FProductionRecipe>& Recipes, const TSet<FString>& RawResources,
                     const FProductionRequest& Request, TArray<FProductionOrder>& Orders,
                     TMap<FString, double>& Imports, TMap<FString, double>& Exports, FString& Error)
{
    Orders.Reset();
    Imports.Reset();
    Exports.Reset();
    TMap<FString, int32> Defaults;
    TMap<FString, int32> RecipeIds;
    for (int32 Index = 0; Index < Recipes.Num(); ++Index)
    {
        const FProductionRecipe& Recipe = Recipes[Index];
        if (Recipe.Outputs.IsEmpty())
            continue;
        RecipeIds.Add(Recipe.Id, Index);
        const FString& Product = Recipe.Outputs[0].Item;
        const int32* Prior = Defaults.Find(Product);
        if (!Prior || (Recipes[*Prior].bAlternate && !Recipe.bAlternate))
            Defaults.Add(Product, Index);
    }
    for (const auto& Choice : Request.RecipeChoices)
    {
        const int32* Index = RecipeIds.Find(Choice.Value);
        if (!Index || RateOf(Recipes[*Index].Outputs, Choice.Key) <= Epsilon || Request.Imports.Contains(Choice.Key) ||
            !Reusable(Recipes[*Index], Choice.Key, Request))
        {
            Error = FString::Printf(TEXT("Recipe %s cannot supply %s"), *Choice.Value, *Choice.Key);
            return false;
        }
        Defaults.Add(Choice.Key, *Index);
    }

    TSet<FString> Visited;
    TSet<FString> Unresolved;
    TArray<int32> OrderedRecipes;
    TMap<int32, TArray<FString>> AssignedProducts;
    TFunction<void(const FString&)> AddProduct = [&](const FString& Item)
    {
        if (Visited.Contains(Item))
            return;
        Visited.Add(Item);
        if (Request.Imports.Contains(Item) || (RawResources.Contains(Item) && !Request.RecipeChoices.Contains(Item)))
            return;
        const int32* RecipeIndex = Defaults.Find(Item);
        if (!RecipeIndex)
        {
            Unresolved.Add(Item);
            return;
        }
        const int32 Index = *RecipeIndex;
        const bool bFirst = !AssignedProducts.Contains(Index);
        AssignedProducts.FindOrAdd(Index).Add(Item);
        if (!bFirst)
            return;
        for (const FProductionAmount& Input : Recipes[Index].Inputs)
            AddProduct(Input.Item);
        OrderedRecipes.Add(Index);
    };
    TArray<FString> TargetNames;
    Request.Targets.GetKeys(TargetNames);
    TargetNames.Sort();
    for (const FString& Item : TargetNames)
        AddProduct(Item);

    // Update net demand in place. A byproduct can reduce the rate of a recipe already visited,
    // so iterate until all rate changes are below the rounding threshold.
    TArray<double> Potentials;
    Potentials.SetNumZeroed(Recipes.Num());
    TMap<FString, double> Balance = Request.Targets;
    TMap<int32, double> RateScale;
    for (const int32 Index : OrderedRecipes)
    {
        double Scale = 1.0;
        for (const auto* Amounts : {&Recipes[Index].Inputs, &Recipes[Index].Outputs})
            for (const auto& Amount : *Amounts)
                Scale = FMath::Max(Scale, Amount.PerMinute);
        RateScale.Add(Index, Scale);
    }
    bool bConverged = false;
    for (int32 Iteration = 0; Iteration < 512; ++Iteration)
    {
        double Change = 0;
        for (const int32 Index : OrderedRecipes)
        {
            const FProductionRecipe& Recipe = Recipes[Index];
            const double Previous = Potentials[Index];
            double Desired = 0;
            for (const FString& Item : AssignedProducts.FindChecked(Index))
            {
                const double Output = RateOf(Recipe.Outputs, Item);
                Desired = FMath::Max(Desired, (Balance.FindRef(Item) +
                    (Reusable(Recipe, Item, Request) ? Output * Previous : 0.0)) / Output);
            }
            const double Delta = Desired - Previous;
            if (!FMath::IsFinite(Desired) || Desired > 1000000.0)
            {
                Error = FString::Printf(TEXT("Required rate diverges for %s"), *Recipe.Id);
                return false;
            }
            Potentials[Index] = Desired;
            Change = FMath::Max(Change, FMath::Abs(Delta) * RateScale.FindChecked(Index));
            ApplyRates(Balance, Recipe.Inputs, Delta);
            for (const FProductionAmount& Product : Recipe.Outputs)
                if (Reusable(Recipe, Product.Item, Request))
                    Balance.FindOrAdd(Product.Item) -= Product.PerMinute * Delta;
        }
        if (Change < Epsilon)
        {
            bConverged = true;
            break;
        }
    }
    if (!bConverged)
    {
        Error = TEXT("Material balance did not converge. Supply an intermediate or select a different recipe.");
        return false;
    }
    for (const FString& Item : Unresolved)
    {
        if (Balance.FindRef(Item) > Epsilon)
        {
            Error = FString::Printf(TEXT("No unlocked recipe for %s. Unlock it or specify import %s."), *Item, *Item);
            return false;
        }
    }
    for (const auto& Entry : Balance)
    {
        if (Entry.Value > Epsilon)
        {
            if (!RawResources.Contains(Entry.Key) && !Request.Imports.Contains(Entry.Key))
            {
                Error = FString::Printf(TEXT("Unmet demand for %s"), *Entry.Key);
                return false;
            }
            Imports.Add(Entry.Key, Entry.Value);
        }
        const double Export = Request.Targets.FindRef(Entry.Key) - FMath::Min(0.0, Entry.Value);
        if (Export > Epsilon)
            Exports.Add(Entry.Key, Export);
    }

    // A steady-state solution can still require a material that none of its machines can produce first.
    TSet<FString> StartedItems;
    for (const auto& Import : Imports)
        StartedItems.Add(Import.Key);
    TSet<int32> StartedRecipes;
    bool bStartedAnother;
    do
    {
        bStartedAnother = false;
        for (const int32 Index : OrderedRecipes)
        {
            const FProductionRecipe& Recipe = Recipes[Index];
            if (Potentials[Index] <= Epsilon || StartedRecipes.Contains(Index) ||
                Recipe.Inputs.ContainsByPredicate([&](const auto& Input) { return !StartedItems.Contains(Input.Item); }))
                continue;
            StartedRecipes.Add(Index);
            for (const auto& Output : Recipe.Outputs)
                if (Reusable(Recipe, Output.Item, Request))
                    StartedItems.Add(Output.Item);
            bStartedAnother = true;
        }
    } while (bStartedAnother);
    for (const int32 Index : OrderedRecipes)
    {
        if (Potentials[Index] > Epsilon && !StartedRecipes.Contains(Index))
        {
            Error = FString::Printf(TEXT("No cold-start supply for %s. Add an external input or choose another recipe."),
                                    *Recipes[Index].Id);
            return false;
        }
    }

    TMap<FString, int32> Stages;
    for (const int32 Index : OrderedRecipes)
    {
        if (Potentials[Index] <= Epsilon)
            continue;
        const FProductionRecipe& Recipe = Recipes[Index];
        int32 Stage = 0;
        for (const FProductionAmount& Input : Recipe.Inputs)
            if (const int32* Prior = Stages.Find(Input.Item))
                Stage = FMath::Max(Stage, *Prior + 1);
        for (const FProductionAmount& Output : Recipe.Outputs)
        {
            Stages.FindOrAdd(Output.Item) = Stage;
            if (!Reusable(Recipe, Output.Item, Request))
                Exports.FindOrAdd(Output.Item) += Output.PerMinute * Potentials[Index];
        }
        const int32* FixedFloor = Request.Floors.Find(Recipe.Id);
        Orders.Add({Index, Potentials[Index], FixedFloor ? *FixedFloor : FMath::Min(8, 1 + Stage / 2)});
    }
    if (Orders.IsEmpty())
    {
        Error = TEXT("This plan contains no production machines");
        return false;
    }
    return true;
}

bool BuildProductionPlan(UWorld* World, UBuildableCache* Cache, FProductionRequest Request,
                         int32 BeltTier, int32 PipeTier, FFactoryCommand& Command, FProductionPlan& Plan, FString& Error)
{
    const AFGRecipeManager* Manager = AFGRecipeManager::Get(World);
    if (!Manager)
    {
        Error = TEXT("Recipe manager unavailable");
        return false;
    }

    const EBuildable SupportedMachines[] = {
        EBuildable::Smelter, EBuildable::Constructor, EBuildable::Assembler, EBuildable::Foundry,
        EBuildable::Manufacturer, EBuildable::OilRefinery, EBuildable::Blender, EBuildable::Converter,
        EBuildable::ParticleAccelerator, EBuildable::QuantumEncoder, EBuildable::Packager
    };
    TMap<UClass*, EBuildable> MachineTypes;
    for (const EBuildable Type : SupportedMachines)
        if (const TSubclassOf<AFGBuildableManufacturer> Machine = Cache->GetBuildableClass<AFGBuildableManufacturer>(Type))
            MachineTypes.Add(Machine.Get(), Type);

    TMap<FString, TArray<FString>> ItemAliases;
    TMap<FString, TArray<FString>> RecipeAliases;
    TSet<FString> RawResources;
    const auto AddItem = [&](TSubclassOf<UFGItemDescriptor> Item)
    {
        if (!Item || Plan.Items.Contains(Item->GetName()))
            return;
        const FString Id = Item->GetName();
        Plan.Items.Add(Id, Item);
        ItemAliases.FindOrAdd(Normalize(Id, TEXT("Desc_"), true)).AddUnique(Id);
        ItemAliases.FindOrAdd(Normalize(UFGItemDescriptor::GetItemName(Item).ToString(), TEXT(""), false)).AddUnique(Id);
        if (Item->IsChildOf(UFGResourceDescriptor::StaticClass()) || Item->GetPathName().Contains(TEXT("/RawResources/")))
            RawResources.Add(Id);
    };
    for (const auto& Item : Manager->GetAllItemDescriptors())
        AddItem(Item);

    auto Available = Manager->GetAllAvailableRecipes();
    Available.Sort([](const auto& A, const auto& B) { return GetNameSafe(A.Get()) < GetNameSafe(B.Get()); });
    TArray<FProductionRecipe> Recipes;
    TArray<TSubclassOf<UFGRecipe>> Classes;
    for (const auto& Class : Available)
    {
        if (!Class)
            continue;
        const UFGRecipe* Recipe = Class.GetDefaultObject();
        const double Duration = Recipe->GetManufacturingDuration();
        if (Duration <= 0 || Recipe->GetProducts().IsEmpty())
            continue;
        TArray<TSubclassOf<UObject>> Producers;
        Recipe->GetProducedIn(Producers);
        const EBuildable* Machine = nullptr;
        for (const auto& Producer : Producers)
        {
            Machine = MachineTypes.Find(Producer.Get());
            if (Machine)
                break;
        }
        if (!Machine)
            continue;

        FProductionRecipe Entry{Class->GetName(), *Machine};
        Entry.bAlternate = Entry.Id.Contains(TEXT("Alternate")) || Entry.Id.Contains(TEXT("Residual")) ||
                           Entry.Id.Contains(TEXT("Recycled"));
        const auto CopyAmounts = [&](const TArray<FItemAmount>& Source, TArray<FProductionAmount>& Destination)
        {
            for (const FItemAmount& Amount : Source)
            {
                if (!Amount.ItemClass || Amount.Amount <= 0)
                    continue;
                AddItem(Amount.ItemClass);
                const double Unit = IsFluid(Amount.ItemClass) ? 1000.0 : 1.0;
                Destination.Add({Amount.ItemClass->GetName(), 60.0 * Amount.Amount / Duration / Unit});
            }
        };
        CopyAmounts(Recipe->GetIngredients(), Entry.Inputs);
        CopyAmounts(Recipe->GetProducts(), Entry.Outputs);
        if (Entry.Outputs.IsEmpty())
            continue;
        RecipeAliases.FindOrAdd(Normalize(Entry.Id, TEXT("Recipe_"), true)).AddUnique(Entry.Id);
        RecipeAliases.FindOrAdd(Normalize(Recipe->GetDisplayName().ToString(), TEXT(""), false)).AddUnique(Entry.Id);
        Recipes.Add(MoveTemp(Entry));
        Classes.Add(Class);
    }

    FProductionRequest Resolved;
    for (const auto& Target : Request.Targets)
    {
        FString Item;
        if (!Resolve(ItemAliases, Target.Key, TEXT("Item"), TEXT("Desc_"), Item, Error))
            return false;
        Resolved.Targets.FindOrAdd(Item) += Target.Value;
    }
    const auto ResolveItems = [&](const TSet<FString>& Source, TSet<FString>& Destination)
    {
        for (const FString& Name : Source)
        {
            FString Item;
            if (!Resolve(ItemAliases, Name, TEXT("Item"), TEXT("Desc_"), Item, Error))
                return false;
            Destination.Add(Item);
        }
        return true;
    };
    if (!ResolveItems(Request.Imports, Resolved.Imports) ||
        !ResolveItems(Request.ExportByproducts, Resolved.ExportByproducts))
        return false;
    for (const auto& Choice : Request.RecipeChoices)
    {
        FString Item;
        FString Recipe;
        if (!Resolve(ItemAliases, Choice.Key, TEXT("Item"), TEXT("Desc_"), Item, Error) ||
            !Resolve(RecipeAliases, Choice.Value, TEXT("Recipe"), TEXT("Recipe_"), Recipe, Error))
            return false;
        Resolved.RecipeChoices.Add(Item, Recipe);
    }
    for (const auto& Floor : Request.Floors)
    {
        FString Recipe;
        if (!Resolve(RecipeAliases, Floor.Key, TEXT("Recipe"), TEXT("Recipe_"), Recipe, Error))
            return false;
        Resolved.Floors.Add(Recipe, Floor.Value);
    }

    TArray<FProductionOrder> Orders;
    if (!SolveProduction(Recipes, RawResources, Resolved, Orders, Plan.Imports, Plan.Exports, Error))
        return false;

    constexpr double BeltRates[] = {60, 120, 270, 480, 780, 1200};
    Plan.BeltCapacity = BeltRates[BeltTier - 1];
    Plan.PipeCapacity = PipeTier == 2 ? 600.0 : 300.0;
    Plan.ExportByproducts = MoveTemp(Resolved.ExportByproducts);
    Command.FloorHeightMeters = Request.FloorHeight;
    Command.BeltTier = BeltTier;

    for (const FProductionOrder& Order : Orders)
    {
        const FProductionRecipe& Recipe = Recipes[Order.RecipeIndex];
        const auto Machine = Cache->GetBuildableClass<AFGBuildableManufacturer>(Recipe.Machine);
        const AFGBuildableManufacturer* Defaults = Machine.GetDefaultObject();
        const auto FullLanes = [&](const TArray<FProductionAmount>& Rates)
        {
            double Ratio = 0;
            for (const auto& Rate : Rates)
                Ratio = FMath::Max(Ratio, Rate.PerMinute /
                    (IsFluid(Plan.Items.FindChecked(Rate.Item)) ? Plan.PipeCapacity : Plan.BeltCapacity));
            return Ratio;
        };
        const double LanesAtFullClock = FMath::Max(FullLanes(Recipe.Inputs), FullLanes(Recipe.Outputs));
        const int32 Count = FMath::CeilToInt(Order.Potential * FMath::Max(1.0, LanesAtFullClock) - Epsilon);
        const double Clock = Order.Potential / FMath::Max(Count, 1);
        if (Count < 1 || Clock + Epsilon < Defaults->GetCurrentMinPotential())
        {
            Error = FString::Printf(TEXT("%s cannot run that slowly with the available belt/pipe tier. Increase the target rate or tier."),
                                    *Recipe.Id);
            return false;
        }
        const int32 PerRow = LanesAtFullClock > Epsilon
            ? FMath::Clamp(FMath::FloorToInt(1.0 / (LanesAtFullClock * Clock) + Epsilon), 1, 6) : 6;
        const auto* Variable = Cast<AFGBuildableManufacturerVariablePower>(Defaults);
        const UFGRecipe* RecipeData = Classes[Order.RecipeIndex].GetDefaultObject();
        const double BasePeak = Variable
            ? FMath::Max(Variable->mEstimatedMaximumPowerConsumption,
                         RecipeData->GetPowerConsumptionConstant() + RecipeData->GetPowerConsumptionFactor())
            : Defaults->GetDefaultProducingPowerConsumption();
        Plan.PeakPowerMW += BasePeak * FMath::Pow(Clock, 1.32192809489) * Count;
        for (int32 Remaining = Count; Remaining > 0; Remaining -= PerRow)
        {
            const int32 RowCount = FMath::Min(PerRow, Remaining);
            FFactoryCommandToken& Row = Command.Rows.AddDefaulted_GetRef();
            Row.Count = RowCount;
            Row.MachineType = Recipe.Machine;
            Row.Floor = Order.Floor;
            Row.ClockPercent = Clock * 100;
            FProductionRow& Flow = Plan.Rows.AddDefaulted_GetRef();
            Flow.Recipe = Classes[Order.RecipeIndex];
            Flow.bFixedFloor = Resolved.Floors.Contains(Recipe.Id);
            Flow.Inputs = Recipe.Inputs;
            Flow.Outputs = Recipe.Outputs;
            for (auto* Rates : {&Flow.Inputs, &Flow.Outputs})
                for (auto& Rate : *Rates)
                    Rate.PerMinute *= Clock * RowCount;
        }
    }
    return true;
}

FString DescribeProduction(const FFactoryCommand& Command, const FProductionPlan& Plan)
{
    int32 Machines = 0;
    for (const FFactoryCommandToken& Row : Command.Rows)
        Machines += Row.Count;
    const auto DescribeRates = [&](const TMap<FString, double>& Rates)
    {
        TArray<FString> Keys;
        Rates.GetKeys(Keys);
        Keys.Sort();
        TArray<FString> Lines;
        for (const FString& Key : Keys)
        {
            const auto Item = Plan.Items.FindChecked(Key);
            Lines.Add(FString::Printf(TEXT("%s %.2f %s/min"), *UFGItemDescriptor::GetItemName(Item).ToString(),
                                     Rates.FindChecked(Key), IsFluid(Item) ? TEXT("m3") : TEXT("items")));
        }
        return FString::Join(Lines, TEXT(", "));
    };
    return FString::Printf(TEXT("%d machines, estimated machine peak %.0f MW\nInputs: %s\nOutputs: %s"),
                           Machines, Plan.PeakPowerMW, *DescribeRates(Plan.Imports), *DescribeRates(Plan.Exports));
}
