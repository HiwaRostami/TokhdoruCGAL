// Copyright NazruGeo. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Main module class for the Tokhdoru plugin.
 * Handles startup and shutdown of the runtime module.
 * The TOKHDORU_API macro is automatically defined by the UE build system
 * based on the module name in Tokhdoru.Build.cs.
 */
class FTokhdoruModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
