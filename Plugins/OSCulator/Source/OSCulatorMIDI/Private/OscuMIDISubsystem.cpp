// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDISubsystem.h"

#include "OSCulatorCore.h"
#include "OscuMIDINoteName.h"
#include "OscuMIDIPort.h"
#include "OscuMarshal.h"
#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"
#include "OscuValue.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MIDIDeviceManager.h"
#include "MIDIDeviceOutputController.h"

#if WITH_EDITOR
#include "Editor.h"
#endif
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"

void UOscuMIDISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Devices are opened once the engine is fully up rather than here. At subsystem
	// initialisation the MIDIDevice module may not have finished starting, and the
	// settings object has not necessarily loaded its config yet.
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddUObject(this, &UOscuMIDISubsystem::Restart);

	// ...unless the engine is already up, in which case that delegate has fired and
	// will never fire again. This subsystem can be created late -- on first access
	// after a hot reload, for instance -- and waiting on a past event would mean
	// silently never opening anything.
	if (GIsRunning)
	{
		Restart();
	}

	// Editing a device name or the map should take effect at once. Needing an editor
	// restart to test a settings change turns every wrong guess into a two-minute
	// round trip.
	SettingsChangedHandle = UOscuSettings::OnSettingsChanged.AddUObject(this, &UOscuMIDISubsystem::Restart);

	// Engine-level rather than a world timer, so a note started in PIE is still
	// released if play stops before its duration elapses.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UOscuMIDISubsystem::Tick));

#if WITH_EDITOR
	// Stopping play should silence the rig immediately rather than leaving notes
	// hanging until their durations run out.
	FEditorDelegates::EndPIE.AddLambda([this](const bool) { FlushPendingNoteOffs(); });
#endif
}

void UOscuMIDISubsystem::Deinitialize()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	if (SettingsChangedHandle.IsValid())
	{
		UOscuSettings::OnSettingsChanged.Remove(SettingsChangedHandle);
		SettingsChangedHandle.Reset();
	}

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// Release everything before the devices close, or the hardware keeps sounding
	// with nothing left to tell it otherwise.
	FlushPendingNoteOffs();

	CloseDevices();
	CloseOutputs();

	Super::Deinitialize();
}

UOscuMIDISubsystem* UOscuMIDISubsystem::Get()
{
	return GEngine != nullptr ? GEngine->GetEngineSubsystem<UOscuMIDISubsystem>() : nullptr;
}

void UOscuMIDISubsystem::SetActiveMap(UOscuMIDIMap* Map)
{
	ActiveMaps.Reset();
	if (Map != nullptr)
	{
		ActiveMaps.Add(Map);
	}
}

void UOscuMIDISubsystem::Restart()
{
	// Release anything sounding before the devices go away underneath it.
	FlushPendingNoteOffs();

	// A settings change comes through here, and Performance Mode is a setting -- so
	// flipping it, either way, cannot leave a remembered slot behind. Also covers the
	// case where a map asset was repointed at a different tag or function.
	ParamSlotCache.Reset();
	WarnedUnresolvedParams.Reset();

	CloseDevices();
	CloseOutputs();

	OpenDevices();

	const UOscuSettings& Settings = *UOscuSettings::Get();

	// Only the output side still goes through the engine's device manager, and only
	// output pays for its enumeration -- which terminates and restarts the engine's
	// PortMidi copy every time it is called. Our input ports are a separate instance
	// and are not disturbed by it, which is the whole reason they are a separate
	// instance.
	TArray<FMIDIDeviceInfo> OutputDevices;
	if (Settings.bEnableMIDIOut && !IsRunningCommandlet() && !FApp::IsUnattended())
	{
		TArray<FMIDIDeviceInfo> InputDevices;
		UMIDIDeviceManager::FindAllMIDIDeviceInfo(InputDevices, OutputDevices);
	}

	OpenOutputs(OutputDevices);
}

int32 UOscuMIDISubsystem::ToWireChannel(int32 Channel)
{
	return FMath::Clamp(Channel, 1, 16) - 1;
}

void UOscuMIDISubsystem::OpenOutputs(const TArray<FMIDIDeviceInfo>& OutputDevices)
{
	const UOscuSettings& Settings = *UOscuSettings::Get();
	if (!Settings.bEnableMIDIOut)
	{
		return;
	}

	if (IsRunningCommandlet() || FApp::IsUnattended())
	{
		return;
	}

	if (Settings.MIDIOutputDeviceNames.Num() == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("MIDI output is enabled but no device names are listed in Project Settings > Plugins > OSCulator."));
		return;
	}

	for (const FString& Wanted : Settings.MIDIOutputDeviceNames)
	{
		const FMIDIDeviceInfo* Found = OutputDevices.FindByPredicate(
			[&Wanted](const FMIDIDeviceInfo& Info) { return Info.DeviceName.Equals(Wanted, ESearchCase::IgnoreCase); });

		if (Found == nullptr)
		{
			TArray<FString> Available;
			for (const FMIDIDeviceInfo& Info : OutputDevices)
			{
				Available.Add(Info.DeviceName);
			}
			UE_LOG(LogOSCulator, Warning, TEXT("MIDI output device '%s' was not found. Available: %s"),
				*Wanted, Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("(none)"));
			continue;
		}

		UMIDIDeviceOutputController* Controller = UMIDIDeviceManager::CreateMIDIDeviceOutputController(Found->DeviceID);
		if (Controller == nullptr)
		{
			UE_LOG(LogOSCulator, Warning, TEXT("MIDI output device '%s' could not be opened."), *Wanted);
			continue;
		}

		FOscuMIDIOutput& Output = Outputs.AddDefaulted_GetRef();
		Output.Controller = Controller;
		Output.Name = FName(*Wanted);

		UE_LOG(LogOSCulator, Log, TEXT("MIDI output open: '%s' (device %d)."), *Wanted, Found->DeviceID);
	}

	UE_LOG(LogOSCulator, Log, TEXT("MIDI output: %d of %d configured device(s) opened."),
		Outputs.Num(), Settings.MIDIOutputDeviceNames.Num());
}

void UOscuMIDISubsystem::CloseOutputs()
{
	for (FOscuMIDIOutput& Output : Outputs)
	{
		if (Output.Controller != nullptr)
		{
			// Same as the input side: the port is only released on shutdown, not on
			// losing the last reference, and a closed controller still has to be
			// marked or the engine will restart it out from under us.
			Output.Controller->ShutdownDevice();
			Output.Controller->MarkAsGarbage();
		}
	}
	Outputs.Reset();
}

int32 UOscuMIDISubsystem::SendNoteOn(int32 Channel, int32 Note, int32 Velocity, FName Device)
{
	const int32 WireChannel = ToWireChannel(Channel);
	const int32 ClampedNote = FMath::Clamp(Note, 0, 127);
	const int32 ClampedVelocity = FMath::Clamp(Velocity, 0, 127);

	int32 Sent = 0;
	for (const FOscuMIDIOutput& Output : Outputs)
	{
		if (!Device.IsNone() && Output.Name != Device)
		{
			continue;
		}
		if (Output.Controller != nullptr)
		{
			Output.Controller->SendMIDINoteOn(WireChannel, ClampedNote, ClampedVelocity);
			++Sent;
		}
	}
	return Sent;
}

int32 UOscuMIDISubsystem::SendNoteOff(int32 Channel, int32 Note, FName Device)
{
	const int32 WireChannel = ToWireChannel(Channel);
	const int32 ClampedNote = FMath::Clamp(Note, 0, 127);

	int32 Sent = 0;
	for (const FOscuMIDIOutput& Output : Outputs)
	{
		if (!Device.IsNone() && Output.Name != Device)
		{
			continue;
		}
		if (Output.Controller != nullptr)
		{
			Output.Controller->SendMIDINoteOff(WireChannel, ClampedNote, 0);
			++Sent;
		}
	}
	return Sent;
}

int32 UOscuMIDISubsystem::SendControlChange(int32 Channel, int32 ControlNumber, int32 Value, FName Device)
{
	const int32 WireChannel = ToWireChannel(Channel);
	const int32 ClampedControl = FMath::Clamp(ControlNumber, 0, 127);
	const int32 ClampedValue = FMath::Clamp(Value, 0, 127);

	int32 Sent = 0;
	for (const FOscuMIDIOutput& Output : Outputs)
	{
		if (!Device.IsNone() && Output.Name != Device)
		{
			continue;
		}
		if (Output.Controller != nullptr)
		{
			Output.Controller->SendMIDIControlChange(WireChannel, ClampedControl, ClampedValue);
			++Sent;
		}
	}
	return Sent;
}

int32 UOscuMIDISubsystem::SendNote(int32 Channel, int32 Note, int32 Velocity, float DurationSeconds, FName Device)
{
	const int32 Sent = SendNoteOn(Channel, Note, Velocity, Device);

	if (DurationSeconds > 0.0f)
	{
		// Scheduled whether or not a device took the note on. The bookkeeping stays
		// honest, and a device opened between now and then still gets the release.
		FOscuPendingNoteOff& Pending = PendingNoteOffs.AddDefaulted_GetRef();
		Pending.Channel = Channel;
		Pending.Note = FMath::Clamp(Note, 0, 127);
		Pending.Device = Device;
		Pending.DueTime = FPlatformTime::Seconds() + DurationSeconds;
	}

	return Sent;
}

void UOscuMIDISubsystem::FlushPendingNoteOffs()
{
	// Copied out first: SendNoteOff must not be walking the array it is clearing.
	TArray<FOscuPendingNoteOff> Outstanding = MoveTemp(PendingNoteOffs);
	PendingNoteOffs.Reset();

	for (const FOscuPendingNoteOff& Pending : Outstanding)
	{
		SendNoteOff(Pending.Channel, Pending.Note, Pending.Device);
	}
}

bool UOscuMIDISubsystem::Tick(float DeltaTime)
{
	// Input first. A note read this frame gets dispatched this frame, and if it was
	// scheduled with a duration its release is queued below in the same pass.
	DrainPorts();

	if (PendingNoteOffs.Num() > 0)
	{
		const double Now = FPlatformTime::Seconds();

		for (int32 Index = PendingNoteOffs.Num() - 1; Index >= 0; --Index)
		{
			if (PendingNoteOffs[Index].DueTime > Now)
			{
				continue;
			}

			const FOscuPendingNoteOff Due = PendingNoteOffs[Index];
			PendingNoteOffs.RemoveAtSwap(Index);
			SendNoteOff(Due.Channel, Due.Note, Due.Device);
		}
	}

	return true;
}

void UOscuMIDISubsystem::OpenDevices()
{
	const UOscuSettings& Settings = *UOscuSettings::Get();
	if (!Settings.bEnableMIDIIn)
	{
		UE_LOG(LogOSCulator, Log, TEXT("MIDI input is disabled in Project Settings > Plugins > OSCulator."));
		return;
	}

	// A headless automation or cook run has no business grabbing real hardware,
	// and would fight whatever the user has open.
	if (IsRunningCommandlet() || FApp::IsUnattended())
	{
		UE_LOG(LogOSCulator, Log, TEXT("MIDI input skipped: running unattended."));
		return;
	}

	// Every configured map, all of them live at once. One that will not load is named
	// and skipped rather than taking the others down with it.
	ActiveMaps.Reset();
	for (const FSoftObjectPath& Path : Settings.MIDIMaps)
	{
		if (!Path.IsValid())
		{
			continue;
		}

		UOscuMIDIMap* Map = Cast<UOscuMIDIMap>(Path.TryLoad());
		if (Map == nullptr)
		{
			UE_LOG(LogOSCulator, Warning, TEXT("MIDI map '%s' could not be loaded."), *Path.ToString());
			continue;
		}

		ActiveMaps.Add(Map);
	}

	if (ActiveMaps.Num() == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("MIDI input is enabled but no map asset is set in Project Settings > Plugins > OSCulator. MIDI will arrive and go nowhere."));
	}
	else
	{
		for (const TObjectPtr<UOscuMIDIMap>& Map : ActiveMaps)
		{
			UE_LOG(LogOSCulator, Log, TEXT("MIDI map loaded: '%s' (%d binding(s)%s)."),
				*Map->GetName(), Map->Bindings.Num(),
				Map->DefaultDevice.IsNone() ? TEXT("") : *FString::Printf(TEXT(", device '%s'"), *Map->DefaultDevice.ToString()));
		}
	}

	if (Settings.MIDIInputDevices.Num() == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("MIDI input is enabled but no devices are listed in Project Settings > Plugins > OSCulator."));
		return;
	}

	const int32 QueueSize = FMath::Clamp(Settings.MIDIInputQueueSize, 64, 16384);

	// Says out loud what is about to be attempted, so "0 devices open" is never a
	// mystery -- the reason is always the next line or two of the log. Neither filter is
	// named here, because both are per device now; each device's open line carries its
	// own, which is the only place they mean anything.
	UE_LOG(LogOSCulator, Log, TEXT("MIDI input: opening %d configured device(s). Queue %d message(s) each."),
		Settings.MIDIInputDevices.Num(), QueueSize);

	for (const FOscuMIDIInputDevice& Wanted : Settings.MIDIInputDevices)
	{
		if (Wanted.Name.IsEmpty())
		{
			// A freshly added array row. Worth a line rather than a silent skip, because
			// "I added my device and nothing happened" usually means the name was never
			// typed in.
			UE_LOG(LogOSCulator, Warning,
				TEXT("MIDI input: a device entry has no name and was skipped. Run OSCulator.MIDIDevices to list what is available."));
			continue;
		}

		// Both built per device, because both are statements about one piece of hardware.
		// Learn lifts the channel mask afterwards and puts it back; the filter is left
		// alone, since a device told not to send notes has nothing to teach.
		const int32 FilterMask = OscuMIDI::BuildFilterMask(Wanted.ListenFor);
		const int32 ChannelMask = OscuMIDI::BuildChannelMaskIgnoring(Wanted.IgnoredChannels, Wanted.Name);

		if (!Wanted.ListenFor.bNotes)
		{
			// Legal, and possibly deliberate, but it switches off everything a map asset
			// can currently address FOR THIS DEVICE. Better said here than discovered by
			// playing a pad and getting nothing. Named per device, because with the
			// filter per device the interesting case is one box out of three being deaf.
			UE_LOG(LogOSCulator, Warning,
				TEXT("MIDI input '%s' is not listening for notes -- tick 'Notes' under its Listen For to change that. No mapping can fire from this device and Learn will not work on it."),
				*Wanted.Name);
		}

		FOscuMIDIPort Port;
		FString Error;

		if (!Port.Open(Wanted.Name, QueueSize, FilterMask, ChannelMask, Error))
		{
			// One bad device must not take the others down with it.
			UE_LOG(LogOSCulator, Warning, TEXT("MIDI input device '%s' %s"), *Wanted.Name, *Error);
			continue;
		}

		UE_LOG(LogOSCulator, Log, TEXT("MIDI input open: '%s' (device %d), dropping %s, listening on %s."),
			*Port.Name, Port.DeviceID,
			*OscuMIDI::DescribeFilterMask(Port.GetFilterMask()),
			*OscuMIDI::DescribeChannelMask(Port.GetChannelMask()));

#if WITH_EDITOR
		// Opened mid-Learn -- a settings edit while a row is armed restarts the
		// subsystem. The port would otherwise come up with its configured mask on and
		// quietly refuse to teach a muted channel.
		if (LearnMap.IsValid())
		{
			Port.SetChannelMask(0xFFFF);
		}
#endif

		Ports.Add(MoveTemp(Port));
	}

	UE_LOG(LogOSCulator, Log, TEXT("MIDI input: %d of %d configured device(s) opened."),
		Ports.Num(), Settings.MIDIInputDevices.Num());
}

void UOscuMIDISubsystem::DrainPorts()
{
	for (FOscuMIDIPort& Port : Ports)
	{
		// The device travels with the message, because a binding may insist on one:
		// two controllers sending the same note on the same channel are two different
		// intentions.
		const FName Device = Port.DeviceId;
		Port.Drain([this, Device](const FOscuMIDIMessage& Message)
		{
			IngestMessage(Device, Message);
		});
	}
}

void UOscuMIDISubsystem::CloseDevices()
{
	for (FOscuMIDIPort& Port : Ports)
	{
		// Explicit, and before the array is emptied. A PortMidi stream is not a
		// managed resource: drop the last reference to it and the port stays held for
		// the life of the process, so no other application can take it.
		Port.Close();
	}
	Ports.Reset();
}

UWorld* UOscuMIDISubsystem::FindDispatchWorld() const
{
	if (GEngine == nullptr)
	{
		return nullptr;
	}

	// Prefer a world that has properly begun play, but do not require it.
	// UWorld::HasBegunPlay also demands GetBegunPlay(), which is set by the
	// WorldSettings and GameMode flow -- a world can have run OnWorldBeginPlay on
	// its subsystems, and so have a populated registry, without it. Dispatching
	// into a world that has not begun is harmless anyway: its registry is empty, so
	// nothing resolves and nothing is called.
	UWorld* Fallback = nullptr;

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game)
		{
			continue;
		}

		UWorld* World = Context.World();
		if (World == nullptr)
		{
			continue;
		}

		if (World->HasBegunPlay())
		{
			return World;
		}
		if (Fallback == nullptr)
		{
			Fallback = World;
		}
	}

	return Fallback;
}

#if WITH_EDITOR

void UOscuMIDISubsystem::ArmLearn(UOscuMIDIMap* Map, const int32 BindingIndex, const int32 SourceIndex)
{
	if (UOscuSettings::Get()->bPerformanceMode)
	{
		// Refused out loud. A Learn tickbox that quietly did nothing would be a
		// miserable thing to debug, and the setting that caused it is not in the asset
		// you are looking at -- so the message names it.
		UE_LOG(LogOSCulator, Warning,
			TEXT("MIDI Learn is disabled: Performance Mode is on in Project Settings > Plugins > OSCulator. Untick it to teach inputs again."));

		if (Map != nullptr && Map->Bindings.IsValidIndex(BindingIndex)
			&& Map->Bindings[BindingIndex].Sources.IsValidIndex(SourceIndex))
		{
			// Untick it again, or the row sits there claiming to be armed.
			Map->Bindings[BindingIndex].Sources[SourceIndex].bLearn = false;
		}
		return;
	}

	LearnMap = Map;
	LearnBindingIndex = BindingIndex;
	LearnSourceIndex = SourceIndex;

	// Lifted on every port for the duration, so an input can be learned from a channel
	// that device is configured to drop. Each port remembers its own mask, so
	// CancelLearn restores one answer per port rather than a single project-wide one.
	for (FOscuMIDIPort& Port : Ports)
	{
		Port.SetChannelMask(0xFFFF);
	}

	if (Ports.Num() == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("Learn is armed, but no MIDI device is open. Check the device list in Project Settings > Plugins > OSCulator."));
	}
	else
	{
		UE_LOG(LogOSCulator, Log, TEXT("Learn armed. Play a note or turn a knob."));
	}
}

void UOscuMIDISubsystem::CancelLearn(const UOscuMIDIMap* Map)
{
	if (LearnMap.Get() != Map)
	{
		return;
	}

	LearnMap.Reset();
	LearnBindingIndex = INDEX_NONE;
	LearnSourceIndex = INDEX_NONE;

	// Each port back to its own configured mask, now that nothing is listening for a
	// stray channel. Per port, because the lists are per device.
	for (FOscuMIDIPort& Port : Ports)
	{
		Port.RestoreChannelMask();
	}
}

bool UOscuMIDISubsystem::ApplyLearn(const FName Device, const FOscuMIDIMessage& Message)
{
	UOscuMIDIMap* Map = LearnMap.Get();
	if (Map == nullptr)
	{
		return false;
	}

	// The asset can be edited while a source sits armed, so the indices are re-checked
	// rather than trusted.
	if (!Map->Bindings.IsValidIndex(LearnBindingIndex))
	{
		CancelLearn(Map);
		return false;
	}
	FOscuMIDIBinding& Binding = Map->Bindings[LearnBindingIndex];

	if (!Binding.Sources.IsValidIndex(LearnSourceIndex))
	{
		CancelLearn(Map);
		return false;
	}
	FOscuMIDISource& Source = Binding.Sources[LearnSourceIndex];

	// Everything about the gesture, not just which note it was. Filling in the device
	// and the channel by hand afterwards is exactly the tedium Learn exists to remove.
	Source.Device = Device;
	Source.Channel = static_cast<uint8>(Message.Channel);
	Source.bLearn = false;

	// The KIND goes on the binding, which is where it lives, and so applies to the whole
	// row. Playing a knob at a row that was note-driven means the row is knob-driven now
	// -- that is what the gesture said. But it re-points the row's other sources too, and
	// their numbers are reinterpreted, so a row that had others is told about it rather
	// than quietly rewritten.
	if (Binding.Type != Message.Type && Binding.Sources.Num() > 1)
	{
		const auto KindName = [](const EOscuMIDIInputType Kind) -> const TCHAR*
		{
			switch (Kind)
			{
			case EOscuMIDIInputType::Note:          return TEXT("notes");
			case EOscuMIDIInputType::ControlChange:  return TEXT("control change");
			default:                                 return TEXT("program change");
			}
		};

		UE_LOG(LogOSCulator, Warning,
			TEXT("Learn changed %s/%s from %s to %s, which re-points its other %d source(s) -- their numbers are reinterpreted. Undo if that was not the intent."),
			*Binding.Tag.ToString(), *Binding.FunctionName.ToString(),
			KindName(Binding.Type), KindName(Message.Type), Binding.Sources.Num() - 1);
	}
	Binding.Type = Message.Type;
	Source.Type = Message.Type;

	if (Message.Type == EOscuMIDIInputType::Note)
	{
		const int32 MiddleCOctave = UOscuSettings::Get()->MiddleCOctave;
		Source.ResolvedNote = static_cast<uint8>(Message.Number);
		Source.Note = OscuMIDINoteName::ToString(static_cast<uint8>(Message.Number), MiddleCOctave);

		// On a range source, one gesture can only be one end of the span, and the
		// bottom is the useful end to capture. If that lands above the current top, the
		// top comes with it rather than being left inverted -- Refresh would otherwise
		// swap the pair and quietly make the note you just played the TOP of the span,
		// which is the opposite of what pressing it meant.
		if (Source.bNoteRange && Source.ResolvedNoteHigh < Source.ResolvedNote)
		{
			Source.ResolvedNoteHigh = Source.ResolvedNote;
			Source.NoteHigh = Source.Note;

			UE_LOG(LogOSCulator, Log,
				TEXT("Learn set the bottom of a note range to %s (%d); its top came with it. Set Note High to open the span."),
				*Source.Note, Message.Number);
		}
	}
	else if (Message.Type == EOscuMIDIInputType::ProgramChange)
	{
		Source.ProgramNumber = static_cast<uint8>(Message.Number);
	}
	else
	{
		Source.ControlNumber = static_cast<uint8>(Message.Number);
	}

	FString What;
	switch (Message.Type)
	{
	case EOscuMIDIInputType::Note:
		What = FString::Printf(TEXT("note %s (%d)"), *Source.Note, Message.Number);
		break;
	case EOscuMIDIInputType::ProgramChange:
		What = FString::Printf(TEXT("program %d"), Message.Number);
		break;
	default:
		What = FString::Printf(TEXT("CC %d"), Message.Number);
		break;
	}

	UE_LOG(LogOSCulator, Log, TEXT("Learned %s on channel %d for %s/%s."),
		*What, Message.Channel, *Binding.Tag.ToString(), *Binding.FunctionName.ToString());

	CancelLearn(Map);

	Map->Refresh();
	Map->MarkPackageDirty();

	// Nudge the details panel so the new input appears without a reselect.
	Map->PostEditChange();

	return true;
}

#endif // WITH_EDITOR

/**
 * Logs every incoming note, not just the first.
 *
 * The question this answers is "is the note number I think I am sending the note
 * number that arrives?" -- which no amount of reading the map can settle, because
 * senders disagree about whether their note labels are 0-based or 1-based.
 */
static TAutoConsoleVariable<int32> CVarOscuMIDIMonitor(
	TEXT("OSCulator.MIDIMonitor"),
	0,
	TEXT("1 logs every incoming MIDI note with its raw channel, note number and resolved name."),
	ECVF_Default);

int32 UOscuMIDISubsystem::IngestNote(const int32 Channel, const int32 Note, const int32 Velocity, const bool bNoteOn, const FName Device)
{
	FOscuMIDIMessage Message;
	Message.Type = EOscuMIDIInputType::Note;
	Message.Channel = Channel;
	Message.Number = Note;
	Message.Value = Velocity;
	Message.bNoteOn = bNoteOn;
	return IngestMessage(Device, Message);
}

int32 UOscuMIDISubsystem::IngestControlChange(const int32 Channel, const int32 ControlNumber, const int32 Value, const FName Device)
{
	FOscuMIDIMessage Message;
	Message.Type = EOscuMIDIInputType::ControlChange;
	Message.Channel = Channel;
	Message.Number = ControlNumber;
	Message.Value = Value;
	return IngestMessage(Device, Message);
}

int32 UOscuMIDISubsystem::ResolveParamSlot(
	const UOscuRouterSubsystem& Router, const FOscuMIDIBinding& Binding, const FName ParamName)
{
	if (ParamName.IsNone())
	{
		return INDEX_NONE;
	}

	if (!UOscuSettings::ShouldCacheSignatureFacts())
	{
		// The live answer, every message. A recompile that reorders parameters is
		// followed rather than silently mismatched, which is the whole reason this is
		// not cached by default while someone is still editing.
		return Router.FindParamSlot(Binding.Tag, Binding.FunctionName, ParamName);
	}

	const uint32 Key = HashCombine(
		HashCombine(GetTypeHash(Binding.Tag), GetTypeHash(Binding.FunctionName)),
		GetTypeHash(ParamName));

	if (const int32* Cached = ParamSlotCache.Find(Key))
	{
		return *Cached;
	}

	// A miss is cached too, INDEX_NONE and all: a name that does not resolve does not
	// resolve on every message either, and re-asking the registry about it forever is
	// exactly the cost this exists to avoid.
	const int32 Slot = Router.FindParamSlot(Binding.Tag, Binding.FunctionName, ParamName);
	ParamSlotCache.Add(Key, Slot);
	return Slot;
}

void UOscuMIDISubsystem::WarnUnresolvedParam(
	const FOscuMIDIBinding& Binding, const FName ParamName, const TCHAR* Role)
{
	const uint32 Key = HashCombine(
		HashCombine(GetTypeHash(Binding.Tag), GetTypeHash(Binding.FunctionName)),
		GetTypeHash(ParamName));

	if (WarnedUnresolvedParams.Contains(Key))
	{
		return;
	}
	WarnedUnresolvedParams.Add(Key);

	// Names what is available, because the whole failure mode here is a typo or a
	// renamed pin, and both are fixed by seeing the real list.
	FString Available = TEXT("(nothing tagged exposes that function)");

	if (const UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(FindDispatchWorld()))
	{
		TArray<FOscuExposedParamInfo> Params;
		TArray<int32> Slots;
		if (Router->DescribeParams(Binding.Tag, Binding.FunctionName, Params, Slots))
		{
			TArray<FString> Names;
			for (int32 Index = 0; Index < Params.Num(); ++Index)
			{
				if (!Params[Index].bOutputOnly)
				{
					Names.Add(FString::Printf(TEXT("%s (%s)"),
						*Params[Index].Name.ToString(), *Params[Index].TypeLabel));
				}
			}
			Available = Names.Num() > 0
				? FString::Join(Names, TEXT(", "))
				: FString(TEXT("(that function takes no arguments)"));
		}
	}

	UE_LOG(LogOSCulator, Warning,
		TEXT("MIDI map: %s -> Parameter on /%s/%s names '%s', which that function does not have. "
			 "That value is not being sent. Available: %s. Reported once."),
		Role, *Binding.Tag.ToString(), *Binding.FunctionName.ToString(), *ParamName.ToString(), *Available);
}

int32 UOscuMIDISubsystem::IngestProgramChange(const int32 Channel, const int32 ProgramNumber, const FName Device)
{
	FOscuMIDIMessage Message;
	Message.Type = EOscuMIDIInputType::ProgramChange;
	Message.Channel = Channel;
	Message.Number = ProgramNumber;

	// Both, for the same reason the port decoder does it: there is no other payload, and
	// a function that wants to know which program arrived should get it as the value.
	Message.Value = ProgramNumber;
	return IngestMessage(Device, Message);
}

int32 UOscuMIDISubsystem::IngestMessage(const FName Device, const FOscuMIDIMessage& Message)
{
	++MessagesReceived;

	const bool bIsNote = Message.Type == EOscuMIDIInputType::Note;

	if (CVarOscuMIDIMonitor.GetValueOnGameThread() != 0)
	{
		if (bIsNote)
		{
			const FString NoteName = (Message.Number >= 0 && Message.Number <= 127)
				? OscuMIDINoteName::ToString(static_cast<uint8>(Message.Number), UOscuSettings::Get()->MiddleCOctave)
				: TEXT("?");

			UE_LOG(LogOSCulator, Log, TEXT("MIDI in [%s]: channel=%d note=%d (%s) velocity=%d %s"),
				*Device.ToString(), Message.Channel, Message.Number, *NoteName, Message.Value,
				Message.bNoteOn ? TEXT("on") : TEXT("off"));
		}
		else if (Message.Type == EOscuMIDIInputType::ProgramChange)
		{
			UE_LOG(LogOSCulator, Log, TEXT("MIDI in [%s]: channel=%d program=%d"),
				*Device.ToString(), Message.Channel, Message.Number);
		}
		else
		{
			UE_LOG(LogOSCulator, Log, TEXT("MIDI in [%s]: channel=%d CC=%d value=%d"),
				*Device.ToString(), Message.Channel, Message.Number, Message.Value);
		}
	}

	if (!bLoggedFirstMessage)
	{
		bLoggedFirstMessage = true;
		UE_LOG(LogOSCulator, Log,
			TEXT("First MIDI message: device='%s' channel=%d %s=%d value=%d. Channels are numbered 1-16 here, matching the map asset."),
			*Device.ToString(), Message.Channel,
			bIsNote ? TEXT("note")
				: (Message.Type == EOscuMIDIInputType::ProgramChange ? TEXT("program") : TEXT("CC")),
			Message.Number, Message.Value);
	}

	if (Message.Channel < 1 || Message.Channel > 16 || Message.Number < 0 || Message.Number > 127)
	{
		return 0;
	}

#if WITH_EDITOR
	// Learn claims the message before anything else looks at it. A note only counts on
	// press -- releasing a pad is not the note you meant -- but any controller movement
	// will do, since a knob has no press.
	//
	// Performance Mode skips the check outright. Learn cannot be armed in that mode, so
	// the weak-pointer test inside ApplyLearn would always fail -- but it would fail once
	// per message, forever, to answer a question already settled by a setting.
	if (!UOscuSettings::Get()->bPerformanceMode
		&& (!bIsNote || Message.bNoteOn)
		&& ApplyLearn(Device, Message))
	{
		return 0;
	}
#endif

	if (ActiveMaps.Num() == 0)
	{
		return 0;
	}

	// Gathered across every map. Which asset a binding lives in is an organisational
	// choice -- per device, per show, or one of each -- and must not change what fires.
	TArray<FOscuMIDIMatch> Matches;
	TArray<FOscuMIDIMatch> FromOneMap;

	for (const TObjectPtr<UOscuMIDIMap>& Map : ActiveMaps)
	{
		if (Map == nullptr)
		{
			continue;
		}

		Map->FindMatches(Device, static_cast<uint8>(Message.Channel), Message.Type, Message.Number, FromOneMap);
		Matches.Append(FromOneMap);
	}

	if (Matches.Num() == 0)
	{
		// Not ours. Anything unbound passes through untouched, for other systems to
		// interpret however they like.
		++MessagesUnmapped;
		return 0;
	}

	UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(FindDispatchWorld());
	if (Router == nullptr)
	{
		// Editor-time messages with nothing playing. Not a fault.
		return 0;
	}

	int32 Calls = 0;

	// Plural on purpose. One input driving two functions on two different actors is a
	// thing people want; the old channel-first structure could only call it a mistake.
	for (const FOscuMIDIMatch& Match : Matches)
	{
		const FOscuMIDIBinding& Binding = *Match.Binding;

		if (bIsNote && !Message.bNoteOn && !Binding.bFireOnNoteOff)
		{
			continue;
		}

		// A released note carries no meaningful velocity, so it shapes zero -- which is
		// OutMin, not necessarily 0.0, and that distinction is the point of a remap.
		const int32 Raw = (bIsNote && !Message.bNoteOn) ? 0 : Message.Value;

		FOscuMessage Outgoing;
		Outgoing.Address = FString::Printf(TEXT("/%s/%s"), *Binding.Tag.ToString(), *Binding.FunctionName.ToString());

		const bool bNamed = !Binding.VelocityParam.IsNone() || !Binding.PitchParam.IsNone();

		if (!bNamed)
		{
			// Byte for byte what this did before any of the naming existed. A binding
			// that names nothing must not shift its arguments because the feature was
			// added, so the untouched path stays literally untouched.
			if (Binding.bSendSourceNumber)
			{
				// Raw and never remapped: which control arrived is an identity, not a
				// measurement.
				Outgoing.Args.Add(FOscuValue::MakeFloat(static_cast<double>(Message.Number)));
			}
			Outgoing.Args.Add(FOscuValue::MakeFloat(Binding.ShapeValue(Raw)));
		}
		else
		{
			// Named assignment. While the project is being edited, slots are resolved
			// against the LIVE signature every message, so a Blueprint recompile that
			// reorders parameters is followed instead of silently mismatched. In a cooked
			// build, or with Performance Mode on, nothing can recompile and the answer is
			// remembered instead -- see ResolveParamSlot.
			int32 VelocitySlot = INDEX_NONE;
			int32 PitchSlot = INDEX_NONE;

			if (!Binding.VelocityParam.IsNone())
			{
				VelocitySlot = ResolveParamSlot(*Router, Binding, Binding.VelocityParam);
				if (VelocitySlot == INDEX_NONE)
				{
					WarnUnresolvedParam(Binding, Binding.VelocityParam, TEXT("Velocity"));
				}
			}

			if (!Binding.PitchParam.IsNone())
			{
				PitchSlot = ResolveParamSlot(*Router, Binding, Binding.PitchParam);
				if (PitchSlot == INDEX_NONE)
				{
					WarnUnresolvedParam(Binding, Binding.PitchParam, TEXT("Pitch"));
				}
			}

			// Sized to the furthest slot actually being written. Everything short of it
			// is zero, which is what Lenient would have left in the frame anyway -- so
			// filling the gaps costs nothing and keeps the message self-describing.
			const int32 HighestSlot = FMath::Max(VelocitySlot, PitchSlot);
			if (HighestSlot >= 0)
			{
				Outgoing.Args.Reserve(HighestSlot + 1);
				for (int32 Slot = 0; Slot <= HighestSlot; ++Slot)
				{
					Outgoing.Args.Add(FOscuValue::MakeFloat(0.0));
				}

				if (VelocitySlot != INDEX_NONE)
				{
					Outgoing.Args[VelocitySlot] = FOscuValue::MakeFloat(Binding.ShapeValue(Raw));
				}

				if (PitchSlot != INDEX_NONE)
				{
					// A released note keeps its pitch -- which pad was let go is still
					// the pad it was, unlike velocity, which has no meaning on release.
					const double Fraction = Match.Source->GetPitchFraction(Message.Number);
					Outgoing.Args[PitchSlot] = FOscuValue::MakeFloat(Binding.ShapePitch(Fraction, Message.Number));
				}
			}
		}

		// Lenient always. MIDI rarely supplies as many values as the signature wants:
		// unfilled parameters keep the zeroes the initialised frame gave them, and a
		// float landing in an int parameter is truncated by the marshal, which is what
		// makes "remap to 0-10 and call an int function" work without a mode.
		Calls += Router->DispatchMessage(Outgoing, EOscuArgPolicy::Lenient);
	}

	if (Calls > 0)
	{
		++MessagesDispatched;
	}
	return Calls;
}
