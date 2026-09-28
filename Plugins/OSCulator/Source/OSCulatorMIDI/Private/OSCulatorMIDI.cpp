// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDIPort.h"

#include "Modules/ModuleManager.h"

/**
 * Exists only to shut our copy of PortMidi down.
 *
 * This module runs its own PortMidi instance -- see OscuMIDIPort.h for why -- and it
 * is started lazily on first use rather than here, because a project with MIDI input
 * disabled should not be talking to the MIDI subsystem at all. Stopping it, though,
 * has to be unconditional and last: the engine subsystem closes its ports during
 * engine shutdown, and this runs after that.
 */
class FOSCulatorMIDIModule : public IModuleInterface
{
public:
	virtual void ShutdownModule() override
	{
		OscuMIDI::Shutdown();
	}
};

IMPLEMENT_MODULE(FOSCulatorMIDIModule, OSCulatorMIDI);
