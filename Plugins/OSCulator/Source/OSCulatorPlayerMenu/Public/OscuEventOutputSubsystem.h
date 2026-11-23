// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OscuPlayerMappings.h"
#include "OscuValue.h"
#include "Subsystems/WorldSubsystem.h"
#include "OscuEventOutputSubsystem.generated.h"

class AActor;
class UOscuEventOutputSubsystem;
struct FOscuEventOutput;

/**
 * Stands in for one Blueprint listener on one event dispatcher of one actor.
 *
 * A dispatcher can only call a function with its own signature, and the signatures are
 * whatever the project's Blueprints declare. So this binds a parameterless OnEvent and
 * catches the call in ProcessEvent, where the engine hands over the dispatcher's
 * parameter block untouched; the dispatcher's signature function says how to read it.
 */
UCLASS(Transient)
class OSCULATORPLAYERMENU_API UOscuEventListener : public UObject
{
	GENERATED_BODY()

public:
	/** Never runs: ProcessEvent intercepts it. It exists so the delegate has a function to name. */
	UFUNCTION()
	void OnEvent() {}

	virtual void ProcessEvent(UFunction* Function, void* Parms) override;

	TWeakObjectPtr<UOscuEventOutputSubsystem> Owner;
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UFunction> Signature;
	FName Dispatcher;
	int32 OutputIndex = INDEX_NONE;
};

/** Everything one event sent (or would have sent, with that output's transport off). */
struct FOscuEventSend
{
	int32 OutputIndex = INDEX_NONE;

	bool bOSC = false;
	FOscuMessage Message;

	/** Note, ControlChange, or None for no MIDI. */
	EOscuPlayerMIDIKind MIDIKind = EOscuPlayerMIDIKind::None;
	int32 MIDIChannel = 1;
	int32 MIDINumber = 0;
	int32 MIDIValue = 0;
	float NoteLength = 0.1f;

	/** "/laser/OnHit 0.5  ·  Ch 1 · CC 21 = 64", for the menu's status line. */
	FString Describe() const;
};

/**
 * Event Outputs: binds every listed event dispatcher on the tagged actors in a game
 * world and sends OSC and/or MIDI whenever a Blueprint calls one.
 *
 * Settings > Event Outputs says what each sends by default; the player menu can
 * re-point the address and the MIDI message, and the OSC target and MIDI devices. Lives
 * in the player menu's module because it needs both transports, but it runs whether or
 * not the menu is enabled.
 */
UCLASS()
class OSCULATORPLAYERMENU_API UOscuEventOutputSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	static UOscuEventOutputSubsystem* Get(const UWorld* World);

	/** Unbinds everything and binds again from the settings. */
	void Rebind();

	/** Dispatcher bindings currently held, across all outputs and actors. */
	int32 GetBoundCount() const { return Listeners.Num(); }

	/** Called by a listener when its dispatcher fires. */
	void HandleEvent(int32 OutputIndex, const UFunction* Signature, const void* Parms);

	/**
	 * Sends one output as if its dispatcher fired with its first number at Value Max (or
	 * with no parameters, if no tagged actor is there to say what they are). For the
	 * menu's "test". Returns what was sent.
	 */
	FOscuEventSend SendTest(int32 OutputIndex);

	/** The last thing sent, transports enabled or not. For tests and the menu. */
	const FOscuEventSend& GetLastSend() const { return LastSend; }
	int32 GetSendCount() const { return SendCount; }

	/** The MIDI message an output sends now: the player's choice if there is one, else the project's. */
	void ResolveMIDI(int32 OutputIndex, EOscuPlayerMIDIKind& OutKind, int32& OutChannel, int32& OutNumber, bool& bOutCustom) const;

	/** The OSC address an output sends to now, and whether it sends OSC at all. */
	FString ResolveOSC(int32 OutputIndex, bool& bOutSend, bool& bOutCustom) const;

	/** The MIDI value 0-127 for a number, through the output's Value Min / Value Max. */
	static int32 ScaleToMIDI(const FOscuEventOutput& Output, double Value);

	/**
	 * Reads what a player typed for an output's MIDI message: "cc 21", "ch 2 cc 21",
	 * "note C3", "note 60", "C3", "21" (keeps the current kind, else CC), or "off".
	 * Channel and number not given are kept from InOut*. False with a reason if unreadable.
	 */
	static bool ParseMIDIOut(const FString& Text, EOscuPlayerMIDIKind& InOutKind, int32& InOutChannel, int32& InOutNumber, FString& OutError);

	/** "Ch 1 · CC 21", "Ch 1 · C3", or "off". */
	static FString DescribeMIDIOut(EOscuPlayerMIDIKind Kind, int32 Channel, int32 Number);

private:
	void BindActor(AActor* Actor);
	void UnbindAll();
	void HandleActorSpawned(AActor* Actor);
	void RefreshOverrides();
	void Send(FOscuEventSend& Send);

	UPROPERTY()
	TArray<TObjectPtr<UOscuEventListener>> Listeners;

	/** Player overrides per output index, refreshed when they or the settings change. */
	TMap<int32, FOscuPlayerOverride> ActiveOverrides;

	/** Outputs already warned about (no such dispatcher on a tagged class). */
	TSet<FString> Warned;

	FOscuEventSend LastSend;
	int32 SendCount = 0;

	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle MappingsChangedHandle;
	FDelegateHandle ActorSpawnedHandle;
};
