// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDILibrary.h"

#include "OSCulatorCore.h"
#include "OscuMIDINoteName.h"
#include "OscuMIDISubsystem.h"
#include "OscuSettings.h"

const FName UOscuMIDILibrary::AllDevices(TEXT("(all)"));

namespace
{
	/** The dropdown carries a literal "(all)"; the subsystem wants None for that. */
	FName ResolveDevice(FName Device)
	{
		return Device == UOscuMIDILibrary::AllDevices ? NAME_None : Device;
	}
}

TArray<FString> UOscuMIDILibrary::GetMIDIOutputDeviceOptions()
{
	TArray<FString> Options;
	Options.Add(AllDevices.ToString());

	for (const FString& DeviceName : UOscuSettings::Get()->MIDIOutputDeviceNames)
	{
		Options.AddUnique(DeviceName);
	}

	return Options;
}

int32 UOscuMIDILibrary::SendMIDINote(FName Device, int32 Channel, int32 Note, int32 Velocity, float DurationSeconds)
{
	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	return MIDI != nullptr ? MIDI->SendNote(Channel, Note, Velocity, DurationSeconds, ResolveDevice(Device)) : 0;
}

int32 UOscuMIDILibrary::SendMIDINoteOn(FName Device, int32 Channel, int32 Note, int32 Velocity)
{
	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	return MIDI != nullptr ? MIDI->SendNoteOn(Channel, Note, Velocity, ResolveDevice(Device)) : 0;
}

int32 UOscuMIDILibrary::SendMIDINoteOff(FName Device, int32 Channel, int32 Note)
{
	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	return MIDI != nullptr ? MIDI->SendNoteOff(Channel, Note, ResolveDevice(Device)) : 0;
}

int32 UOscuMIDILibrary::SendMIDIControlChange(FName Device, int32 Channel, int32 ControlNumber, int32 Value)
{
	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	return MIDI != nullptr ? MIDI->SendControlChange(Channel, ControlNumber, Value, ResolveDevice(Device)) : 0;
}

void UOscuMIDILibrary::ReleaseAllMIDINotes()
{
	if (UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get())
	{
		MIDI->FlushPendingNoteOffs();
	}
}

bool UOscuMIDILibrary::IsMIDIOutputReady()
{
	const UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	return MIDI != nullptr && MIDI->GetOpenOutputCount() > 0;
}

int32 UOscuMIDILibrary::GetPendingMIDINoteOffCount()
{
	const UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	return MIDI != nullptr ? MIDI->GetPendingNoteOffCount() : 0;
}

int32 UOscuMIDILibrary::MIDINoteFromName(const FString& NoteName)
{
	uint8 Note = 0;
	FString Error;
	if (!OscuMIDINoteName::Parse(NoteName, UOscuSettings::Get()->MiddleCOctave, Note, Error))
	{
		UE_LOG(LogOSCulator, Warning, TEXT("'%s' is not a usable note name: %s"), *NoteName, *Error);
		return INDEX_NONE;
	}
	return Note;
}

FString UOscuMIDILibrary::MIDINoteToName(int32 Note)
{
	if (Note < 0 || Note > 127)
	{
		return FString();
	}
	return OscuMIDINoteName::ToString(static_cast<uint8>(Note), UOscuSettings::Get()->MiddleCOctave);
}
