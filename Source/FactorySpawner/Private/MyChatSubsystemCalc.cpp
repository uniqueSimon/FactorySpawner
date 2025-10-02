#include "MyChatSubsystem.h"
#include "FactorySpawner.h"
#include "FGRecipe.h"
#include "BuildableCache.h"

namespace
{
	struct FBeltConnector {
		FGuid UnitId;
		int32 SocketNumber;
	};

	struct FConnectionQueue
	{
		TQueue<FBeltConnector> Input;
		TQueue<FBeltConnector> Output;
	};

	struct FCachedPowerConnections
	{
		FGuid LastMachine;
		FGuid LastPole;
		FGuid FirstPole;
	};

	struct FConnector {
		int32 Index;
		int32 LocationX;
	};
	struct FMachineConfig {
		int32 Width; int32 LengthFront; int32 LengthBehind; int32 PowerPoleY; TArray<FConnector> Input; TArray<FConnector> Output;
	};

	TMap<EMachineType, TArray<FMachineConfig>> MachineConfigList = {
		{EMachineType::Constructor, {
			FMachineConfig(800, 500 + 400,500 + 400,-500,{FConnector(1,0)},{FConnector(0,0)})
	}},
		{EMachineType::Smelter,     {
			FMachineConfig(500, 500 + 400,400 + 400,-500,{FConnector(0,0)},{FConnector(1,0)})
	}},
		{EMachineType::Foundry,     {
			FMachineConfig(1000, 700 + 400,400 + 400,-500,{FConnector(2,-200),FConnector(0,200)},{FConnector(1,-200)})
	}},
		{EMachineType::Assembler,     {
			FMachineConfig(900, 1000 + 400,700 + 400,-900,{FConnector(1,-200),FConnector(2,200)},{FConnector(0,0)})
	}}, //width half step!
		{EMachineType::Manufacturer,     {
			FMachineConfig(1800, 1900 + 400,900 + 400,-1100,{FConnector(4,-600), FConnector(2,-200),FConnector(1,200),FConnector(0,600)},{FConnector(3,0)}),
			FMachineConfig(1800, 1600 + 400,900 + 400,-1100,{FConnector(4,-600), FConnector(2,-200),FConnector(1,200)},{FConnector(3,0)})
		}}
	};

	void CalculateMachineSetup(FBuildPlan& BuildPlan, EBuildable MachineType, TOptional<FString>& Recipe, float Underclock, int32 XCursor, int32 YCursor, FMachineConfig& MachineConfig, FConnectionQueue& ConnectionQueue, FCachedPowerConnections& CachedPowerConnections, bool bFirstUnitInRow, bool bEvenIndex, bool bLastIndex)
	{
		FGuid MachineId = FGuid::NewGuid();
		FVector MachineLocation = FVector(XCursor, YCursor, 0);

		if (bFirstUnitInRow && Underclock > 0) {
			BuildPlan.BuildableUnits.Add({ MachineId ,MachineType, MachineLocation, Recipe, Underclock });
		}
		else {
			BuildPlan.BuildableUnits.Add({ MachineId ,MachineType, MachineLocation, Recipe });
		}

		if (!bEvenIndex) {
			if (bLastIndex) {
				// prev Power pole -> Machine
				BuildPlan.WireConnections.Add({ CachedPowerConnections.LastPole,MachineId });
			}
			else {
				CachedPowerConnections.LastMachine = MachineId;
			}
		}
		else
		{
			// Power pole
			FGuid PowerPoleId = FGuid::NewGuid();
			FVector PowerPoleLocation = FVector(-MachineConfig.Width / 2, -MachineConfig.LengthFront / 2, 0);
			BuildPlan.BuildableUnits.Add({ PowerPoleId ,EBuildable::PowerPole,MachineLocation + PowerPoleLocation });

			// Power pole -> Machine
			BuildPlan.WireConnections.Add({ PowerPoleId,MachineId });

			if (bFirstUnitInRow)
			{
				if (CachedPowerConnections.FirstPole.IsValid())
				{
					// Power pole -> power pole from last row
					BuildPlan.WireConnections.Add({ PowerPoleId,CachedPowerConnections.FirstPole });
				}
				CachedPowerConnections.FirstPole = PowerPoleId;
			}
			else
			{
				// Power pole -> Last Machine
				BuildPlan.WireConnections.Add({ PowerPoleId,CachedPowerConnections.LastMachine });

				// Power pole -> last power pole
				BuildPlan.WireConnections.Add({ PowerPoleId,CachedPowerConnections.LastPole });
			}
			CachedPowerConnections.LastPole = PowerPoleId;
		}

		int32 Height = 0;
		for (FConnector Connector : MachineConfig.Input)
		{
			FGuid SplitterId = FGuid::NewGuid();
			FVector SplitterLocation = MachineLocation + FVector(Connector.LocationX, -MachineConfig.LengthFront + 200, 100 + Height);
			BuildPlan.BuildableUnits.Add({ SplitterId,EBuildable::Splitter, SplitterLocation });

			FBeltConnector ConnPrevUnit;
			if (!bFirstUnitInRow) {
				ConnectionQueue.Input.Dequeue(ConnPrevUnit);
				BuildPlan.BeltConnections.Add({ ConnPrevUnit.UnitId, ConnPrevUnit.SocketNumber,SplitterId,1 });
			}
			ConnectionQueue.Input.Enqueue({ SplitterId,0 });

			BuildPlan.BeltConnections.Add({ SplitterId, 3,MachineId ,Connector.Index });

			Height += 200;
		}

		for (FConnector Connector : MachineConfig.Output)
		{
			FGuid MergerId = FGuid::NewGuid();
			FVector MergerLocation = MachineLocation + FVector(Connector.LocationX, MachineConfig.LengthBehind - 200, 100);
			BuildPlan.BuildableUnits.Add({ MergerId,EBuildable::Merger, MergerLocation });

			FBeltConnector ConnPrevUnit;
			if (!bFirstUnitInRow) {
				ConnectionQueue.Output.Dequeue(ConnPrevUnit);
				BuildPlan.BeltConnections.Add({ MergerId,1,ConnPrevUnit.UnitId, ConnPrevUnit.SocketNumber });
			}
			ConnectionQueue.Output.Enqueue({ MergerId,0 });

			BuildPlan.BeltConnections.Add({ MachineId ,Connector.Index,MergerId,2 });
		}
	}
}

FBuildPlan AMyChatSubsystem::CalculateClusterSetup(UWorld* World, TArray< FClusterConfig>& ClusterConfig)
{
	FBuildPlan BuildPlan;
	int32 YCursor = 0;
	int32 XCursor = 0;
	int32 FirstMachineWidth;
	FCachedPowerConnections CachedPowerConnections;

	for (int32 RowIndex = 0; RowIndex < ClusterConfig.Num(); ++RowIndex)
	{
		FClusterConfig RowConfig = ClusterConfig[RowIndex];
		EMachineType MachineType = RowConfig.MachineType;
		TOptional<FString>Recipe = RowConfig.RecipeName;
		int32 Count = FMath::CeilToInt(RowConfig.Count);
		float Underclock = Count - RowConfig.Count;

		EBuildable MachineBuildable = static_cast<EBuildable>(MachineType);
		UClass* BaseClass = UBuildableCache::GetBuildableClass(MachineBuildable);
		TSubclassOf<AFGBuildableManufacturer> MachineClass = BaseClass;
		int32 Variant = 0;
		if (Recipe.IsSet()) {
			TSubclassOf<UFGRecipe> RecipeClass = UBuildableCache::GetRecipeClass(Recipe.GetValue(), MachineClass, World);
			if (RecipeClass) {
				UFGRecipe* RecipeCDO = RecipeClass->GetDefaultObject<UFGRecipe>();
				TArray<FItemAmount> Items = RecipeCDO->GetIngredients();
				int32 InputPorts = Items.Num();
				if (MachineType == EMachineType::Manufacturer && InputPorts == 3) {
					Variant = 1;
				}
			}
		}
		FMachineConfig MachineConfig = MachineConfigList[MachineType][Variant];

		if (RowIndex == 0) {
			FirstMachineWidth = MachineConfig.Width;
		}
		else {
			YCursor += MachineConfig.LengthFront;
			XCursor = FMath::CeilToInt((MachineConfig.Width - FirstMachineWidth) / 2.0f / 100) * 100;
		}
		FConnectionQueue ConnectionQueue;
		for (int32 i = 0; i < RowConfig.Count; ++i)
		{
			CalculateMachineSetup(BuildPlan, MachineBuildable, Recipe, Underclock, XCursor, YCursor, MachineConfig, ConnectionQueue, CachedPowerConnections, i == 0, i % 2 == 0, i == RowConfig.Count - 1);

			XCursor += MachineConfig.Width;


		}
		YCursor += MachineConfig.LengthBehind;

		//clean-up power connections from last machine in row
		CachedPowerConnections.LastMachine.Invalidate();
		CachedPowerConnections.LastPole.Invalidate();
	}

	return BuildPlan;
};

