#include "FactoryCommandParser.h"
#include "FactorySpawnerChat.h"

static inline EBuildable ParseBuildableFromString(const FString& Input)
{
    FString Normalized = Input.ToLower();

    if (Normalized == "smelter")
        return EBuildable::Smelter;
    if (Normalized == "constructor")
        return EBuildable::Constructor;
    if (Normalized == "assembler")
        return EBuildable::Assembler;
    if (Normalized == "foundry")
        return EBuildable::Foundry;
    if (Normalized == "manufacturer")
        return EBuildable::Manufacturer;

    // Optionally handle other buildables or return invalid
    return EBuildable::Invalid;
}

bool FFactoryCommandParser::ParseCommand(const FString& Input, TArray<FFactoryCommandToken>& OutTokens,
                                         FString& OutError)
{
    OutTokens.Reset();

    // Split groups by comma
    TArray<FString> Groups;
    Input.ParseIntoArray(Groups, TEXT(","), true);

    for (int32 g = 0; g < Groups.Num(); ++g)
    {
        FString Group = Groups[g].TrimStartAndEnd();
        if (Group.IsEmpty())
            continue;

        TArray<FString> Parts;
        Group.ParseIntoArrayWS(Parts);

        // Enforce 2 - 4 parts
        if (Parts.Num() < 2 || Parts.Num() > 4)
        {
            OutError = FString::Printf(
                TEXT("Group %d: expected 2 - 4 tokens (count machine [recipe] [clock%%]), got %d: '%s'"), g + 1,
                Parts.Num(), *Group);
            return false;
        }

        // Part 1: count
        int32 Count;
        if (!LexTryParseString(Count, *Parts[0]) || Count <= 0)
        {
            OutError = FString::Printf(TEXT("Group %d: count must be a positive integer, got '%s'"), g + 1, *Parts[0]);
            return false;
        }

        FFactoryCommandToken Token;
        Token.Count = Count;

        // Part 2: machine type

        EBuildable EnumVal = ParseBuildableFromString(Parts[1]);
        if (EnumVal == EBuildable::Invalid)
        {
            OutError = FString::Printf(TEXT("Group %d: unknown machine type '%s'. Choose: Smelter, Constructor, "
                                            "Assembler, Foundry or Manufacturer!"),
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

            if (!(ClockSpeed > 0.0f && ClockSpeed < 100.0f))
            {
                OutError =
                    FString::Printf(TEXT("Group %d: clock percent must be > 0 and < 100, got %f"), g + 1, ClockSpeed);
                return false;
            }

            Token.ClockPercent = ClockSpeed;
        }

        OutTokens.Add(Token);
    }

    return true;
}
