#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "FactorySpawner.h"
#include "MyChatSubsystem.generated.h"

UENUM(BlueprintType)
enum class EMachineType :uint8
{
	Smelter,
	Constructor,
	Assembler,
	Foundry,
	Manufacturer,
	Invalid,
};

UENUM(BlueprintType)
enum class EBuildable : uint8 {
	Smelter = static_cast<uint8>(EMachineType::Smelter),
	Constructor = static_cast<uint8>(EMachineType::Constructor),
	Assembler = static_cast<uint8>(EMachineType::Assembler),
	Foundry = static_cast<uint8>(EMachineType::Foundry),
	Manufacturer = static_cast<uint8>(EMachineType::Manufacturer),

	Splitter,
	Merger,
	PowerPole,

	Belt,
	Lift,
	PowerLine,
};

struct FClusterConfig
{
	int32 Count;
	EMachineType MachineType;
	TOptional<FString> RecipeName;
};

struct FBuildableUnit
{
	FGuid Id;
	EBuildable Buildable;
	FVector Location;
	TOptional<FString> Recipe;
};

struct FWireConnection {
	FGuid FromUnit;
	FGuid ToUnit;
};

struct FBeltConnection {
	FGuid FromUnit;
	int32 FromSocket;
	FGuid ToUnit;
	int32 ToSocket;
};
struct FBuildPlan
{
	TArray<FBuildableUnit> BuildableUnits;
	TArray<FBeltConnection> BeltConnections;
	TArray<FWireConnection> WireConnections;
};

UCLASS()
class FACTORYSPAWNER_API AMyChatSubsystem : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	AMyChatSubsystem();

	static FBuildPlan CurrentBuildPlan;

	EExecutionStatus ExecuteCommand_Implementation(class UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;

	void BeginPlay() override;

private:
	static FBuildPlan CalculateClusterSetup(UWorld* World, TArray< FClusterConfig>& ClusterConfig);
};
