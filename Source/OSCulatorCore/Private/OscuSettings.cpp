// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuSettings.h"
#include "OSCulatorCore.h"
#include "OscuEndpointFile.h"

#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

UOscuSettings::FOscuSettingsChanged UOscuSettings::OnSettingsChanged;

namespace
{
	/**
	 * The one real CDO. The Project Settings editor makes its own copy of it in the
	 * transient package, also flagged RF_ClassDefaultObject, when the page opens: that
	 * copy must neither load the file again (it would overwrite the last-load status)
	 * nor write it.
	 */
	bool IsTheClassDefault(const UOscuSettings& Settings)
	{
		return Settings.HasAnyFlags(RF_ClassDefaultObject)
			&& Settings.GetClass()->GetDefaultObject(/*bCreateIfNeeded*/ false) == &Settings;
	}
}

const UOscuSettings* UOscuSettings::Get()
{
	return GetDefault<UOscuSettings>();
}

void UOscuSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Config has already been loaded into the CDO by now; the file goes on top, and
	// before any transport has read a port.
	if (IsTheClassDefault(*this))
	{
		OscuEndpointFile::LoadAtStartup(*this);
	}
}

#if WITH_EDITOR
void UOscuSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnSettingsChanged.Broadcast();

	if (IsTheClassDefault(*this))
	{
		const FString Path = OscuEndpointFile::GetPath();
		if (!OscuEndpointFile::Save(*this, Path))
		{
			UE_LOG(LogOSCulator, Warning, TEXT("OSCulator: could not write %s. Is it read-only?"), *Path);
			FNotificationInfo Info(FText::Format(
				NSLOCTEXT("OSCulator", "EndpointSaveFailed", "OSCulator could not write {0}. Is it read-only?"),
				FText::FromString(Path)));
			Info.ExpireDuration = 8.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}
}
#endif
