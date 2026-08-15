// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDISubsystem.h"

#include "OSCulatorCore.h"
#include "OscuMIDINoteName.h"
#include "OscuMarshal.h"
#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"
#include "OscuValue.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MIDIDeviceInputController.h"
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

void UOscuMIDISubsystem::Restart()
{
	// Release anything sounding before the devices go away underneath it.
	FlushPendingNoteOffs();

	CloseDevices();
	CloseOutputs();

	OpenDevices();
	OpenOutputs();
}

int32 UOscuMIDISubsystem::ToWireChannel(int32 Channel)
{
	return FMath::Clamp(Channel, 1, 16) - 1;
}

void UOscuMIDISubsystem::OpenOutputs()
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

	TArray<FMIDIDeviceInfo> InputDevices;
	TArray<FMIDIDeviceInfo> OutputDevices;
	UMIDIDeviceManager::FindAllMIDIDeviceInfo(InputDevices, OutputDevices);

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
			// losing the last reference.
			Output.Controller->ShutdownDevice();
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

	ActiveMap = Cast<UOscuMIDIMap>(Settings.MIDIMap.TryLoad());
	if (ActiveMap == nullptr && Settings.MIDIMap.IsValid())
	{
		UE_LOG(LogOSCulator, Warning, TEXT("MIDI map '%s' could not be loaded."), *Settings.MIDIMap.ToString());
	}
	else if (ActiveMap == nullptr)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("MIDI input is enabled but no map asset is set in Project Settings > Plugins > OSCulator. Notes will arrive and go nowhere."));
	}

	if (Settings.MIDIInputDeviceNames.Num() == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("MIDI input is enabled but no device names are listed in Project Settings > Plugins > OSCulator."));
		return;
	}

	TArray<FMIDIDeviceInfo> InputDevices;
	TArray<FMIDIDeviceInfo> OutputDevices;
	UMIDIDeviceManager::FindAllMIDIDeviceInfo(InputDevices, OutputDevices);

	// Says out loud what is about to be attempted, so "0 devices open" is never a
	// mystery -- the reason is always the next line or two of the log.
	UE_LOG(LogOSCulator, Log, TEXT("MIDI input: opening %d configured device(s); %d input(s) present on this machine."),
		Settings.MIDIInputDeviceNames.Num(), InputDevices.Num());

	for (const FString& Wanted : Settings.MIDIInputDeviceNames)
	{
		// By name, never by enumeration order. Exclusivity conflicts between plugins
		// are real, and taking whatever is first is how you end up fighting for a port.
		const FMIDIDeviceInfo* Found = InputDevices.FindByPredicate(
			[&Wanted](const FMIDIDeviceInfo& Info) { return Info.DeviceName.Equals(Wanted, ESearchCase::IgnoreCase); });

		if (Found == nullptr)
		{
			TArray<FString> Available;
			for (const FMIDIDeviceInfo& Info : InputDevices)
			{
				Available.Add(Info.DeviceName);
			}
			UE_LOG(LogOSCulator, Warning, TEXT("MIDI input device '%s' was not found. Available: %s"),
				*Wanted,
				Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("(none)"));
			continue;
		}

		if (Found->bIsAlreadyInUse)
		{
			// One busy device must not take the others down with it.
			UE_LOG(LogOSCulator, Warning, TEXT("MIDI input device '%s' is already in use by another application. Skipped."), *Wanted);
			continue;
		}

		UMIDIDeviceInputController* Controller = UMIDIDeviceManager::CreateMIDIDeviceInputController(Found->DeviceID);
		if (Controller == nullptr)
		{
			UE_LOG(LogOSCulator, Warning, TEXT("MIDI input device '%s' could not be opened."), *Wanted);
			continue;
		}

		Controller->OnMIDINoteOn.AddDynamic(this, &UOscuMIDISubsystem::HandleNoteOn);
		Controller->OnMIDINoteOff.AddDynamic(this, &UOscuMIDISubsystem::HandleNoteOff);
		Controllers.Add(Controller);

		UE_LOG(LogOSCulator, Log, TEXT("MIDI input open: '%s' (device %d)."), *Wanted, Found->DeviceID);
	}

	UE_LOG(LogOSCulator, Log, TEXT("MIDI input: %d of %d configured device(s) opened."),
		Controllers.Num(), Settings.MIDIInputDeviceNames.Num());
}

void UOscuMIDISubsystem::CloseDevices()
{
	for (TObjectPtr<UMIDIDeviceInputController>& Controller : Controllers)
	{
		if (Controller == nullptr)
		{
			continue;
		}

		Controller->OnMIDINoteOn.RemoveAll(this);
		Controller->OnMIDINoteOff.RemoveAll(this);

		// Closing the port has to be explicit. Dropping the reference only queues
		// the controller for garbage collection, and the PortMidi stream stays open
		// until that eventually runs -- so the device keeps being held long after
		// OSCulator has stopped using it, and no other application can take it.
		// ShutdownDevice is null-guarded, so the destructor calling it again is fine.
		Controller->ShutdownDevice();
	}
	Controllers.Reset();
}

void UOscuMIDISubsystem::HandleNoteOn(UMIDIDeviceInputController* Controller, int32 Timestamp, int32 Channel, int32 Note, int32 Velocity)
{
	IngestNote(Channel, Note, Velocity, /*bNoteOn*/ true);
}

void UOscuMIDISubsystem::HandleNoteOff(UMIDIDeviceInputController* Controller, int32 Timestamp, int32 Channel, int32 Note, int32 Velocity)
{
	IngestNote(Channel, Note, Velocity, /*bNoteOn*/ false);
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

void UOscuMIDISubsystem::ArmLearn(UOscuMIDIMap* Map, int32 ChannelIndex, int32 NoteIndex)
{
	LearnMap = Map;
	LearnChannelIndex = ChannelIndex;
	LearnNoteIndex = NoteIndex;

	if (Controllers.Num() == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("Learn is armed, but no MIDI device is open. Check the device list in Project Settings > Plugins > OSCulator."));
	}
	else
	{
		UE_LOG(LogOSCulator, Log, TEXT("Learn armed. Play a note."));
	}
}

void UOscuMIDISubsystem::CancelLearn(const UOscuMIDIMap* Map)
{
	if (LearnMap.Get() == Map)
	{
		LearnMap.Reset();
		LearnChannelIndex = INDEX_NONE;
		LearnNoteIndex = INDEX_NONE;
	}
}

bool UOscuMIDISubsystem::ApplyLearn(int32 Channel, int32 Note)
{
	UOscuMIDIMap* Map = LearnMap.Get();
	if (Map == nullptr)
	{
		return false;
	}

	// The asset can be edited while a row sits armed, so the indices are re-checked
	// rather than trusted.
	if (!Map->Channels.IsValidIndex(LearnChannelIndex))
	{
		CancelLearn(Map);
		return false;
	}
	FOscuMIDIChannelMap& ChannelMap = Map->Channels[LearnChannelIndex];

	if (!ChannelMap.Notes.IsValidIndex(LearnNoteIndex))
	{
		CancelLearn(Map);
		return false;
	}
	FOscuMIDINoteMap& NoteMap = ChannelMap.Notes[LearnNoteIndex];

	const int32 MiddleCOctave = UOscuSettings::Get()->MiddleCOctave;
	NoteMap.ResolvedNote = static_cast<uint8>(Note);
	NoteMap.Note = OscuMIDINoteName::ToString(static_cast<uint8>(Note), MiddleCOctave);
	NoteMap.bLearn = false;

	if (ChannelMap.Channel != Channel)
	{
		// Deliberately not "corrected" -- moving the row would move its siblings too.
		UE_LOG(LogOSCulator, Warning,
			TEXT("Learned note %s arrived on channel %d, but this row sits under channel %d. The note was written; change the channel yourself if that was not intended."),
			*NoteMap.Note, Channel, ChannelMap.Channel);
	}

	UE_LOG(LogOSCulator, Log, TEXT("Learned %s (note %d) for '%s'."),
		*NoteMap.Note, Note, *NoteMap.FunctionName.ToString());

	CancelLearn(Map);

	Map->Refresh();
	Map->MarkPackageDirty();

	// Nudge the details panel so the new note appears without a reselect.
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

int32 UOscuMIDISubsystem::IngestNote(int32 Channel, int32 Note, int32 Velocity, bool bNoteOn)
{
	++NotesReceived;

	if (CVarOscuMIDIMonitor.GetValueOnGameThread() != 0)
	{
		const FString NoteName = (Note >= 0 && Note <= 127)
			? OscuMIDINoteName::ToString(static_cast<uint8>(Note), UOscuSettings::Get()->MiddleCOctave)
			: TEXT("?");

		UE_LOG(LogOSCulator, Log, TEXT("MIDI in: channel=%d note=%d (%s) velocity=%d %s"),
			Channel, Note, *NoteName, Velocity, bNoteOn ? TEXT("on") : TEXT("off"));
	}

	if (!bLoggedFirstNote)
	{
		bLoggedFirstNote = true;
		UE_LOG(LogOSCulator, Log,
			TEXT("First MIDI note: channel=%d note=%d velocity=%d (%s). Channels are numbered 1-16 here, matching the map asset."),
			Channel, Note, Velocity, bNoteOn ? TEXT("on") : TEXT("off"));
	}

	if (Channel < 1 || Channel > 16 || Note < 0 || Note > 127)
	{
		return 0;
	}

#if WITH_EDITOR
	// Learn claims the note before anything else looks at it, and only on note on --
	// releasing a pad should not count as the note you meant.
	if (bNoteOn && ApplyLearn(Channel, Note))
	{
		return 0;
	}
#endif

	if (ActiveMap == nullptr)
	{
		return 0;
	}

	const FOscuMIDIMatch Match = ActiveMap->FindMapping(static_cast<uint8>(Channel), static_cast<uint8>(Note));
	if (!Match.IsValid())
	{
		// Not ours. Anything unmapped passes through untouched, for other systems
		// to interpret however they like.
		++NotesUnmapped;
		return 0;
	}

	if (!bNoteOn && !Match.Note->bFireOnNoteOff)
	{
		return 0;
	}

	// A note off carries no meaningful velocity, so it sends zero.
	const double Normalised = bNoteOn ? static_cast<double>(Velocity) / 127.0 : 0.0;

	FOscuMessage Message;
	Message.Address = FString::Printf(TEXT("/%s/%s"), *Match.Tag.ToString(), *Match.Note->FunctionName.ToString());

	switch (Match.Note->Mode)
	{
	case EOscuMIDIValueMode::RawVelocity:
		Message.Args.Add(FOscuValue::MakeFloat(bNoteOn ? static_cast<double>(Velocity) : 0.0));
		break;

	case EOscuMIDIValueMode::NoteAndVelocity:
		Message.Args.Add(FOscuValue::MakeFloat(static_cast<double>(Note)));
		Message.Args.Add(FOscuValue::MakeFloat(Normalised));
		break;

	case EOscuMIDIValueMode::Normalized01:
	default:
		Message.Args.Add(FOscuValue::MakeFloat(Normalised));
		break;
	}

	UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(FindDispatchWorld());
	if (Router == nullptr)
	{
		// Editor-time notes with nothing playing. Not a fault.
		return 0;
	}

	// Lenient always. MIDI supplies one value regardless of what the signature
	// wants, and that is the entire reason MIDI needs no marshalling of its own:
	// unfilled parameters keep the zeroes the initialised frame already gave them.
	const int32 Calls = Router->DispatchMessage(Message, EOscuArgPolicy::Lenient);
	if (Calls > 0)
	{
		++NotesDispatched;
	}
	return Calls;
}
