// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "OscuMIDIMap.h"
#include "OscuMIDIPort.h"
#include "Subsystems/EngineSubsystem.h"
#include "OscuMIDISubsystem.generated.h"

class UMIDIDeviceOutputController;
class UOscuRouterSubsystem;
class UWorld;
struct FMIDIDeviceInfo;

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

	bool IsOpen() const { return Ports.Num() > 0; }
	int32 GetOpenDeviceCount() const { return Ports.Num(); }

	/** The open input ports, for the status command to describe. */
	const TArray<FOscuMIDIPort>& GetPorts() const { return Ports; }
	/**
	 * Every loaded map, all consulted on every message.
	 *
	 * A list because how the mapping is split is the author's choice -- one asset per
	 * device, one per show, or one for everything -- and none of those should be a
	 * different code path here.
	 */
	const TArray<TObjectPtr<UOscuMIDIMap>>& GetActiveMaps() const { return ActiveMaps; }

	/** Replaces the loaded set. For tests and for Learn. */
	void SetActiveMaps(TArray<TObjectPtr<UOscuMIDIMap>> Maps) { ActiveMaps = MoveTemp(Maps); }

	/** The one-map convenience over SetActiveMaps. */
	void SetActiveMap(UOscuMIDIMap* Map);

	uint64 GetMessagesReceived() const { return MessagesReceived; }
	uint64 GetMessagesDispatched() const { return MessagesDispatched; }
	uint64 GetMessagesUnmapped() const { return MessagesUnmapped; }

	/**
	 * The whole ingest path, minus the hardware.
	 *
	 * Public so a test can drive it without a physical controller -- everything from
	 * here down is what a real message does. Returns how many actors were called.
	 *
	 * One message may fire several bindings: sharing an input between two functions on
	 * two different actors is a feature, not an authoring mistake.
	 */
	int32 IngestMessage(FName Device, const FOscuMIDIMessage& Message);

	/** Channel is 1-16. Convenience over IngestMessage, for tests and for callers. */
	int32 IngestNote(int32 Channel, int32 Note, int32 Velocity, bool bNoteOn, FName Device = NAME_None);
	int32 IngestControlChange(int32 Channel, int32 ControlNumber, int32 Value, FName Device = NAME_None);

	/**
	 * A program change carries one number and nothing else.
	 *
	 * No value parameter, because there is no second data byte to supply: the program
	 * number is both what a source matches on and what a function receives.
	 */
	int32 IngestProgramChange(int32 Channel, int32 ProgramNumber, FName Device = NAME_None);

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
	void ArmLearn(UOscuMIDIMap* Map, int32 BindingIndex, int32 SourceIndex);

	/** Disarms, but only if this map is the one currently armed. */
	void CancelLearn(const UOscuMIDIMap* Map);

	bool IsLearning() const { return LearnMap.IsValid(); }
#endif

private:
	void OpenDevices();
	void CloseDevices();

	/** Reads every open port and ingests what it finds. Called from Tick. */
	void DrainPorts();

	/** The playing world, or null when only the editor is up. */
	UWorld* FindDispatchWorld() const;

	/**
	 * Takes an already-enumerated list, because UMIDIDeviceManager::FindAllMIDIDeviceInfo
	 * is not a read: it terminates the engine's PortMidi copy, brings it back, and
	 * restarts every controller it can still reach through TObjectIterator. Calling it
	 * once per restart rather than once per half keeps that churn to a minimum. Input
	 * no longer goes through it at all.
	 */
	void OpenOutputs(const TArray<FMIDIDeviceInfo>& OutputDevices);
	void CloseOutputs();

	/** Drains due note-offs. Engine-level, so it survives PIE stopping. */
	bool Tick(float DeltaTime);

	/**
	 * Plain structs, not UObjects: a port is a PortMidi stream and a name, with no
	 * reason to be garbage collected or replicated. They are closed explicitly in
	 * CloseDevices and in Deinitialize.
	 */
	TArray<FOscuMIDIPort> Ports;

	UPROPERTY()
	TArray<FOscuMIDIOutput> Outputs;

	TArray<FOscuPendingNoteOff> PendingNoteOffs;
	FTSTicker::FDelegateHandle TickerHandle;

	UPROPERTY()
	TArray<TObjectPtr<UOscuMIDIMap>> ActiveMaps;

	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle SettingsChangedHandle;

	/** The first message is logged in full, to confirm the channel numbering against
	 *  real hardware. Ten seconds of noise against an hour of "why does nothing
	 *  trigger". */
	bool bLoggedFirstMessage = false;

	uint64 MessagesReceived = 0;
	uint64 MessagesDispatched = 0;
	uint64 MessagesUnmapped = 0;

	/**
	 * Says once that a named parameter does not resolve, then stops.
	 *
	 * A name that matches nothing matches nothing on every message, so the unthrottled
	 * version of this warning is a wall of identical lines at the message rate -- which
	 * buries the one line that mattered and costs more than the dispatch it is
	 * complaining about. Keyed by tag, function and parameter name, so three different
	 * mistakes still produce three different lines.
	 */
	void WarnUnresolvedParam(const FOscuMIDIBinding& Binding, FName ParamName, const TCHAR* Role);

	TSet<uint32> WarnedUnresolvedParams;

	/**
	 * Resolved parameter slots, when they are allowed to be remembered.
	 *
	 * Only consulted while UOscuSettings::ShouldCacheSignatureFacts() is true -- a
	 * cooked build, or Performance Mode. The reason the slot is normally re-derived per
	 * message is that a Blueprint could be recompiled underneath us and reorder its
	 * parameters; where that cannot happen, re-deriving it was only ever waste.
	 *
	 * Keyed by tag, function and parameter name, and cleared on Restart -- which a
	 * settings change triggers, so flipping Performance Mode cannot leave a stale answer
	 * behind.
	 */
	TMap<uint32, int32> ParamSlotCache;

	/** The slot for a named parameter, from the cache when caching is allowed. */
	int32 ResolveParamSlot(const UOscuRouterSubsystem& Router, const FOscuMIDIBinding& Binding, FName ParamName);

#if WITH_EDITOR
	/** Writes the input into the armed source. True if it consumed the message. */
	bool ApplyLearn(FName Device, const FOscuMIDIMessage& Message);

	/** Weak: the asset can be closed or reimported while a row sits armed. */
	TWeakObjectPtr<UOscuMIDIMap> LearnMap;
	int32 LearnBindingIndex = INDEX_NONE;
	int32 LearnSourceIndex = INDEX_NONE;
#endif
};
