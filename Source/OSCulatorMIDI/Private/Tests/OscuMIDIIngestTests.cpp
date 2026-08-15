// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDISubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuMIDIMap.h"
#include "OscuSettings.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

namespace OscuMIDITest
{
	/**
	 * Notes are given as bare numbers rather than names wherever the test is not
	 * specifically about naming, so that a project's MiddleCOctave setting cannot
	 * change what these tests mean.
	 */
	FOscuMIDINoteMap MakeNote(const TCHAR* NoteText, const TCHAR* FunctionName, EOscuMIDIValueMode Mode = EOscuMIDIValueMode::Normalized01)
	{
		FOscuMIDINoteMap Note;
		Note.Note = NoteText;
		Note.FunctionName = FName(FunctionName);
		Note.Mode = Mode;
		return Note;
	}

	UOscuMIDIMap* MakeMap(uint8 Channel, const TCHAR* Tag, std::initializer_list<FOscuMIDINoteMap> Notes)
	{
		UOscuMIDIMap* Map = NewObject<UOscuMIDIMap>(GetTransientPackage());

		FOscuMIDIChannelMap ChannelMap;
		ChannelMap.Channel = Channel;
		ChannelMap.Tag = FName(Tag);
		ChannelMap.Notes.Append(Notes);

		Map->Channels.Add(MoveTemp(ChannelMap));
		Map->Refresh();
		return Map;
	}

	/** Puts the subsystem's map back however the test leaves. */
	struct FScopedActiveMap
	{
		UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
		UOscuMIDIMap* Saved = nullptr;

		explicit FScopedActiveMap(UOscuMIDIMap* Map)
		{
			if (MIDI != nullptr)
			{
				Saved = MIDI->GetActiveMap();
				MIDI->SetActiveMap(Map);
			}
		}

		~FScopedActiveMap()
		{
			if (MIDI != nullptr)
			{
				MIDI->SetActiveMap(Saved);
			}
		}
	};
}

//////////////////////////////////////////////////////////////////////////
// The Phase 6 acceptance test

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIIngestTest,
	"OSCulator.MIDI.Ingest",
	OscuTest::Flags)

bool FOscuMIDIIngestTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuMIDITest;

	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	if (!TestNotNull(TEXT("The MIDI engine subsystem exists"), MIDI))
	{
		return false;
	}

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuMIDIMap* Map = MakeMap(1, TEXT("laser"), {
		MakeNote(TEXT("60"), TEXT("SetIntensity")),
		MakeNote(TEXT("62"), TEXT("Fire")),
		MakeNote(TEXT("64"), TEXT("Stop")),
	});
	FScopedActiveMap ActiveMap(Map);

	// A clean single-parameter mapping: full velocity normalises to 1.0.
	{
		const int32 Calls = MIDI->IngestNote(1, 60, 127, /*bNoteOn*/ true);
		TestEqual(TEXT("The note fired one actor"), Calls, 1);
		TestEqual(TEXT("It called the mapped function"), Laser->LastCalled, FName("SetIntensity"));
		TestEqual(TEXT("Velocity 127 normalises to 1.0"), Laser->LastIntensity, 1.0);
	}

	{
		MIDI->IngestNote(1, 60, 64, true);
		TestEqual(TEXT("Velocity 64 normalises to 64/127"), Laser->LastIntensity, 64.0 / 127.0, 0.0001);
	}

	// The acceptance criterion proper: velocity in parameter 0, zeroes elsewhere.
	// Fire is (FVector Dir, FName Mode, float Power), so the single supplied value
	// lands in Dir.X and everything after it keeps what the zeroed frame gave it.
	// This is why an author wanting a MIDI-triggerable event puts the
	// velocity-relevant parameter first.
	{
		Laser->LastMode = TEXT("stale");
		Laser->LastPower = 99.0f;

		const int32 Calls = MIDI->IngestNote(1, 62, 127, true);
		TestEqual(TEXT("A five-argument signature still fires from one MIDI value"), Calls, 1);
		TestEqual(TEXT("Called Fire"), Laser->LastCalled, FName("Fire"));
		TestEqual(TEXT("Velocity landed in parameter 0"), Laser->LastDir.X, 1.0);
		TestEqual(TEXT("Parameter 0's remaining components are zero"), Laser->LastDir.Y, 0.0);
		TestEqual(TEXT("...and zero"), Laser->LastDir.Z, 0.0);
		TestEqual(TEXT("Later parameters are zeroed, not stale"), Laser->LastMode, FName());
		TestEqual(TEXT("...and zeroed"), Laser->LastPower, 0.0f);
	}

	// A zero-argument trigger takes the velocity and discards it, because surplus
	// arguments are tolerated.
	{
		const int32 Calls = MIDI->IngestNote(1, 64, 100, true);
		TestEqual(TEXT("A zero-argument trigger fires from a note"), Calls, 1);
		TestEqual(TEXT("Called Stop"), Laser->LastCalled, FName("Stop"));
	}

	// Note off is ignored unless the row asks for it.
	{
		const int32 CallsBefore = Laser->CallCount;
		MIDI->IngestNote(1, 60, 0, /*bNoteOn*/ false);
		TestEqual(TEXT("Note off does nothing by default"), Laser->CallCount, CallsBefore);
	}

	// Unmapped notes and channels pass through untouched.
	{
		const uint64 UnmappedBefore = MIDI->GetNotesUnmapped();
		TestEqual(TEXT("An unmapped note calls nothing"), MIDI->IngestNote(1, 99, 127, true), 0);
		TestEqual(TEXT("A mapped note on the wrong channel calls nothing"), MIDI->IngestNote(2, 60, 127, true), 0);
		TestEqual(TEXT("Both were counted as unmapped"), MIDI->GetNotesUnmapped(), UnmappedBefore + 2);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Value modes and note off

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIValueModeTest,
	"OSCulator.MIDI.ValueModes",
	OscuTest::Flags)

bool FOscuMIDIValueModeTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuMIDITest;

	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	if (!TestNotNull(TEXT("The MIDI engine subsystem exists"), MIDI))
	{
		return false;
	}

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();

	FOscuMIDINoteMap OffCapable = MakeNote(TEXT("67"), TEXT("SetIntensity"));
	OffCapable.bFireOnNoteOff = true;

	UOscuMIDIMap* Map = MakeMap(1, TEXT("laser"), {
		MakeNote(TEXT("60"), TEXT("SetIntensity"), EOscuMIDIValueMode::RawVelocity),
		MakeNote(TEXT("62"), TEXT("Chase"), EOscuMIDIValueMode::NoteAndVelocity),
		OffCapable,
	});
	FScopedActiveMap ActiveMap(Map);

	// Raw velocity is passed through unscaled.
	MIDI->IngestNote(1, 60, 100, true);
	TestEqual(TEXT("RawVelocity passes 0-127 through"), Laser->LastIntensity, 100.0);

	// NoteAndVelocity sends two arguments: the note number, then the normalised
	// velocity. Chase is (float Speed, TArray<float> Points), so the note lands in
	// Speed and the velocity is swallowed by the trailing array.
	MIDI->IngestNote(1, 62, 127, true);
	TestEqual(TEXT("NoteAndVelocity puts the note number first"), Laser->LastSpeed, 62.0f);
	if (TestEqual(TEXT("...and the velocity follows it"), Laser->LastPoints.Num(), 1))
	{
		TestEqual(TEXT("...normalised"), Laser->LastPoints[0], 1.0f);
	}

	// With bFireOnNoteOff, the note off fires with a velocity of zero.
	MIDI->IngestNote(1, 67, 127, true);
	TestEqual(TEXT("Note on carries its velocity"), Laser->LastIntensity, 1.0);

	const int32 CallsBefore = Laser->CallCount;
	MIDI->IngestNote(1, 67, 0, /*bNoteOn*/ false);
	TestEqual(TEXT("Note off fired too"), Laser->CallCount, CallsBefore + 1);
	TestEqual(TEXT("Note off sends zero"), Laser->LastIntensity, 0.0);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// The map asset itself

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIMapTest,
	"OSCulator.MIDI.Map",
	OscuTest::Flags)

bool FOscuMIDIMapTest::RunTest(const FString& Parameters)
{
	using namespace OscuMIDITest;

	// Pin the octave convention for the duration, so name resolution is being
	// tested rather than the project's setting.
	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	const int32 SavedMiddleC = Settings->MiddleCOctave;
	Settings->MiddleCOctave = 3;

	{
		UOscuMIDIMap* Map = MakeMap(1, TEXT("laser"), {
			MakeNote(TEXT("C3"), TEXT("Fire")),
			MakeNote(TEXT("Db3"), TEXT("Stop")),
		});

		// Names resolve to numbers, and the display string is normalised to sharps
		// so a row reads the same as everything else will show it.
		if (TestEqual(TEXT("Both rows survived"), Map->Channels[0].Notes.Num(), 2))
		{
			TestEqual(TEXT("C3 resolves to 60"), static_cast<int32>(Map->Channels[0].Notes[0].ResolvedNote), 60);
			TestEqual(TEXT("Db3 resolves to 61"), static_cast<int32>(Map->Channels[0].Notes[1].ResolvedNote), 61);
			TestEqual(TEXT("Db3 is rewritten as C#3"), Map->Channels[0].Notes[1].Note, FString(TEXT("C#3")));
		}

		// The flat lookup finds by channel and note, and carries the channel's tag.
		const FOscuMIDIMatch Match = Map->FindMapping(1, 60);
		if (TestTrue(TEXT("Channel 1 note 60 is mapped"), Match.IsValid()))
		{
			TestEqual(TEXT("It carries the channel's tag"), Match.Tag, FName("laser"));
			TestEqual(TEXT("...and the right function"), Match.Note->FunctionName, FName("Fire"));
		}

		TestFalse(TEXT("An unmapped note is not found"), Map->FindMapping(1, 99).IsValid());
		TestFalse(TEXT("The right note on the wrong channel is not found"), Map->FindMapping(2, 60).IsValid());

		TestTrue(TEXT("IsNoteTaken sees a mapped note"), Map->IsNoteTaken(1, 60));
		TestFalse(TEXT("IsNoteTaken rejects a free one"), Map->IsNoteTaken(1, 99));
	}

	{
		// Channel numbering is 1-16 in the asset AND in what MIDIDevice delivers --
		// its input controller computes (Status % 16) + 1 before broadcasting -- so
		// there is no conversion at ingest. If someone ever adds one, this fails.
		UOscuMIDIMap* Map = MakeMap(10, TEXT("drums"), { MakeNote(TEXT("36"), TEXT("Hit")) });

		TestTrue(TEXT("Channel 10 is found as channel 10"), Map->FindMapping(10, 36).IsValid());
		TestFalse(TEXT("...not as channel 9"), Map->FindMapping(9, 36).IsValid());
		TestFalse(TEXT("...and not as channel 11"), Map->FindMapping(11, 36).IsValid());
	}

	Settings->MiddleCOctave = SavedMiddleC;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
