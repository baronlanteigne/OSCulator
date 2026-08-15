// Copyright Baron Lanteigne. All Rights Reserved.

#include "OSCulatorCore.h"
#include "OscuMIDIMap.h"
#include "OscuMIDINoteName.h"
#include "OscuMIDISubsystem.h"
#include "OscuSettings.h"

#include "HAL/IConsoleManager.h"
#include "MIDIDeviceManager.h"

namespace
{
	/**
	 * Answers "what is my controller actually called?".
	 *
	 * Device names have to be typed into settings exactly, and there is no picker
	 * for them, so without this the only way to find out is to guess wrong and read
	 * the failure.
	 */
	void DevicesCommand(const TArray<FString>& Args, FOutputDevice& Ar)
	{
		TArray<FMIDIDeviceInfo> InputDevices;
		TArray<FMIDIDeviceInfo> OutputDevices;
		UMIDIDeviceManager::FindAllMIDIDeviceInfo(InputDevices, OutputDevices);

		auto Report = [&Ar](const TCHAR* Heading, const TArray<FMIDIDeviceInfo>& Devices)
		{
			Ar.Log(*FString::Printf(TEXT("  %s (%d):"), Heading, Devices.Num()));
			if (Devices.Num() == 0)
			{
				Ar.Log(TEXT("    (none)"));
				return;
			}
			for (const FMIDIDeviceInfo& Device : Devices)
			{
				Ar.Log(*FString::Printf(TEXT("    \"%s\"%s"),
					*Device.DeviceName,
					Device.bIsAlreadyInUse ? TEXT("   [already in use by another application]") : TEXT("")));
			}
		};

		Ar.Log(TEXT("[OSCulator] MIDI devices. Copy a name verbatim into Project Settings > Plugins > OSCulator."));
		Report(TEXT("Inputs"), InputDevices);
		Report(TEXT("Outputs"), OutputDevices);
	}

	void StatusCommand(const TArray<FString>& Args, FOutputDevice& Ar)
	{
		UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
		if (MIDI == nullptr)
		{
			Ar.Log(TEXT("[OSCulator] No MIDI subsystem."));
			return;
		}

		const UOscuSettings& Settings = *UOscuSettings::Get();

		if (!Settings.bEnableMIDIIn)
		{
			Ar.Log(TEXT("[OSCulator] MIDI input is disabled in Project Settings > Plugins > OSCulator."));
			return;
		}

		Ar.Log(*FString::Printf(TEXT("[OSCulator] MIDI devices open: %d"), MIDI->GetOpenDeviceCount()));
		if (MIDI->GetOpenDeviceCount() == 0)
		{
			Ar.Log(TEXT("  Nothing is open. Run OSCulator.MIDIDevices to see what is available,"));
			Ar.Log(TEXT("  then restart the editor -- devices are opened once at engine start."));
		}

		const UOscuMIDIMap* Map = MIDI->GetActiveMap();
		if (Map == nullptr)
		{
			Ar.Log(TEXT("  No map asset is set. Notes will arrive and go nowhere."));
		}
		else
		{
			int32 RowCount = 0;
			for (const FOscuMIDIChannelMap& Channel : Map->Channels)
			{
				RowCount += Channel.Notes.Num();
			}
			Ar.Log(*FString::Printf(TEXT("  Map: %s (%d channel(s), %d mapping(s))"),
				*Map->GetName(), Map->Channels.Num(), RowCount));

			for (const FOscuMIDIChannelMap& Channel : Map->Channels)
			{
				Ar.Log(*FString::Printf(TEXT("    channel %d -> /%s"), Channel.Channel, *Channel.Tag.ToString()));
				for (const FOscuMIDINoteMap& Note : Channel.Notes)
				{
					Ar.Log(*FString::Printf(TEXT("      %-6s (%3d) -> %s"),
						*Note.Note, Note.ResolvedNote, *Note.FunctionName.ToString()));
				}
			}
		}

		Ar.Log(*FString::Printf(TEXT("  notes  received %llu, dispatched %llu, unmapped %llu"),
			MIDI->GetNotesReceived(), MIDI->GetNotesDispatched(), MIDI->GetNotesUnmapped()));
	}
}

namespace
{
	void RestartCommand(const TArray<FString>& Args, FOutputDevice& Ar)
	{
		UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
		if (MIDI == nullptr)
		{
			Ar.Log(TEXT("[OSCulator] No MIDI subsystem."));
			return;
		}

		MIDI->Restart();
		Ar.Log(*FString::Printf(TEXT("[OSCulator] Reopened MIDI. %d device(s) now open -- see the log for details."),
			MIDI->GetOpenDeviceCount()));
	}
}

static FAutoConsoleCommandWithArgsAndOutputDevice GOscuMIDIRestartCommand(
	TEXT("OSCulator.MIDIRestart"),
	TEXT("Closes and reopens the MIDI devices, re-reading settings. Use after changing device names."),
	FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&RestartCommand));

static FAutoConsoleCommandWithArgsAndOutputDevice GOscuMIDIDevicesCommand(
	TEXT("OSCulator.MIDIDevices"),
	TEXT("Lists the MIDI devices this machine can see, with their exact names."),
	FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&DevicesCommand));

static FAutoConsoleCommandWithArgsAndOutputDevice GOscuMIDIStatusCommand(
	TEXT("OSCulator.MIDIStatus"),
	TEXT("Reports which MIDI devices are open, which map is active, and the note counters."),
	FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&StatusCommand));
