// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDIPort.h"

#include "OSCulatorCore.h"
#include "OscuSettings.h"

#include "portmidi.h"

namespace OscuMIDI
{
	namespace Private
	{
		static bool bInitialised = false;
	}

	bool EnsureInitialised(FString& OutError)
	{
		if (Private::bInitialised)
		{
			return true;
		}

		const PmError Error = Pm_Initialize();
		if (Error != pmNoError)
		{
			OutError = ANSI_TO_TCHAR(Pm_GetErrorText(Error));
			return false;
		}

		Private::bInitialised = true;
		return true;
	}

	void Shutdown()
	{
		if (!Private::bInitialised)
		{
			return;
		}

		Private::bInitialised = false;
		Pm_Terminate();
	}

	void EnumerateDevices(TArray<FOscuMIDIDeviceInfo>& OutDevices)
	{
		OutDevices.Reset();

		FString Error;
		if (!EnsureInitialised(Error))
		{
			UE_LOG(LogOSCulator, Warning, TEXT("MIDI could not be started: %s"), *Error);
			return;
		}

		const int32 Count = Pm_CountDevices();
		OutDevices.Reserve(Count);

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const PmDeviceInfo* Info = Pm_GetDeviceInfo(Index);
			if (Info == nullptr)
			{
				continue;
			}

			FOscuMIDIDeviceInfo& Device = OutDevices.AddDefaulted_GetRef();
			Device.Name = ANSI_TO_TCHAR(Info->name);
			Device.DeviceID = Index;
			Device.bInput = Info->input != 0;
			Device.bOutput = Info->output != 0;
			Device.bOpenedByUs = Info->opened != 0;
		}
	}

	int32 BuildFilterMask(const FOscuMIDIInputMessages& Listen)
	{
		// PortMidi's filter is expressed as what to throw away, and the settings are
		// expressed as what to keep. The inversion happens here, once, so that it
		// cannot be got wrong twice.
		//
		// Pm_SetFilter replaces the mask rather than adding to it, so PortMidi's own
		// default has to be repeated. Leave PM_FILT_ACTIVE out and active sensing comes
		// back on, several hundred messages a minute per device, forever.
		//
		// Reset and the undefined real-time messages get no setting: nothing can
		// subscribe to them and nothing means anything by them.
		int32 Mask = PM_FILT_ACTIVE | PM_FILT_RESET | PM_FILT_UNDEFINED;

		if (!Listen.bNotes)
		{
			Mask |= PM_FILT_NOTE;
		}
		if (!Listen.bControlChange)
		{
			Mask |= PM_FILT_CONTROL;
		}
		if (!Listen.bProgramChange)
		{
			Mask |= PM_FILT_PROGRAM;
		}
		if (!Listen.bPitchBend)
		{
			Mask |= PM_FILT_PITCHBEND;
		}
		if (!Listen.bAftertouch)
		{
			Mask |= PM_FILT_AFTERTOUCH;
		}
		if (!Listen.bClock)
		{
			Mask |= PM_FILT_CLOCK | PM_FILT_TICK | PM_FILT_MTC;
		}
		if (!Listen.bTransport)
		{
			Mask |= PM_FILT_PLAY | PM_FILT_SONG_POSITION | PM_FILT_SONG_SELECT;
		}
		if (!Listen.bSysEx)
		{
			Mask |= PM_FILT_SYSEX;
		}

		return Mask;
	}

	int32 BuildChannelMaskIgnoring(const TArray<int32>& IgnoredChannels, const FString& DeviceName)
	{
		// Starts from all sixteen and clears what the device was told to drop, so the
		// empty list -- the default, and what a device with nothing configured has --
		// means "hear everything" without a special case.
		int32 Mask = 0xFFFF;
		TArray<FString> OutOfRange;

		for (const int32 Channel : IgnoredChannels)
		{
			if (Channel >= 1 && Channel <= 16)
			{
				// PortMidi counts channels from 0; everything OSCulator shows the user
				// counts from 1, matching the map asset and every DAW.
				Mask &= ~Pm_Channel(Channel - 1);
			}
			else
			{
				// A freshly added array row starts at 0, so this is common rather than
				// exotic. Harmless in this direction -- it ignores nothing -- but said
				// out loud, because a row that looks set and does nothing is its own
				// kind of confusing.
				OutOfRange.Add(FString::FromInt(Channel));
			}
		}

		if (OutOfRange.Num() > 0)
		{
			UE_LOG(LogOSCulator, Warning,
				TEXT("MIDI input '%s': ignored-channel entries %s are outside 1-16 and had no effect."),
				*DeviceName, *FString::Join(OutOfRange, TEXT(", ")));
		}

		if ((Mask & 0xFFFF) == 0)
		{
			// Taken at face value rather than second-guessed: all sixteen listed is a
			// thing someone can mean. But it is indistinguishable from broken hardware
			// from the outside, so it never happens silently.
			UE_LOG(LogOSCulator, Warning,
				TEXT("MIDI input '%s': all sixteen channels are in Ignored Channels, so the device will be opened and hear nothing. Remove it from the device list instead if that was the intent."),
				*DeviceName);
		}

		return Mask;
	}

	FString DescribeFilterMask(const int32 Mask)
	{
		TArray<FString> Dropped;

		if ((Mask & PM_FILT_NOTE) != 0)
		{
			Dropped.Add(TEXT("NOTES"));
		}
		if ((Mask & PM_FILT_CLOCK) != 0)
		{
			Dropped.Add(TEXT("clock"));
		}
		if ((Mask & PM_FILT_MTC) != 0)
		{
			Dropped.Add(TEXT("timecode"));
		}
		if ((Mask & PM_FILT_PLAY) != 0)
		{
			Dropped.Add(TEXT("transport"));
		}
		if ((Mask & PM_FILT_SYSEX) != 0)
		{
			Dropped.Add(TEXT("sysex"));
		}
		if ((Mask & PM_FILT_CONTROL) != 0)
		{
			Dropped.Add(TEXT("control change"));
		}
		if ((Mask & PM_FILT_CHANNEL_AFTERTOUCH) != 0)
		{
			Dropped.Add(TEXT("aftertouch"));
		}
		if ((Mask & PM_FILT_PITCHBEND) != 0)
		{
			Dropped.Add(TEXT("pitch bend"));
		}
		if ((Mask & PM_FILT_PROGRAM) != 0)
		{
			Dropped.Add(TEXT("program change"));
		}

		// Active sensing is always in the mask, so naming it every time is noise.
		return Dropped.Num() > 0
			? FString::Join(Dropped, TEXT(", "))
			: FString(TEXT("nothing but active sensing"));
	}

	FString DescribeChannelMask(const int32 Mask)
	{
		if ((Mask & 0xFFFF) == 0xFFFF)
		{
			return FString(TEXT("all channels"));
		}

		// Phrased as what is missing, to match how it is configured: the setting is a
		// list of channels to ignore, so a log line listing the survivors instead would
		// have to be mentally inverted every time it is read.
		TArray<FString> Ignored;
		for (int32 Channel = 1; Channel <= 16; ++Channel)
		{
			if ((Mask & Pm_Channel(Channel - 1)) == 0)
			{
				Ignored.Add(FString::FromInt(Channel));
			}
		}

		return Ignored.Num() < 16
			? FString::Printf(TEXT("all channels except %s"), *FString::Join(Ignored, TEXT(", ")))
			: FString(TEXT("no channels at all"));
	}
}

bool FOscuMIDIPort::Open(const FString& DeviceName, const int32 QueueSize, const int32 InFilterMask, const int32 InChannelMask, FString& OutError)
{
	check(Stream == nullptr);

	if (!OscuMIDI::EnsureInitialised(OutError))
	{
		return false;
	}

	TArray<FOscuMIDIDeviceInfo> Devices;
	OscuMIDI::EnumerateDevices(Devices);

	// By name, never by enumeration order. Device indices move when anything is
	// plugged in, and taking whatever is first is how you end up reading the wrong box.
	const FOscuMIDIDeviceInfo* Found = Devices.FindByPredicate(
		[&DeviceName](const FOscuMIDIDeviceInfo& Info)
		{
			return Info.bInput && Info.Name.Equals(DeviceName, ESearchCase::IgnoreCase);
		});

	if (Found == nullptr)
	{
		TArray<FString> Available;
		for (const FOscuMIDIDeviceInfo& Info : Devices)
		{
			if (Info.bInput)
			{
				Available.Add(Info.Name);
			}
		}
		OutError = FString::Printf(TEXT("not found. Available inputs: %s"),
			Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("(none)"));
		return false;
	}

	if (Found->bOpenedByUs)
	{
		OutError = TEXT("is already open in this session. It is probably listed twice in settings.");
		return false;
	}

	PmStream* Opened = nullptr;
	const PmError Error = Pm_OpenInput(&Opened, Found->DeviceID, nullptr, QueueSize, nullptr, nullptr);
	if (Error != pmNoError || Opened == nullptr)
	{
		// The likeliest cause by far is another application holding the port. Whether
		// that is even possible depends on the driver: an Elektron TM-1 was verified
		// open in TouchDesigner and here at the same time, both receiving, while plenty
		// of Windows MIDI inputs refuse a second client outright. Either way PortMidi's
		// own "in use" flag only ever sees this process, so a conflict cannot be
		// detected before trying -- hence reporting it here rather than checking first.
		OutError = FString::Printf(TEXT("could not be opened (%s). Another application may be holding it."),
			ANSI_TO_TCHAR(Pm_GetErrorText(Error)));
		return false;
	}

	Stream = Opened;
	Name = DeviceName;
	DeviceId = FName(*DeviceName);
	DeviceID = Found->DeviceID;

	// Both of these must happen before the first read. A failure is reported but not
	// fatal: a device carrying more than it should is still better than no device.
	FilterMask = InFilterMask;
	const PmError FilterError = Pm_SetFilter(Opened, FilterMask);
	if (FilterError != pmNoError)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("MIDI input '%s': the message filter would not apply (%s)."),
			*Name, ANSI_TO_TCHAR(Pm_GetErrorText(FilterError)));
		FilterMask = 0;
	}

	ChannelMask = InChannelMask;
	const PmError ChannelError = Pm_SetChannelMask(Opened, ChannelMask);
	if (ChannelError != pmNoError)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("MIDI input '%s': the channel mask would not apply (%s)."),
			*Name, ANSI_TO_TCHAR(Pm_GetErrorText(ChannelError)));
		ChannelMask = 0xFFFF;
	}

	// Remembered so Learn can lift the mask and put back THIS device's own one. Each
	// port carries a different mask now, so there is no single value to recompute from.
	ConfiguredChannelMask = ChannelMask;

	return true;
}

void FOscuMIDIPort::Close()
{
	if (Stream == nullptr)
	{
		return;
	}

	const PmError Error = Pm_Close(static_cast<PmStream*>(Stream));
	if (Error != pmNoError)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("MIDI input '%s' did not close cleanly (%s)."),
			*Name, ANSI_TO_TCHAR(Pm_GetErrorText(Error)));
	}

	Stream = nullptr;
}

void FOscuMIDIPort::SetChannelMask(const int32 InChannelMask)
{
	if (Stream == nullptr)
	{
		return;
	}

	const PmError Error = Pm_SetChannelMask(static_cast<PmStream*>(Stream), InChannelMask);
	if (Error == pmNoError)
	{
		ChannelMask = InChannelMask;
	}
}

void FOscuMIDIPort::RestoreChannelMask()
{
	SetChannelMask(ConfiguredChannelMask);
}

int32 FOscuMIDIPort::Drain(const TFunctionRef<void(const FOscuMIDIMessage&)>& OnMessage)
{
	if (Stream == nullptr)
	{
		return 0;
	}

	// Read in batches rather than allocating per frame, and keep going until PortMidi
	// has nothing left: a batch smaller than the queue simply means more than one pass.
	constexpr int32 BatchSize = 256;
	PmEvent Batch[BatchSize];

	int32 Consumed = 0;

	for (;;)
	{
		const int32 Count = Pm_Read(static_cast<PmStream*>(Stream), Batch, BatchSize);

		if (Count < 0)
		{
			const PmError Error = static_cast<PmError>(Count);
			if (Error == pmBufferOverflow)
			{
				// Self-recovering by design: PortMidi has already discarded the queue
				// and resumes with the next message to arrive. Said out loud anyway,
				// because what it discarded may have included a cue -- and unlike the
				// engine's version of this message, this one says what to do about it.
				++Overflows;
				UE_LOG(LogOSCulator, Warning,
					TEXT("MIDI input '%s' overflowed its %d-message queue and was flushed; messages were lost. Raise the queue size, or narrow this device's own Listen For or Ignored Channels, in Project Settings > Plugins > OSCulator."),
					*Name, UOscuSettings::Get()->MIDIInputQueueSize);
			}
			else
			{
				UE_LOG(LogOSCulator, Warning, TEXT("MIDI input '%s' read error: %s"),
					*Name, ANSI_TO_TCHAR(Pm_GetErrorText(Error)));
			}
			break;
		}

		if (Count == 0)
		{
			break;
		}

		Consumed += Count;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const PmMessage Message = Batch[Index].message;
			const int32 Status = Pm_MessageStatus(Message);
			const int32 Data1 = Pm_MessageData1(Message);
			const int32 Data2 = Pm_MessageData2(Message);

			FOscuMIDIMessage Decoded;

			// 1-16, matching the map asset -- and the same convention UE's plugin
			// reported, so nothing downstream of here had to change.
			Decoded.Channel = (Status & 0x0F) + 1;
			Decoded.Number = Data1;
			Decoded.Value = Data2;

			switch (Status & 0xF0)
			{
			case 0x90:
				Decoded.Type = EOscuMIDIInputType::Note;

				// A note on with zero velocity is a note off. Every sequencer does
				// this, and reading it as a note on leaves the note stuck forever.
				Decoded.bNoteOn = Data2 > 0;
				OnMessage(Decoded);
				break;

			case 0x80:
				Decoded.Type = EOscuMIDIInputType::Note;
				Decoded.bNoteOn = false;
				OnMessage(Decoded);
				break;

			case 0xB0:
				Decoded.Type = EOscuMIDIInputType::ControlChange;
				OnMessage(Decoded);
				break;

			case 0xC0:
				Decoded.Type = EOscuMIDIInputType::ProgramChange;

				// One data byte, not two. Data2 is undefined on the wire for this
				// status, so the program number is carried as BOTH the number a source
				// matches on and the value a function receives -- there is nothing else
				// to put there, and a function that wants to know which program arrived
				// should not have to ask for it through a separate setting.
				Decoded.Value = Data1;
				OnMessage(Decoded);
				break;

			default:
				// Survived the filter but is not something OSCulator binds yet.
				++UnusedMessages;
				break;
			}
		}

		if (Count < BatchSize)
		{
			break;
		}
	}

	return Consumed;
}
