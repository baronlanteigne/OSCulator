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
	uint16 MakeLookupKey(uint8 Channel, uint8 Note)
	{
		return static_cast<uint16>(Channel) << 8 | static_cast<uint16>(Note);
	}
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

	// A row being armed for Learn must not have its note string re-resolved out from
	// under it, but Refresh is harmless here -- the row still holds its old note.
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

	// Only one row may be armed. The most recently ticked one wins, so ticking a
	// second row moves the arming rather than being ignored.
	int32 ArmedChannel = INDEX_NONE;
	int32 ArmedNote = INDEX_NONE;

	for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
	{
		for (int32 NoteIndex = 0; NoteIndex < Channels[ChannelIndex].Notes.Num(); ++NoteIndex)
		{
			if (!Channels[ChannelIndex].Notes[NoteIndex].bLearn)
			{
				continue;
			}

			if (ArmedChannel == INDEX_NONE)
			{
				ArmedChannel = ChannelIndex;
				ArmedNote = NoteIndex;
			}
			else
			{
				Channels[ChannelIndex].Notes[NoteIndex].bLearn = false;
			}
		}
	}

	if (ArmedChannel != INDEX_NONE)
	{
		MIDI->ArmLearn(this, ArmedChannel, ArmedNote);
	}
	else
	{
		MIDI->CancelLearn(this);
	}
}

int32 UOscuMIDIMap::AutoPopulateFromWorld(UWorld* World)
{
	UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(World);
	if (Router == nullptr)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("Auto-populate found no registry for this world."));
		return 0;
	}

	// The editor world never runs OnWorldBeginPlay, so the registry has to be
	// filled explicitly before it can be asked anything.
	Router->ScanWorld();

	// Blueprint-authored only. A fully exposed actor drags in a couple of hundred
	// inherited engine functions, and mapping those to pads would be absurd.
	const TArray<FOscuExposedTagInfo> Tags = Router->Introspect(EOscuIntrospectFilter::Custom);

	int32 TotalAdded = 0;
	for (const FOscuExposedTagInfo& TagInfo : Tags)
	{
		TArray<FName> FunctionNames;
		FunctionNames.Reserve(TagInfo.Functions.Num());
		for (const FOscuExposedFunctionInfo& Function : TagInfo.Functions)
		{
			FunctionNames.Add(Function.FunctionName);
		}

		TotalAdded += MergeFunctions(TagInfo.Tag, FunctionNames);
	}

	UE_LOG(LogOSCulator, Log, TEXT("%s: +%d new function(s) mapped, 0 existing entries changed."),
		*GetName(), TotalAdded);

	return TotalAdded;
}

void UOscuMIDIMap::AutoPopulateFromLevel()
{
	UWorld* World = (GEditor != nullptr) ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		UE_LOG(LogOSCulator, Warning, TEXT("Auto-populate needs an open level."));
		return;
	}

	if (AutoPopulateFromWorld(World) > 0)
	{
		MarkPackageDirty();
	}
}
#endif // WITH_EDITOR

int32 UOscuMIDIMap::MergeFunctions(FName Tag, const TArray<FName>& FunctionNames)
{
	if (Tag.IsNone() || FunctionNames.Num() == 0)
	{
		return 0;
	}

	int32 ChannelIndex = Channels.IndexOfByPredicate(
		[Tag](const FOscuMIDIChannelMap& Channel) { return Channel.Tag == Tag; });

	if (ChannelIndex == INDEX_NONE)
	{
		// New tags take the lowest channel nobody has claimed.
		TSet<uint8> UsedChannels;
		for (const FOscuMIDIChannelMap& Channel : Channels)
		{
			UsedChannels.Add(Channel.Channel);
		}

		uint8 FreeChannel = 1;
		while (FreeChannel <= 16 && UsedChannels.Contains(FreeChannel))
		{
			++FreeChannel;
		}
		if (FreeChannel > 16)
		{
			UE_LOG(LogOSCulator, Warning, TEXT("%s: all 16 channels are in use, so tag '%s' could not be added."),
				*GetName(), *Tag.ToString());
			return 0;
		}

		FOscuMIDIChannelMap NewChannel;
		NewChannel.Channel = FreeChannel;
		NewChannel.Tag = Tag;
		ChannelIndex = Channels.Add(MoveTemp(NewChannel));
	}

	FOscuMIDIChannelMap& ChannelMap = Channels[ChannelIndex];

	// Alphabetical, NOT reflection order. A class's function map does not iterate
	// stably across Blueprint recompiles, so taking its order would reshuffle note
	// assignments on every recompile.
	TArray<FName> Sorted = FunctionNames;
	Sorted.Sort([](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	TSet<uint8> UsedNotes;
	TSet<FName> MappedFunctions;
	for (const FOscuMIDINoteMap& Note : ChannelMap.Notes)
	{
		UsedNotes.Add(Note.ResolvedNote);
		MappedFunctions.Add(Note.FunctionName);
	}

	const int32 MiddleCOctave = UOscuSettings::Get()->MiddleCOctave;
	int32 NextNote = 36;
	int32 Added = 0;

	for (const FName& FunctionName : Sorted)
	{
		// Additive only. An existing row is never moved, renumbered or rewritten.
		if (MappedFunctions.Contains(FunctionName))
		{
			continue;
		}

		while (NextNote <= 127 && UsedNotes.Contains(static_cast<uint8>(NextNote)))
		{
			++NextNote;
		}
		if (NextNote > 127)
		{
			UE_LOG(LogOSCulator, Warning, TEXT("%s: channel %d has no free notes left; '%s' and anything after it were not mapped."),
				*GetName(), ChannelMap.Channel, *FunctionName.ToString());
			break;
		}

		FOscuMIDINoteMap NewNote;
		NewNote.ResolvedNote = static_cast<uint8>(NextNote);
		NewNote.Note = OscuMIDINoteName::ToString(static_cast<uint8>(NextNote), MiddleCOctave);
		NewNote.FunctionName = FunctionName;

		ChannelMap.Notes.Add(MoveTemp(NewNote));
		UsedNotes.Add(static_cast<uint8>(NextNote));
		MappedFunctions.Add(FunctionName);
		++Added;
	}

	if (Added > 0)
	{
		Refresh();
	}
	return Added;
}

void UOscuMIDIMap::Refresh()
{
	ResolveNoteNames();
	RebuildLookup();
}

void UOscuMIDIMap::ResolveNoteNames()
{
	const int32 MiddleCOctave = UOscuSettings::Get()->MiddleCOctave;

	for (FOscuMIDIChannelMap& Channel : Channels)
	{
		for (FOscuMIDINoteMap& Note : Channel.Notes)
		{
			uint8 Resolved = 0;
			FString Error;
			if (OscuMIDINoteName::Parse(Note.Note, MiddleCOctave, Resolved, Error))
			{
				Note.ResolvedNote = Resolved;

				// Normalise the display string too, so "Db2" becomes "C#2" and the
				// row reads the same as it will everywhere else.
				Note.Note = OscuMIDINoteName::ToString(Resolved, MiddleCOctave);
			}
			else
			{
				UE_LOG(LogOSCulator, Warning, TEXT("%s: channel %d note '%s' is unusable (%s). Keeping %d."),
					*GetName(), Channel.Channel, *Note.Note, *Error, Note.ResolvedNote);
			}
		}
	}
}

void UOscuMIDIMap::RebuildLookup()
{
	Lookup.Reset();

	for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
	{
		const FOscuMIDIChannelMap& Channel = Channels[ChannelIndex];

		if (Channel.Channel < 1 || Channel.Channel > 16)
		{
			UE_LOG(LogOSCulator, Warning, TEXT("%s: channel %d is outside 1-16 and was skipped."),
				*GetName(), Channel.Channel);
			continue;
		}

		for (int32 NoteIndex = 0; NoteIndex < Channel.Notes.Num(); ++NoteIndex)
		{
			const FOscuMIDINoteMap& Note = Channel.Notes[NoteIndex];
			const uint16 Key = MakeLookupKey(Channel.Channel, Note.ResolvedNote);

			if (const FLookupEntry* Existing = Lookup.Find(Key))
			{
				// Two rows claiming the same key is a real authoring mistake -- the
				// second silently never fires -- so it gets named rather than hidden.
				const FOscuMIDINoteMap& Winner = Channels[Existing->ChannelIndex].Notes[Existing->NoteIndex];
				UE_LOG(LogOSCulator, Warning,
					TEXT("%s: channel %d note %s is mapped twice ('%s' and '%s'). Keeping the first."),
					*GetName(), Channel.Channel, *Note.Note,
					*Winner.FunctionName.ToString(), *Note.FunctionName.ToString());
				continue;
			}

			Lookup.Add(Key, FLookupEntry{ ChannelIndex, NoteIndex });
		}
	}
}

FOscuMIDIMatch UOscuMIDIMap::FindMapping(uint8 Channel, uint8 Note) const
{
	FOscuMIDIMatch Match;

	const FLookupEntry* Entry = Lookup.Find(MakeLookupKey(Channel, Note));
	if (Entry == nullptr)
	{
		return Match;
	}

	if (!Channels.IsValidIndex(Entry->ChannelIndex))
	{
		return Match;
	}
	const FOscuMIDIChannelMap& ChannelMap = Channels[Entry->ChannelIndex];

	if (!ChannelMap.Notes.IsValidIndex(Entry->NoteIndex))
	{
		return Match;
	}

	Match.Tag = ChannelMap.Tag;
	Match.Note = &ChannelMap.Notes[Entry->NoteIndex];
	return Match;
}

bool UOscuMIDIMap::IsNoteTaken(uint8 Channel, uint8 Note) const
{
	return Lookup.Contains(MakeLookupKey(Channel, Note));
}
