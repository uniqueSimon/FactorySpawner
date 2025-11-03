#include "BuildableCache.h"
#include "FGRecipeManager.h"
#include "FGBuildableConveyorBelt.h"
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

    // Generic soft object loader (for meshes)
    template <typename T> T* LoadObjectSoft(const FString& Path, EBuildable Type)
    {
        TSoftObjectPtr<T> SoftObj(Path);
        T* Loaded = SoftObj.LoadSynchronous();
        if (!Loaded)
            UE_LOG(LogFactorySpawner, Warning, TEXT("Failed to load object for %d at path: %s"), (int32) Type, *Path);
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
        {EBuildable::Belt,
         "/Game/FactoryGame/Buildable/Factory/ConveyorBeltMk1/Build_ConveyorBeltMk1.Build_ConveyorBeltMk1_C"},
        {EBuildable::Lift,
         "/Game/FactoryGame/Buildable/Factory/ConveyorLiftMk1/Build_ConveyorLiftMk1.Build_ConveyorLiftMk1_C"}};

    // Mesh paths table
    TMap<EBuildable, TArray<FString>> MeshPaths = {
        {EBuildable::Smelter,
         {"/Game/FactoryGame/Buildable/Factory/SmelterMk1/Mesh/SmelterMk1_static.SmelterMk1_static",
          "/Game/FactoryGame/Buildable/Factory/SmelterMk1/Mesh/SM_VAT_Smelter_01.SM_VAT_Smelter_01"}},
        {EBuildable::Constructor,
         {"/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Mesh/ConstructorMk1_static.ConstructorMk1_static",
          "/Game/FactoryGame/Buildable/Factory/ConstructorMk1/Mesh/SM_VAT_Constructor_MK1.SM_VAT_Constructor_MK1"}},
        {EBuildable::Assembler,
         {"/Game/FactoryGame/Buildable/Factory/AssemblerMk1/Mesh/AssemblerMk1_static.AssemblerMk1_static",
          "/Game/FactoryGame/Buildable/Factory/AssemblerMk1/Mesh/SM_Assembler_VAT.SM_Assembler_VAT"}},
        {EBuildable::Foundry,
         {"/Game/FactoryGame/Buildable/Factory/FoundryMk1/Mesh/FoundryMk1_static.FoundryMk1_static",
          "/Game/FactoryGame/Buildable/Factory/FoundryMk1/Mesh/SM_VAT_Foundry.SM_VAT_Foundry"}},
        {EBuildable::Manufacturer,
         {"/Game/FactoryGame/Buildable/Factory/ManufacturerMk1/Mesh/SM_Manufacturer.SM_Manufacturer",
          "/Game/FactoryGame/Buildable/Factory/ManufacturerMk1/Mesh/SM_VAT_Manufacturer.SM_VAT_Manufacturer"}},
        {EBuildable::Splitter,
         {"/Game/FactoryGame/Buildable/Factory/CA_Splitter/Mesh/"
          "ConveyorAttachmentSplitter_static.ConveyorAttachmentSplitter_static",
          "/Game/FactoryGame/Buildable/Factory/CA_Splitter/Mesh/SM_Splitter_01.SM_Splitter_01"}},
        {EBuildable::Merger,
         {"/Game/FactoryGame/Buildable/Factory/CA_Merger/Mesh/"
          "ConveyorAttachmentMerger_static.ConveyorAttachmentMerger_static",
          "/Game/FactoryGame/Buildable/Factory/CA_Merger/Mesh/SM_Merger_01.SM_Merger_01"}},
        {EBuildable::PowerPole,
         {"/Game/FactoryGame/Buildable/Factory/PowerPoleMk1/Mesh/SM_PowerPole_Mk1.SM_PowerPole_Mk1"}}};

    FString FormatRecipeName(const FString& Recipe)
    {
        return FString::Printf(TEXT("Recipe_%s_C"), *Recipe);
    }
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

//-------------------------------------------------
// Recipe loader
//-------------------------------------------------
TSubclassOf<UFGRecipe> UBuildableCache::GetRecipeClass(const FString& Recipe,
                                                       TSubclassOf<AFGBuildableManufacturer> ProducedIn, UWorld* World)
{
    if (CachedRecipeClasses.Contains(Recipe))
        return CachedRecipeClasses[Recipe];

    if (WrongRecipes.ContainsByPredicate([&](const FWrongRecipe& Item)
                                         { return Item.Name == Recipe && Item.ProducedIn == ProducedIn->GetName(); }))
        return nullptr;

    AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World);
    TArray<TSubclassOf<UFGRecipe>> AvailableRecipes;
    RecipeManager->GetAvailableRecipesForProducer(ProducedIn, AvailableRecipes);

    TSubclassOf<UFGRecipe>* FoundRecipe = AvailableRecipes.FindByPredicate(
        [&](const TSubclassOf<UFGRecipe>& RecipeClass) { return RecipeClass->GetName() == FormatRecipeName(Recipe); });

    if (!FoundRecipe)
    {
        WrongRecipes.Add({Recipe, ProducedIn->GetName()});

        TArray<FString> Names;
        for (auto& R : AvailableRecipes)
        {
            FString N = R->GetName();
            N.RemoveFromStart(TEXT("Recipe_"));
            N.RemoveFromEnd(TEXT("_C"));
            Names.Add(N);
        }

        FString Msg = Names.Num() == 0
                          ? FString::Printf(TEXT("The machine %s has not been unlocked yet!"), *ProducedIn->GetName())
                          : FString::Printf(TEXT("Following recipes are available for machine %s: %s"),
                                            *ProducedIn->GetName(), *FString::Join(Names, TEXT(", ")));

        FFactorySpawnerModule::ChatLog(World, FString::Printf(TEXT("Recipe %s not found for machine %s. %s"), *Recipe,
                                                              *ProducedIn->GetName(), *Msg));
        return nullptr;
    }

    CachedRecipeClasses.Add(Recipe, *FoundRecipe);
    return *FoundRecipe;
}

//-------------------------------------------------
// Mesh loader
//-------------------------------------------------
TArray<UStaticMesh*> UBuildableCache::GetStaticMesh(EBuildable Type)
{
    if (CachedMeshes.Contains(Type))
        return CachedMeshes[Type];

    if (!MeshPaths.Contains(Type))
    {
        UE_LOG(LogFactorySpawner, Warning, TEXT("No mesh paths defined for buildable type %d"), (int32) Type);
        return {};
    }

    TArray<UStaticMesh*> LoadedMeshes;
    for (const FString& Path : MeshPaths[Type])
    {
        if (UStaticMesh* Mesh = LoadObjectSoft<UStaticMesh>(Path, Type))
            LoadedMeshes.Add(Mesh);
    }

    CachedMeshes.Add(Type, LoadedMeshes);
    return LoadedMeshes;
}

void UBuildableCache::ClearCache()
{
    CachedClasses.Empty();
    CachedRecipeClasses.Empty();
    CachedMeshes.Empty();
    WrongRecipes.Empty();

    UE_LOG(LogFactorySpawner, Log, TEXT("[BuildableCache] Cleared all cached data."));
}