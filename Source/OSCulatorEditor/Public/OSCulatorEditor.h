// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor-only UI for OSCulator's assets.
 *
 * A module of its own rather than more WITH_EDITOR blocks in OSCulatorMIDI, because
 * Slate and PropertyEditor are editor modules: linking them from a Runtime module is
 * what stops the plugin building for a packaged target. The runtime side keeps the data
 * and the behaviour; this side only draws it.
 *
 * Nothing here is required. Delete the module and the MIDI map still works through the
 * stock details panel -- which is why the asset keeps its Summary and Available
 * Parameters strings even though the table makes both redundant.
 */
class FOSCulatorEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
