// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OscuIntrospection.h"
#include "OscuMIDIMap.generated.h"

/** What kind of MIDI message drives a binding. */
UENUM()
enum class EOscuMIDIInputType : uint8
{
	/** Note on, and optionally note off. Velocity is the value. */
	Note,

	/** Control change. The controller value is the value. */
	ControlChange UMETA(DisplayName = "Control Change"),

	/**
	 * Program change. The program number is the whole message.
	 *
	 * Deliberately the odd one out, because the message is: a program change carries ONE
	 * data byte where a note and a controller carry two. There is no velocity, no
	 * controller value, nothing continuous at all -- so a row driven by one is a trigger
	 * that happens to know which program arrived.
	 */
	ProgramChange UMETA(DisplayName = "Program Change")
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

	/**
	 * Note or Control Change -- MIRRORED from the binding, not authored here.
	 *
	 * The kind of message a row answers to is a property of the row, not of each input
	 * on it: pitch means something for notes and nothing for controllers, and a release
	 * exists for one and not the other. A binding whose sources disagreed about which
	 * they were would have half its value settings meaningless, with no way to say which
	 * half.
	 *
	 * Kept here, and kept serialised, for two reasons. The source's own Note and CC
	 * Number fields hide on it -- an EditCondition can only read members of its own
	 * struct, so it has to be reachable from inside FOscuMIDISource. And an asset
	 * authored when this WAS the authored copy still has it on disk, which is what
	 * MigrateSourceTypesToBindings reads to recover the binding's type.
	 *
	 * Hidden from the panel rather than shown read-only: the row already says "Note" or
	 * "CC" in its summary, and a greyed duplicate on every source is noise.
	 */
	UPROPERTY()
	EOscuMIDIInputType Type = EOscuMIDIInputType::Note;

	/**
	 * Match a span of pitches rather than one note, and hand the position within that
	 * span to the binding as a value.
	 *
	 * Off is the default and stays the default: one note firing one function is what
	 * most rows are, and a trigger does not want a pitch value at all. Tick it and Note
	 * becomes the bottom of the span with Note High the top.
	 *
	 * Every pitch in the span fires the binding -- the span is contiguous, with no way
	 * to punch holes in it. That is deliberate rather than a shortcut: a row that fires
	 * on some notes of its range and not others is better written as two rows, and a
	 * single note is still just a single note with this unticked.
	 *
	 * Note sources only. Control change has a value already and a controller number
	 * that means nothing on a scale, so a span of CC numbers would be a different
	 * feature with no use behind it.
	 */
	UPROPERTY(EditAnywhere, Category = "Source",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note", EditConditionHides,
				DisplayName = "Note Range"))
	bool bNoteRange = false;

	/**
	 * "C3", "C#2", "Db2", or a bare "61". Normalised to sharps when edited.
	 *
	 * The bottom of the span when Note Range is ticked, and the whole of it when not.
	 */
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

	/** The top of the span, inclusive. Same spelling rules as Note. */
	UPROPERTY(EditAnywhere, Category = "Source",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note && bNoteRange", EditConditionHides,
				DisplayName = "Note High"))
	FString NoteHigh = TEXT("C4");

	/** What Note High resolved to. Swapped with the low bound if they are inverted. */
	UPROPERTY(VisibleAnywhere, Category = "Source",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note && bNoteRange", EditConditionHides,
				DisplayName = "Resolved Note High"))
	uint8 ResolvedNoteHigh = 72;

	UPROPERTY(EditAnywhere, Category = "Source",
		meta = (EditCondition = "Type == EOscuMIDIInputType::ControlChange", EditConditionHides,
				ClampMin = "0", ClampMax = "127", DisplayName = "CC Number"))
	uint8 ControlNumber = 1;

	/**
	 * Which program, 0-127. Its own field rather than sharing the CC one, so the label
	 * can say what it is -- a program number and a controller number are not the same
	 * kind of thing and sharing a box would read as a typo.
	 *
	 * Numbered from 0 here, matching the wire. Hardware disagrees with itself about this
	 * -- plenty of synths show patch 1 for program 0 -- so the number OSCulator shows is
	 * the number that arrived, and translating to whatever your box prints on its screen
	 * is left alone rather than guessed at.
	 */
	UPROPERTY(EditAnywhere, Category = "Source",
		meta = (EditCondition = "Type == EOscuMIDIInputType::ProgramChange", EditConditionHides,
				ClampMin = "0", ClampMax = "127", DisplayName = "Program Number"))
	uint8 ProgramNumber = 0;

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

	/** The note, CC or program number -- whichever this source is keyed on. */
	int32 GetNumber() const
	{
		switch (Type)
		{
		case EOscuMIDIInputType::Note:          return ResolvedNote;
		case EOscuMIDIInputType::ControlChange:  return ControlNumber;
		default:                                 return ProgramNumber;
		}
	}

	/** True when this source spans more than the one pitch. */
	bool IsNoteRange() const { return Type == EOscuMIDIInputType::Note && bNoteRange; }

	/** Bottom of the span, inclusive. The only note when this is not a range. */
	int32 GetNoteLow() const { return ResolvedNote; }

	/**
	 * Top of the span, inclusive. Equal to the low bound when this is not a range.
	 *
	 * Already ordered: Refresh swaps an inverted pair rather than leaving a span that
	 * matches nothing, so callers never have to check which way round it is.
	 */
	int32 GetNoteHigh() const { return IsNoteRange() ? ResolvedNoteHigh : ResolvedNote; }

	/**
	 * Where a pitch sits in the span, 0..1. Zero for a single note.
	 *
	 * The span normalises across its own extent, so an octave and two octaves both
	 * cover the whole output range. A one-note span has nowhere to sit and reports 0,
	 * which shapes to OutMin -- the bottom of the range, not a silent midpoint.
	 */
	double GetPitchFraction(int32 Pitch) const
	{
		const int32 Low = GetNoteLow();
		const int32 High = GetNoteHigh();
		if (High <= Low)
		{
			return 0.0;
		}
		return FMath::Clamp(static_cast<double>(Pitch - Low) / static_cast<double>(High - Low), 0.0, 1.0);
	}

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
	 * Note or Control Change, for every source on this binding.
	 *
	 * Per function rather than per source, because the two behave differently in ways
	 * that reach the value settings below: a note carries a pitch and a release, a
	 * controller carries neither. With the kind settled here, every setting on the row
	 * is meaningful for every source on it -- and the pitch settings can simply vanish
	 * from the panel for a controller-driven row, which is only possible because an
	 * EditCondition can read members of its own struct and nothing else.
	 *
	 * The cost is that one function driven by BOTH a pad and a knob is two bindings
	 * rather than one binding with two sources. They no longer share a remap range
	 * automatically, which is the trade: coherent rows against one fewer way to express
	 * a less common setup.
	 *
	 * Changing this re-points every source on the row, and their numbers are
	 * reinterpreted -- note 36 becomes CC 36. Refresh mirrors it down.
	 */
	UPROPERTY(EditAnywhere, Category = "Target")
	EOscuMIDIInputType Type = EOscuMIDIInputType::Note;

	/**
	 * Everything that fires this. None is legal and inert -- that is what auto-map
	 * produces before you have assigned anything, and an unassigned binding is how the
	 * asset stays a complete inventory of what could be controlled.
	 *
	 * All of one kind, set by Type above. Several sources still means several
	 * controllers, or several notes, reaching one function.
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
	UPROPERTY(EditAnywhere, Category = "Value",
		meta = (EditCondition = "Type != EOscuMIDIInputType::ProgramChange", EditConditionHides))
	bool bRemap = true;

	/** The value sent for an incoming 0. May be greater than OutMax, which inverts. */
	UPROPERTY(EditAnywhere, Category = "Value",
		meta = (EditCondition = "Type != EOscuMIDIInputType::ProgramChange && bRemap", EditConditionHides))
	float OutMin = 0.0f;

	/** The value sent for an incoming 127. */
	UPROPERTY(EditAnywhere, Category = "Value",
		meta = (EditCondition = "Type != EOscuMIDIInputType::ProgramChange && bRemap", EditConditionHides))
	float OutMax = 1.0f;

	/**
	 * Send the note or CC number as a first argument, ahead of the value.
	 *
	 * For a function that handles several notes itself and needs to know which one
	 * arrived. The number is sent raw and is never remapped -- it is an identity, not a
	 * measurement.
	 *
	 * Ignored once either parameter below is named: naming where a value goes and also
	 * saying "put something in front of it" cannot both be honoured, and the named
	 * assignment is the more specific statement.
	 */
	UPROPERTY(EditAnywhere, Category = "Value")
	bool bSendSourceNumber = false;

	// ---- Which parameter gets what ----

	/**
	 * The parameter the message's value drives, by name. Empty means the first one.
	 *
	 * What "the value" is depends on the row's Type, and the three are not the same kind
	 * of thing: velocity for a note, the controller value for a control change, and the
	 * PROGRAM NUMBER for a program change -- which is an identity rather than a
	 * magnitude, so it is never remapped.
	 *
	 * The C++ name is still VelocityParam, from when notes were the only thing with a
	 * value worth naming. Renaming it would need a property redirect and would move
	 * nobody's project forward, so the display name carries the meaning instead.
	 *
	 * By name rather than by position because position was the thing that made a
	 * function need writing around MIDI: the marshal fills parameters in declaration
	 * order, so the velocity-relevant one had to be first or you got nothing. Naming it
	 * means the function is written for what it does and the map adapts, which is the
	 * right way round.
	 *
	 * A name is a wire SLOT once resolved, not a parameter index -- a vec3 occupies
	 * three slots and an output pin occupies none -- and the resolution happens against
	 * the live signature, so a Blueprint recompile that reorders parameters is followed
	 * rather than silently mismatched.
	 *
	 * A name that matches nothing is reported once and that value is not sent, rather
	 * than falling back to a position that would quietly drive the wrong thing.
	 */
	UPROPERTY(EditAnywhere, Category = "Value", meta = (DisplayName = "Value -> Parameter"))
	FName VelocityParam;

	/**
	 * The parameter pitch drives, by name. Only meaningful for a note-range source.
	 *
	 * Empty means pitch is not sent at all, which is what every binding driven by a
	 * single note wants: one pitch carries no information.
	 *
	 * Leave this and Velocity -> Parameter both empty and the binding behaves exactly
	 * as it did before any of this existed.
	 */
	UPROPERTY(EditAnywhere, Category = "Value",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note", EditConditionHides,
				DisplayName = "Pitch -> Parameter"))
	FName PitchParam;

	/**
	 * Rescale the pitch's position in its span into PitchOutMin..PitchOutMax.
	 *
	 * Separate from the velocity remap because they are separate measurements that
	 * routinely want different ranges -- a pitch sweeping 0..1 while velocity drives a
	 * 0..10 intensity. Untick it to get the raw note number instead, which is also how
	 * you ask for pitch as an identity rather than a position.
	 */
	UPROPERTY(EditAnywhere, Category = "Value",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note", EditConditionHides,
				DisplayName = "Remap Pitch"))
	bool bRemapPitch = true;

	/** The value sent at the bottom of the span. May exceed PitchOutMax, which inverts. */
	UPROPERTY(EditAnywhere, Category = "Value",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note && bRemapPitch", EditConditionHides,
				DisplayName = "Pitch Out Min"))
	float PitchOutMin = 0.0f;

	/** The value sent at the top of the span. */
	UPROPERTY(EditAnywhere, Category = "Value",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note && bRemapPitch", EditConditionHides,
				DisplayName = "Pitch Out Max"))
	float PitchOutMax = 1.0f;

	/**
	 * The parameter names this binding's function actually offers, with their slots.
	 *
	 * Filled by Validate, which is the only thing that has a level to ask. Transient
	 * and derived: it is a fact about the level that happens to be open, and an asset
	 * that stored it would claim a signature from whichever level was open last.
	 *
	 * Here because a name you cannot discover is a name you cannot type. The engine's
	 * GetOptions dropdown cannot see which array element is being edited -- it resolves
	 * to the owning asset -- so it would offer every parameter name in the whole map,
	 * which reads as a list of valid choices and is not one. Showing this binding's own
	 * names is less clever and more use.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Value",
		meta = (DisplayName = "Available Parameters", MultiLine = true))
	FString AvailableParams;

	/**
	 * Also fire when a note is released, with a value of 0 before remapping.
	 *
	 * Hidden for a controller row rather than shown and ignored: a control change has no
	 * release, so the setting had nothing to mean there. It only became possible to hide
	 * once Type moved onto the binding.
	 */
	UPROPERTY(EditAnywhere, Category = "Value",
		meta = (EditCondition = "Type == EOscuMIDIInputType::Note", EditConditionHides))
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

	/**
	 * 0-127 in, shaped value out.
	 *
	 * A program change is passed through untouched however Remap is set: a program number
	 * is an identity, and squashing patch 5 into 0.039 is never what anyone meant. Same
	 * reasoning as Send Source Number, which has always been documented as raw.
	 */
	double ShapeValue(int32 Raw) const;

	/**
	 * A 0..1 position in a span, shaped into the pitch output range.
	 *
	 * With Remap Pitch off the raw note number is returned instead, so the caller
	 * passes the pitch itself rather than its fraction. Two inputs rather than one
	 * because "raw" and "normalised" are different numbers, unlike the velocity path
	 * where raw and remapped share the same 0-127 domain.
	 */
	double ShapePitch(double Fraction, int32 RawPitch) const;
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

private:
	/**
	 * Copies each binding's Type down onto its sources.
	 *
	 * Part of Refresh, and the reason a source's Type is a mirror rather than a second
	 * opinion. Runs on every edit, so changing a row's kind re-points every input on it
	 * in one step.
	 */
	void MirrorBindingTypes();

	/**
	 * Recovers a binding's Type from its sources, for an asset authored before Type
	 * moved up.
	 *
	 * PostLoad only, and deliberately never from Refresh: disagreement between a binding
	 * and its sources is exactly what a legitimate edit looks like for the instant
	 * before the mirror runs, so doing this on every edit would undo the edit. On load
	 * there has been no edit, and the sources are the only record of what the row meant.
	 */
	void MigrateSourceTypesToBindings();

public:

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
	 * trigger and gets a note; one whose first assignable parameter carries a magnitude
	 * is continuous and gets a controller.
	 *
	 * ParamsByFunction carries each function's parameters in declaration order. The
	 * whole list rather than just the first, because it decides two things now: note
	 * versus controller, and which parameters velocity and pitch default to.
	 */
	int32 MergeFunctions(FName Tag, const TArray<FName>& FunctionNames,
		const TMap<FName, TArray<FOscuExposedParamInfo>>& ParamsByFunction, int32* OutAssigned = nullptr);

	/**
	 * Points velocity and pitch at this function's first two continuous parameters.
	 *
	 * Additive only, and that is the whole contract: it fills the pair only when BOTH
	 * are still empty, so a deliberate assignment is never second-guessed and running
	 * auto-map twice cannot move anything. Returns true if it wrote something.
	 *
	 * Velocity takes the first and pitch the second, which keeps the common case
	 * identical to the old positional default -- velocity into the first parameter --
	 * while giving pitch somewhere to go the moment a source becomes a note range.
	 *
	 * A function with one continuous parameter gets velocity only; with none, nothing.
	 * Filling pitch on a single-note source costs nothing: one pitch has no position, so
	 * it shapes to Pitch Out Min, which is 0.0 by default -- exactly the zero the frame
	 * already held.
	 */
	static bool AssignDefaultValueParams(FOscuMIDIBinding& Binding, const TArray<FOscuExposedParamInfo>& Params);

	/** How a validation pass turned out. */
	struct FOscuMIDIValidation
	{
		int32 Found = 0;
		int32 FunctionMissing = 0;
		int32 TagMissing = 0;

		/** Bindings with no source at all. Not a fault -- just not assigned yet. */
		int32 Unassigned = 0;

		/**
		 * Bindings whose function offers a continuous parameter but which name none.
		 *
		 * Reported rather than fixed. Validate's contract is that it never edits and
		 * never dirties the package, which is the whole reason it is safe to click on an
		 * asset you are unsure about -- so it says how many rows Auto-Map would fill in
		 * and leaves the writing to the button that is allowed to write.
		 */
		int32 CouldDefaultValueParams = 0;

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
