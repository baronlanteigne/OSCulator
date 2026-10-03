// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDISubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuMIDIMap.h"
#include "OscuRouterSubsystem.h"
#include "OscuSettings.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

namespace OscuMIDIRangeTest
{
	/** A note-range source over a bare-number span, so MiddleCOctave cannot shift it. */
	FOscuMIDISource NoteSpan(const uint8 Channel, const TCHAR* Low, const TCHAR* High)
	{
		FOscuMIDISource Source;
		Source.Type = EOscuMIDIInputType::Note;
		Source.Channel = Channel;
		Source.bNoteRange = true;
		Source.Note = Low;
		Source.NoteHigh = High;
		return Source;
	}

	FOscuMIDISource SingleNote(const uint8 Channel, const TCHAR* NoteText)
	{
		FOscuMIDISource Source;
		Source.Type = EOscuMIDIInputType::Note;
		Source.Channel = Channel;
		Source.Note = NoteText;
		return Source;
	}

	UOscuMIDIMap* MakeMap(std::initializer_list<FOscuMIDIBinding> Bindings)
	{
		UOscuMIDIMap* Map = NewObject<UOscuMIDIMap>(GetTransientPackage());
		Map->Bindings.Append(Bindings);

		// The kind lives on the binding and Refresh mirrors it down onto every source, so
		// a source-only Type would be overwritten. Adopted from the first source here so
		// each test still reads as a statement about the source it built.
		for (FOscuMIDIBinding& Binding : Map->Bindings)
		{
			if (Binding.Sources.Num() > 0)
			{
				Binding.Type = Binding.Sources[0].Type;
			}
		}

		Map->Refresh();
		return Map;
	}

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
// The span itself: what it covers, and how a pitch inside it normalises.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDINoteRangeSpanTest,
	"OSCulator.MIDI.NoteRangeSpan",
	OscuTest::Flags)

bool FOscuMIDINoteRangeSpanTest::RunTest(const FString& Parameters)
{
	using namespace OscuMIDIRangeTest;

	// ---- A single note is a span of one, and reports itself as such ----
	{
		FOscuMIDISource Single = SingleNote(1, TEXT("60"));
		FOscuMIDIBinding Binding;
		Binding.Sources.Add(Single);
		UOscuMIDIMap* Map = MakeMap({ Binding });
		const FOscuMIDISource& Resolved = Map->Bindings[0].Sources[0];

		TestFalse(TEXT("an untouched source is not a range"), Resolved.IsNoteRange());
		TestEqual(TEXT("its low bound is the note"), Resolved.GetNoteLow(), 60);
		TestEqual(TEXT("its high bound is the same note"), Resolved.GetNoteHigh(), 60);

		// The degenerate case has nowhere to sit. It must not divide by zero, and it
		// must land on the bottom of the output range rather than halfway up it.
		TestEqual(TEXT("a one-note span has no position"), Resolved.GetPitchFraction(60), 0.0);
	}

	// ---- A real span normalises across its own extent ----
	{
		FOscuMIDIBinding Binding;
		Binding.Sources.Add(NoteSpan(1, TEXT("60"), TEXT("72")));
		UOscuMIDIMap* Map = MakeMap({ Binding });
		const FOscuMIDISource& Span = Map->Bindings[0].Sources[0];

		TestTrue(TEXT("it is a range"), Span.IsNoteRange());
		TestEqual(TEXT("low bound"), Span.GetNoteLow(), 60);
		TestEqual(TEXT("high bound"), Span.GetNoteHigh(), 72);

		TestEqual(TEXT("the bottom note is 0"), Span.GetPitchFraction(60), 0.0);
		TestEqual(TEXT("the top note is 1"), Span.GetPitchFraction(72), 1.0);
		TestEqual(TEXT("the middle is halfway"), Span.GetPitchFraction(66), 0.5);

		// Out-of-span pitches cannot arrive through the lookup, but a clamp costs
		// nothing and means GetPitchFraction is safe to call on anything.
		TestEqual(TEXT("below the span clamps to 0"), Span.GetPitchFraction(40), 0.0);
		TestEqual(TEXT("above the span clamps to 1"), Span.GetPitchFraction(100), 1.0);
	}

	// ---- The extent is what normalises, not the full 0-127 ----
	{
		FOscuMIDIBinding Narrow;
		Narrow.Sources.Add(NoteSpan(1, TEXT("60"), TEXT("62")));
		UOscuMIDIMap* Map = MakeMap({ Narrow });
		const FOscuMIDISource& Span = Map->Bindings[0].Sources[0];

		// Three notes covering the whole range. Against a raw 0-127 denominator the top
		// of this span would read as 0.49, and a two-note controller would be useless.
		TestEqual(TEXT("a three-note span still reaches 1"), Span.GetPitchFraction(62), 1.0);
		TestEqual(TEXT("and still has a middle"), Span.GetPitchFraction(61), 0.5);
	}

	// ---- An inverted span is swapped, not left matching nothing ----
	//
	// No AddExpectedError here: the swap is reported at Log level, not Warning. It is a
	// benign auto-correction of an ordering slip, and the assertion below is the real
	// check -- that the pair came out the right way round.
	{
		FOscuMIDIBinding Binding;
		Binding.Sources.Add(NoteSpan(1, TEXT("72"), TEXT("60")));
		UOscuMIDIMap* Map = MakeMap({ Binding });
		const FOscuMIDISource& Span = Map->Bindings[0].Sources[0];

		TestEqual(TEXT("the low bound ends up low"), Span.GetNoteLow(), 60);
		TestEqual(TEXT("the high bound ends up high"), Span.GetNoteHigh(), 72);
	}

	// ---- Every note in the span matches; nothing outside it does ----
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("SetIntensity"));
		Binding.Sources.Add(NoteSpan(1, TEXT("60"), TEXT("64")));
		UOscuMIDIMap* Map = MakeMap({ Binding });

		TArray<FOscuMIDIMatch> Matches;
		for (int32 Note = 60; Note <= 64; ++Note)
		{
			Map->FindMatches(NAME_None, 1, EOscuMIDIInputType::Note, Note, Matches);
			TestEqual(*FString::Printf(TEXT("note %d is inside the span"), Note), Matches.Num(), 1);
		}

		Map->FindMatches(NAME_None, 1, EOscuMIDIInputType::Note, 59, Matches);
		TestEqual(TEXT("one below the span misses"), Matches.Num(), 0);

		Map->FindMatches(NAME_None, 1, EOscuMIDIInputType::Note, 65, Matches);
		TestEqual(TEXT("one above the span misses"), Matches.Num(), 0);

		// The span is claimed for auto-map too, or auto-map would hand out a note that
		// a range already answers to.
		TestTrue(TEXT("the middle of a span counts as taken"),
			Map->IsInputTaken(1, EOscuMIDIInputType::Note, 62));
		TestFalse(TEXT("just outside it does not"),
			Map->IsInputTaken(1, EOscuMIDIInputType::Note, 65));

		// A span on one channel says nothing about another.
		Map->FindMatches(NAME_None, 2, EOscuMIDIInputType::Note, 62, Matches);
		TestEqual(TEXT("the span does not leak across channels"), Matches.Num(), 0);
	}

	// ---- Control change ignores the flag entirely ----
	{
		FOscuMIDISource CC;
		CC.Type = EOscuMIDIInputType::ControlChange;
		CC.Channel = 1;
		CC.ControlNumber = 7;

		// Ticked, but on a CC source. It must stay a single controller rather than
		// spanning CC numbers, which is a feature nobody asked for.
		CC.bNoteRange = true;

		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("SetIntensity"));
		Binding.Sources.Add(CC);
		UOscuMIDIMap* Map = MakeMap({ Binding });

		TestFalse(TEXT("a CC source is never a range"), Map->Bindings[0].Sources[0].IsNoteRange());

		TArray<FOscuMIDIMatch> Matches;
		Map->FindMatches(NAME_None, 1, EOscuMIDIInputType::ControlChange, 7, Matches);
		TestEqual(TEXT("its own CC matches"), Matches.Num(), 1);
		Map->FindMatches(NAME_None, 1, EOscuMIDIInputType::ControlChange, 8, Matches);
		TestEqual(TEXT("the next CC up does not"), Matches.Num(), 0);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Shaping a pitch into an output range.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIPitchShapeTest,
	"OSCulator.MIDI.PitchShape",
	OscuTest::Flags)

bool FOscuMIDIPitchShapeTest::RunTest(const FString& Parameters)
{
	FOscuMIDIBinding Binding;

	// ---- The stated default: 0.0 to 1.0 across the span ----
	TestTrue(TEXT("pitch remap is on by default"), Binding.bRemapPitch);
	TestEqual(TEXT("default low"), Binding.PitchOutMin, 0.0f);
	TestEqual(TEXT("default high"), Binding.PitchOutMax, 1.0f);

	TestEqual(TEXT("fraction 0 gives 0"), Binding.ShapePitch(0.0, 60), 0.0);
	TestEqual(TEXT("fraction 1 gives 1"), Binding.ShapePitch(1.0, 72), 1.0);
	TestEqual(TEXT("fraction 0.5 gives 0.5"), Binding.ShapePitch(0.5, 66), 0.5);

	// ---- An arbitrary range ----
	Binding.PitchOutMin = 10.0f;
	Binding.PitchOutMax = 20.0f;
	TestEqual(TEXT("bottom of a 10-20 range"), Binding.ShapePitch(0.0, 60), 10.0);
	TestEqual(TEXT("top of a 10-20 range"), Binding.ShapePitch(1.0, 72), 20.0);
	TestEqual(TEXT("middle of a 10-20 range"), Binding.ShapePitch(0.5, 66), 15.0);

	// ---- Inverted on purpose: lowest note, highest value ----
	Binding.PitchOutMin = 1.0f;
	Binding.PitchOutMax = 0.0f;
	TestEqual(TEXT("an inverted range starts high"), Binding.ShapePitch(0.0, 60), 1.0);
	TestEqual(TEXT("an inverted range ends low"), Binding.ShapePitch(1.0, 72), 0.0);
	TestEqual(TEXT("and still has a middle"), Binding.ShapePitch(0.5, 66), 0.5);

	// ---- Remap off hands over the note number, not the fraction ----
	Binding.bRemapPitch = false;
	TestEqual(TEXT("raw pitch ignores the fraction"), Binding.ShapePitch(0.0, 64), 64.0);
	TestEqual(TEXT("raw pitch ignores the range too"), Binding.ShapePitch(1.0, 100), 100.0);
	TestEqual(TEXT("raw pitch is clamped to 0-127"), Binding.ShapePitch(0.0, 9999), 127.0);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Addressing a parameter by name, which is the thing that makes two values usable.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIParamSlotTest,
	"OSCulator.MIDI.ParamSlots",
	OscuTest::Flags)

bool FOscuMIDIParamSlotTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;

	FScopedTestWorld Scope;
	Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = UOscuRouterSubsystem::Get(Scope.World);
	if (!TestNotNull(TEXT("the registry exists"), Router))
	{
		return false;
	}

	const FName Laser(TEXT("laser"));

	// ---- A slot is a running sum, not a parameter index ----
	//
	// Fire(FVector Dir, FName Mode, float Power): Dir eats three slots, so Mode is at 3
	// and Power at 4. Getting this wrong is the whole hazard of naming a parameter --
	// it would silently drive a neighbour instead of failing.
	{
		const FName Fire(TEXT("Fire"));
		TestEqual(TEXT("the first parameter is slot 0"),
			Router->FindParamSlot(Laser, Fire, FName(TEXT("Dir"))), 0);
		TestEqual(TEXT("a vec3 pushes the next parameter to slot 3"),
			Router->FindParamSlot(Laser, Fire, FName(TEXT("Mode"))), 3);
		TestEqual(TEXT("and the one after it to slot 4"),
			Router->FindParamSlot(Laser, Fire, FName(TEXT("Power"))), 4);

		int32 ArgCount = 0;
		Router->FindParamSlot(Laser, Fire, FName(TEXT("Dir")), &ArgCount);
		TestEqual(TEXT("a vec3 reports three slots"), ArgCount, 3);
		Router->FindParamSlot(Laser, Fire, FName(TEXT("Power")), &ArgCount);
		TestEqual(TEXT("a float reports one"), ArgCount, 1);
	}

	// ---- Plain scalars in a row ----
	{
		const FName Configure(TEXT("Configure"));
		TestEqual(TEXT("bool at 0"), Router->FindParamSlot(Laser, Configure, FName(TEXT("bEnabled"))), 0);
		TestEqual(TEXT("string at 1"), Router->FindParamSlot(Laser, Configure, FName(TEXT("Label"))), 1);
		TestEqual(TEXT("int at 2"), Router->FindParamSlot(Laser, Configure, FName(TEXT("Count"))), 2);
	}

	// ---- An output pin occupies no slot and cannot be assigned ----
	{
		const FName Query(TEXT("Query"));
		TestEqual(TEXT("the input is slot 0"),
			Router->FindParamSlot(Laser, Query, FName(TEXT("In"))), 0);

		// Named, present in the signature, and still not assignable: it is written by
		// the call. Reporting a slot for it would mean the message overwrote an output.
		TestEqual(TEXT("an output-only parameter is not addressable"),
			Router->FindParamSlot(Laser, Query, FName(TEXT("OutResult"))), INDEX_NONE);
	}

	// ---- Misses are misses, not slot 0 ----
	{
		TestEqual(TEXT("a name that does not exist"),
			Router->FindParamSlot(Laser, FName(TEXT("SetIntensity")), FName(TEXT("Nonexistent"))), INDEX_NONE);
		TestEqual(TEXT("a function that does not exist"),
			Router->FindParamSlot(Laser, FName(TEXT("NoSuchFunction")), FName(TEXT("Intensity"))), INDEX_NONE);
		TestEqual(TEXT("a tag that does not exist"),
			Router->FindParamSlot(FName(TEXT("nosuchtag")), FName(TEXT("SetIntensity")), FName(TEXT("Intensity"))), INDEX_NONE);
		TestEqual(TEXT("an empty name resolves to nothing"),
			Router->FindParamSlot(Laser, FName(TEXT("SetIntensity")), NAME_None), INDEX_NONE);
	}

	// ---- The discoverable list, which is how a name gets typed correctly ----
	{
		TArray<FOscuExposedParamInfo> Params;
		TArray<int32> Slots;

		TestTrue(TEXT("Fire can be described"),
			Router->DescribeParams(Laser, FName(TEXT("Fire")), Params, Slots));
		TestEqual(TEXT("it has three parameters"), Params.Num(), 3);
		TestEqual(TEXT("with slots alongside them"), Slots.Num(), 3);
		if (Slots.Num() == 3)
		{
			TestEqual(TEXT("slots line up with the sum"), Slots[2], 4);
		}

		TestFalse(TEXT("an unknown function describes nothing"),
			Router->DescribeParams(Laser, FName(TEXT("NoSuchFunction")), Params, Slots));
		TestEqual(TEXT("and leaves the output empty"), Params.Num(), 0);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// End to end: a span of notes driving two parameters of one function.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIRangeDispatchTest,
	"OSCulator.MIDI.RangeDispatch",
	OscuTest::Flags)

bool FOscuMIDIRangeDispatchTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuMIDIRangeTest;

	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	if (!TestNotNull(TEXT("the MIDI subsystem exists"), MIDI))
	{
		return false;
	}

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();


	// ---- Pitch and velocity into two named parameters of one signature ----
	//
	// Sweep(FVector Origin, float Pitch, float Level) puts both useful scalars BEHIND a
	// vec3, at slots 3 and 4. Declaration order can only ever reach Origin, so this is
	// the case a positional scheme cannot express at all.
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("Sweep"));
		Binding.Sources.Add(NoteSpan(1, TEXT("60"), TEXT("72")));

		Binding.PitchParam = FName(TEXT("Pitch"));
		Binding.VelocityParam = FName(TEXT("Level"));

		// Different ranges on purpose, so a crossed pair cannot read as correct.
		Binding.PitchOutMin = 0.0f;
		Binding.PitchOutMax = 1.0f;
		Binding.OutMin = 0.0f;
		Binding.OutMax = 10.0f;

		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		// Top of the span, full velocity.
		Laser->CallCount = 0;
		MIDI->IngestNote(1, 72, 127, true);
		TestEqual(TEXT("the top of the span fired once"), Laser->CallCount, 1);
		TestEqual(TEXT("pitch reached slot 3 at the top of its range"), Laser->LastSweepPitch, 1.0f);
		TestEqual(TEXT("velocity reached slot 4 in its own range"), Laser->LastSweepLevel, 10.0f);

		// Middle of the span, half velocity. Neither value may borrow the other's range.
		Laser->CallCount = 0;
		MIDI->IngestNote(1, 66, 64, true);
		TestEqual(TEXT("the middle of the span fired"), Laser->CallCount, 1);
		TestEqual(TEXT("pitch is halfway through 0..1"), Laser->LastSweepPitch, 0.5f);
		TestTrue(TEXT("velocity is proportional through 0..10"),
			FMath::IsNearlyEqual(Laser->LastSweepLevel, 10.0f * 64.0f / 127.0f, 1e-4f));

		// Bottom of the span, full velocity: the crossed-wires case. Pitch must read 0
		// while velocity reads 10, which is unmistakable if the slots were swapped.
		Laser->CallCount = 0;
		MIDI->IngestNote(1, 60, 127, true);
		TestEqual(TEXT("the bottom of the span fired"), Laser->CallCount, 1);
		TestEqual(TEXT("pitch is at the bottom"), Laser->LastSweepPitch, 0.0f);
		TestEqual(TEXT("while velocity is at the top"), Laser->LastSweepLevel, 10.0f);

		// The vec3 nobody assigned is left zeroed rather than fed a stray value.
		TestEqual(TEXT("the unassigned struct parameter stays zero"),
			Laser->LastSweepOrigin, FVector::ZeroVector);

		// Outside the span: nothing at all.
		Laser->CallCount = 0;
		MIDI->IngestNote(1, 80, 127, true);
		TestEqual(TEXT("a note outside the span does not fire"), Laser->CallCount, 0);
	}

	// ---- Pitch alone, with velocity left unnamed ----
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("Sweep"));
		Binding.Sources.Add(NoteSpan(1, TEXT("60"), TEXT("72")));
		Binding.PitchParam = FName(TEXT("Level"));
		Binding.bRemapPitch = false;

		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		// Remap off: the raw note number, which is how pitch is asked for as an
		// identity rather than as a position.
		Laser->LastSweepLevel = -1.0f;
		Laser->LastSweepPitch = -1.0f;
		MIDI->IngestNote(1, 67, 100, true);
		TestEqual(TEXT("an unremapped pitch is the note number"), Laser->LastSweepLevel, 67.0f);

		// Velocity was not named, so it is not sent. Slot 3 stays zeroed rather than
		// inheriting the old positional behaviour halfway.
		TestEqual(TEXT("an unnamed velocity is not sent"), Laser->LastSweepPitch, 0.0f);
	}

	// ---- Naming nothing must behave exactly as it did before ----
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("SetIntensity"));
		Binding.Sources.Add(SingleNote(1, TEXT("60")));

		// No PitchParam, no VelocityParam: the value goes to the first slot, which is
		// the documented behaviour every existing asset relies on.
		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		Laser->LastIntensity = -1.0;
		MIDI->IngestNote(1, 60, 127, true);
		TestEqual(TEXT("an unnamed binding still fills the first parameter"), Laser->LastIntensity, 1.0);

		Laser->LastIntensity = -1.0;
		MIDI->IngestNote(1, 60, 64, true);
		TestTrue(TEXT("and still remaps velocity"),
			FMath::IsNearlyEqual(Laser->LastIntensity, 64.0 / 127.0, 1e-6));
	}

	// ---- Send Source Number still works on an unnamed binding ----
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("Sweep"));
		Binding.Sources.Add(SingleNote(1, TEXT("62")));
		Binding.bSendSourceNumber = true;

		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		// The legacy layout: number first, then the value -- so they land in the vec3's
		// first two slots. Unchanged by any of this, which is the point.
		Laser->LastSweepOrigin = FVector::ZeroVector;
		MIDI->IngestNote(1, 62, 127, true);
		TestEqual(TEXT("the source number still goes first"), Laser->LastSweepOrigin.X, 62.0);
		TestEqual(TEXT("and the value still follows it"), Laser->LastSweepOrigin.Y, 1.0);
	}

	// ---- A velocity name alone reaches past slot 0 ----
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("Sweep"));
		Binding.Sources.Add(SingleNote(1, TEXT("62")));
		Binding.VelocityParam = FName(TEXT("Level"));
		Binding.OutMin = 0.0f;
		Binding.OutMax = 1.0f;

		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		Laser->LastSweepLevel = -1.0f;
		Laser->CallCount = 0;
		MIDI->IngestNote(1, 62, 127, true);
		TestEqual(TEXT("it fired"), Laser->CallCount, 1);
		TestEqual(TEXT("velocity reached a named parameter at slot 4"), Laser->LastSweepLevel, 1.0f);
	}

	// ---- A name that matches nothing is reported and sends nothing ----
	{
		AddExpectedError(TEXT("which that function does not have"),
			EAutomationExpectedErrorFlags::Contains, 0);

		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("SetIntensity"));
		Binding.Sources.Add(SingleNote(1, TEXT("64")));
		Binding.VelocityParam = FName(TEXT("NoSuchParameter"));

		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		Laser->LastIntensity = -1.0;
		Laser->CallCount = 0;
		MIDI->IngestNote(1, 64, 127, true);

		// It still FIRES. Whether a binding matched is a question about the input, and
		// the note did arrive on an input this binding claims -- so a pad that triggers
		// an event still triggers it. What the broken name costs is the value, not the
		// call.
		TestEqual(TEXT("an unresolvable name still fires the event"), Laser->CallCount, 1);

		// And the value is simply not supplied: the parameter keeps the zero the
		// initialised frame gave it, which is what Lenient does everywhere else when
		// MIDI has fewer values than the signature wants. Crucially it is NOT quietly
		// redirected to slot 0, which would drive whatever happened to be first.
		TestEqual(TEXT("and leaves the parameter at zero rather than guessing"),
			Laser->LastIntensity, 0.0);

		// Repeating it must not repeat the warning; the throttle is keyed per binding.
		MIDI->IngestNote(1, 64, 127, true);
		MIDI->IngestNote(1, 64, 127, true);
	}

	// ---- A released note keeps its pitch but loses its velocity ----
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("Sweep"));
		Binding.Sources.Add(NoteSpan(1, TEXT("60"), TEXT("72")));
		Binding.PitchParam = FName(TEXT("Pitch"));
		Binding.VelocityParam = FName(TEXT("Level"));
		Binding.bFireOnNoteOff = true;
		Binding.OutMin = 0.0f;
		Binding.OutMax = 10.0f;

		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		Laser->CallCount = 0;
		MIDI->IngestNote(1, 72, 0, false);
		TestEqual(TEXT("the release fired"), Laser->CallCount, 1);

		// Which pad was let go is still the pad it was -- unlike velocity, which has no
		// meaning on release and shapes from zero.
		TestEqual(TEXT("pitch survives the release"), Laser->LastSweepPitch, 1.0f);
		TestEqual(TEXT("velocity shapes from zero"), Laser->LastSweepLevel, 0.0f);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Performance Mode: changes what work is done, never what the work produces.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIPerformanceModeTest,
	"OSCulator.MIDI.PerformanceMode",
	OscuTest::Flags)

bool FOscuMIDIPerformanceModeTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuMIDIRangeTest;

	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	if (!TestNotNull(TEXT("the MIDI subsystem exists"), MIDI))
	{
		return false;
	}

	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	const bool bSavedMode = Settings->bPerformanceMode;

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();

	// A named binding, which is the only kind the mode changes anything for: both values
	// go to parameters sitting behind a vec3, so the slots have to be resolved by name
	// either way.
	auto MakeNamed = []
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(TEXT("laser"));
		Binding.FunctionName = FName(TEXT("Sweep"));
		Binding.Sources.Add(NoteSpan(1, TEXT("60"), TEXT("72")));
		Binding.PitchParam = FName(TEXT("Pitch"));
		Binding.VelocityParam = FName(TEXT("Level"));
		Binding.PitchOutMin = 0.0f;
		Binding.PitchOutMax = 1.0f;
		Binding.OutMin = 0.0f;
		Binding.OutMax = 10.0f;
		return Binding;
	};

	// Drives the same gestures and reports what landed, so the two modes can be compared
	// value for value rather than by inspection.
	struct FOutcome
	{
		int32 Calls = 0;
		float TopPitch = 0.0f;
		float TopLevel = 0.0f;
		float MidPitch = 0.0f;
		float MidLevel = 0.0f;
	};

	auto Run = [&]() -> FOutcome
	{
		UOscuMIDIMap* Map = MakeMap({ MakeNamed() });
		FScopedActiveMap Active(Map);

		FOutcome Out;
		Laser->CallCount = 0;

		MIDI->IngestNote(1, 72, 127, true);
		Out.TopPitch = Laser->LastSweepPitch;
		Out.TopLevel = Laser->LastSweepLevel;

		MIDI->IngestNote(1, 66, 64, true);
		Out.MidPitch = Laser->LastSweepPitch;
		Out.MidLevel = Laser->LastSweepLevel;

		// Repeated, because Performance Mode answers the first message from the registry
		// and every one after it from a remembered slot. If the two paths disagreed, the
		// second message is where it would show.
		MIDI->IngestNote(1, 72, 127, true);
		TestEqual(TEXT("a repeat still lands the same"), Laser->LastSweepPitch, Out.TopPitch);
		TestEqual(TEXT("...on both values"), Laser->LastSweepLevel, Out.TopLevel);

		Out.Calls = Laser->CallCount;
		return Out;
	};

	// ---- Editing: slots resolved live, every message ----
	Settings->bPerformanceMode = false;
	const FOutcome Editing = Run();

	// ---- Performance: resolved once and remembered ----
	Settings->bPerformanceMode = true;
	const FOutcome Performing = Run();

	// THE CONTRACT. The mode is allowed to change how much work is done to deliver a
	// value and nothing whatsoever about the value. A difference here means Performance
	// Mode is not a performance switch, it is a behaviour switch, and nobody could safely
	// leave it on for a show.
	TestEqual(TEXT("the same number of calls fired"), Performing.Calls, Editing.Calls);
	TestEqual(TEXT("top of span: pitch identical"), Performing.TopPitch, Editing.TopPitch);
	TestEqual(TEXT("top of span: velocity identical"), Performing.TopLevel, Editing.TopLevel);
	TestEqual(TEXT("mid span: pitch identical"), Performing.MidPitch, Editing.MidPitch);
	TestEqual(TEXT("mid span: velocity identical"), Performing.MidLevel, Editing.MidLevel);

	// And the values are right, not merely equal to each other -- two identically broken
	// runs would otherwise pass.
	TestEqual(TEXT("and were correct to begin with"), Editing.TopPitch, 1.0f);
	TestEqual(TEXT("on both channels"), Editing.TopLevel, 10.0f);

#if WITH_EDITOR
	// ---- Learn is refused, out loud, and does not leave a row looking armed ----
	{
		Settings->bPerformanceMode = true;

		AddExpectedError(TEXT("MIDI Learn is disabled"), EAutomationExpectedErrorFlags::Contains, 0);

		UOscuMIDIMap* Map = MakeMap({ MakeNamed() });
		Map->Bindings[0].Sources[0].bLearn = true;

		MIDI->ArmLearn(Map, 0, 0);

		TestFalse(TEXT("Learn did not arm"), MIDI->IsLearning());

		// Cleared, or the row sits there claiming to be armed while nothing listens --
		// which is the confusing failure the refusal exists to avoid.
		TestFalse(TEXT("and the tickbox was cleared"), Map->Bindings[0].Sources[0].bLearn);
	}

	// ---- With the mode off, Learn arms as it always did ----
	{
		Settings->bPerformanceMode = false;

		UOscuMIDIMap* Map = MakeMap({ MakeNamed() });
		MIDI->ArmLearn(Map, 0, 0);

		TestTrue(TEXT("Learn arms normally when editing"), MIDI->IsLearning());

		MIDI->CancelLearn(Map);
		TestFalse(TEXT("and disarms"), MIDI->IsLearning());
	}
#endif

	Settings->bPerformanceMode = bSavedMode;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Program Change: one data byte, so it is a trigger that knows its own number.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIProgramChangeTest,
	"OSCulator.MIDI.ProgramChange",
	OscuTest::Flags)

bool FOscuMIDIProgramChangeTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuMIDIRangeTest;

	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	if (!TestNotNull(TEXT("the MIDI subsystem exists"), MIDI))
	{
		return false;
	}

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	AOscuTestActor* Cube = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_cube") });
	Scope.BeginPlay();

	auto ProgramSource = [](const uint8 Channel, const uint8 Program)
	{
		FOscuMIDISource Source;
		Source.Type = EOscuMIDIInputType::ProgramChange;
		Source.Channel = Channel;
		Source.ProgramNumber = Program;
		return Source;
	};

	auto BindProgram = [&ProgramSource](const TCHAR* Tag, const TCHAR* Function, uint8 Channel, uint8 Program)
	{
		FOscuMIDIBinding Binding;
		Binding.Tag = FName(Tag);
		Binding.FunctionName = FName(Function);
		Binding.Type = EOscuMIDIInputType::ProgramChange;
		Binding.Sources.Add(ProgramSource(Channel, Program));
		return Binding;
	};

	// ---- A program fires its row, and only its row ----
	{
		UOscuMIDIMap* Map = MakeMap({ BindProgram(TEXT("laser"), TEXT("Stop"), 1, 5) });
		FScopedActiveMap Active(Map);

		TestEqual(TEXT("the source stayed a program change"),
			static_cast<int32>(Map->Bindings[0].Sources[0].Type),
			static_cast<int32>(EOscuMIDIInputType::ProgramChange));

		Laser->CallCount = 0;
		TestEqual(TEXT("program 5 fires it"), MIDI->IngestProgramChange(1, 5), 1);
		TestEqual(TEXT("once"), Laser->CallCount, 1);
		TestEqual(TEXT("and it was the right function"), Laser->LastCalled, FName("Stop"));

		TestEqual(TEXT("a different program does not"), MIDI->IngestProgramChange(1, 6), 0);
		TestEqual(TEXT("a different channel does not"), MIDI->IngestProgramChange(2, 5), 0);
	}

	// ---- It does not collide with a note or a CC of the same number ----
	{
		UOscuMIDIMap* Map = MakeMap({
			BindProgram(TEXT("laser"), TEXT("Stop"), 1, 7),
		});
		FOscuMIDIBinding Knob;
		Knob.Tag = FName("cube");
		Knob.FunctionName = FName("SetIntensity");
		Knob.Type = EOscuMIDIInputType::ControlChange;
		FOscuMIDISource CC;
		CC.Type = EOscuMIDIInputType::ControlChange;
		CC.Channel = 1;
		CC.ControlNumber = 7;
		Knob.Sources.Add(CC);
		Map->Bindings.Add(Knob);
		Map->Refresh();

		FScopedActiveMap Active(Map);

		// Same channel, same number 7, different KIND. The lookup key carries the type,
		// so these are two separate inputs rather than one shared one.
		Laser->CallCount = 0;
		Cube->CallCount = 0;

		TestEqual(TEXT("program 7 fires only the program row"), MIDI->IngestProgramChange(1, 7), 1);
		TestEqual(TEXT("the program row fired"), Laser->CallCount, 1);
		TestEqual(TEXT("the CC row did not"), Cube->CallCount, 0);

		TestEqual(TEXT("CC 7 fires only the CC row"), MIDI->IngestControlChange(1, 7, 127), 1);
		TestEqual(TEXT("the CC row fired"), Cube->CallCount, 1);
		TestEqual(TEXT("the program row did not fire again"), Laser->CallCount, 1);
	}

	// ---- ONE program triggering SEVERAL functions, which is the ask ----
	{
		UOscuMIDIMap* Map = MakeMap({
			BindProgram(TEXT("laser"), TEXT("Stop"), 1, 12),
			BindProgram(TEXT("laser"), TEXT("Fire"), 1, 12),
			BindProgram(TEXT("cube"), TEXT("Stop"), 1, 12),
		});
		FScopedActiveMap Active(Map);

		Laser->CallCount = 0;
		Cube->CallCount = 0;

		// Three bindings claim program 12. All three fire -- across two different actors
		// and two different functions on one of them.
		TestEqual(TEXT("one program change called three times"), MIDI->IngestProgramChange(1, 12), 3);
		TestEqual(TEXT("twice on the first actor"), Laser->CallCount, 2);
		TestEqual(TEXT("and once on the second"), Cube->CallCount, 1);
	}

	// ---- The program number reaches the function, unremapped ----
	{
		FOscuMIDIBinding Binding = BindProgram(TEXT("laser"), TEXT("SetIntensity"), 1, 42);

		// Remap is ON, which is the default and is HIDDEN for a program change row. It
		// must be ignored: a program number is which patch, not how much, and squashing
		// 42 into 0.33 is never what anyone meant.
		Binding.bRemap = true;
		Binding.OutMin = 0.0f;
		Binding.OutMax = 1.0f;

		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		Laser->LastIntensity = -1.0;
		MIDI->IngestProgramChange(1, 42);
		TestEqual(TEXT("the program number arrives raw, ignoring Remap"), Laser->LastIntensity, 42.0);
	}

	// ---- And it can be pointed at a parameter by name ----
	{
		FOscuMIDIBinding Binding = BindProgram(TEXT("laser"), TEXT("Sweep"), 1, 9);

		// Sweep(FVector Origin, float Pitch, float Level): Level is slot 4, unreachable
		// by declaration order.
		Binding.VelocityParam = FName(TEXT("Level"));

		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		Laser->LastSweepLevel = -1.0f;
		Laser->LastSweepPitch = -1.0f;
		MIDI->IngestProgramChange(1, 9);

		TestEqual(TEXT("the program number reached a named parameter"), Laser->LastSweepLevel, 9.0f);

		// No pitch on a program change -- there is no such thing -- so slot 3 is left
		// alone rather than fed something invented.
		TestEqual(TEXT("and nothing was invented for pitch"), Laser->LastSweepPitch, 0.0f);
	}

	// ---- A zero-argument trigger simply fires ----
	{
		UOscuMIDIMap* Map = MakeMap({ BindProgram(TEXT("laser"), TEXT("Stop"), 1, 0) });
		FScopedActiveMap Active(Map);

		// Program 0 is a real program number, not an absent one. A row keyed on it must
		// fire, or the lowest patch on every box is silently unmappable.
		Laser->CallCount = 0;
		TestEqual(TEXT("program 0 is a program"), MIDI->IngestProgramChange(1, 0), 1);
		TestEqual(TEXT("and fired the trigger"), Laser->CallCount, 1);
	}

	// ---- Pitch and note-off settings are inert on a program change row ----
	{
		FOscuMIDIBinding Binding = BindProgram(TEXT("laser"), TEXT("Sweep"), 1, 3);
		Binding.VelocityParam = FName(TEXT("Level"));

		// Set, but hidden in the panel and meaningless here. Nothing may act on them.
		Binding.PitchParam = FName(TEXT("Pitch"));
		Binding.bFireOnNoteOff = true;

		UOscuMIDIMap* Map = MakeMap({ Binding });
		FScopedActiveMap Active(Map);

		Laser->LastSweepPitch = -1.0f;
		MIDI->IngestProgramChange(1, 3);

		// A program change has no pitch, so the named pitch parameter gets the shaped
		// fraction of a span that does not exist -- which is PitchOutMin, 0 by default,
		// the same zero the frame already held. The point is that it is not the program
		// number appearing twice.
		TestEqual(TEXT("the value went where it was named"), Laser->LastSweepLevel, 3.0f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
