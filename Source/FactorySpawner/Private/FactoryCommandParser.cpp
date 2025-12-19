#include "FactoryCommandParser.h"
#include "FactorySpawnerChat.h"

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
        {TEXT("packager"), EBuildable::Packager}};

    const EBuildable* Found = BuildableMap.Find(Input.ToLower());
    return Found ? *Found : EBuildable::Invalid;
}

bool FFactoryCommandParser::ParseCommand(const FString& Input, TArray<FFactoryCommandToken>& OutTokens,
                                         FString& OutError)
{
    OutTokens.Reset();

    // Check for optional beltTier parameter at the end
    TOptional<int32> BeltTier;
    FString CommandInput = Input;

    // Look for "beltTier N" pattern (case-insensitive)
    int32 BeltTierIndex = CommandInput.Find(TEXT("beltTier"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
    if (BeltTierIndex != INDEX_NONE)
    {
        // Extract everything after "beltTier"
        FString BeltTierPart = CommandInput.RightChop(BeltTierIndex + 8).TrimStartAndEnd(); // 8 = len("beltTier")

        // Check if there's a comma before beltTier
        FString BeforeBeltTier = CommandInput.Left(BeltTierIndex).TrimEnd();
        if (BeforeBeltTier.EndsWith(TEXT(",")))
        {
            BeforeBeltTier = BeforeBeltTier.LeftChop(1).TrimEnd();
        }

        // Parse the tier number
        int32 Tier;
        if (!LexTryParseString(Tier, *BeltTierPart) || Tier < 1 || Tier > 5)
        {
            OutError = FString::Printf(TEXT("beltTier must be 1-6, got '%s'"), *BeltTierPart);
            return false;
        }

        BeltTier = Tier;
        CommandInput = BeforeBeltTier;
    }

    // Split groups by comma
    TArray<FString> Groups;
    CommandInput.ParseIntoArray(Groups, TEXT(","), true);

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

        int32 Count;
        if (!LexTryParseString(Count, *Parts[0]) || Count <= 0)
        {
            OutError = FString::Printf(TEXT("Group %d: count must be positive, got '%s'"), g + 1, *Parts[0]);
            return false;
        }

        FFactoryCommandToken Token{Count};

        // Part 2: machine type

        EBuildable EnumVal = ParseBuildableFromString(Parts[1]);
        if (EnumVal == EBuildable::Invalid)
        {
            OutError = FString::Printf(TEXT("Group %d: unknown machine type '%s'. Choose: Constructor, "
                                            "Assembler, Manufacturer, Packager, Refinery, Blender, "
                                            "ParticleAccelerator, Converter, QuantumEncoder, Smelter or Foundry!"),
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

            if (ClockSpeed <= 0.0f || ClockSpeed >= 100.0f)
            {
                OutError = FString::Printf(TEXT("Group %d: clock must be 0-100, got %.1f"), g + 1, ClockSpeed);
                return false;
            }

            Token.ClockPercent = ClockSpeed;
        }

        // Apply belt tier if specified
        if (BeltTier.IsSet())
        {
            Token.BeltTier = BeltTier.GetValue();
        }

        OutTokens.Add(Token);
    }

    return true;
}
