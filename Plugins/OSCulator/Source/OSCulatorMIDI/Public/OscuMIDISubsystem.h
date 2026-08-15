// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "OscuMIDIMap.h"
#include "Subsystems/EngineSubsystem.h"
#include "OscuMIDISubsystem.generated.h"

class UMIDIDeviceInputController;
class UMIDIDeviceOutputController;
class UWorld;

/** One opened output device, paired with the name it was configured under. */
USTRUCT()
struct FOscuMIDIOutput
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMIDIDeviceOutputController> Controller = nullptr;

	UPROPERTY()
	FName Name;
};

/**
 * A note waiting to be released.
 *
 * These live at engine level rather than on a world timer, so that stopping PIE
 * partway through a note still releases it. A note left on is a stuck note on real
 * hardware, which is the kind of thing that ruins a show and cannot be fixed from
 * inside Unreal.
 */
struct FOscuPendingNoteOff
{
	int32 Channel = 1;
	int32 Note = 0;
	FName Device;
	double DueTime = 0.0;
};

/**
 * Owns the MIDI input devices and turns notes into router calls.
 *
 * An engine subsystem rather than a world one, for two reasons. MIDI devices are
 * global hardware, and opening or closing them on every PIE start would be both
 * slow and a good way to lose a race with another application for the port. And
 * Learn has to work at edit time, where no game world exists.
 *
 * Dispatch is still gated to a playing world -- notes arriving in the editor go
 * nowhere unless Learn has claimed them.
 *
 * This is a mapping layer, not a transport. It builds an FOscuMessage and hands it
 * to the same router OSC uses, so it adds no dispatch code of its own.
 */
UCLASS()
class OSCULATORMIDI_API UOscuMIDISubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UOscuMIDISubsystem* Get();

	/** Closes whatever is open, then re-reads settings and opens again. */
	void Restart();

	bool IsOpen() const { return Controllers.Num() > 0; }
	int32 GetOpenDeviceCount() const { return Controllers.Num(); }
	UOscuMIDIMap* GetActiveMap() const { return ActiveMap; }

	/** Overrides the configured map. For tests and for Learn. */
	void SetActiveMap(UOscuMIDIMap* Map) { ActiveMap = Map; }

	uint64 GetNotesReceived() const { return NotesReceived; }
	uint64 GetNotesDispatched() const { return NotesDispatched; }
	uint64 GetNotesUnmapped() const { return NotesUnmapped; }

	/**
	 * The whole ingest path, minus the hardware.
	 *
	 * Public so a test can drive it without a physical controller -- everything
	 * from here down is what a real note does.
	 *
	 * Channel is 1-16, matching both the asset and what MIDIDevice delivers.
	 * Returns how many actors were called.
	 */
	int32 IngestNote(int32 Channel, int32 Note, int32 Velocity, bool bNoteOn);

	UFUNCTION()
	void HandleNoteOn(UMIDIDeviceInputController* Controller, int32 Timestamp, int32 Channel, int32 Note, int32 Velocity);

	UFUNCTION()
	void HandleNoteOff(UMIDIDeviceInputController* Controller, int32 Timestamp, int32 Channel, int32 Note, int32 Velocity);

	// ---- Output ----

	/**
	 * UE's MIDIDevice plugin disagrees with itself: its input controller reports
	 * channels as 1-16, but its output controller ORs the channel straight into the
	 * status byte and so wants 0-15. OSCulator is 1-16 everywhere -- matching the
	 * map asset and every DAW -- so the subtraction happens here and nowhere else.
	 */
	static int32 ToWireChannel(int32 Channel);

	/** Returns how many devices the message went to. Device None means all of them. */
	int32 SendNoteOn(int32 Channel, int32 Note, int32 Velocity, FName Device = NAME_None);
	int32 SendNoteOff(int32 Channel, int32 Note, FName Device = NAME_None);
	int32 SendControlChange(int32 Channel, int32 ControlNumber, int32 Value, FName Device = NAME_None);

	/**
	 * Note on now, note off after DurationSeconds.
	 *
	 * A duration of zero or less sends the note on and schedules nothing, leaving
	 * the release to the caller.
	 */
	int32 SendNote(int32 Channel, int32 Note, int32 Velocity, float DurationSeconds, FName Device = NAME_None);

	/** Sends every outstanding note off immediately. Called on shutdown and on PIE end. */
	void FlushPendingNoteOffs();

	int32 GetPendingNoteOffCount() const { return PendingNoteOffs.Num(); }
	int32 GetOpenOutputCount() const { return Outputs.Num(); }

#if WITH_EDITOR
	/**
	 * Redirects the next note into a map row instead of dispatching it.
	 *
	 * This is the one thing OSCulator does at edit time. Nothing else here runs
	 * outside play -- Learn only works because the devices are already open, so it
	 * is a redirection rather than a second listener.
	 */
	void ArmLearn(UOscuMIDIMap* Map, int32 ChannelIndex, int32 NoteIndex);

	/** Disarms, but only if this map is the one currently armed. */
	void CancelLearn(const UOscuMIDIMap* Map);

	bool IsLearning() const { return LearnMap.IsValid(); }
#endif

private:
	void OpenDevices();
	void CloseDevices();

	/** The playing world, or null when only the editor is up. */
	UWorld* FindDispatchWorld() const;

	void OpenOutputs();
	void CloseOutputs();

	/** Drains due note-offs. Engine-level, so it survives PIE stopping. */
	bool Tick(float DeltaTime);

	UPROPERTY()
	TArray<TObjectPtr<UMIDIDeviceInputController>> Controllers;

	UPROPERTY()
	TArray<FOscuMIDIOutput> Outputs;

	TArray<FOscuPendingNoteOff> PendingNoteOffs;
	FTSTicker::FDelegateHandle TickerHandle;

	UPROPERTY()
	TObjectPtr<UOscuMIDIMap> ActiveMap;

	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle SettingsChangedHandle;

	/** The first note is logged in full, to confirm the channel numbering against
	 *  real hardware. Ten seconds of noise against an hour of "why does nothing
	 *  trigger". */
	bool bLoggedFirstNote = false;

	uint64 NotesReceived = 0;
	uint64 NotesDispatched = 0;
	uint64 NotesUnmapped = 0;

#if WITH_EDITOR
	/** Writes the note into the armed row. True if it consumed the note. */
	bool ApplyLearn(int32 Channel, int32 Note);

	/** Weak: the asset can be closed or reimported while a row sits armed. */
	TWeakObjectPtr<UOscuMIDIMap> LearnMap;
	int32 LearnChannelIndex = INDEX_NONE;
	int32 LearnNoteIndex = INDEX_NONE;
#endif
};
