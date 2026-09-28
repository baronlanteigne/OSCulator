// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OscuMIDIMap.generated.h"

/** What kind of MIDI message drives a binding. */
UENUM()
enum class EOscuMIDIInputType : uint8
{
	/** Note on, and optionally note off. Velocity is the value. */
	Note,

	/** Control change. The controller value is the value. */
	ControlChange UMETA(DisplayName = "Control Change")
};

/**
 * Whether a binding still points at something that exists in the open level.
 *
 * A plain enum, not a UENUM, and not a UPROPERTY: it is a judgement about one level at
 * one moment, and saving it into the asset would mean an asset claiming a row is broken
 * because you happen to have a different level open.
 */
enum class EOscuMIDIRowStatus : uint8
{
	/** Not looked at since this asset was loaded. */
	Unchecked,

	/** The function was found on at least one actor carrying this tag. */
	Found,

	/** Actors carry the tag, but none of them has this function. The prunable case. */
	FunctionMissing,

	/** Nothing in this level carries the tag at all, so the row cannot be judged. */
	TagMissing
};

/**
 * One thing that can fire a binding: a note or a controller, on a channel, from a
 * device.
 *
 * A binding may have several, so the same function can be reachable from a pad, a
 * knob, and a second controller at once without duplicating anything about the target.
 */
USTRUCT()
struct OSCULATORMIDI_API FOscuMIDISource
{
	GENERATED_BODY()

	/**
	 * Which device this must arrive from. Empty means any of them.
	 *
	 * Empty is the useful default: with one controller there is nothing to
	 * disambiguate, and typing a device name into every row would be busywork. Fill it
	 * in when two devices send the same note and you want only one of them.
	 */
	UPROPERTY(EditAnywhere, Category = "Source")
	FName Device;

	/** 1-16, as every DAW displays it. */
	UPROPERTY(EditAnywhere, Category = "Source", meta = (ClampMin = "1", ClampMax = "16"))
	uint8 Channel = 1;

	UPROPERTY(EditAnywhere, Category = "Source")
	EOscuMIDIInputType Type = EOscuMIDIInputType::Note;

	/** "C3", "C#2", "Db2", or a bare "61". Normalised to sharps when edited. */
	UPROPERTY(EditAnywhere, Category = "Source",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note", EditConditionHides))
	FString Note = TEXT("C3");

	/**
	 * What Note actually resolved to. Always visible, never editable.
	 *
	 * Note 60 is C3 in Ableton and C4 in scientific pitch notation, so a name alone is
	 * ambiguous. Showing the number next to it removes the ambiguity without making
	 * anyone go and check the setting.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Source",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note", EditConditionHides))
	uint8 ResolvedNote = 60;

	UPROPERTY(EditAnywhere, Category = "Source",
		meta = (EditCondition = "Type == EOscuMIDIInputType::ControlChange", EditConditionHides,
				ClampMin = "0", ClampMax = "127", DisplayName = "CC Number"))
	uint8 ControlNumber = 1;

	/**
	 * Tick to arm: the next thing you play or turn is written into this source.
	 *
	 * Nobody types note numbers when they can hit a pad. Only one source across the
	 * whole asset can be armed at a time, and it disarms itself once something lands.
	 * It captures the device and channel too, so one gesture fills the whole row.
	 *
	 * Transient, so an armed source is never saved in that state.
	 */
	UPROPERTY(EditAnywhere, Transient, Category = "Source")
	bool bLearn = false;

	/** This source in one line. Transient and derived; the array uses it as the title. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Source")
	FString Summary;

	/** The note number or the CC number, whichever this source is. */
	int32 GetNumber() const { return Type == EOscuMIDIInputType::Note ? ResolvedNote : ControlNumber; }

	/** The device this actually requires: its own, or the asset's default. */
	FName ResolveDevice(FName AssetDefault) const { return Device.IsNone() ? AssetDefault : Device; }
};

/**
 * One function on one tag, and everything that can fire it.
 *
 * Bindings are written target-first: you list what is controllable and then assign
 * inputs, rather than building MIDI scaffolding and hanging functions off it. That is
 * the order people actually work in -- and it removes a whole class of problem, because
 * a function that no longer exists is simply absent from the list rather than lurking
 * as a row that points nowhere.
 */
USTRUCT()
struct OSCULATORMIDI_API FOscuMIDIBinding
{
	GENERATED_BODY()

	/** Tag with the prefix already stripped: "laser", from an actor tagged OSC_laser. */
	UPROPERTY(EditAnywhere, Category = "Target")
	FName Tag;

	/** Called on every actor carrying the tag. */
	UPROPERTY(EditAnywhere, Category = "Target")
	FName FunctionName;

	/**
	 * Everything that fires this. None is legal and inert -- that is what auto-map
	 * produces before you have assigned anything, and an unassigned binding is how the
	 * asset stays a complete inventory of what could be controlled.
	 */
	UPROPERTY(EditAnywhere, Category = "Target", meta = (TitleProperty = "Summary"))
	TArray<FOscuMIDISource> Sources;

	/**
	 * Rescale the incoming 0-127 into OutMin..OutMax before calling.
	 *
	 * On by default, because 0-1 is what everything else in this pipeline speaks and a
	 * raw 0-127 landing in a function expecting a normalised float is a silent
	 * hundredfold error. Untick it to pass the value through as it arrived.
	 *
	 * On the binding rather than on each source, so a pad and a knob driving the same
	 * function agree about what its range means.
	 */
	UPROPERTY(EditAnywhere, Category = "Value")
	bool bRemap = true;

	/** The value sent for an incoming 0. May be greater than OutMax, which inverts. */
	UPROPERTY(EditAnywhere, Category = "Value", meta = (EditCondition = "bRemap", EditConditionHides))
	float OutMin = 0.0f;

	/** The value sent for an incoming 127. */
	UPROPERTY(EditAnywhere, Category = "Value", meta = (EditCondition = "bRemap", EditConditionHides))
	float OutMax = 1.0f;

	/**
	 * Send the note or CC number as a first argument, ahead of the value.
	 *
	 * For a function that handles several notes itself and needs to know which one
	 * arrived. The number is sent raw and is never remapped -- it is an identity, not a
	 * measurement.
	 */
	UPROPERTY(EditAnywhere, Category = "Value")
	bool bSendSourceNumber = false;

	/**
	 * Also fire when a note is released, with a value of 0 before remapping.
	 *
	 * Note sources only; control change has no release.
	 */
	UPROPERTY(EditAnywhere, Category = "Value")
	bool bFireOnNoteOff = false;

	/**
	 * What this binding does, in one line, including whether its target still exists.
	 * Transient because it is derived, and because half of it is only true of the level
	 * that was open when Validate last ran.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Target")
	FString Summary;

	/** Set by Validate. Not saved, and not shown except through Summary. */
	EOscuMIDIRowStatus Status = EOscuMIDIRowStatus::Unchecked;

	/** 0-127 in, shaped value out. */
	double ShapeValue(int32 Raw) const;
};

/**
 * How auto-map should assign inputs for one tag.
 *
 * Without this, auto-map can only list what is controllable; with it, it can also
 * assign. Mapping fifty functions by hand is the thing that makes a control surface not
 * worth building, so the point of this table is that you describe the layout once per
 * tag and let the rest follow.
 */
USTRUCT()
struct OSCULATORMIDI_API FOscuMIDIAutoMapRule
{
	GENERATED_BODY()

	/** Which tag this rule is for. */
	UPROPERTY(EditAnywhere, Category = "Rule")
	FName Tag;

	/** Written into every source this rule creates. Empty means any device. */
	UPROPERTY(EditAnywhere, Category = "Rule")
	FName Device;

	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "1", ClampMax = "16"))
	uint8 Channel = 1;

	/** Notes are handed out upward from here. 36 is the bottom-left pad on most grids. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "127"))
	uint8 FirstNote = 36;

	/** CC numbers are handed out upward from here. */
	UPROPERTY(EditAnywhere, Category = "Rule", meta = (ClampMin = "0", ClampMax = "127", DisplayName = "First CC"))
	uint8 FirstControl = 1;
};

/** One source that matched an incoming message, and the binding it belongs to. */
struct FOscuMIDIMatch
{
	const FOscuMIDIBinding* Binding = nullptr;
	const FOscuMIDISource* Source = nullptr;

	bool IsValid() const { return Binding != nullptr && Source != nullptr; }
};

/**
 * What is controllable, and what fires it.
 *
 * A DataAsset rather than settings, so different shows get different maps without
 * recompiling, and so the map is versioned and diffable as content. Which map is ACTIVE
 * lives in Project Settings; what it contains lives here.
 *
 * The asset is authoritative, never derived. A level populates and validates it, but
 * the list is stored -- otherwise opening a different level would silently change what
 * your show file appears to contain.
 *
 * Anything not bound here is ignored by OSCulator entirely and passes through
 * untouched, for other systems to interpret however they like.
 */
UCLASS(BlueprintType, meta = (DisplayName = "OSCulator MIDI Map"))
class OSCULATORMIDI_API UOscuMIDIMap : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The device every source in this asset means unless it says otherwise.
	 *
	 * The point of this is an asset per controller: name the hardware once here and
	 * every row below inherits it, instead of repeating it on every source. Leave it
	 * empty and an empty source device means any device, which is what a
	 * one-controller setup wants.
	 *
	 * Resolved live rather than written into the rows, so repointing an asset at a
	 * different controller is one edit.
	 */
	UPROPERTY(EditAnywhere, Category = "OSCulator")
	FName DefaultDevice;

	/** Per-tag layout for auto-map. Fill this in first; it is what makes auto-map assign. */
	UPROPERTY(EditAnywhere, Category = "OSCulator",
		meta = (TitleProperty = "{Tag} -> ch {Channel}"))
	TArray<FOscuMIDIAutoMapRule> AutoMapRules;

	UPROPERTY(EditAnywhere, Category = "OSCulator", meta = (TitleProperty = "Summary"))
	TArray<FOscuMIDIBinding> Bindings;

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Re-reads every note name, rebuilds the lookup, and refreshes every summary. */
	void Refresh();

	/**
	 * Every source that matches an incoming message.
	 *
	 * Plural, deliberately. One note firing two functions on two different actors is a
	 * thing people want, and the old channel-first structure treated it as an authoring
	 * error. Device is matched leniently: a source with no device accepts any.
	 */
	void FindMatches(FName Device, uint8 Channel, EOscuMIDIInputType Type, int32 Number, TArray<FOscuMIDIMatch>& OutMatches) const;

	/** True if any source already claims this input. Used by auto-map. */
	bool IsInputTaken(uint8 Channel, EOscuMIDIInputType Type, int32 Number) const;

	/** The binding for a tag and function, or null. */
	FOscuMIDIBinding* FindBinding(FName Tag, FName FunctionName);

	/**
	 * Adds a binding for every function not already present under this tag, and
	 * assigns an input to any unassigned binding if the tag has an auto-map rule.
	 *
	 * Returns how many bindings were added, and how many sources were assigned.
	 *
	 * Additive only, and that matters: a Blueprint class's function map does not
	 * iterate stably across recompiles, so anything that rewrote existing rows would
	 * reshuffle which pad triggers what every time you compiled. Existing bindings and
	 * existing sources are never touched.
	 *
	 * Note or CC is chosen from the signature -- a function that takes nothing is a
	 * trigger and gets a note; one whose first parameter is numeric is continuous and
	 * gets a controller. ParamTypeLabels carries the first parameter's type label per
	 * function, empty for none.
	 */
	int32 MergeFunctions(FName Tag, const TArray<FName>& FunctionNames, const TMap<FName, FString>& FirstParamTypes, int32* OutAssigned = nullptr);

	/** How a validation pass turned out. */
	struct FOscuMIDIValidation
	{
		int32 Found = 0;
		int32 FunctionMissing = 0;
		int32 TagMissing = 0;

		/** Bindings with no source at all. Not a fault -- just not assigned yet. */
		int32 Unassigned = 0;

		/** Functions the level exposes under a mapped tag that no binding claims. */
		TArray<FString> Unclaimed;

		/** Inputs claimed by more than one binding, which is legal but worth seeing. */
		TArray<FString> Shared;

		int32 Total() const { return Found + FunctionMissing + TagMissing; }
	};

	/**
	 * Checks every binding against what the level actually exposes, and says so.
	 *
	 * Reports; never edits.
	 */
	FOscuMIDIValidation Validate(UWorld* World);

#if WITH_EDITOR
	/** The details panel button for Validate. */
	UFUNCTION(CallInEditor, Category = "OSCulator", meta = (DisplayName = "Validate Against Level"))
	void ValidateAgainstLevel();

	/**
	 * Deletes every binding whose tag is present in the level but whose function is not.
	 *
	 * Runs a validation pass first, so it can never act on a stale judgement. Bindings
	 * whose TAG is missing are left alone however long they have been broken: a tag
	 * absent from this level is probably present in another one, and deleting a show's
	 * mappings because the wrong map was open is not a recoverable mistake.
	 */
	UFUNCTION(CallInEditor, Category = "OSCulator", meta = (DisplayName = "Prune Missing Functions"))
	void PruneMissingFunctions();

	/** Lists every tagged actor's functions, and assigns inputs where a rule says how. */
	int32 AutoMapFromWorld(UWorld* World);

	/**
	 * The button in the details panel. Scaffolding for getting started -- it runs only
	 * when clicked, and your own decisions always win.
	 */
	UFUNCTION(CallInEditor, Category = "OSCulator", meta = (DisplayName = "Auto-Map From Level"))
	void AutoMapFromLevel();

	/** Points the MIDI subsystem at whichever source has bLearn ticked, if any. */
	void UpdateLearnArming();
#endif

private:
	void ResolveNoteNames();
	void RebuildLookup();
	void RebuildSummaries();

	/** Indices rather than pointers, because the arrays reallocate when edited. */
	struct FLookupEntry
	{
		int32 BindingIndex = INDEX_NONE;
		int32 SourceIndex = INDEX_NONE;
	};

	/**
	 * Keyed by channel, type and number -- NOT device, which is matched at dispatch.
	 *
	 * A multimap because sharing an input between bindings is a feature here, not a
	 * mistake. Not serialised; rebuilt on load and on edit.
	 */
	TMultiMap<uint32, FLookupEntry> Lookup;
};
