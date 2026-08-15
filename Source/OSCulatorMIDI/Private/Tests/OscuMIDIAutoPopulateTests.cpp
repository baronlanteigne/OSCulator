// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDIMap.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuSettings.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

namespace OscuAutoPopulateTest
{
	UOscuMIDIMap* MakeEmptyMap()
	{
		return NewObject<UOscuMIDIMap>(GetTransientPackage());
	}

	const FOscuMIDINoteMap* FindRow(const UOscuMIDIMap& Map, FName Tag, FName FunctionName)
	{
		for (const FOscuMIDIChannelMap& Channel : Map.Channels)
		{
			if (Channel.Tag != Tag)
			{
				continue;
			}
			for (const FOscuMIDINoteMap& Note : Channel.Notes)
			{
				if (Note.FunctionName == FunctionName)
				{
					return &Note;
				}
			}
		}
		return nullptr;
	}
}

//////////////////////////////////////////////////////////////////////////
// The other half of the Phase 6 acceptance test:
// auto-populate run twice produces zero changes the second time.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIAutoPopulateTest,
	"OSCulator.MIDI.AutoPopulate",
	OscuTest::Flags)

bool FOscuMIDIAutoPopulateTest::RunTest(const FString& Parameters)
{
	using namespace OscuAutoPopulateTest;

	// Pin the octave convention so note names are predictable.
	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	const int32 SavedMiddleC = Settings->MiddleCOctave;
	Settings->MiddleCOctave = 3;

	{
		UOscuMIDIMap* Map = MakeEmptyMap();

		// Deliberately NOT alphabetical, standing in for a class's function map --
		// whose iteration order is not stable across Blueprint recompiles.
		const TArray<FName> Functions = { FName("Stop"), FName("Fire"), FName("Aim") };

		const int32 FirstRun = Map->MergeFunctions(FName("laser"), Functions);
		TestEqual(TEXT("First run maps all three"), FirstRun, 3);

		if (!TestEqual(TEXT("A channel was created for the tag"), Map->Channels.Num(), 1))
		{
			Settings->MiddleCOctave = SavedMiddleC;
			return false;
		}
		TestEqual(TEXT("New tags start at channel 1"), static_cast<int32>(Map->Channels[0].Channel), 1);

		// Assigned alphabetically from note 36, whatever order they arrived in.
		TestEqual(TEXT("Aim took note 36"), static_cast<int32>(FindRow(*Map, TEXT("laser"), TEXT("Aim"))->ResolvedNote), 36);
		TestEqual(TEXT("Fire took note 37"), static_cast<int32>(FindRow(*Map, TEXT("laser"), TEXT("Fire"))->ResolvedNote), 37);
		TestEqual(TEXT("Stop took note 38"), static_cast<int32>(FindRow(*Map, TEXT("laser"), TEXT("Stop"))->ResolvedNote), 38);

		// Note 36 is C1 when middle C is 3.
		TestEqual(TEXT("Names are written alongside the numbers"), FindRow(*Map, TEXT("laser"), TEXT("Aim"))->Note, FString(TEXT("C1")));

		// THE acceptance criterion.
		const int32 SecondRun = Map->MergeFunctions(FName("laser"), Functions);
		TestEqual(TEXT("Second run changes nothing"), SecondRun, 0);
		TestEqual(TEXT("...and adds no rows"), Map->Channels[0].Notes.Num(), 3);

		// Order shuffled, as a recompile would. Still nothing.
		const TArray<FName> Reordered = { FName("Fire"), FName("Aim"), FName("Stop") };
		TestEqual(TEXT("A reordered third run changes nothing"), Map->MergeFunctions(FName("laser"), Reordered), 0);
		TestEqual(TEXT("Aim is still on note 36"), static_cast<int32>(FindRow(*Map, TEXT("laser"), TEXT("Aim"))->ResolvedNote), 36);
		TestEqual(TEXT("Fire is still on note 37"), static_cast<int32>(FindRow(*Map, TEXT("laser"), TEXT("Fire"))->ResolvedNote), 37);
	}

	{
		// Additive only: a hand-edited row must survive untouched, and a genuinely
		// new function must be appended without disturbing it.
		UOscuMIDIMap* Map = MakeEmptyMap();
		Map->MergeFunctions(FName("laser"), { FName("Fire") });

		// The user moves Fire to note 60 and switches its mode by hand.
		Map->Channels[0].Notes[0].Note = TEXT("C3");
		Map->Channels[0].Notes[0].Mode = EOscuMIDIValueMode::RawVelocity;
		Map->Channels[0].Notes[0].bFireOnNoteOff = true;
		Map->Refresh();

		const int32 Added = Map->MergeFunctions(FName("laser"), { FName("Fire"), FName("Stop") });
		TestEqual(TEXT("Only the new function is added"), Added, 1);

		const FOscuMIDINoteMap* Fire = FindRow(*Map, TEXT("laser"), TEXT("Fire"));
		if (TestNotNull(TEXT("The hand-edited row survives"), Fire))
		{
			TestEqual(TEXT("Its note was not renumbered"), static_cast<int32>(Fire->ResolvedNote), 60);
			TestEqual(TEXT("Its mode was not reset"), static_cast<int32>(Fire->Mode), static_cast<int32>(EOscuMIDIValueMode::RawVelocity));
			TestTrue(TEXT("Its note-off flag was not reset"), Fire->bFireOnNoteOff);
		}

		// The new row takes the lowest free note, skipping nothing it should not.
		const FOscuMIDINoteMap* Stop = FindRow(*Map, TEXT("laser"), TEXT("Stop"));
		if (TestNotNull(TEXT("The new function was appended"), Stop))
		{
			TestEqual(TEXT("It took the lowest free note from 36"), static_cast<int32>(Stop->ResolvedNote), 36);
		}
	}

	{
		// Several tags take consecutive channels, and existing tags keep theirs.
		UOscuMIDIMap* Map = MakeEmptyMap();
		Map->MergeFunctions(FName("laser"), { FName("Fire") });
		Map->MergeFunctions(FName("fog"), { FName("Burst") });
		Map->MergeFunctions(FName("lights"), { FName("Flash") });

		if (TestEqual(TEXT("Three tags, three channels"), Map->Channels.Num(), 3))
		{
			TestEqual(TEXT("Channels are assigned in order from 1"), static_cast<int32>(Map->Channels[0].Channel), 1);
			TestEqual(TEXT("...2"), static_cast<int32>(Map->Channels[1].Channel), 2);
			TestEqual(TEXT("...3"), static_cast<int32>(Map->Channels[2].Channel), 3);
		}

		// Each tag numbers its notes independently, so every channel starts at 36.
		TestEqual(TEXT("Each channel's notes start at 36"), static_cast<int32>(FindRow(*Map, TEXT("fog"), TEXT("Burst"))->ResolvedNote), 36);

		// Re-running everything is still a no-op.
		int32 Total = 0;
		Total += Map->MergeFunctions(FName("laser"), { FName("Fire") });
		Total += Map->MergeFunctions(FName("fog"), { FName("Burst") });
		Total += Map->MergeFunctions(FName("lights"), { FName("Flash") });
		TestEqual(TEXT("Re-running every tag changes nothing"), Total, 0);
		TestEqual(TEXT("No extra channels appeared"), Map->Channels.Num(), 3);
	}

	{
		// A gap left by a deleted row is reused rather than skipped.
		UOscuMIDIMap* Map = MakeEmptyMap();
		Map->MergeFunctions(FName("laser"), { FName("Aim"), FName("Fire"), FName("Stop") });
		Map->Channels[0].Notes.RemoveAt(1);   // Fire, which held note 37
		Map->Refresh();

		TestEqual(TEXT("Re-adding the removed function"), Map->MergeFunctions(FName("laser"), { FName("Fire") }), 1);
		TestEqual(TEXT("It reuses the freed note"), static_cast<int32>(FindRow(*Map, TEXT("laser"), TEXT("Fire"))->ResolvedNote), 37);
	}

	Settings->MiddleCOctave = SavedMiddleC;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
