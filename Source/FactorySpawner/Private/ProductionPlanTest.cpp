#include "ProductionPlan.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace
{
    FProductionRecipe Recipe(const TCHAR* Name, TArray<FProductionAmount> Inputs, TArray<FProductionAmount> Outputs)
    {
        return {Name, EBuildable::Constructor, MoveTemp(Inputs), MoveTemp(Outputs)};
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactorySpawnerProductionParser, "FactorySpawner.Production.Request",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactorySpawnerProductionParser::RunTest(const FString&)
{
    FProductionRequest Request;
    FString Error;
    if (!TestTrue(TEXT("Multi-product preview"),
                  ParseProductionRequest(TEXT("plan Motor=20, Stator=10, import Steel Pipe, export Water, "
                                              "recipe Iron Ingot=PureIronIngot, floor Motor=3, floorHeight 48, beltTier 6"),
                                         Request, Error)))
        return false;
    TestTrue(TEXT("Preview mode"), Request.bPreview);
    TestEqual(TEXT("Target rate"), Request.Targets.FindRef(TEXT("Motor")), 20.0);
    TestEqual(TEXT("Second target rate"), Request.Targets.FindRef(TEXT("Stator")), 10.0);
    TestTrue(TEXT("Import"), Request.Imports.Contains(TEXT("Steel Pipe")));
    TestTrue(TEXT("Export produced byproduct"), Request.ExportByproducts.Contains(TEXT("Water")));
    TestEqual(TEXT("Recipe override"), Request.RecipeChoices.FindRef(TEXT("Iron Ingot")), FString(TEXT("PureIronIngot")));
    TestEqual(TEXT("Floor"), Request.Floors.FindRef(TEXT("Motor")), 3);
    TestEqual(TEXT("Floor height"), Request.FloorHeight, 48);
    TestEqual(TEXT("Tier"), Request.BeltTier.Get(0), 6);
    TestFalse(TEXT("Reject duplicate targets"), ParseProductionRequest(TEXT("build Motor=1, Motor=2"), Request, Error));
    TestFalse(TEXT("Reject negative rate"), ParseProductionRequest(TEXT("build Motor=-1"), Request, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactorySpawnerProductionSolid, "FactorySpawner.Production.SolidShared",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactorySpawnerProductionSolid::RunTest(const FString&)
{
    const TArray<FProductionRecipe> Recipes = {
        Recipe(TEXT("Smelt"), {{TEXT("Ore"), 30}}, {{TEXT("Ingot"), 30}}),
        Recipe(TEXT("Plate"), {{TEXT("Ingot"), 30}}, {{TEXT("Plate"), 20}}),
        Recipe(TEXT("Rod"), {{TEXT("Ingot"), 15}}, {{TEXT("Rod"), 15}}),
        Recipe(TEXT("Screw"), {{TEXT("Rod"), 10}}, {{TEXT("Screw"), 40}}),
        Recipe(TEXT("RIP"), {{TEXT("Plate"), 30}, {TEXT("Screw"), 60}}, {{TEXT("RIP"), 5}})
    };
    FProductionRequest Request;
    Request.Targets = {{TEXT("RIP"), 5}, {TEXT("Rod"), 10}};
    TArray<FProductionOrder> Orders;
    TMap<FString, double> Inputs, Outputs;
    FString Error;
    if (!SolveProduction(Recipes, {TEXT("Ore")}, Request, Orders, Inputs, Outputs, Error))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Shared rod producer"), Orders.Num(), 5);
    TestEqual(TEXT("Imports aggregate both targets"), Inputs.FindRef(TEXT("Ore")), 70.0);
    TestEqual(TEXT("Plate output"), Outputs.FindRef(TEXT("RIP")), 5.0);
    TestEqual(TEXT("Rod output"), Outputs.FindRef(TEXT("Rod")), 10.0);
    TestEqual(TEXT("No unexpected outputs"), Outputs.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactorySpawnerProductionFluid, "FactorySpawner.Production.FluidByproduct",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactorySpawnerProductionFluid::RunTest(const FString&)
{
    const TArray<FProductionRecipe> Recipes = {
        Recipe(TEXT("Plastic"), {{TEXT("Oil"), 30}}, {{TEXT("Plastic"), 20}, {TEXT("HeavyOil"), 10}}),
        Recipe(TEXT("Fuel"), {{TEXT("HeavyOil"), 6}}, {{TEXT("Fuel"), 4}}),
    };
    FProductionRequest Request;
    Request.Targets = {{TEXT("Plastic"), 20}, {TEXT("Fuel"), 4}};
    TArray<FProductionOrder> Orders;
    TMap<FString, double> Inputs, Outputs;
    FString Error;
    if (!SolveProduction(Recipes, {TEXT("Oil")}, Request, Orders, Inputs, Outputs, Error))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Crude oil input"), Inputs.FindRef(TEXT("Oil")), 30.0);
    TestEqual(TEXT("Unused heavy oil goes to an output"), Outputs.FindRef(TEXT("HeavyOil")), 4.0);

    Request.Imports.Add(TEXT("HeavyOil"));
    Request.ExportByproducts.Add(TEXT("HeavyOil"));
    if (!SolveProduction(Recipes, {TEXT("Oil")}, Request, Orders, Inputs, Outputs, Error))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Separate inlet"), Inputs.FindRef(TEXT("HeavyOil")), 6.0);
    TestEqual(TEXT("Separate full byproduct outlet"), Outputs.FindRef(TEXT("HeavyOil")), 10.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactorySpawnerProductionRecycle, "FactorySpawner.Production.RecycleAndStartup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactorySpawnerProductionRecycle::RunTest(const FString&)
{
    const TArray<FProductionRecipe> Recipes = {
        Recipe(TEXT("Alumina"), {{TEXT("Bauxite"), 120}, {TEXT("Water"), 180}},
               {{TEXT("Alumina"), 120}, {TEXT("Silica"), 50}}),
        Recipe(TEXT("Scrap"), {{TEXT("Alumina"), 240}, {TEXT("Coal"), 120}},
               {{TEXT("Scrap"), 360}, {TEXT("Water"), 120}}),
        Recipe(TEXT("Ingot"), {{TEXT("Scrap"), 90}, {TEXT("Silica"), 75}}, {{TEXT("Ingot"), 60}}),
        Recipe(TEXT("Silica"), {{TEXT("Quartz"), 22.5}}, {{TEXT("Silica"), 37.5}})
    };
    FProductionRequest Request;
    Request.Targets.Add(TEXT("Ingot"), 60);
    TArray<FProductionOrder> Orders;
    TMap<FString, double> Inputs, Outputs;
    FString Error;
    if (!SolveProduction(Recipes, {TEXT("Bauxite"), TEXT("Water"), TEXT("Coal"), TEXT("Quartz")},
                         Request, Orders, Inputs, Outputs, Error))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("Water consumed minus recovered"), Inputs.FindRef(TEXT("Water")), 60.0);
    TestEqual(TEXT("Aggregate silica credits"), Inputs.FindRef(TEXT("Quartz")), 30.0);
    TestEqual(TEXT("No surplus water"), Outputs.FindRef(TEXT("Water")), 0.0);

    const TArray<FProductionRecipe> Blocked = {
        Recipe(TEXT("A"), {{TEXT("B"), 10}}, {{TEXT("A"), 10}}),
        Recipe(TEXT("B"), {{TEXT("A"), 5}}, {{TEXT("B"), 10}})
    };
    Request.Targets = {{TEXT("A"), 5}};
    TestFalse(TEXT("A balanced but unseeded loop fails"),
              SolveProduction(Blocked, {}, Request, Orders, Inputs, Outputs, Error));
    TestTrue(TEXT("Cold-start diagnostic"), Error.Contains(TEXT("cold-start")));
    return true;
}

#endif
