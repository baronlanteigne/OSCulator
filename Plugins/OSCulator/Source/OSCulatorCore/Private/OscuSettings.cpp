// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuSettings.h"

UOscuSettings::FOscuSettingsChanged UOscuSettings::OnSettingsChanged;

const UOscuSettings* UOscuSettings::Get()
{
	return GetDefault<UOscuSettings>();
}

#if WITH_EDITOR
void UOscuSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnSettingsChanged.Broadcast();
}
#endif
