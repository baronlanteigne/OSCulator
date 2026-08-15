// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OscuMIDIMap.generated.h"

/** What a note's velocity becomes on the way to the function. */
UENUM()
enum class EOscuMIDIValueMode : uint8
{
	/** Velocity as it arrives, 0-127. */
	RawVelocity,

	/** Velocity / 127. The default, because everything else in this pipeline is 0-1. */
	Normalized01,

	/** Two arguments: the note number, then the normalised velocity. */
	NoteAndVelocity
};

/** One note on one channel, and what it calls. */
USTRUCT()
struct OSCULATORMIDI_API FOscuMIDINoteMap
{
	GENERATED_BODY()

	/** "C3", "C#2", "Db2", or a bare "61". Normalised to sharps when edited. */
	UPROPERTY(EditAnywhere, Category = "Note")
	FString Note = TEXT("C3");

	/**
	 * What Note actually resolved to. Always visible, never editable.
	 *
	 * Note 60 is C3 in Ableton and C4 in scientific pitch notation, so a name alone
	 * is ambiguous. Showing the number next to it removes the ambiguity without
	 * making anyone go and check the setting.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Note")
	uint8 ResolvedNote = 60;

	/** The function to call on every actor carrying this channel's tag. */
	UPROPERTY(EditAnywhere, Category = "Note")
	FName FunctionName;

	UPROPERTY(EditAnywhere, Category = "Note")
	EOscuMIDIValueMode Mode = EOscuMIDIValueMode::Normalized01;

	/** Also fire on note off, with a velocity of zero. */
	UPROPERTY(EditAnywhere, Category = "Note")
	bool bFireOnNoteOff = false;

	/**
	 * Tick to arm: the next note played is written into this row.
	 *
	 * Nobody types note numbers when they can hit a pad, so this is what actually
	 * gets used -- the Note string is mostly there to keep the asset readable and
	 * diffable. Only one row can be armed at a time, and it disarms itself once a
	 * note lands.
	 *
	 * Transient, so an armed row is never saved in that state.
	 */
	UPROPERTY(EditAnywhere, Transient, Category = "Note")
	bool bLearn = false;
};

/** One MIDI channel, bound to one tag. */
USTRUCT()
struct OSCULATORMIDI_API FOscuMIDIChannelMap
{
	GENERATED_BODY()

	/**
	 * 1-16, as every DAW displays it.
	 *
	 * The MIDI wire protocol packs channel into the status byte as 0-15, but UE's
	 * MIDIDevice plugin already converts -- MIDIDeviceInputController.cpp computes
	 * `(Status % 16) + 1` before broadcasting. So this needs NO conversion at
	 * ingest; the delivered channel and this field are the same numbering.
	 */
	UPROPERTY(EditAnywhere, Category = "Channel", meta = (ClampMin = "1", ClampMax = "16"))
	uint8 Channel = 1;

	/** Tag with the prefix already stripped: "laser", from an actor tagged OSC_laser. */
	UPROPERTY(EditAnywhere, Category = "Channel")
	FName Tag;

	UPROPERTY(EditAnywhere, Category = "Channel",
		meta = (TitleProperty = "{Note} ({ResolvedNote}) -> {FunctionName}"))
	TArray<FOscuMIDINoteMap> Notes;
};

/** What a channel/note lookup found. */
struct FOscuMIDIMatch
{
	FName Tag;
	const FOscuMIDINoteMap* Note = nullptr;

	bool IsValid() const { return Note != nullptr; }
};

/**
 * Channel and note to tag and function.
 *
 * A DataAsset rather than settings, so different shows get different maps without
 * recompiling, and so the map is versioned and diffable as content. Which map is
 * ACTIVE lives in Project Settings; what it contains lives here.
 *
 * Anything not in this map is ignored by OSCulator entirely and passes through
 * untouched -- other incoming MIDI is yours to handle however you like elsewhere.
 */
UCLASS(BlueprintType, meta = (DisplayName = "OSCulator MIDI Map"))
class OSCULATORMIDI_API UOscuMIDIMap : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "OSCulator",
		meta = (TitleProperty = "Ch {Channel} -> {Tag}"))
	TArray<FOscuMIDIChannelMap> Channels;

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Re-reads every Note string into ResolvedNote and rebuilds the flat lookup. */
	void Refresh();

	/** O(1) by construction: the flat lookup is built once, not searched per note. */
	FOscuMIDIMatch FindMapping(uint8 Channel, uint8 Note) const;

	/** True if any entry claims this channel and note. Used by auto-populate. */
	bool IsNoteTaken(uint8 Channel, uint8 Note) const;

	/**
	 * Adds any of these functions not already mapped under this tag, each at the
	 * next free note from 36 upwards. Returns how many rows were added.
	 *
	 * Two rules make this safe, and it is genuinely unsafe without them:
	 *
	 *  - Functions are merged in ALPHABETICAL order. A class's function map does
	 *    not iterate stably across Blueprint recompiles, so an order taken from
	 *    reflection would reshuffle note assignments every time you recompiled.
	 *  - It is ADDITIVE ONLY. An existing row is never moved, renumbered or
	 *    rewritten; only genuinely new functions are appended to unused notes.
	 *
	 * Without both, recompiling a Blueprint silently rewrites the whole mapping and
	 * you find out mid-show.
	 */
	int32 MergeFunctions(FName Tag, const TArray<FName>& FunctionNames);

#if WITH_EDITOR
	/**
	 * Merges every Blueprint-authored function of every tagged actor in a level.
	 * Returns how many rows were added. Additive only, as above.
	 */
	int32 AutoPopulateFromWorld(UWorld* World);

	/**
	 * The button in the details panel. Scaffolding for getting started -- it runs
	 * only when clicked, and your own mapping decisions always win.
	 */
	UFUNCTION(CallInEditor, Category = "OSCulator", meta = (DisplayName = "Auto-Populate From Level"))
	void AutoPopulateFromLevel();

	/** Points the MIDI subsystem at whichever row has bLearn ticked, if any. */
	void UpdateLearnArming();
#endif

private:
	void ResolveNoteNames();
	void RebuildLookup();

	/** Indices rather than pointers, because the arrays reallocate when edited. */
	struct FLookupEntry
	{
		int32 ChannelIndex = INDEX_NONE;
		int32 NoteIndex = INDEX_NONE;
	};

	/** Keyed (Channel << 8) | Note. Not serialised; rebuilt on load and on edit. */
	TMap<uint16, FLookupEntry> Lookup;
};
