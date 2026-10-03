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
	FOscuMIDISource NoteOn(uint8 Channel, const TCHAR* NoteText, FName Device = NAME_None)
	{
		FOscuMIDISource Source;
		Source.Type = EOscuMIDIInputType::Note;
		Source.Channel = Channel;
		Source.Note = NoteText;
		Source.Device = Device;
		return Source;
	}

	FOscuMIDISource ControlChange(uint8 Channel, uint8 Number, FName Device = NAME_None)
	{
		FOscuMIDISource Source;
		Source.Type = EOscuMIDIInputType::ControlChange;
		Source.Channel = Channel;
		Source.ControlNumber = Number;
		Source.Device = Device;
		return Source;
	}

	FOscuMIDIBinding Bind(const TCHAR* Tag, const TCHAR* FunctionName, std::initializer_list<FOscuMIDISource> Sources)
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(Tag);
		Binding.FunctionName = FName(FunctionName);
		Binding.Sources.Append(Sources);

		// The kind lives on the binding now and is mirrored down onto every source, so a
		// test that set it only on the source would have it overwritten by Refresh. Taken
		// from the first source so every existing call site still reads as it did.
		if (Binding.Sources.Num() > 0)
		{
			Binding.Type = Binding.Sources[0].Type;
		}

		return Binding;
	}

	UOscuMIDIMap* MakeMap(std::initializer_list<FOscuMIDIBinding> Bindings)
	{
		UOscuMIDIMap* Map = NewObject<UOscuMIDIMap>(GetTransientPackage());
		Map->Bindings.Append(Bindings);
		Map->Refresh();
		return Map;
	}

	/** Puts the subsystem's maps back however the test leaves. Saves the whole set,
	 *  not just the first, now that several can be live at once. */
	struct FScopedActiveMap
	{
		UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
		TArray<TObjectPtr<UOscuMIDIMap>> Saved;

		explicit FScopedActiveMap(UOscuMIDIMap* Map)
		{
			if (MIDI != nullptr)
			{
				Saved = MIDI->GetActiveMaps();
				MIDI->SetActiveMap(Map);
			}
		}

		explicit FScopedActiveMap(std::initializer_list<UOscuMIDIMap*> Maps)
		{
			if (MIDI != nullptr)
			{
				Saved = MIDI->GetActiveMaps();

				TArray<TObjectPtr<UOscuMIDIMap>> Live;
				for (UOscuMIDIMap* Map : Maps)
				{
					Live.Add(Map);
				}
				MIDI->SetActiveMaps(MoveTemp(Live));
			}
		}

		~FScopedActiveMap()
		{
			if (MIDI != nullptr)
			{
				MIDI->SetActiveMaps(Saved);
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

	UOscuMIDIMap* Map = MakeMap({
		Bind(TEXT("laser"), TEXT("SetIntensity"), { NoteOn(1, TEXT("60")) }),
		Bind(TEXT("laser"), TEXT("Fire"), { NoteOn(1, TEXT("62")) }),
		Bind(TEXT("laser"), TEXT("Stop"), { NoteOn(1, TEXT("64")) }),
	});
	FScopedActiveMap ActiveMap(Map);

	// A clean single-parameter mapping: full velocity remaps to 1.0.
	{
		const int32 Calls = MIDI->IngestNote(1, 60, 127, /*bNoteOn*/ true);
		TestEqual(TEXT("The note fired one actor"), Calls, 1);
		TestEqual(TEXT("It called the mapped function"), Laser->LastCalled, FName("SetIntensity"));
		TestEqual(TEXT("Velocity 127 remaps to 1.0"), Laser->LastIntensity, 1.0);
	}

	{
		MIDI->IngestNote(1, 60, 64, true);
		TestEqual(TEXT("Velocity 64 remaps to 64/127"), Laser->LastIntensity, 64.0 / 127.0, 0.0001);
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

	// A zero-argument trigger takes the value and discards it, because surplus
	// arguments are tolerated.
	{
		const int32 Calls = MIDI->IngestNote(1, 64, 100, true);
		TestEqual(TEXT("A zero-argument trigger fires from a note"), Calls, 1);
		TestEqual(TEXT("Called Stop"), Laser->LastCalled, FName("Stop"));
	}

	// Note off is ignored unless the binding asks for it.
	{
		const int32 CallsBefore = Laser->CallCount;
		MIDI->IngestNote(1, 60, 0, /*bNoteOn*/ false);
		TestEqual(TEXT("Note off does nothing by default"), Laser->CallCount, CallsBefore);
	}

	// Unbound notes and channels pass through untouched.
	{
		const uint64 UnmappedBefore = MIDI->GetMessagesUnmapped();
		TestEqual(TEXT("An unbound note calls nothing"), MIDI->IngestNote(1, 99, 127, true), 0);
		TestEqual(TEXT("A bound note on the wrong channel calls nothing"), MIDI->IngestNote(2, 60, 127, true), 0);
		TestEqual(TEXT("Both were counted as unmapped"), MIDI->GetMessagesUnmapped(), UnmappedBefore + 2);

		// And a CC on a note's number is a different input entirely, not a near miss.
		TestEqual(TEXT("A CC numbered like a bound note calls nothing"), MIDI->IngestControlChange(1, 60, 127), 0);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Control change, and the value shaping that goes with it

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIControlChangeTest,
	"OSCulator.MIDI.ControlChange",
	OscuTest::Flags)

bool FOscuMIDIControlChangeTest::RunTest(const FString& Parameters)
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

	// Same function, four different shaping settings, so the differences are the only
	// thing the test is looking at.
	FOscuMIDIBinding Normalised = Bind(TEXT("laser"), TEXT("SetIntensity"), { ControlChange(1, 74) });

	FOscuMIDIBinding Raw = Bind(TEXT("laser"), TEXT("SetIntensity"), { ControlChange(1, 75) });
	Raw.bRemap = false;

	FOscuMIDIBinding Scaled = Bind(TEXT("laser"), TEXT("SetIntensity"), { ControlChange(1, 76) });
	Scaled.OutMin = 0.0f;
	Scaled.OutMax = 10.0f;

	FOscuMIDIBinding Inverted = Bind(TEXT("laser"), TEXT("SetIntensity"), { ControlChange(1, 77) });
	Inverted.OutMin = 1.0f;
	Inverted.OutMax = 0.0f;

	UOscuMIDIMap* Map = MakeMap({ Normalised, Raw, Scaled, Inverted });
	FScopedActiveMap ActiveMap(Map);

	{
		const int32 Calls = MIDI->IngestControlChange(1, 74, 127);
		TestEqual(TEXT("A control change fires its binding"), Calls, 1);
		TestEqual(TEXT("Called the bound function"), Laser->LastCalled, FName("SetIntensity"));
		TestEqual(TEXT("127 remaps to 1.0 by default"), Laser->LastIntensity, 1.0);
	}

	MIDI->IngestControlChange(1, 74, 0);
	TestEqual(TEXT("0 remaps to 0.0"), Laser->LastIntensity, 0.0);

	MIDI->IngestControlChange(1, 74, 64);
	TestEqual(TEXT("64 remaps to 64/127"), Laser->LastIntensity, 64.0 / 127.0, 0.0001);

	// Remapping off is the escape hatch for a function that wants the wire value.
	MIDI->IngestControlChange(1, 75, 100);
	TestEqual(TEXT("With remapping off the raw 0-127 arrives"), Laser->LastIntensity, 100.0);

	MIDI->IngestControlChange(1, 76, 127);
	TestEqual(TEXT("A 0-10 range scales to its top"), Laser->LastIntensity, 10.0);

	// OutMin above OutMax is how a fader is inverted, and it must not clamp to zero --
	// which is what a range-clamping helper would have done.
	MIDI->IngestControlChange(1, 77, 0);
	TestEqual(TEXT("An inverted range sends its maximum for 0"), Laser->LastIntensity, 1.0);
	MIDI->IngestControlChange(1, 77, 127);
	TestEqual(TEXT("...and its minimum for 127"), Laser->LastIntensity, 0.0);

	// The maths on its own, including the values no controller will send but a badly
	// behaved sender might.
	FOscuMIDIBinding Shape;
	TestEqual(TEXT("ShapeValue clamps below zero"), Shape.ShapeValue(-5), 0.0);
	TestEqual(TEXT("ShapeValue clamps above 127"), Shape.ShapeValue(200), 1.0);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Value shaping on notes, and the settings that used to be value modes

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

	// What used to be RawVelocity.
	FOscuMIDIBinding RawVelocity = Bind(TEXT("laser"), TEXT("SetIntensity"), { NoteOn(1, TEXT("60")) });
	RawVelocity.bRemap = false;

	// What used to be NoteAndVelocity.
	FOscuMIDIBinding WithNumber = Bind(TEXT("laser"), TEXT("Chase"), { NoteOn(1, TEXT("62")) });
	WithNumber.bSendSourceNumber = true;

	FOscuMIDIBinding OffCapable = Bind(TEXT("laser"), TEXT("SetIntensity"), { NoteOn(1, TEXT("67")) });
	OffCapable.bFireOnNoteOff = true;

	UOscuMIDIMap* Map = MakeMap({ RawVelocity, WithNumber, OffCapable });
	FScopedActiveMap ActiveMap(Map);

	MIDI->IngestNote(1, 60, 100, true);
	TestEqual(TEXT("Remapping off passes 0-127 through"), Laser->LastIntensity, 100.0);

	// Sending the source number puts it first, unremapped. Chase is
	// (float Speed, TArray<float> Points), so the note lands in Speed and the value is
	// swallowed by the trailing array.
	MIDI->IngestNote(1, 62, 127, true);
	TestEqual(TEXT("The source number goes first, raw"), Laser->LastSpeed, 62.0f);
	if (TestEqual(TEXT("...and the value follows it"), Laser->LastPoints.Num(), 1))
	{
		TestEqual(TEXT("...remapped"), Laser->LastPoints[0], 1.0f);
	}

	// With bFireOnNoteOff, the release fires with a raw value of zero.
	MIDI->IngestNote(1, 67, 127, true);
	TestEqual(TEXT("Note on carries its velocity"), Laser->LastIntensity, 1.0);

	const int32 CallsBefore = Laser->CallCount;
	MIDI->IngestNote(1, 67, 0, /*bNoteOn*/ false);
	TestEqual(TEXT("Note off fired too"), Laser->CallCount, CallsBefore + 1);
	TestEqual(TEXT("Note off sends zero"), Laser->LastIntensity, 0.0);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// One input, several targets -- and the device filter

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDISharedInputTest,
	"OSCulator.MIDI.SharedInput",
	OscuTest::Flags)

bool FOscuMIDISharedInputTest::RunTest(const FString& Parameters)
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
	AOscuTestActor* Cube = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_cube") });
	Scope.BeginPlay();

	// The thing the old channel-first structure could not express: one note, on one
	// channel, driving different functions on two different actors.
	UOscuMIDIMap* Map = MakeMap({
		Bind(TEXT("laser"), TEXT("Stop"), { NoteOn(1, TEXT("36")) }),
		Bind(TEXT("cube"), TEXT("SetIntensity"), { NoteOn(1, TEXT("36")) }),
	});
	FScopedActiveMap ActiveMap(Map);

	{
		const int32 Calls = MIDI->IngestNote(1, 36, 127, true);
		TestEqual(TEXT("One note called two actors"), Calls, 2);
		TestEqual(TEXT("The first binding fired"), Laser->LastCalled, FName("Stop"));
		TestEqual(TEXT("The second binding fired too"), Cube->LastCalled, FName("SetIntensity"));
		TestEqual(TEXT("...with its own shaping"), Cube->LastIntensity, 1.0);
	}

	// Several sources on one binding: two pads reaching the same function.
	//
	// All of one kind, because Type is a property of the binding now. Two notes, two
	// channels, one target.
	{
		UOscuMIDIMap* Multi = MakeMap({
			Bind(TEXT("laser"), TEXT("SetIntensity"), { NoteOn(1, TEXT("40")), NoteOn(2, TEXT("41")) }),
		});
		FScopedActiveMap Active(Multi);

		TestEqual(TEXT("The first source fires it"), MIDI->IngestNote(1, 40, 127, true), 1);
		TestEqual(TEXT("...remapped"), Laser->LastIntensity, 1.0);

		TestEqual(TEXT("The second source fires the same function"), MIDI->IngestNote(2, 41, 0, false), 0);
		TestEqual(TEXT("...and a release is ignored without On Note Off"), Laser->LastIntensity, 1.0);

		TestEqual(TEXT("The second source fires on press"), MIDI->IngestNote(2, 41, 64, true), 1);
		TestTrue(TEXT("...through the same shaping"),
			FMath::IsNearlyEqual(Laser->LastIntensity, 64.0 / 127.0, 1e-6));
	}

	// A pad AND a knob on one function is now two bindings, not two sources.
	//
	// This is the deliberate cost of making Type per function: the kinds behave
	// differently enough -- a note has a pitch and a release, a controller has neither --
	// that one row answering to both left half its value settings meaningless. The
	// capability is not gone, it is spelled differently, and both rows still fire.
	{
		FOscuMIDIBinding FromPad = Bind(TEXT("laser"), TEXT("SetIntensity"), { NoteOn(1, TEXT("42")) });
		FOscuMIDIBinding FromKnob = Bind(TEXT("laser"), TEXT("SetIntensity"), { ControlChange(2, 7) });

		UOscuMIDIMap* Split = MakeMap({ FromPad, FromKnob });
		FScopedActiveMap Active(Split);

		TestEqual(TEXT("the note row is note-driven"),
			static_cast<int32>(Split->Bindings[0].Type), static_cast<int32>(EOscuMIDIInputType::Note));
		TestEqual(TEXT("the knob row is controller-driven"),
			static_cast<int32>(Split->Bindings[1].Type), static_cast<int32>(EOscuMIDIInputType::ControlChange));

		TestEqual(TEXT("the pad drives it"), MIDI->IngestNote(1, 42, 127, true), 1);
		TestEqual(TEXT("...remapped"), Laser->LastIntensity, 1.0);

		TestEqual(TEXT("and the knob drives the same function"), MIDI->IngestControlChange(2, 7, 0), 1);
		TestEqual(TEXT("...through its own row's shaping"), Laser->LastIntensity, 0.0);
	}

	// A source cannot disagree with its binding: the kind is mirrored down.
	{
		FOscuMIDIBinding Mixed;
		Mixed.Tag = FName("laser");
		Mixed.FunctionName = FName("SetIntensity");
		Mixed.Type = EOscuMIDIInputType::Note;

		// Built as a controller, on a note-driven row. After Refresh it is a note source,
		// and its number is reinterpreted -- CC 7 becomes note 7.
		Mixed.Sources.Add(ControlChange(3, 7));

		UOscuMIDIMap* Map2 = NewObject<UOscuMIDIMap>(GetTransientPackage());
		Map2->Bindings.Add(Mixed);
		Map2->Refresh();

		TestEqual(TEXT("the source adopted the binding's kind"),
			static_cast<int32>(Map2->Bindings[0].Sources[0].Type),
			static_cast<int32>(EOscuMIDIInputType::Note));

		FScopedActiveMap Active(Map2);
		TestEqual(TEXT("so a control change no longer reaches it"),
			MIDI->IngestControlChange(3, 7, 127), 0);
	}

	// A source naming a device accepts only that device; one naming none accepts all.
	{
		UOscuMIDIMap* Fussy = MakeMap({
			Bind(TEXT("laser"), TEXT("Stop"), { NoteOn(1, TEXT("41"), FName("Elektron TM-1")) }),
			Bind(TEXT("cube"), TEXT("Stop"), { NoteOn(1, TEXT("41")) }),
		});
		FScopedActiveMap Active(Fussy);

		TestEqual(TEXT("A message from the named device reaches both"),
			MIDI->IngestNote(1, 41, 127, true, FName("Elektron TM-1")), 2);

		TestEqual(TEXT("A message from another device reaches only the unfussy one"),
			MIDI->IngestNote(1, 41, 127, true, FName("Some Other Box")), 1);

		// The common case: nothing names a device, so nothing has to be typed in.
		TestEqual(TEXT("A message from nowhere in particular still reaches the unfussy one"),
			MIDI->IngestNote(1, 41, 127, true), 1);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Several maps at once, and the per-asset default device

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIMultipleMapsTest,
	"OSCulator.MIDI.MultipleMaps",
	OscuTest::Flags)

bool FOscuMIDIMultipleMapsTest::RunTest(const FString& Parameters)
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
	AOscuTestActor* Cube = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_cube") });
	Scope.BeginPlay();

	// One asset per device, each naming its hardware once at the top instead of on
	// every row. The sources themselves leave Device empty.
	UOscuMIDIMap* Pads = MakeMap({ Bind(TEXT("laser"), TEXT("Stop"), { NoteOn(1, TEXT("36")) }) });
	Pads->DefaultDevice = FName("Elektron TM-1");
	Pads->Refresh();

	UOscuMIDIMap* Knobs = MakeMap({ Bind(TEXT("cube"), TEXT("SetIntensity"), { ControlChange(1, 7) }) });
	Knobs->DefaultDevice = FName("Midi Fighter Twister");
	Knobs->Refresh();

	FScopedActiveMap Active({ Pads, Knobs });

	// Both maps are consulted, so which asset a binding lives in is organisation and
	// nothing more.
	{
		TestEqual(TEXT("A binding in the first map fires"),
			MIDI->IngestNote(1, 36, 127, true, FName("Elektron TM-1")), 1);
		TestEqual(TEXT("...calling its function"), Laser->LastCalled, FName("Stop"));

		TestEqual(TEXT("A binding in the second map fires too"),
			MIDI->IngestControlChange(1, 7, 127, FName("Midi Fighter Twister")), 1);
		TestEqual(TEXT("...calling its function"), Cube->LastCalled, FName("SetIntensity"));
	}

	// The asset's default is a real requirement, not decoration: the same note from the
	// wrong controller must not fire a per-device map.
	{
		const int32 CallsBefore = Laser->CallCount;
		TestEqual(TEXT("The same note from another device does not fire"),
			MIDI->IngestNote(1, 36, 127, true, FName("Midi Fighter Twister")), 0);
		TestEqual(TEXT("...and called nothing"), Laser->CallCount, CallsBefore);
	}

	// A source naming its own device overrides the asset default, so one row can come
	// from a different box without splitting the asset.
	{
		UOscuMIDIMap* Mixed = MakeMap({
			Bind(TEXT("laser"), TEXT("Stop"), { NoteOn(1, TEXT("40")) }),
			Bind(TEXT("cube"), TEXT("Stop"), { NoteOn(1, TEXT("41"), FName("Some Other Box")) }),
		});
		Mixed->DefaultDevice = FName("Elektron TM-1");
		Mixed->Refresh();

		FScopedActiveMap Only(Mixed);

		TestEqual(TEXT("The inheriting row wants the asset's device"),
			MIDI->IngestNote(1, 40, 127, true, FName("Elektron TM-1")), 1);
		TestEqual(TEXT("...and refuses another"),
			MIDI->IngestNote(1, 40, 127, true, FName("Some Other Box")), 0);

		TestEqual(TEXT("The overriding row wants its own device"),
			MIDI->IngestNote(1, 41, 127, true, FName("Some Other Box")), 1);
		TestEqual(TEXT("...and refuses the asset's"),
			MIDI->IngestNote(1, 41, 127, true, FName("Elektron TM-1")), 0);
	}

	// With no default anywhere, empty still means any device -- which is what a
	// one-controller project relies on, and what every existing asset contains.
	{
		UOscuMIDIMap* Anywhere = MakeMap({ Bind(TEXT("laser"), TEXT("Stop"), { NoteOn(1, TEXT("42")) }) });
		FScopedActiveMap Only(Anywhere);

		TestEqual(TEXT("No default and no source device accepts anything"),
			MIDI->IngestNote(1, 42, 127, true, FName("Anything At All")), 1);
		TestEqual(TEXT("...including nothing in particular"),
			MIDI->IngestNote(1, 42, 127, true), 1);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
