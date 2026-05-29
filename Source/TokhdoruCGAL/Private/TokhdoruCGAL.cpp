// Copyright NazruGeo. All Rights Reserved.

#include "TokhdoruCGAL.h"

void FTokhdoruCGALModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("TokhdoruCGAL Module Started"));
}

void FTokhdoruCGALModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("TokhdoruCGAL Module Shut Down"));
}

IMPLEMENT_MODULE(FTokhdoruCGALModule, TokhdoruCGAL)
