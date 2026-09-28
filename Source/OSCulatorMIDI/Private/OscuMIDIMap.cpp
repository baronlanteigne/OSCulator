// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDIMap.h"

#include "OSCulatorCore.h"
#include "OscuIntrospection.h"
#include "OscuMIDINoteName.h"
#include "OscuMIDISubsystem.h"
#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace
{
	/** Device is deliberately absent: it is matched leniently at dispatch, not keyed. */
	uint32 MakeLookupKey(uint8 Channel, EOscuMIDIInputType Type, int32 Number)
	{
		return static_cast<uint32>(Channel) << 16
			| static_cast<uint32>(Type) << 8
			| static_cast<uint32>(static_cast<uint8>(Number));
	}

	const TCHAR* TypeLabel(EOscuMIDIInputType Type)
	{
		return Type == EOscuMIDIInputType::Note ? TEXT("note") : TEXT("CC");
	}

	/**
	 * A function that takes nothing is a trigger; one whose first parameter is a number
	 * is continuous. That distinction is the whole of the note-versus-controller
	 * decision, and it is already in the signature -- so nobody has to answer it twice.
	 */
	EOscuMIDIInputType InputTypeForSignature(const FString& FirstParamType)
	{
		if (FirstParamType.IsEmpty())
		{
			return EOscuMIDIInputType::Note;
		}
		if (FirstParamType == TEXT("float") || FirstParamType == TEXT("int"))
		{
			return EOscuMIDIInputType::ControlChange;
		}

		// A vector, a transform, a string: a continuous controller cannot fill one of
		// those meaningfully, so it gets a note and whatever the remaining parameters
		// are keep the zeroes the initialised frame gave them.
		return EOscuMIDIInputType::Note;
	}
}

double FOscuMIDIBinding::ShapeValue(const int32 Raw) const
{
	const double Clamped = FMath::Clamp(static_cast<double>(Raw), 0.0, 127.0);

	if (!bRemap)
	{
		return Clamped;
	}

	// Deliberately not FMath::GetMappedRangeValueClamped: OutMin may exceed OutMax on
	// purpose, which is how a fader is inverted, and clamping to a reversed range is a
	// classic way to get zero out of everything.
	return static_cast<double>(OutMin) + (Clamped / 127.0) * (static_cast<double>(OutMax) - static_cast<double>(OutMin));
}

void UOscuMIDIMap::PostLoad()
{
	Super::PostLoad();
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

void UOscuMIDIMap::Refresh()
{
	ResolveNoteNames();
	RebuildLookup();
	RebuildSummaries();
}

void UOscuMIDIMap::ResolveNoteNames()
{
	const int32 MiddleCOctave = UOscuSettings::Get()->MiddleCOctave;

	for (FOscuMIDIBinding& Binding : Bindings)
	{
		for (FOscuMIDISource& Source : Binding.Sources)
		{
			if (Source.Type != EOscuMIDIInputType::Note)
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
			Lookup.Add(MakeLookupKey(Source.Channel, Source.Type, Source.GetNumber()),
				FLookupEntry{ BindingIndex, SourceIndex });
		}
	}
}

void UOscuMIDIMap::RebuildSummaries()
{
	for (FOscuMIDIBinding& Binding : Bindings)
	{
		for (FOscuMIDISource& Source : Binding.Sources)
		{
			FString Text = Source.Type == EOscuMIDIInputType::Note
				? FString::Printf(TEXT("Note  ch %d  %s (%d)"), Source.Channel, *Source.Note, Source.ResolvedNote)
				: FString::Printf(TEXT("CC    ch %d  #%d"), Source.Channel, Source.ControlNumber);

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

int32 UOscuMIDIMap::MergeFunctions(const FName Tag, const TArray<FName>& FunctionNames, const TMap<FName, FString>& FirstParamTypes, int32* OutAssigned)
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

	int32 Added = 0;
	for (const FName& FunctionName : Sorted)
	{
		if (FindBinding(Tag, FunctionName) == nullptr)
		{
			FOscuMIDIBinding& Binding = Bindings.AddDefaulted_GetRef();
			Binding.Tag = Tag;
			Binding.FunctionName = FunctionName;
			++Added;
		}
	}

	// Phase two, and only for a tag whose layout has been described. Without a rule the
	// bindings are still listed -- the asset is meant to be a complete inventory of
	// what could be controlled -- they simply arrive unassigned.
	const FOscuMIDIAutoMapRule* Rule = AutoMapRules.FindByPredicate(
		[Tag](const FOscuMIDIAutoMapRule& Candidate) { return Candidate.Tag == Tag; });

	if (Rule == nullptr)
	{
		if (Added > 0)
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

		const FString* FirstParam = FirstParamTypes.Find(FunctionName);
		const EOscuMIDIInputType Type = InputTypeForSignature(FirstParam != nullptr ? *FirstParam : FString());

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
		Source.Type = Type;
		if (Type == EOscuMIDIInputType::Note)
		{
			Source.ResolvedNote = static_cast<uint8>(Next);
			Source.Note = OscuMIDINoteName::ToString(static_cast<uint8>(Next), MiddleCOctave);
		}
		else
		{
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

		for (const FOscuMIDISource& Source : Binding.Sources)
		{
			ByInput.FindOrAdd(MakeLookupKey(Source.Channel, Source.Type, Source.GetNumber()))
				.Add(FString::Printf(TEXT("%s/%s"), *Binding.Tag.ToString(), *Binding.FunctionName.ToString()));
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

	int32 TotalAdded = 0;
	int32 TotalAssigned = 0;

	for (const FOscuExposedTagInfo& TagInfo : Tags)
	{
		TArray<FName> FunctionNames;
		TMap<FName, FString> FirstParamTypes;
		FunctionNames.Reserve(TagInfo.Functions.Num());

		for (const FOscuExposedFunctionInfo& Function : TagInfo.Functions)
		{
			FunctionNames.Add(Function.FunctionName);

			// Only the first parameter, because that is what decides trigger versus
			// continuous. An output-only pin consumes nothing, so it is skipped when
			// looking for what the function actually wants.
			FString FirstParam;
			for (const FOscuExposedParamInfo& Param : Function.Params)
			{
				if (!Param.bOutputOnly)
				{
					FirstParam = Param.TypeLabel;
					break;
				}
			}
			FirstParamTypes.Add(Function.FunctionName, FirstParam);
		}

		TotalAdded += MergeFunctions(TagInfo.Tag, FunctionNames, FirstParamTypes, &TotalAssigned);
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

	Validate(World);

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
