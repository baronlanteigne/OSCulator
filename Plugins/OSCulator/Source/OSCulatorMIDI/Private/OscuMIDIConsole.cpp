// Copyright Baron Lanteigne. All Rights Reserved.

#include "OSCulatorCore.h"
#include "OscuMIDIMap.h"
#include "OscuMIDINoteName.h"
#include "OscuMIDIPort.h"
#include "OscuMIDISubsystem.h"
#include "OscuSettings.h"

#include "HAL/IConsoleManager.h"
#include "MIDIDeviceManager.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

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
		Ar.Log(TEXT("[OSCulator] MIDI devices. Copy a name verbatim into Project Settings > Plugins > OSCulator."));

		// Inputs are enumerated through OSCulator's own PortMidi instance, which is the
		// one that will actually be opening them. Asking the engine's instead would
		// list the same hardware while saying nothing about what we can do with it.
		TArray<FOscuMIDIDeviceInfo> Devices;
		OscuMIDI::EnumerateDevices(Devices);

		int32 InputCount = 0;
		for (const FOscuMIDIDeviceInfo& Device : Devices)
		{
			InputCount += Device.bInput ? 1 : 0;
		}

		Ar.Log(*FString::Printf(TEXT("  Inputs (%d):"), InputCount));
		if (InputCount == 0)
		{
			Ar.Log(TEXT("    (none)"));
		}
		for (const FOscuMIDIDeviceInfo& Device : Devices)
		{
			if (Device.bInput)
			{
				Ar.Log(*FString::Printf(TEXT("    \"%s\"%s"), *Device.Name,
					Device.bOpenedByUs ? TEXT("   [open by OSCulator]") : TEXT("")));
			}
		}

		// Output still runs on the engine's MIDIDevice plugin, so its device list comes
		// from there. Note that its "already in use" flag only ever sees this process.
		TArray<FMIDIDeviceInfo> EngineInputs;
		TArray<FMIDIDeviceInfo> EngineOutputs;
		UMIDIDeviceManager::FindAllMIDIDeviceInfo(EngineInputs, EngineOutputs);

		Ar.Log(*FString::Printf(TEXT("  Outputs (%d):"), EngineOutputs.Num()));
		if (EngineOutputs.Num() == 0)
		{
			Ar.Log(TEXT("    (none)"));
		}
		for (const FMIDIDeviceInfo& Device : EngineOutputs)
		{
			Ar.Log(*FString::Printf(TEXT("    \"%s\"%s"), *Device.DeviceName,
				Device.bIsAlreadyInUse ? TEXT("   [already in use]") : TEXT("")));
		}
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

		if (Settings.bPerformanceMode)
		{
			// Said first, and only when it is on. Learn not arming is otherwise a
			// mystery, and this is the one line that explains it.
			Ar.Log(TEXT("[OSCulator] PERFORMANCE MODE is on: Learn is disabled, recompile tracking is off,"));
			Ar.Log(TEXT("  and parameter slots are resolved once instead of per message."));
		}

		Ar.Log(*FString::Printf(TEXT("[OSCulator] MIDI devices open: %d"), MIDI->GetOpenDeviceCount()));
		if (MIDI->GetOpenDeviceCount() == 0)
		{
			Ar.Log(TEXT("  Nothing is open. Run OSCulator.MIDIDevices to see what is available,"));
			Ar.Log(TEXT("  then OSCulator.MIDIRestart."));
		}

		// What each port is dropping, and what it has lost. An overflow count above zero
		// is the one number here that asks for action: notes were thrown away.
		for (const FOscuMIDIPort& Port : MIDI->GetPorts())
		{
			Ar.Log(*FString::Printf(TEXT("  '%s': queue %d, dropping %s, listening on %s"),
				*Port.Name, UOscuSettings::Get()->MIDIInputQueueSize,
				*OscuMIDI::DescribeFilterMask(Port.GetFilterMask()),
				*OscuMIDI::DescribeChannelMask(Port.GetChannelMask())));

			Ar.Log(*FString::Printf(TEXT("     unused messages %llu, queue overflows %llu%s"),
				Port.UnusedMessages, Port.Overflows,
				Port.Overflows > 0
					? TEXT("  <-- messages were lost; raise the queue size or narrow the filter")
					: TEXT("")));
		}

		const TArray<TObjectPtr<UOscuMIDIMap>>& Maps = MIDI->GetActiveMaps();
		if (Maps.Num() == 0)
		{
			Ar.Log(TEXT("  No map asset is set. MIDI will arrive and go nowhere."));
		}

		for (const TObjectPtr<UOscuMIDIMap>& Map : Maps)
		{
			if (Map == nullptr)
			{
				continue;
			}

			int32 Assigned = 0;
			for (const FOscuMIDIBinding& Binding : Map->Bindings)
			{
				Assigned += Binding.Sources.Num() > 0 ? 1 : 0;
			}

			// Unassigned bindings are counted separately rather than hidden. They are the
			// normal state after auto-map, and "20 bindings, 3 assigned" is the answer to
			// "why does nothing happen".
			Ar.Log(*FString::Printf(TEXT("  Map: %s%s (%d binding(s), %d assigned, %d unassigned)"),
				*Map->GetName(),
				Map->DefaultDevice.IsNone() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *Map->DefaultDevice.ToString()),
				Map->Bindings.Num(), Assigned, Map->Bindings.Num() - Assigned));

			for (const FOscuMIDIBinding& Binding : Map->Bindings)
			{
				Ar.Log(*FString::Printf(TEXT("    %s"), *Binding.Summary));
			}
		}

		Ar.Log(*FString::Printf(TEXT("  messages  received %llu, dispatched %llu, unmapped %llu"),
			MIDI->GetMessagesReceived(), MIDI->GetMessagesDispatched(), MIDI->GetMessagesUnmapped()));
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

namespace
{
	/**
	 * Every input claimed across every active map, so an overlap between two assets is
	 * visible at all.
	 *
	 * UOscuMIDIMap::Validate can only see its own bindings, which is fine for everything
	 * else it reports but blind to the one thing several live maps make possible: two
	 * assets claiming the same input. Sharing an input is a feature -- one pad driving
	 * two actors -- so this reports rather than warns. What it buys you is that an
	 * ACCIDENTAL overlap stops being invisible.
	 */
	void ValidateCommand(const TArray<FString>& Args, FOutputDevice& Ar)
	{
		UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
		if (MIDI == nullptr)
		{
			Ar.Log(TEXT("[OSCulator] No MIDI subsystem."));
			return;
		}

		const TArray<TObjectPtr<UOscuMIDIMap>>& Maps = MIDI->GetActiveMaps();
		if (Maps.Num() == 0)
		{
			Ar.Log(TEXT("[OSCulator] No map assets are loaded. Set MIDI Maps in Project Settings > Plugins > OSCulator."));
			return;
		}

		// Per-map first: broken targets, unclaimed functions, the things that need a
		// level to judge. The editor world is the right one to ask at edit time; a
		// running game has its own.
		UWorld* World = nullptr;
#if WITH_EDITOR
		if (GEditor != nullptr)
		{
			World = GEditor->GetEditorWorldContext().World();
		}
#endif
		if (World == nullptr)
		{
			World = GWorld;
		}

		if (World != nullptr)
		{
			for (const TObjectPtr<UOscuMIDIMap>& Map : Maps)
			{
				if (Map != nullptr)
				{
					const UOscuMIDIMap::FOscuMIDIValidation Result = Map->Validate(World);
					Ar.Log(*FString::Printf(TEXT("[OSCulator] %s: %d ok, %d broken, %d for another level, %d unassigned. See the log for detail."),
						*Map->GetName(), Result.Found, Result.FunctionMissing, Result.TagMissing, Result.Unassigned));

				if (Result.CouldDefaultValueParams > 0)
				{
					Ar.Log(*FString::Printf(
						TEXT("    %d binding(s) could have velocity/pitch pointed at a parameter. Auto-Map From Level fills them in."),
						Result.CouldDefaultValueParams));
				}
				}
			}
		}
		else
		{
			Ar.Log(TEXT("[OSCulator] No level open, so bindings could not be checked against actors. Overlaps below are still accurate."));
		}

		// Now the part no single asset can answer.
		struct FClaim
		{
			FString Asset;
			FString Target;
			FName Device;
		};

		TMap<uint32, TArray<FClaim>> ByInput;

		for (const TObjectPtr<UOscuMIDIMap>& Map : Maps)
		{
			if (Map == nullptr)
			{
				continue;
			}

			for (const FOscuMIDIBinding& Binding : Map->Bindings)
			{
				for (const FOscuMIDISource& Source : Binding.Sources)
				{
					const uint32 Key = static_cast<uint32>(Source.Channel) << 16
						| static_cast<uint32>(Source.Type) << 8
						| static_cast<uint32>(static_cast<uint8>(Source.GetNumber()));

					ByInput.FindOrAdd(Key).Add(FClaim{
						Map->GetName(),
						FString::Printf(TEXT("%s/%s"), *Binding.Tag.ToString(), *Binding.FunctionName.ToString()),
						Source.ResolveDevice(Map->DefaultDevice) });
				}
			}
		}

		int32 Reported = 0;

		for (const TPair<uint32, TArray<FClaim>>& Pair : ByInput)
		{
			if (Pair.Value.Num() < 2)
			{
				continue;
			}

			// Two claims on the same number only actually collide if their devices can
			// both match one message. Per-device maps deliberately reuse note numbers,
			// and reporting those as overlaps would make the report useless.
			bool bOverlaps = false;
			for (int32 i = 0; i < Pair.Value.Num() && !bOverlaps; ++i)
			{
				for (int32 j = i + 1; j < Pair.Value.Num() && !bOverlaps; ++j)
				{
					const FName A = Pair.Value[i].Device;
					const FName B = Pair.Value[j].Device;
					bOverlaps = A.IsNone() || B.IsNone() || A == B;
				}
			}

			if (!bOverlaps)
			{
				continue;
			}

			const uint8 Channel = static_cast<uint8>(Pair.Key >> 16);
			const EOscuMIDIInputType Kind = static_cast<EOscuMIDIInputType>((Pair.Key >> 8) & 0xFF);
			const TCHAR* KindLabel =
				Kind == EOscuMIDIInputType::Note ? TEXT("note")
				: (Kind == EOscuMIDIInputType::ProgramChange ? TEXT("program") : TEXT("CC"));

			if (Reported == 0)
			{
				Ar.Log(TEXT("[OSCulator] Inputs driving more than one binding (legal -- one pad, several actors -- but worth a look):"));
			}
			++Reported;

			Ar.Log(*FString::Printf(TEXT("  ch %d %s %d:"), Channel, KindLabel, Pair.Key & 0xFF));
			for (const FClaim& Claim : Pair.Value)
			{
				Ar.Log(*FString::Printf(TEXT("      %s   [%s%s]"), *Claim.Target, *Claim.Asset,
					Claim.Device.IsNone() ? TEXT(", any device") : *FString::Printf(TEXT(", %s"), *Claim.Device.ToString())));
			}
		}

		if (Reported == 0)
		{
			Ar.Log(TEXT("[OSCulator] No input drives more than one binding."));
		}
	}
}

static FAutoConsoleCommandWithArgsAndOutputDevice GOscuMIDIValidateCommand(
	TEXT("OSCulator.MIDIValidate"),
	TEXT("Checks every active map against the open level, and reports inputs claimed by more than one binding across all of them."),
	FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateStatic(&ValidateCommand));

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
