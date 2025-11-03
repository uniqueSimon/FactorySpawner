#include "FactorySpawner.h"
#include "FGChatManager.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "FFactorySpawnerModule"

DEFINE_LOG_CATEGORY(LogFactorySpawner);

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