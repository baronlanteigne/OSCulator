// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDIMap.h"

#include "OSCulatorCore.h"
#include "OscuIntrospection.h"
#include "OscuMIDINoteName.h"
#include "OscuMIDISubsystem.h"
#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"

#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/Blueprint.h"
#endif

namespace
{
	/**
	 * Device is deliberately absent: it is matched leniently at dispatch, not keyed.
	 *
	 * Both note kinds collapse to ONE key. A note arrives off the wire as a note and
	 * knows nothing about whether the row it should reach is a single note or a span --
	 * so if the two kinds keyed separately, every incoming note would need two lookups
	 * and every caller would have to remember to do both. A range already registers
	 * once per note it covers, so sharing the key makes a span and a single note match
	 * through exactly the same path.
	 */
	uint32 MakeLookupKey(uint8 Channel, EOscuMIDIInputType Type, int32 Number)
	{
		const EOscuMIDIInputType Keyed = OscuIsNoteType(Type) ? EOscuMIDIInputType::Note : Type;

		return static_cast<uint32>(Channel) << 16
			| static_cast<uint32>(Keyed) << 8
			| static_cast<uint32>(static_cast<uint8>(Number));
	}

	const TCHAR* TypeLabel(EOscuMIDIInputType Type)
	{
		switch (Type)
		{
		case EOscuMIDIInputType::Note:          return TEXT("note");
		case EOscuMIDIInputType::NoteRange:     return TEXT("notes");
		case EOscuMIDIInputType::ControlChange:  return TEXT("CC");
		default:                                 return TEXT("program");
		}
	}

	/** The parameters a message can actually write to, in declaration order. */
	void GatherAssignable(const TArray<FOscuExposedParamInfo>& Params, TArray<const FOscuExposedParamInfo*>& Out)
	{
		Out.Reset();
		for (const FOscuExposedParamInfo& Param : Params)
		{
			if (!Param.bOutputOnly)
			{
				Out.Add(&Param);
			}
		}
	}

	/**
	 * A function that takes nothing is a trigger; one whose first assignable parameter
	 * carries a magnitude is continuous. That distinction is the whole of the
	 * note-versus-controller decision, and it is already in the signature -- so nobody
	 * has to answer it twice.
	 */
	EOscuMIDIInputType InputTypeForSignature(const TArray<FOscuExposedParamInfo>& Params)
	{
		TArray<const FOscuExposedParamInfo*> Assignable;
		GatherAssignable(Params, Assignable);

		if (Assignable.Num() == 0)
		{
			// Takes nothing, so it is a trigger, and a pad is the right thing for it.
			return EOscuMIDIInputType::Note;
		}

		// Reads the classified fact rather than comparing the type label against "float"
		// and "int", which quietly excluded byte and would drift the moment the type
		// table grew an entry.
		//
		// A vector, a transform, a string: a continuous controller cannot fill one of
		// those meaningfully, so it gets a note and whatever the remaining parameters
		// are keep the zeroes the initialised frame gave them.
		return Assignable[0]->bContinuous
			? EOscuMIDIInputType::ControlChange
			: EOscuMIDIInputType::Note;
	}
}

double FOscuMIDIBinding::ShapeValue(const int32 Raw) const
{
	const double Clamped = FMath::Clamp(static_cast<double>(Raw), 0.0, 127.0);

	if (Type == EOscuMIDIInputType::ProgramChange)
	{
		// A program number is which patch, not how much. Remapping it would turn
		// program 5 into 0.039, which is never what anyone meant -- so it is passed
		// through whatever Remap says, and Remap is hidden for this row anyway.
		return Clamped;
	}

	if (!bRemap)
	{
		return Clamped;
	}

	// Deliberately not FMath::GetMappedRangeValueClamped: OutMin may exceed OutMax on
	// purpose, which is how a fader is inverted, and clamping to a reversed range is a
	// classic way to get zero out of everything.
	return static_cast<double>(OutMin) + (Clamped / 127.0) * (static_cast<double>(OutMax) - static_cast<double>(OutMin));
}

double FOscuMIDIBinding::ShapePitch(const double Fraction, const int32 RawPitch) const
{
	if (!bRemapPitch)
	{
		// The note number itself, not its position. This is how pitch is asked for as
		// an identity -- "which pad was it" -- rather than as a measurement.
		return static_cast<double>(FMath::Clamp(RawPitch, 0, 127));
	}

	// Same deliberate avoidance of GetMappedRangeValueClamped as ShapeValue: PitchOutMin
	// may exceed PitchOutMax on purpose, which is how a span is inverted so the lowest
	// note gives the highest value.
	const double Clamped = FMath::Clamp(Fraction, 0.0, 1.0);
	return static_cast<double>(PitchOutMin) + Clamped * (static_cast<double>(PitchOutMax) - static_cast<double>(PitchOutMin));
}

bool UOscuMIDIMap::AssignDefaultValueParams(FOscuMIDIBinding& Binding, const TArray<FOscuExposedParamInfo>& Params)
{
	// Both empty or nothing happens. Half-filled means someone chose one deliberately,
	// and completing their sentence for them is exactly the kind of help that moves a
	// mapping you had already made.
	if (!Binding.VelocityParam.IsNone() || !Binding.PitchParam.IsNone())
	{
		return false;
	}

	TArray<const FOscuExposedParamInfo*> Assignable;
	GatherAssignable(Params, Assignable);

	TArray<FName> Continuous;
	for (const FOscuExposedParamInfo* Param : Assignable)
	{
		if (Param->bContinuous)
		{
			Continuous.Add(Param->Name);
			if (Continuous.Num() == 2)
			{
				break;
			}
		}
	}

	if (Continuous.Num() == 0)
	{
		// A trigger, or a function whose parameters are all strings and structs. Left
		// unnamed, which means it behaves exactly as it always did.
		return false;
	}

	Binding.VelocityParam = Continuous[0];

	// Pitch only for a SPAN. A single note has no position in one, so pointing pitch at
	// a parameter there would put the same number in it on every hit -- which is the
	// state Single Note and Note Range exist to tell apart.
	if (Continuous.Num() > 1 && Binding.Type == EOscuMIDIInputType::NoteRange)
	{
		Binding.PitchParam = Continuous[1];
	}

	return true;
}

void UOscuMIDIMap::PostLoad()
{
	Super::PostLoad();

	// Order matters and is the whole of the migration. Refresh mirrors Type DOWN from
	// each binding onto its sources, so it has to run second: an asset authored when the
	// source held the authored copy would otherwise have every controller row silently
	// become a note row, reinterpreting CC 7 as note 7.
	MigrateSourceTypesToBindings();
	MigrateNoteRanges();
	Refresh();
}

#if WITH_EDITOR
void UOscuMIDIMap::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	Refresh();
	UpdateLearnArming();
}

void UOscuMIDIMap::UpdateLearnArming()
{
	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	if (MIDI == nullptr)
	{
		return;
	}

	// Only one source may be armed. The most recently ticked one wins, so ticking a
	// second moves the arming rather than being ignored.
	int32 ArmedBinding = INDEX_NONE;
	int32 ArmedSource = INDEX_NONE;

	for (int32 BindingIndex = 0; BindingIndex < Bindings.Num(); ++BindingIndex)
	{
		for (int32 SourceIndex = 0; SourceIndex < Bindings[BindingIndex].Sources.Num(); ++SourceIndex)
		{
			if (!Bindings[BindingIndex].Sources[SourceIndex].bLearn)
			{
				continue;
			}

			if (ArmedBinding != INDEX_NONE)
			{
				Bindings[ArmedBinding].Sources[ArmedSource].bLearn = false;
			}
			ArmedBinding = BindingIndex;
			ArmedSource = SourceIndex;
		}
	}

	if (ArmedBinding != INDEX_NONE)
	{
		MIDI->ArmLearn(this, ArmedBinding, ArmedSource);
	}
	else
	{
		MIDI->CancelLearn(this);
	}
}
#endif // WITH_EDITOR

void UOscuMIDIMap::MigrateNoteRanges()
{
	int32 Promoted = 0;
	int32 PitchCleared = 0;

	for (FOscuMIDIBinding& Binding : Bindings)
	{
		if (Binding.Type != EOscuMIDIInputType::Note)
		{
			continue;
		}

		// ANY span makes the row a span. A row carrying one span and one single note was
		// always really a span; the single note lives on inside it as a one-note span,
		// which GetPitchFraction already handles by reporting 0.
		const bool bWasRange = Binding.Sources.ContainsByPredicate(
			[](const FOscuMIDISource& Source) { return Source.bNoteRange; });

		if (bWasRange)
		{
			Binding.Type = EOscuMIDIInputType::NoteRange;
			++Promoted;
			continue;
		}

		// Stays a single note, so its pitch assignment was the broken case: no span
		// means no position, so it sent Pitch Out Min on every hit. Cleared rather than
		// left hidden, so the asset says what it does.
		if (!Binding.PitchParam.IsNone())
		{
			UE_LOG(LogOSCulator, Warning,
				TEXT("%s: %s/%s is a Single Note row, so its Pitch -> %s could only ever send the same "
					 "number. Cleared. Set the row to Note Range if a span was what you meant."),
				*GetName(), *Binding.Tag.ToString(), *Binding.FunctionName.ToString(),
				*Binding.PitchParam.ToString());

			Binding.PitchParam = NAME_None;
			++PitchCleared;
		}
	}

	if (Promoted > 0 || PitchCleared > 0)
	{
		UE_LOG(LogOSCulator, Log,
			TEXT("%s: %d row(s) became Note Range, %d pitch assignment(s) cleared from Single Note rows."),
			*GetName(), Promoted, PitchCleared);
	}
}

void UOscuMIDIMap::Refresh()
{
	// First, so everything below sees one answer about what kind each row is.
	MirrorBindingTypes();

	ResolveNoteNames();
	RebuildLookup();
	RebuildSummaries();
}

void UOscuMIDIMap::MirrorBindingTypes()
{
	for (FOscuMIDIBinding& Binding : Bindings)
	{
		for (FOscuMIDISource& Source : Binding.Sources)
		{
			Source.Type = Binding.Type;
		}
	}
}

void UOscuMIDIMap::MigrateSourceTypesToBindings()
{
	for (FOscuMIDIBinding& Binding : Bindings)
	{
		if (Binding.Sources.Num() == 0)
		{
			// Nothing to recover from. An unassigned row keeps whatever Type it has,
			// which for a fresh asset is the Note default.
			continue;
		}

		// The first source is taken as the row's intent. A pre-migration asset COULD
		// have held a mix -- that was legal then -- and there is no answer for what such
		// a row should become, so the mix is reported rather than silently resolved.
		const EOscuMIDIInputType Adopted = Binding.Sources[0].Type;

		const bool bMixed = Binding.Sources.ContainsByPredicate(
			[Adopted](const FOscuMIDISource& Source) { return Source.Type != Adopted; });

		if (Binding.Type != Adopted)
		{
			Binding.Type = Adopted;
		}

		if (bMixed)
		{
			UE_LOG(LogOSCulator, Warning,
				TEXT("%s: %s/%s had sources of more than one kind, which is no longer possible -- the whole row is now '%s' from its first source. "
					 "Split the others into a second binding if they were deliberate."),
				*GetName(), *Binding.Tag.ToString(), *Binding.FunctionName.ToString(), TypeLabel(Adopted));
		}
	}
}

void UOscuMIDIMap::ResolveNoteNames()
{
	const int32 MiddleCOctave = UOscuSettings::Get()->MiddleCOctave;

	for (FOscuMIDIBinding& Binding : Bindings)
	{
		for (FOscuMIDISource& Source : Binding.Sources)
		{
			if (!OscuIsNoteType(Source.Type))
			{
				continue;
			}

			uint8 Resolved = 0;
			FString Error;
			if (OscuMIDINoteName::Parse(Source.Note, MiddleCOctave, Resolved, Error))
			{
				Source.ResolvedNote = Resolved;

				// Normalise the display string too, so "Db2" becomes "C#2" and reads
				// the same as it will everywhere else.
				Source.Note = OscuMIDINoteName::ToString(Resolved, MiddleCOctave);
			}
			else
			{
				UE_LOG(LogOSCulator, Warning, TEXT("%s: %s/%s has an unusable note '%s' (%s). Keeping %d."),
					*GetName(), *Binding.Tag.ToString(), *Binding.FunctionName.ToString(),
					*Source.Note, *Error, Source.ResolvedNote);
			}

			if (!Source.IsNoteRange())
			{
				continue;
			}

			uint8 ResolvedHigh = 0;
			if (OscuMIDINoteName::Parse(Source.NoteHigh, MiddleCOctave, ResolvedHigh, Error))
			{
				Source.ResolvedNoteHigh = ResolvedHigh;
				Source.NoteHigh = OscuMIDINoteName::ToString(ResolvedHigh, MiddleCOctave);
			}
			else
			{
				UE_LOG(LogOSCulator, Warning, TEXT("%s: %s/%s has an unusable high note '%s' (%s). Keeping %d."),
					*GetName(), *Binding.Tag.ToString(), *Binding.FunctionName.ToString(),
					*Source.NoteHigh, *Error, Source.ResolvedNoteHigh);
			}

			if (Source.ResolvedNoteHigh < Source.ResolvedNote)
			{
				// Swapped rather than rejected. A span typed high-end-first is an
				// ordering slip, not a change of mind, and the alternative is a row
				// that matches no note at all while looking entirely filled in.
				UE_LOG(LogOSCulator, Log,
					TEXT("%s: %s/%s had its note range inverted (%d..%d); swapped."),
					*GetName(), *Binding.Tag.ToString(), *Binding.FunctionName.ToString(),
					Source.ResolvedNote, Source.ResolvedNoteHigh);

				Swap(Source.ResolvedNote, Source.ResolvedNoteHigh);
				Swap(Source.Note, Source.NoteHigh);
			}
		}
	}
}

void UOscuMIDIMap::RebuildLookup()
{
	Lookup.Reset();

	for (int32 BindingIndex = 0; BindingIndex < Bindings.Num(); ++BindingIndex)
	{
		const FOscuMIDIBinding& Binding = Bindings[BindingIndex];

		for (int32 SourceIndex = 0; SourceIndex < Binding.Sources.Num(); ++SourceIndex)
		{
			const FOscuMIDISource& Source = Binding.Sources[SourceIndex];

			if (Source.Channel < 1 || Source.Channel > 16)
			{
				UE_LOG(LogOSCulator, Warning, TEXT("%s: %s/%s has a source on channel %d, outside 1-16. Skipped."),
					*GetName(), *Binding.Tag.ToString(), *Binding.FunctionName.ToString(), Source.Channel);
				continue;
			}

			// Added, never rejected as a duplicate. Two bindings sharing one input is
			// exactly how one pad drives two different actors, which the old
			// channel-first structure could only treat as a mistake.
			//
			// A range source registers once per note it covers, up to 128 entries. That
			// keeps FindMatches, IsInputTaken and auto-map conflict detection exactly as
			// they were -- one hash lookup per message, no second matching path to keep
			// in step -- and 128 small entries is nothing next to a second code path
			// that three callers would have to learn about.
			const int32 Low = Source.GetNoteLow();
			const int32 High = Source.GetNoteHigh();

			if (Source.IsNoteRange())
			{
				for (int32 Number = Low; Number <= High; ++Number)
				{
					Lookup.Add(MakeLookupKey(Source.Channel, Source.Type, Number),
						FLookupEntry{ BindingIndex, SourceIndex });
				}
			}
			else
			{
				Lookup.Add(MakeLookupKey(Source.Channel, Source.Type, Source.GetNumber()),
					FLookupEntry{ BindingIndex, SourceIndex });
			}
		}
	}
}

void UOscuMIDIMap::RebuildSummaries()
{
	for (FOscuMIDIBinding& Binding : Bindings)
	{
		for (FOscuMIDISource& Source : Binding.Sources)
		{
			FString Text;
			if (Source.Type == EOscuMIDIInputType::ProgramChange)
			{
				Text = FString::Printf(TEXT("Prog  ch %d  #%d"), Source.Channel, Source.ProgramNumber);
			}
			else if (Source.Type == EOscuMIDIInputType::ControlChange)
			{
				Text = FString::Printf(TEXT("CC    ch %d  #%d"), Source.Channel, Source.ControlNumber);
			}
			else if (Source.IsNoteRange())
			{
				Text = FString::Printf(TEXT("Notes ch %d  %s (%d) - %s (%d)  [%d notes]"),
					Source.Channel, *Source.Note, Source.ResolvedNote,
					*Source.NoteHigh, Source.ResolvedNoteHigh,
					Source.GetNoteHigh() - Source.GetNoteLow() + 1);
			}
			else
			{
				Text = FString::Printf(TEXT("Note  ch %d  %s (%d)"), Source.Channel, *Source.Note, Source.ResolvedNote);
			}

			// Only when it narrows something. "(any device)" on every row would be
			// noise in the common single-controller case.
			if (!Source.Device.IsNone())
			{
				Text += FString::Printf(TEXT("   from %s"), *Source.Device.ToString());
			}
			if (Source.bLearn)
			{
				Text += TEXT("   <-- ARMED");
			}

			Source.Summary = MoveTemp(Text);
		}

		FString Text = FString::Printf(TEXT("%s/%s"),
			Binding.Tag.IsNone() ? TEXT("(no tag)") : *Binding.Tag.ToString(),
			Binding.FunctionName.IsNone() ? TEXT("(no function)") : *Binding.FunctionName.ToString());

		if (Binding.Sources.Num() == 0)
		{
			// Legal and inert. Worth saying, because "nothing happens when I press the
			// pad" and "this row has no pad" are the same sentence from the outside.
			Text += TEXT("   -- unassigned");
		}
		else
		{
			Text += FString::Printf(TEXT("   <- %s"), *Binding.Sources[0].Summary);
			if (Binding.Sources.Num() > 1)
			{
				Text += FString::Printf(TEXT(" (+%d more)"), Binding.Sources.Num() - 1);
			}
		}

		// Where the values land, but only when it was said explicitly. A row that names
		// nothing behaves as it always did, and saying so on every row would bury the
		// handful that were deliberately routed.
		TArray<FString> Routing;
		if (!Binding.PitchParam.IsNone())
		{
			Routing.Add(FString::Printf(TEXT("pitch->%s"), *Binding.PitchParam.ToString()));
		}
		if (!Binding.VelocityParam.IsNone())
		{
			Routing.Add(FString::Printf(TEXT("vel->%s"), *Binding.VelocityParam.ToString()));
		}
		if (Routing.Num() > 0)
		{
			Text += FString::Printf(TEXT("   [%s]"), *FString::Join(Routing, TEXT(", ")));
		}

		switch (Binding.Status)
		{
		case EOscuMIDIRowStatus::FunctionMissing:
			Text += TEXT("   <-- FUNCTION NOT FOUND");
			break;
		case EOscuMIDIRowStatus::TagMissing:
			Text += TEXT("   <-- tag not in this level");
			break;
		default:
			break;
		}

		Binding.Summary = MoveTemp(Text);
	}
}

void UOscuMIDIMap::FindMatches(const FName Device, const uint8 Channel, const EOscuMIDIInputType Type, const int32 Number, TArray<FOscuMIDIMatch>& OutMatches) const
{
	OutMatches.Reset();

	TArray<FLookupEntry> Entries;
	Lookup.MultiFind(MakeLookupKey(Channel, Type, Number), Entries);

	for (const FLookupEntry& Entry : Entries)
	{
		if (!Bindings.IsValidIndex(Entry.BindingIndex))
		{
			continue;
		}
		const FOscuMIDIBinding& Binding = Bindings[Entry.BindingIndex];

		if (!Binding.Sources.IsValidIndex(Entry.SourceIndex))
		{
			continue;
		}
		const FOscuMIDISource& Source = Binding.Sources[Entry.SourceIndex];

		// A source that names no device falls back to the asset's DefaultDevice, and
		// if that is empty too it accepts every device. Anything named has to match the
		// port the message actually came from.
		const FName Required = Source.ResolveDevice(DefaultDevice);
		if (!Required.IsNone() && Required != Device)
		{
			continue;
		}

		OutMatches.Add(FOscuMIDIMatch{ &Binding, &Source });
	}
}

bool UOscuMIDIMap::IsInputTaken(const uint8 Channel, const EOscuMIDIInputType Type, const int32 Number) const
{
	return Lookup.Contains(MakeLookupKey(Channel, Type, Number));
}

FOscuMIDIBinding* UOscuMIDIMap::FindBinding(const FName Tag, const FName FunctionName)
{
	return Bindings.FindByPredicate([Tag, FunctionName](const FOscuMIDIBinding& Binding)
	{
		return Binding.Tag == Tag && Binding.FunctionName == FunctionName;
	});
}

int32 UOscuMIDIMap::MergeFunctions(const FName Tag, const TArray<FName>& FunctionNames,
	const TMap<FName, TArray<FOscuExposedParamInfo>>& ParamsByFunction, int32* OutAssigned)
{
	if (Tag.IsNone() || FunctionNames.Num() == 0)
	{
		return 0;
	}

	// Alphabetical, NOT reflection order. A class's function map does not iterate
	// stably across Blueprint recompiles, so taking its order would reshuffle note
	// assignments on every recompile.
	TArray<FName> Sorted = FunctionNames;
	Sorted.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	static const TArray<FOscuExposedParamInfo> NoParams;

	int32 Added = 0;
	int32 Defaulted = 0;

	for (const FName& FunctionName : Sorted)
	{
		const TArray<FOscuExposedParamInfo>* Params = ParamsByFunction.Find(FunctionName);
		const TArray<FOscuExposedParamInfo>& ParamList = Params != nullptr ? *Params : NoParams;

		FOscuMIDIBinding* Existing = FindBinding(Tag, FunctionName);

		if (Existing == nullptr)
		{
			FOscuMIDIBinding& Binding = Bindings.AddDefaulted_GetRef();
			Binding.Tag = Tag;
			Binding.FunctionName = FunctionName;
			Defaulted += AssignDefaultValueParams(Binding, ParamList) ? 1 : 0;
			++Added;
		}
		else
		{
			// Existing rows get the defaults too, but only where both are still empty.
			// That is what lets an asset authored before any of this existed pick the
			// assignment up by clicking Auto-Map again, without the additive-only rule
			// being bent: filling an empty field moves nothing.
			Defaulted += AssignDefaultValueParams(*Existing, ParamList) ? 1 : 0;
		}
	}

	if (Defaulted > 0)
	{
		UE_LOG(LogOSCulator, Log,
			TEXT("%s: pointed velocity and pitch at default parameters on %d binding(s) under '%s'."),
			*GetName(), Defaulted, *Tag.ToString());
	}

	// Phase two, and only for a tag whose layout has been described. Without a rule the
	// bindings are still listed -- the asset is meant to be a complete inventory of
	// what could be controlled -- they simply arrive unassigned.
	const FOscuMIDIAutoMapRule* Rule = AutoMapRules.FindByPredicate(
		[Tag](const FOscuMIDIAutoMapRule& Candidate) { return Candidate.Tag == Tag; });

	if (Rule == nullptr)
	{
		if (Added > 0 || Defaulted > 0)
		{
			Refresh();
		}
		return Added;
	}

	const int32 MiddleCOctave = UOscuSettings::Get()->MiddleCOctave;
	int32 NextNote = Rule->FirstNote;
	int32 NextControl = Rule->FirstControl;
	int32 Assigned = 0;

	// Rebuilt first, so IsInputTaken sees the bindings just added and anything the
	// user assigned by hand since the last run.
	Refresh();

	for (const FName& FunctionName : Sorted)
	{
		FOscuMIDIBinding* Binding = FindBinding(Tag, FunctionName);

		// Never touches a binding that already has a source. Existing assignments are
		// the whole thing that must not move.
		if (Binding == nullptr || Binding->Sources.Num() > 0)
		{
			continue;
		}

		if (Binding->Type == EOscuMIDIInputType::ProgramChange
			|| Binding->Type == EOscuMIDIInputType::NoteRange)
		{
			// Left entirely alone. Neither kind is something to GUESS from a signature --
			// a program change is a deliberate "patch 7 fires this", and a span is a
			// deliberate "these notes sweep it". The auto-map rule describes a first note
			// and a first CC, with nothing to say about either. Overwriting the type here
			// would also undo the choice.
			UE_LOG(LogOSCulator, Log,
				TEXT("%s: '%s' is set to %s, so auto-map left it for you to assign."),
				*GetName(), *FunctionName.ToString(),
				Binding->Type == EOscuMIDIInputType::ProgramChange
					? TEXT("Program Change") : TEXT("Note Range"));
			continue;
		}

		const TArray<FOscuExposedParamInfo>* Params = ParamsByFunction.Find(FunctionName);
		const EOscuMIDIInputType Type = InputTypeForSignature(Params != nullptr ? *Params : NoParams);

		// Onto the binding, which is where the kind lives now. The source picks it up
		// from the mirror, so it is never written in two places.
		Binding->Type = Type;

		int32& Next = (Type == EOscuMIDIInputType::Note) ? NextNote : NextControl;
		while (Next <= 127 && IsInputTaken(Rule->Channel, Type, Next))
		{
			++Next;
		}
		if (Next > 127)
		{
			UE_LOG(LogOSCulator, Warning,
				TEXT("%s: channel %d has no free %s numbers left; '%s' was listed but not assigned."),
				*GetName(), Rule->Channel, TypeLabel(Type), *FunctionName.ToString());
			continue;
		}

		FOscuMIDISource& Source = Binding->Sources.AddDefaulted_GetRef();
		Source.Device = Rule->Device;
		Source.Channel = Rule->Channel;
		Source.Type = Type;  // the mirror will agree; set now so IsInputTaken is right below
		if (Type == EOscuMIDIInputType::Note)
		{
			Source.ResolvedNote = static_cast<uint8>(Next);
			Source.Note = OscuMIDINoteName::ToString(static_cast<uint8>(Next), MiddleCOctave);
		}
		else
		{
			// Program Change never reaches here -- it is skipped above -- so this is the
			// control-change case.
			Source.ControlNumber = static_cast<uint8>(Next);
		}

		++Next;
		++Assigned;

		// So the next IsInputTaken sees this one.
		Refresh();
	}

	if (OutAssigned != nullptr)
	{
		*OutAssigned += Assigned;
	}

	if (Added > 0 || Assigned > 0)
	{
		Refresh();
	}
	return Added;
}

UOscuMIDIMap::FOscuMIDIValidation UOscuMIDIMap::Validate(UWorld* World)
{
	FOscuMIDIValidation Result;

	UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(World);
	if (Router == nullptr)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("%s: validation found no registry for this world."), *GetName());
		return Result;
	}

	// The editor world never runs OnWorldBeginPlay, so the registry has to be filled
	// explicitly before it can be asked anything.
	Router->ScanWorld();

	// Two different questions need two different filters, and conflating them would be
	// actively destructive.
	//
	// "Does this binding still point at something callable?" has to be asked of
	// EVERYTHING exposed. Auto-map only ever OFFERS Blueprint-authored functions,
	// because a fully exposed actor buries its own handful under two hundred inherited
	// ones -- but a binding made by hand to a native function is perfectly legal and
	// the router calls it happily. Judging against the Custom view would flag those as
	// broken, and Prune would then delete working mappings.
	TMap<FName, TSet<FName>> ByTag;
	for (const FOscuExposedTagInfo& TagInfo : Router->Introspect(EOscuIntrospectFilter::All))
	{
		TSet<FName>& Functions = ByTag.Add(TagInfo.Tag);
		for (const FOscuExposedFunctionInfo& Function : TagInfo.Functions)
		{
			Functions.Add(Function.FunctionName);
		}
	}

	// "What could I map that I have not?" is the opposite: the Custom view, or the
	// suggestion list is two hundred inherited engine functions per tag and useless.
	TMap<FName, TSet<FName>> OfferableByTag;
	for (const FOscuExposedTagInfo& TagInfo : Router->Introspect(EOscuIntrospectFilter::Custom))
	{
		TSet<FName>& Functions = OfferableByTag.Add(TagInfo.Tag);
		for (const FOscuExposedFunctionInfo& Function : TagInfo.Functions)
		{
			Functions.Add(Function.FunctionName);
		}
	}

	TSet<TPair<FName, FName>> Claimed;
	TMap<uint32, TArray<FString>> ByInput;

	for (FOscuMIDIBinding& Binding : Bindings)
	{
		Claimed.Add(TPair<FName, FName>(Binding.Tag, Binding.FunctionName));

		const TSet<FName>* Functions = ByTag.Find(Binding.Tag);
		if (Functions == nullptr)
		{
			Binding.Status = EOscuMIDIRowStatus::TagMissing;
			++Result.TagMissing;
		}
		else if (Functions->Contains(Binding.FunctionName))
		{
			Binding.Status = EOscuMIDIRowStatus::Found;
			++Result.Found;
		}
		else
		{
			Binding.Status = EOscuMIDIRowStatus::FunctionMissing;
			++Result.FunctionMissing;
		}

		if (Binding.Sources.Num() == 0)
		{
			++Result.Unassigned;
		}

		// The names this binding can actually assign to, with the slot each occupies.
		// Written into the row so they are discoverable where they are typed; the panel
		// has no way to offer them as a dropdown, because the engine's GetOptions hook
		// resolves to the owning asset and cannot tell which array element is in front
		// of the user.
		{
			TArray<FOscuExposedParamInfo> Params;
			TArray<int32> Slots;

			if (!Router->DescribeParams(Binding.Tag, Binding.FunctionName, Params, Slots))
			{
				Binding.AvailableParams = TEXT("-- nothing in this level exposes this function --");
			}
			else if (Params.Num() == 0)
			{
				Binding.AvailableParams = TEXT("-- takes no arguments; it is a trigger --");
			}
			else
			{
				// Would Auto-Map have something to say here? Counted, never written.
				if (Binding.VelocityParam.IsNone() && Binding.PitchParam.IsNone())
				{
					const bool bHasContinuous = Params.ContainsByPredicate(
						[](const FOscuExposedParamInfo& Param)
						{
							return Param.bContinuous && !Param.bOutputOnly;
						});

					if (bHasContinuous)
					{
						++Result.CouldDefaultValueParams;
					}
				}

				TArray<FString> Lines;
				for (int32 Index = 0; Index < Params.Num(); ++Index)
				{
					const FOscuExposedParamInfo& Param = Params[Index];

					// Output pins are listed rather than hidden, with the reason: they
					// are real parameters and their absence from the assignable set is
					// otherwise indistinguishable from a typo in the name.
					Lines.Add(Param.bOutputOnly
						? FString::Printf(TEXT("%s : %s  -- output, cannot be assigned"),
							*Param.Name.ToString(), *Param.TypeLabel)
						: FString::Printf(TEXT("%s : %s  @ slot %d"),
							*Param.Name.ToString(), *Param.TypeLabel, Slots[Index]));
				}
				Binding.AvailableParams = FString::Join(Lines, LINE_TERMINATOR);
			}
		}

		for (const FOscuMIDISource& Source : Binding.Sources)
		{
			// A range source claims every input it covers, so a clash with another
			// binding anywhere in the span is reported rather than only at the bottom
			// note.
			for (int32 Number = Source.GetNoteLow(); Number <= Source.GetNoteHigh(); ++Number)
			{
				ByInput.FindOrAdd(MakeLookupKey(Source.Channel, Source.Type,
						OscuIsNoteType(Source.Type) ? Number : Source.GetNumber()))
					.Add(FString::Printf(TEXT("%s/%s"), *Binding.Tag.ToString(), *Binding.FunctionName.ToString()));

				if (!OscuIsNoteType(Source.Type))
				{
					break;
				}
			}
		}
	}

	for (const TPair<FName, TSet<FName>>& Pair : OfferableByTag)
	{
		for (const FName& Function : Pair.Value)
		{
			if (!Claimed.Contains(TPair<FName, FName>(Pair.Key, Function)))
			{
				Result.Unclaimed.Add(FString::Printf(TEXT("%s/%s"), *Pair.Key.ToString(), *Function.ToString()));
			}
		}
	}
	Result.Unclaimed.Sort();

	// Reported, not warned about. Sharing an input is a feature -- one pad driving two
	// actors -- but an accidental collision looks identical from the inside, so it is
	// listed and left to the reader.
	for (const TPair<uint32, TArray<FString>>& Pair : ByInput)
	{
		if (Pair.Value.Num() > 1)
		{
			const uint8 Channel = static_cast<uint8>(Pair.Key >> 16);
			const EOscuMIDIInputType Type = static_cast<EOscuMIDIInputType>((Pair.Key >> 8) & 0xFF);
			Result.Shared.Add(FString::Printf(TEXT("ch %d %s %d -> %s"),
				Channel, TypeLabel(Type), Pair.Key & 0xFF, *FString::Join(Pair.Value, TEXT(", "))));
		}
	}
	Result.Shared.Sort();

	RebuildSummaries();

	UE_LOG(LogOSCulator, Log,
		TEXT("%s: %d binding(s) checked -- %d ok, %d with a missing function, %d whose tag is not in this level, %d unassigned."),
		*GetName(), Result.Total(), Result.Found, Result.FunctionMissing, Result.TagMissing, Result.Unassigned);

	if (Result.FunctionMissing > 0)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("%s: these bindings point at functions that no longer exist:"), *GetName());
		for (const FOscuMIDIBinding& Binding : Bindings)
		{
			if (Binding.Status == EOscuMIDIRowStatus::FunctionMissing)
			{
				// Printed with everything that would be lost by deleting it, so the
				// choice between re-pointing and pruning is an informed one.
				UE_LOG(LogOSCulator, Warning, TEXT("    %s"), *Binding.Summary);
			}
		}
	}

	if (Result.Unclaimed.Num() > 0)
	{
		UE_LOG(LogOSCulator, Log, TEXT("%s: the level exposes these under mapped tags, and nothing claims them:"), *GetName());
		for (const FString& Entry : Result.Unclaimed)
		{
			UE_LOG(LogOSCulator, Log, TEXT("    %s"), *Entry);
		}

		if (Result.FunctionMissing > 0)
		{
			UE_LOG(LogOSCulator, Log,
				TEXT("%s: a renamed function appears in both lists above. Retyping the name in the broken binding keeps its sources and value settings."),
				*GetName());
		}
	}

	if (Result.Shared.Num() > 0)
	{
		UE_LOG(LogOSCulator, Log, TEXT("%s: these inputs drive more than one binding:"), *GetName());
		for (const FString& Entry : Result.Shared)
		{
			UE_LOG(LogOSCulator, Log, TEXT("    %s"), *Entry);
		}
	}

	return Result;
}

#if WITH_EDITOR

namespace
{
	/**
	 * The Blueprint behind a class, when it has edits that are not compiled in yet.
	 *
	 * This is the one failure that looked like a plugin bug and is not. A custom event
	 * added and not compiled lives on the SKELETON class; the actor in the level is an
	 * instance of the GENERATED one, which is what gets walked here. So the function is
	 * invisible until a compile moves it across -- and loading the asset fresh compiles
	 * it, which is exactly why restarting the editor appeared to be the fix.
	 *
	 * Empty for a native class, and for a Blueprint that is up to date.
	 */
	FString FindStaleBlueprintName(const UClass* Class)
	{
#if WITH_EDITORONLY_DATA
		if (Class != nullptr)
		{
			if (const UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy.Get()))
			{
				if (!Blueprint->IsUpToDate())
				{
					return Blueprint->GetName();
				}
			}
		}
#endif
		return FString();
	}

	/** "BP_Laser_2, BP_Laser_3", capped so one tag on forty actors stays readable. */
	FString DescribeActors(const FOscuExposedTagInfo& TagInfo)
	{
		TArray<FString> Names;
		for (const TWeakObjectPtr<AActor>& Weak : TagInfo.Actors)
		{
			if (const AActor* Actor = Weak.Get())
			{
				Names.Add(Actor->GetName());
			}
			if (Names.Num() == 4)
			{
				Names.Add(FString::Printf(TEXT("+%d more"), TagInfo.Actors.Num() - 4));
				break;
			}
		}
		return FString::Join(Names, TEXT(", "));
	}

	/**
	 * Says what the level actually offers, before anything is merged into the asset.
	 *
	 * Exists because "+0 binding(s) listed" was the whole answer to four different
	 * problems -- nothing tagged, a prefix that does not match the tags, a tag whose
	 * Blueprint is not compiled, and a tag whose instance never inherited it from its
	 * class defaults -- and gave no way to tell which one you had. A click that found
	 * nothing should say what it looked at.
	 */
	void ReportLevelInventory(const UOscuMIDIMap& Map, const UOscuRouterSubsystem& Router,
		const TArray<FOscuExposedTagInfo>& Tags)
	{
		const FOscuScanReport& Scan = Router.GetLastScan();
		const FString& Prefix = UOscuSettings::Get()->TagPrefix;

		UE_LOG(LogOSCulator, Log,
			TEXT("%s: level inventory -- %d tag(s) from %d tagged actor(s), %d actor(s) walked."),
			*Map.GetName(), Tags.Num(), Scan.ActorsTagged, Scan.ActorsScanned);

		for (const FOscuExposedTagInfo& TagInfo : Tags)
		{
			UE_LOG(LogOSCulator, Log, TEXT("    %-24s %2d actor(s)  %2d mappable function(s)   [%s]"),
				*TagInfo.Tag.ToString(), TagInfo.Actors.Num(), TagInfo.Functions.Num(), *DescribeActors(TagInfo));
		}

		if (Tags.Num() == 0)
		{
			UE_LOG(LogOSCulator, Warning,
				TEXT("%s: nothing in this level carries a '%s' tag, so there is nothing to map. "
					 "%d actor(s) walked; %d of them are tagged with something that is not prefixed '%s'. "
					 "The prefix is Project Settings > Plugins > OSCulator > Registry > Tag Prefix."),
				*Map.GetName(), *Prefix, Scan.ActorsScanned, Scan.ActorsWithUnprefixedTags, *Prefix);
		}

		// A tag that registered but offers nothing. Almost always an uncompiled
		// Blueprint, which is the one cause a user cannot see from the panel.
		for (const FOscuExposedTagInfo& TagInfo : Tags)
		{
			if (TagInfo.Functions.Num() > 0)
			{
				continue;
			}

			TArray<FString> Stale;
			TArray<FString> Fresh;
			for (const TWeakObjectPtr<AActor>& Weak : TagInfo.Actors)
			{
				const AActor* Actor = Weak.Get();
				if (Actor == nullptr)
				{
					continue;
				}

				const FString StaleName = FindStaleBlueprintName(Actor->GetClass());
				if (!StaleName.IsEmpty())
				{
					Stale.AddUnique(StaleName);
				}
				else
				{
					Fresh.AddUnique(Actor->GetClass()->GetName());
				}
			}

			UE_LOG(LogOSCulator, Warning,
				TEXT("%s: tag '%s' has %d actor(s) but no Blueprint-authored function that MIDI can drive."),
				*Map.GetName(), *TagInfo.Tag.ToString(), TagInfo.Actors.Num());

			if (Stale.Num() > 0)
			{
				UE_LOG(LogOSCulator, Warning,
					TEXT("    %s has uncompiled changes. A function added since the last compile is not on the class yet -- "
						 "compile the Blueprint and click Auto-Map again."),
					*FString::Join(Stale, TEXT(", ")));
			}
			if (Fresh.Num() > 0)
			{
				UE_LOG(LogOSCulator, Warning,
					TEXT("    %s is compiled, so either it authors no events, or its parameters cannot be marshalled. "
						 "Search this log for 'skipped:' to see the reason each function was turned down."),
					*FString::Join(Fresh, TEXT(", ")));
			}
		}

		// The tag is on the Blueprint and not on the thing in the level.
		if (Scan.TaggedOnlyOnClassDefaults.Num() > 0)
		{
			UE_LOG(LogOSCulator, Warning,
				TEXT("%s: these actors carry a prefixed tag on their class defaults that the placed instance did not inherit:"),
				*Map.GetName());
			for (const FString& Entry : Scan.TaggedOnlyOnClassDefaults)
			{
				UE_LOG(LogOSCulator, Warning, TEXT("    %s"), *Entry);
			}
			UE_LOG(LogOSCulator, Warning,
				TEXT("    Add the tag on the actor in the level, or delete and re-place it. Reopening the level "
					 "re-reads Tags from the class, which is why restarting the editor looks like the fix."));
		}
	}
}

int32 UOscuMIDIMap::AutoMapFromWorld(UWorld* World)
{
	UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(World);
	if (Router == nullptr)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("Auto-map found no registry for this world."));
		return 0;
	}

	// The editor world never runs OnWorldBeginPlay, so the registry has to be filled
	// explicitly before it can be asked anything.
	Router->ScanWorld();

	// Blueprint-authored only. A fully exposed actor drags in a couple of hundred
	// inherited engine functions, and offering those as pads would be absurd.
	const TArray<FOscuExposedTagInfo> Tags = Router->Introspect(EOscuIntrospectFilter::Custom);

	// Before the merge, not after: what the level offers is the answer to "why did
	// nothing appear", and it is true whether anything was added or not.
	ReportLevelInventory(*this, *Router, Tags);

	int32 TotalAdded = 0;
	int32 TotalAssigned = 0;

	for (const FOscuExposedTagInfo& TagInfo : Tags)
	{
		TArray<FName> FunctionNames;
		TMap<FName, TArray<FOscuExposedParamInfo>> ParamsByFunction;
		FunctionNames.Reserve(TagInfo.Functions.Num());

		for (const FOscuExposedFunctionInfo& Function : TagInfo.Functions)
		{
			FunctionNames.Add(Function.FunctionName);

			// The whole parameter list, not just the first one's type label. It decides
			// trigger versus continuous as it always did, and now also which parameters
			// velocity and pitch are pointed at -- which needs the names, and needs to
			// see past the first entry.
			ParamsByFunction.Add(Function.FunctionName, Function.Params);
		}

		TotalAdded += MergeFunctions(TagInfo.Tag, FunctionNames, ParamsByFunction, &TotalAssigned);
	}

	UE_LOG(LogOSCulator, Log,
		TEXT("%s: +%d binding(s) listed, %d input(s) assigned, 0 existing entries changed."),
		*GetName(), TotalAdded, TotalAssigned);

	if (AutoMapRules.Num() == 0)
	{
		UE_LOG(LogOSCulator, Warning,
			TEXT("%s: no auto-map rules, so nothing could be assigned an input. Add a rule per tag -- device, channel, first note, first CC -- and click again."),
			*GetName());
	}

	return TotalAdded;
}

void UOscuMIDIMap::AutoMapFromLevel()
{
	UWorld* World = (GEditor != nullptr) ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("Auto-map needs an open level."));
		return;
	}

	if (AutoMapFromWorld(World) > 0)
	{
		MarkPackageDirty();
	}

	// Always, even when nothing was added -- especially when nothing was added. "No new
	// functions" and "three of your bindings are broken" are two halves of the same
	// answer, and one click should give both.
	Validate(World);
	PostEditChange();
}

void UOscuMIDIMap::ValidateAgainstLevel()
{
	UWorld* World = (GEditor != nullptr) ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("Validation needs an open level."));
		return;
	}

	const FOscuMIDIValidation Result = Validate(World);

	if (Result.CouldDefaultValueParams > 0)
	{
		// Said, not done. Validate never edits, so this points at the button that may.
		UE_LOG(LogOSCulator, Log,
			TEXT("%s: %d binding(s) have a numeric parameter but name none for velocity or pitch. "
				 "Click Auto-Map From Level to point them at the first two, or set them by hand -- "
				 "Available Parameters on each row lists the names."),
			*GetName(), Result.CouldDefaultValueParams);
	}

	// Nothing was edited, so the package is deliberately not dirtied. Nudge the panel
	// so the new titles appear without a reselect.
	PostEditChange();
}

void UOscuMIDIMap::PruneMissingFunctions()
{
	UWorld* World = (GEditor != nullptr) ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("Pruning needs an open level -- it has to see what exists before deleting anything."));
		return;
	}

	// Re-checked here rather than trusting whatever the last pass decided. The level may
	// have changed since, and this is the one action in this asset that destroys work.
	const FOscuMIDIValidation Before = Validate(World);
	if (Before.FunctionMissing == 0)
	{
		UE_LOG(LogOSCulator, Log, TEXT("%s: nothing to prune."), *GetName());
		return;
	}

	int32 Removed = 0;
	for (int32 Index = Bindings.Num() - 1; Index >= 0; --Index)
	{
		// Only FunctionMissing. A binding whose TAG is absent is almost certainly for
		// another level, and deleting a show's mappings because the wrong map was open
		// is not a recoverable mistake.
		if (Bindings[Index].Status != EOscuMIDIRowStatus::FunctionMissing)
		{
			continue;
		}

		UE_LOG(LogOSCulator, Log, TEXT("%s: pruned %s"), *GetName(), *Bindings[Index].Summary);

		// RemoveAt, not RemoveAtSwap: these rows are read in order by a human.
		Bindings.RemoveAt(Index);
		++Removed;
	}

	UE_LOG(LogOSCulator, Log, TEXT("%s: pruned %d binding(s). %d with a missing tag were left alone."),
		*GetName(), Removed, Before.TagMissing);

	if (Removed > 0)
	{
		Refresh();
		MarkPackageDirty();
		PostEditChange();
	}
}

#endif // WITH_EDITOR
