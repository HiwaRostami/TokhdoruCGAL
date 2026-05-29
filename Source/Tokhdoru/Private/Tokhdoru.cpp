// Copyright NazruGeo. All Rights Reserved.

#include "Tokhdoru.h"

void FTokhdoruModule::StartupModule()
{
    UE_LOG(LogTemp, Log, TEXT("Tokhdoru Module Started"));
}

void FTokhdoruModule::ShutdownModule()
{
    UE_LOG(LogTemp, Log, TEXT("Tokhdoru Module Shut Down"));
}

IMPLEMENT_MODULE(FTokhdoruModule, Tokhdoru)
