#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogFactorySpawner, Verbose, All);

class FFactorySpawnerModule : public IModuleInterface
{
  public:
    // Logging function, so that the user sees it in the chat window
    static void ChatLog(UWorld* World, const FString& Message);
};
