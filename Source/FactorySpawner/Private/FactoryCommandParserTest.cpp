#include "FactoryCommandParser.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactorySpawnerFloorsTest, "FactorySpawner.Command.Floors",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactorySpawnerFloorsTest::RunTest(const FString&)
{
    FFactoryCommand Command;
    FString Error;
    const bool bParsed = FFactoryCommandParser::ParseCommand(
        TEXT("2 Smelter IngotIron 75% floor=3, 3 Constructor IronPlate FLOOR=2, 1 Assembler, floorHeight 48, beltTier 3"),
        Command, Error);
    if (!TestTrue(*Error, bParsed) || !TestEqual(TEXT("Row count"), Command.Rows.Num(), 3))
        return false;

    TestEqual(TEXT("Source floor"), Command.Rows[0].Floor, 3);
    TestEqual(TEXT("Destination floor"), Command.Rows[1].Floor, 2);
    TestEqual(TEXT("Unspecified floor"), Command.Rows[2].Floor, 1);
    TestEqual(TEXT("Clock percent"), Command.Rows[0].ClockPercent.Get(0.0f), 75.0f);
    TestEqual(TEXT("Destination recipe"), Command.Rows[1].Recipe.Get(FString()), FString(TEXT("IronPlate")));
    TestEqual(TEXT("Floor height"), Command.FloorHeightMeters, 48);
    TestEqual(TEXT("Belt tier"), Command.BeltTier.Get(0), 3);

    if (!TestTrue(TEXT("Single-row command"),
                  FFactoryCommandParser::ParseCommand(TEXT("1 Smelter IngotIron"), Command, Error)))
        return false;
    TestEqual(TEXT("Default floor height"), Command.FloorHeightMeters, 32);
    TestFalse(TEXT("Automatic belt tier"), Command.BeltTier.IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactorySpawnerInvalidFloorsTest, "FactorySpawner.Command.InvalidFloors",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactorySpawnerInvalidFloorsTest::RunTest(const FString&)
{
    const TCHAR* InvalidCommands[] = {
        TEXT("1 Smelter floor=0"),
        TEXT("1 Smelter floor=9"),
        TEXT("1 Smelter floor=top"),
        TEXT("1 Smelter floor=2 floor=3"),
        TEXT("1 Smelter floor=2 75"),
        TEXT("1 Smelter floor=2, floorHeight 8"),
        TEXT("1 Smelter floor=2, floorHeight 40, floorHeight 48"),
        TEXT("floorHeight 32"),
    };
    for (const TCHAR* Input : InvalidCommands)
    {
        FFactoryCommand Command;
        FString Error;
        TestFalse(Input, FFactoryCommandParser::ParseCommand(Input, Command, Error));
        TestTrue(TEXT("Error message"), !Error.IsEmpty());
        TestTrue(TEXT("No partially parsed result"), Command.Rows.IsEmpty());
    }
    return true;
}

#endif
