// Copyright Epic Games, Inc. All Rights Reserved.

#include "FactorySpawner.h"
#include "FGChatManager.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "FFactorySpawnerModule"

DEFINE_LOG_CATEGORY(LogFactorySpawner);

void FFactorySpawnerModule::StartupModule()
{
    // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin
    // file per-module
}

void FFactorySpawnerModule::ShutdownModule()
{
    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.
}

void FFactorySpawnerModule::ChatLog(UWorld* World, const FString& Message)
{
    AFGChatManager* ChatMgr = AFGChatManager::Get(World);
    FChatMessageStruct NewMessage;
    NewMessage.MessageText = FText::FromString(Message);
    NewMessage.MessageType = EFGChatMessageType::CMT_SystemMessage;
    NewMessage.MessageSender = FText::FromString(TEXT("System"));
    NewMessage.MessageSenderColor = FLinearColor::Blue;
    NewMessage.ServerTimeStamp = World->GetTimeSeconds();
    NewMessage.bIsLocalPlayerMessage = true;

    ChatMgr->BroadcastChatMessage(NewMessage);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFactorySpawnerModule, FactorySpawner)