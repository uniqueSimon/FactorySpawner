#include "BuildableCache.h"
#include "FGRecipeManager.h"
#include "FGBuildableConveyorBelt.h"
#include "FGBuildableConveyorLift.h"
#include "FGBuildablePipeline.h"
#include "SoftObjectPtr.h"
#include "FGBuildableManufacturer.h"
#include "FactorySpawner.h"
#include "FactorySpawnerChat.h"

namespace
{
    // Helper: get enum name as string
    FString GetEnumName(EBuildable Value)
    {
        const UEnum* EnumPtr = StaticEnum<EBuildable>();
        return EnumPtr->GetNameStringByValue(static_cast<int64>(Value));
    }

    // Generic soft class loader
    template <typename T> TSubclassOf<T> LoadClassSoft(const FString& Path, EBuildable Type)
    {
        TSoftClassPtr<T> SoftClass(Path);
        TSubclassOf<T> Loaded = SoftClass.LoadSynchronous();
        if (!Loaded)
            UE_LOG(LogFactorySpawner, Error, TEXT("Failed to load class for %d at path: %s"), (int32) Type, *Path);
        return Loaded;
    }

    // Machine class paths table
    TMap<EBuildable, FString> MachineClassPaths = {
        {EBuildable::Splitter, "/Game/FactoryGame/Buildable/Factory/CA_Splitter/"
                               "Build_ConveyorAttachmentSplitter.Build_ConveyorAttachmentSplitter_C"},
        {EBuildable::Merger, "/Game/FactoryGame/Buildable/Factory/CA_Merger/"
                             "Build_ConveyorAttachmentMerger.Build_ConveyorAttachmentMerger_C"},
        {EBuildable::PowerPole,
         "/Game/FactoryGame/Buildable/Factory/PowerPoleMk1/Build_PowerPoleMk1.Build_PowerPoleMk1_C"},
        {EBuildable::PowerLine, "/Game/FactoryGame/Buildable/Factory/PowerLine/Build_PowerLine.Build_PowerLine_C"},
        {EBuildable::PipeCross, "/Game/FactoryGame/Buildable/Factory/PipeJunction/"
                                "Build_PipelineJunction_Cross.Build_PipelineJunction_Cross_C"},
        {EBuildable::OilRefinery,
         "/Game/FactoryGame/Buildable/Factory/OilRefinery/Build_OilRefinery.Build_OilRefinery_C"},
        {EBuildable::Blender, "/Game/FactoryGame/Buildable/Factory/Blender/Build_Blender.Build_Blender_C"},
        {EBuildable::Converter, "/Game/FactoryGame/Buildable/Factory/Converter/Build_Converter.Build_Converter_C"},
        {EBuildable::ParticleAccelerator,
         "/Game/FactoryGame/Buildable/Factory/HadronCollider/Build_HadronCollider.Build_HadronCollider_C"},
        {EBuildable::QuantumEncoder,
         "/Game/FactoryGame/Buildable/Factory/QuantumEncoder/Build_QuantumEncoder.Build_QuantumEncoder_C"},
        {EBuildable::Packager, "/Game/FactoryGame/Buildable/Factory/Packager/Build_Packager.Build_Packager_C"}};

} // namespace

//-------------------------------------------------
// Buildable class loader
//-------------------------------------------------
template <typename T> TSubclassOf<T> UBuildableCache::GetBuildableClass(EBuildable Type)
{
    if (CachedClasses.Contains(Type))
        return Cast<UClass>(CachedClasses[Type]);

    FString Path;
    if (!MachineClassPaths.Contains(Type))
        Path = FString::Printf(TEXT("/Game/FactoryGame/Buildable/Factory/%sMk1/Build_%sMk1.Build_%sMk1_C"),
                               *GetEnumName(Type), *GetEnumName(Type), *GetEnumName(Type));
    else
        Path = MachineClassPaths[Type];

    TSubclassOf<T> LoadedClass = LoadClassSoft<T>(Path, Type);
    if (LoadedClass)
        CachedClasses.Add(Type, LoadedClass);

    return LoadedClass;
}

void UBuildableCache::SetBeltClass(int32 Tier)
{
    FString Path = FString::Printf(
        TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorBeltMk%d/Build_ConveyorBeltMk%d.Build_ConveyorBeltMk%d_C"),
        Tier, Tier, Tier);
    TSubclassOf<AFGBuildableConveyorBelt> LoadedClass = LoadClassSoft<AFGBuildableConveyorBelt>(Path, EBuildable::Belt);
    if (LoadedClass)
        CachedClasses.Add(EBuildable::Belt, LoadedClass);
}

void UBuildableCache::SetLiftClass(int32 Tier)
{
    FString Path = FString::Printf(
        TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorLiftMk%d/Build_ConveyorLiftMk%d.Build_ConveyorLiftMk%d_C"),
        Tier, Tier, Tier);
    TSubclassOf<AFGBuildableConveyorLift> LoadedClass = LoadClassSoft<AFGBuildableConveyorLift>(Path, EBuildable::Lift);
    if (LoadedClass)
        CachedClasses.Add(EBuildable::Lift, LoadedClass);
}

int32 UBuildableCache::GetHighestUnlockedBeltTier(UWorld* World)
{
    AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);

    // Check belt tiers from 6 down to 1
    for (int32 Tier = 6; Tier >= 1; --Tier)
    {
        FString RecipePath = FString::Printf(
            TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_ConveyorBeltMk%d.Recipe_ConveyorBeltMk%d_C"), Tier, Tier);

        TSoftClassPtr<UFGRecipe> SoftClass(RecipePath);
        TSubclassOf<UFGRecipe> RecipeClass = SoftClass.LoadSynchronous();

        if (RecipeClass && RecipeManager->IsRecipeAvailable(RecipeClass))
        {
            return Tier;
        }
    }

    // Default to Mk1 if nothing found
    return 1;
}

void UBuildableCache::SetPipelineClass(int32 Tier)
{
    EBuildable PipeType = (Tier == 2) ? EBuildable::Pipeline2 : EBuildable::Pipeline;
    FString Path = (Tier == 2)
                       ? TEXT("/Game/FactoryGame/Buildable/Factory/PipelineMk2/Build_PipelineMK2.Build_PipelineMK2_C")
                       : TEXT("/Game/FactoryGame/Buildable/Factory/Pipeline/Build_Pipeline.Build_Pipeline_C");

    TSubclassOf<AFGBuildablePipeline> LoadedClass = LoadClassSoft<AFGBuildablePipeline>(Path, PipeType);
    if (LoadedClass)
        CachedClasses.Add(EBuildable::Pipeline, LoadedClass);
}

int32 UBuildableCache::GetHighestUnlockedPipelineTier(UWorld* World)
{
    AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);

    // Check Mk2 first
    FString RecipePath = TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_PipelineMk2.Recipe_PipelineMk2_C");
    TSoftClassPtr<UFGRecipe> SoftClass(RecipePath);
    TSubclassOf<UFGRecipe> RecipeClass = SoftClass.LoadSynchronous();

    if (RecipeClass && RecipeManager->IsRecipeAvailable(RecipeClass))
    {
        return 2;
    }

    // Default to Mk1
    return 1;
}

//-------------------------------------------------
// Recipe loader
//-------------------------------------------------
TSubclassOf<UFGRecipe> UBuildableCache::GetRecipeClass(const FString& Recipe,
                                                       TSubclassOf<AFGBuildableManufacturer> ProducedIn, UWorld* World)
{
    if (CachedRecipeClasses.Contains(Recipe))
        return CachedRecipeClasses[Recipe];

    FString ProducedInName = ProducedIn->GetName();
    if (WrongRecipes.ContainsByPredicate([&](const FWrongRecipe& Item)
                                         { return Item.Name == Recipe && Item.ProducedIn == ProducedInName; }))
        return nullptr;

    TArray<TSubclassOf<UFGRecipe>> AvailableRecipes;
    AFGRecipeManager::Get(World)->GetAvailableRecipesForProducer(ProducedIn, AvailableRecipes);

    FString FormattedName = FString::Printf(TEXT("Recipe_%s_C"), *Recipe);
    TSubclassOf<UFGRecipe>* FoundRecipe = AvailableRecipes.FindByPredicate([&](const TSubclassOf<UFGRecipe>& R)
                                                                           { return R->GetName() == FormattedName; });

    if (!FoundRecipe)
    {
        WrongRecipes.Add({Recipe, ProducedInName});

        TArray<FString> Names;
        for (const auto& R : AvailableRecipes)
        {
            FString N = R->GetName();
            N.RemoveFromStart(TEXT("Recipe_"));
            N.RemoveFromEnd(TEXT("_C"));
            Names.Add(N);
        }

        FString Msg = Names.IsEmpty() ? FString::Printf(TEXT("Machine %s not unlocked yet!"), *ProducedInName)
                                      : FString::Printf(TEXT("Available recipes for %s: %s"), *ProducedInName,
                                                        *FString::Join(Names, TEXT(", ")));

        FFactorySpawnerModule::ChatLog(World, FString::Printf(TEXT("Recipe '%s' not found. %s"), *Recipe, *Msg));
        return nullptr;
    }

    CachedRecipeClasses.Add(Recipe, *FoundRecipe);
    return *FoundRecipe;
}

void UBuildableCache::ClearCache()
{
    CachedClasses.Empty();
    CachedRecipeClasses.Empty();
    WrongRecipes.Empty();
    UE_LOG(LogFactorySpawner, Log, TEXT("Cache cleared"));
}