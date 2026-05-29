// Copyright NazruGeo. All Rights Reserved.

#include "TokhdoruActorCustomization.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"

TSharedRef<IDetailCustomization> FTokhdoruActorCustomization::MakeInstance()
{
	return MakeShareable(new FTokhdoruActorCustomization);
}

void FTokhdoruActorCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Future customization: reorder categories, hide properties, etc.
}
