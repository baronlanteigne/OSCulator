// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OscuMIDILibrary.generated.h"

/**
 * The Blueprint face of MIDI output.
 *
 * Channels are 1-16 throughout, matching the map asset and every DAW. No node here
 * needs a world context: MIDI devices are engine-level, so these work in any
 * Blueprint without a Self pin.
 */
UCLASS()
class OSCULATORMIDI_API UOscuMIDILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Note on now, note off automatically after Duration Seconds.
	 *
	 * One node rather than two calls with a Delay between them. The release is
	 * scheduled at engine level, so it still fires if play stops first -- a note
	 * left on is a stuck note on real hardware, and nothing inside Unreal can
	 * silence it afterwards.
	 *
	 * A duration of zero or less sends the note on and schedules nothing, leaving
	 * the release to you.
	 *
	 * Returns how many devices took it. Leave Device empty for all of them.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|MIDI", meta = (DisplayName = "Send MIDI Note"))
	static int32 SendMIDINote(
		UPARAM(meta = (GetOptions = "GetMIDIOutputDeviceOptions")) FName Device,
		int32 Channel = 1, int32 Note = 60, int32 Velocity = 127, float DurationSeconds = 0.1f);

	UFUNCTION(BlueprintCallable, Category = "OSCulator|MIDI", meta = (DisplayName = "Send MIDI Note On"))
	static int32 SendMIDINoteOn(
		UPARAM(meta = (GetOptions = "GetMIDIOutputDeviceOptions")) FName Device,
		int32 Channel = 1, int32 Note = 60, int32 Velocity = 127);

	UFUNCTION(BlueprintCallable, Category = "OSCulator|MIDI", meta = (DisplayName = "Send MIDI Note Off"))
	static int32 SendMIDINoteOff(
		UPARAM(meta = (GetOptions = "GetMIDIOutputDeviceOptions")) FName Device,
		int32 Channel = 1, int32 Note = 60);

	UFUNCTION(BlueprintCallable, Category = "OSCulator|MIDI", meta = (DisplayName = "Send MIDI Control Change"))
	static int32 SendMIDIControlChange(
		UPARAM(meta = (GetOptions = "GetMIDIOutputDeviceOptions")) FName Device,
		int32 Channel = 1, int32 ControlNumber = 1, int32 Value = 0);

	/**
	 * Fills the Device dropdown from Project Settings.
	 *
	 * The list is read live, so adding a device in settings updates the pin without
	 * a recompile. "(all)" means every open output.
	 */
	UFUNCTION()
	static TArray<FString> GetMIDIOutputDeviceOptions();

	/** The dropdown entry meaning "every open device". */
	static const FName AllDevices;

	/** Releases every note still waiting on its duration. The panic button. */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|MIDI", meta = (DisplayName = "Release All MIDI Notes"))
	static void ReleaseAllMIDINotes();

	UFUNCTION(BlueprintPure, Category = "OSCulator|MIDI")
	static bool IsMIDIOutputReady();

	UFUNCTION(BlueprintPure, Category = "OSCulator|MIDI")
	static int32 GetPendingMIDINoteOffCount();

	/**
	 * "C3", "C#2", "Db2" or "61" to a note number, using the project's Middle C
	 * Octave setting. Returns -1 if it cannot be read.
	 */
	UFUNCTION(BlueprintPure, Category = "OSCulator|MIDI", meta = (DisplayName = "MIDI Note From Name"))
	static int32 MIDINoteFromName(const FString& NoteName);

	/** The inverse, normalised to sharps: 61 becomes "C#3". */
	UFUNCTION(BlueprintPure, Category = "OSCulator|MIDI", meta = (DisplayName = "MIDI Note To Name"))
	static FString MIDINoteToName(int32 Note);
};
