#include "FactoryCommandParser.h"

static inline EBuildable ParseBuildableFromString(const FString& Input)
{
    static const TMap<FString, EBuildable> BuildableMap = {
        {TEXT("smelter"), EBuildable::Smelter},
        {TEXT("constructor"), EBuildable::Constructor},
        {TEXT("assembler"), EBuildable::Assembler},
        {TEXT("foundry"), EBuildable::Foundry},
        {TEXT("manufacturer"), EBuildable::Manufacturer},
        {TEXT("oilrefinery"), EBuildable::OilRefinery},
        {TEXT("refinery"), EBuildable::OilRefinery},
        {TEXT("blender"), EBuildable::Blender},
        {TEXT("converter"), EBuildable::Converter},
        {TEXT("particleaccelerator"), EBuildable::ParticleAccelerator},
        {TEXT("accelerator"), EBuildable::ParticleAccelerator},
        {TEXT("hadroncollider"), EBuildable::ParticleAccelerator},
        {TEXT("quantumencoder"), EBuildable::QuantumEncoder},
        {TEXT("encoder"), EBuildable::QuantumEncoder},
        {TEXT("coalgenerator"), EBuildable::CoalGenerator},
        {TEXT("fuelgenerator"), EBuildable::FuelGenerator},
        {TEXT("nuclearreactor"), EBuildable::NuclearReactor},
        {TEXT("nuclearpowerplant"), EBuildable::NuclearReactor},
        {TEXT("packager"), EBuildable::Packager}};

    const EBuildable* Found = BuildableMap.Find(Input.ToLower());
    return Found ? *Found : EBuildable::Invalid;
}

bool FFactoryCommandParser::ParseCommand(const FString& Input, FFactoryCommand& OutCommand, FString& OutError)
{
    OutCommand = {};
    OutError.Reset();
    FFactoryCommand Command;
    bool bHasFloorHeight = false;

    // Split rows and whole-command options by comma.
    TArray<FString> Groups;
    Input.ParseIntoArray(Groups, TEXT(","), true);

    for (int32 g = 0; g < Groups.Num(); ++g)
    {
        FString Group = Groups[g].TrimStartAndEnd();
        if (Group.IsEmpty())
            continue;

        TArray<FString> Parts;
        Group.ParseIntoArrayWS(Parts);

        // Parse optional beltTier and floorHeight settings as standalone groups.
        const bool bBeltTier = Parts[0].Equals(TEXT("beltTier"), ESearchCase::IgnoreCase);
        const bool bFloorHeight = Parts[0].Equals(TEXT("floorHeight"), ESearchCase::IgnoreCase);
        if (bBeltTier || bFloorHeight)
        {
            const int32 Minimum = bBeltTier ? 1 : 16;
            const int32 Maximum = bBeltTier ? 6 : 64;
            int32 Value;
            if (Parts.Num() != 2 || !LexTryParseString(Value, *Parts[1]) || Value < Minimum || Value > Maximum)
            {
                OutError = FString::Printf(TEXT("Group %d: %s must be %d-%d"), g + 1, *Parts[0], Minimum, Maximum);
                return false;
            }
            if ((bBeltTier && Command.BeltTier.IsSet()) || (bFloorHeight && bHasFloorHeight))
            {
                OutError = FString::Printf(TEXT("Group %d: duplicate %s setting"), g + 1, *Parts[0]);
                return false;
            }
            if (bBeltTier)
                Command.BeltTier = Value;
            else
            {
                Command.FloorHeightMeters = Value;
                bHasFloorHeight = true;
            }
            continue;
        }

        int32 Floor = 1;
        if (Parts.Last().StartsWith(TEXT("floor="), ESearchCase::IgnoreCase))
        {
            const FString FloorPart = Parts.Pop().RightChop(6);
            if (!LexTryParseString(Floor, *FloorPart) || Floor < 1 || Floor > 8)
            {
                OutError = FString::Printf(TEXT("Group %d: floor must be 1-8, got '%s'"), g + 1, *FloorPart);
                return false;
            }
        }

        if (Parts.ContainsByPredicate([](const FString& Part) { return Part.StartsWith(TEXT("floor=")); }))
        {
            OutError = FString::Printf(TEXT("Group %d: add floor=N once, at the end of the machine row"), g + 1);
            return false;
        }

        // Enforce 2 - 4 machine tokens after the optional floor suffix.
        if (Parts.Num() < 2 || Parts.Num() > 4)
        {
            OutError = FString::Printf(
                TEXT("Group %d: expected count machine [recipe] [clock%%] [floor=N], got %d tokens: '%s'"), g + 1,
                Parts.Num(), *Group);
            return false;
        }

        int32 Count;
        if (!LexTryParseString(Count, *Parts[0]) || Count <= 0)
        {
            OutError = FString::Printf(TEXT("Group %d: count must be positive, got '%s'"), g + 1, *Parts[0]);
            return false;
        }

        FFactoryCommandToken Token{Count};
        Token.Floor = Floor;

        // Part 2: machine type

        EBuildable EnumVal = ParseBuildableFromString(Parts[1]);
        if (EnumVal == EBuildable::Invalid)
        {
            OutError = FString::Printf(TEXT("Group %d: unknown machine type '%s'. Choose: Constructor, "
                                            "Assembler, Manufacturer, Packager, Refinery, Blender, "
                                            "ParticleAccelerator, Converter, QuantumEncoder, Smelter, Foundry, "
                                            "CoalGenerator, FuelGenerator or NuclearReactor!"),
                                       g + 1, *Parts[1]);
            return false;
        }
        Token.MachineType = EnumVal;

        // Part 3: optional recipe
        if (Parts.Num() >= 3)
        {
            Token.Recipe = Parts[2];
        }

        // Part 4: optional clock percent
        if (Parts.Num() == 4)
        {
            FString ClockToken = Parts[3].TrimStartAndEnd();
            if (ClockToken.EndsWith(TEXT("%")))
            {
                ClockToken = ClockToken.LeftChop(1);
            }
            float ClockSpeed;
            if (!LexTryParseString(ClockSpeed, *ClockToken))
            {
                OutError = FString::Printf(
                    TEXT("Group %d: invalid clock percent '%s' (must be numeric, optionally with decimal point)"),
                    g + 1, *Parts[3]);
                return false;
            }

            if (!FMath::IsFinite(ClockSpeed) || ClockSpeed <= 0.0f || ClockSpeed >= 100.0f)
            {
                OutError = FString::Printf(TEXT("Group %d: clock must be 0-100, got %.1f"), g + 1, ClockSpeed);
                return false;
            }

            Token.ClockPercent = ClockSpeed;
        }

        Command.Rows.Add(MoveTemp(Token));
    }

    if (Command.Rows.IsEmpty())
    {
        OutError = TEXT("Specify at least one machine row");
        return false;
    }
    OutCommand = MoveTemp(Command);
    return true;
}
