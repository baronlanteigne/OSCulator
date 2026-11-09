// Copyright Baron Lanteigne. All Rights Reserved.

#include "OSCulatorEditor.h"

#include "OscuMIDIMap.h"
#include "OscuMIDIMapDetails.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "OSCulatorEditor"

void FOSCulatorEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomClassLayout(
		UOscuMIDIMap::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FOscuMIDIMapDetails::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FOSCulatorEditorModule::ShutdownModule()
{
	// Guarded: on editor shutdown PropertyEditor may already be gone, and loading it
	// back just to unregister would resurrect a module mid-teardown.
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.UnregisterCustomClassLayout(UOscuMIDIMap::StaticClass()->GetFName());
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FOSCulatorEditorModule, OSCulatorEditor)
