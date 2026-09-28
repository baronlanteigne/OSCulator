// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDIMap.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuSettings.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

namespace OscuMIDIBindingTest
{
	UOscuMIDIMap* MakeMap()
	{
		return NewObject<UOscuMIDIMap>(GetTransientPackage());
	}

	void AddRule(UOscuMIDIMap& Map, const TCHAR* Tag, uint8 Channel, const TCHAR* Device = TEXT(""), uint8 FirstNote = 36, uint8 FirstControl = 1)
	{
		FOscuMIDIAutoMapRule& Rule = Map.AutoMapRules.AddDefaulted_GetRef();
		Rule.Tag = FName(Tag);
		Rule.Channel = Channel;
		Rule.Device = FString(Device).IsEmpty() ? NAME_None : FName(Device);
		Rule.FirstNote = FirstNote;
		Rule.FirstControl = FirstControl;
	}

	/** Triggers take nothing; a continuous function takes a number first. */
	TMap<FName, FString> Signatures(std::initializer_list<TPair<const TCHAR*, const TCHAR*>> Pairs)
	{
		TMap<FName, FString> Out;
		for (const TPair<const TCHAR*, const TCHAR*>& Pair : Pairs)
		{
			Out.Add(FName(Pair.Key), FString(Pair.Value));
		}
		return Out;
	}

	const FOscuMIDIBinding* Find(const UOscuMIDIMap& Map, const TCHAR* Tag, const TCHAR* Function)
	{
		return Map.Bindings.FindByPredicate([Tag, Function](const FOscuMIDIBinding& Binding)
		{
			return Binding.Tag == FName(Tag) && Binding.FunctionName == FName(Function);
		});
	}
}

//////////////////////////////////////////////////////////////////////////
// Auto-map lists everything, and assigns only where a rule says how.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIAutoMapTest,
	"OSCulator.MIDI.AutoMap",
	OscuTest::Flags)

bool FOscuMIDIAutoMapTest::RunTest(const FString& Parameters)
{
	using namespace OscuMIDIBindingTest;

	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	const int32 SavedMiddleC = Settings->MiddleCOctave;
	Settings->MiddleCOctave = 3;

	// Deliberately NOT alphabetical, standing in for a class's function map -- whose
	// iteration order is not stable across Blueprint recompiles.
	const TArray<FName> Functions = { FName("Stop"), FName("Fire"), FName("Aim") };
	const TMap<FName, FString> Types = Signatures({
		{ TEXT("Stop"), TEXT("") },          // a trigger
		{ TEXT("Fire"), TEXT("vec3") },      // not fillable from one controller
		{ TEXT("Aim"), TEXT("float") },      // continuous
	});

	// Without a rule, everything is listed and nothing is assigned. That is the point:
	// the asset is a complete inventory of what could be controlled, and assignment is
	// a separate, deliberate step.
	{
		UOscuMIDIMap* Map = MakeMap();

		int32 Assigned = 0;
		const int32 Added = Map->MergeFunctions(FName("laser"), Functions, Types, &Assigned);

		TestEqual(TEXT("Every function was listed"), Added, 3);
		TestEqual(TEXT("Nothing was assigned without a rule"), Assigned, 0);

		for (const FOscuMIDIBinding& Binding : Map->Bindings)
		{
			TestEqual(*FString::Printf(TEXT("%s has no source"), *Binding.FunctionName.ToString()),
				Binding.Sources.Num(), 0);
		}

		// And an unassigned binding says so, rather than looking like a working row.
		TestTrue(TEXT("An unassigned binding is labelled"),
			Map->Bindings[0].Summary.Contains(TEXT("unassigned")));
	}

	// With a rule, the type of input is chosen from the signature.
	{
		UOscuMIDIMap* Map = MakeMap();
		AddRule(*Map, TEXT("laser"), /*Channel*/ 5, TEXT("Elektron TM-1"), /*FirstNote*/ 36, /*FirstControl*/ 20);

		int32 Assigned = 0;
		Map->MergeFunctions(FName("laser"), Functions, Types, &Assigned);
		TestEqual(TEXT("Every function was assigned"), Assigned, 3);

		const FOscuMIDIBinding* Aim = Find(*Map, TEXT("laser"), TEXT("Aim"));
		if (TestNotNull(TEXT("The continuous function was bound"), Aim) && Aim->Sources.Num() == 1)
		{
			// A function that takes a number wants a knob, not a pad. Nobody should have
			// to answer that question when the signature already has.
			TestEqual(TEXT("A float-first function gets a controller"),
				static_cast<int32>(Aim->Sources[0].Type), static_cast<int32>(EOscuMIDIInputType::ControlChange));
			TestEqual(TEXT("...numbered from the rule"), static_cast<int32>(Aim->Sources[0].ControlNumber), 20);
			TestEqual(TEXT("...on the rule's channel"), static_cast<int32>(Aim->Sources[0].Channel), 5);
			TestEqual(TEXT("...from the rule's device"), Aim->Sources[0].Device, FName("Elektron TM-1"));
		}

		const FOscuMIDIBinding* Stop = Find(*Map, TEXT("laser"), TEXT("Stop"));
		if (TestNotNull(TEXT("The trigger was bound"), Stop) && Stop->Sources.Num() == 1)
		{
			TestEqual(TEXT("A zero-argument function gets a note"),
				static_cast<int32>(Stop->Sources[0].Type), static_cast<int32>(EOscuMIDIInputType::Note));
		}

		const FOscuMIDIBinding* Fire = Find(*Map, TEXT("laser"), TEXT("Fire"));
		if (TestNotNull(TEXT("The vector function was bound"), Fire) && Fire->Sources.Num() == 1)
		{
			// A controller cannot fill a vector meaningfully, so it gets a note and the
			// remaining components keep the zeroes the frame gave them.
			TestEqual(TEXT("A vector-first function gets a note, not a knob"),
				static_cast<int32>(Fire->Sources[0].Type), static_cast<int32>(EOscuMIDIInputType::Note));
		}

		// Notes are handed out alphabetically by function name -- Aim, Fire, Stop -- so
		// Fire and Stop take 36 and 37. Reflection order would have reshuffled these on
		// every Blueprint recompile.
		TestEqual(TEXT("The first note went to the alphabetically first trigger"),
			static_cast<int32>(Fire->Sources[0].ResolvedNote), 36);
		TestEqual(TEXT("...and the next to the one after it"),
			static_cast<int32>(Stop->Sources[0].ResolvedNote), 37);
	}

	// Run twice, and the second run changes nothing. This is the acceptance criterion
	// that keeps a recompile from moving anybody's pads.
	{
		UOscuMIDIMap* Map = MakeMap();
		AddRule(*Map, TEXT("laser"), 1);

		int32 FirstAssigned = 0;
		const int32 FirstAdded = Map->MergeFunctions(FName("laser"), Functions, Types, &FirstAssigned);

		const int32 NoteBefore = Find(*Map, TEXT("laser"), TEXT("Stop"))->Sources[0].ResolvedNote;

		int32 SecondAssigned = 0;
		const int32 SecondAdded = Map->MergeFunctions(FName("laser"), Functions, Types, &SecondAssigned);

		TestEqual(TEXT("The first run listed three"), FirstAdded, 3);
		TestEqual(TEXT("The second run listed none"), SecondAdded, 0);
		TestEqual(TEXT("The second run assigned none"), SecondAssigned, 0);
		TestEqual(TEXT("Nothing moved"), static_cast<int32>(Find(*Map, TEXT("laser"), TEXT("Stop"))->Sources[0].ResolvedNote), NoteBefore);
		TestEqual(TEXT("And nothing was duplicated"), Map->Bindings.Num(), 3);
	}

	// An input already taken by hand is skipped rather than stolen.
	{
		UOscuMIDIMap* Map = MakeMap();
		AddRule(*Map, TEXT("laser"), 1, TEXT(""), /*FirstNote*/ 36);

		FOscuMIDIBinding& Manual = Map->Bindings.AddDefaulted_GetRef();
		Manual.Tag = FName("other");
		Manual.FunctionName = FName("Something");
		FOscuMIDISource& Source = Manual.Sources.AddDefaulted_GetRef();
		Source.Channel = 1;
		Source.Note = TEXT("36");
		Map->Refresh();

		int32 Assigned = 0;
		Map->MergeFunctions(FName("laser"), { FName("Stop") }, Signatures({ { TEXT("Stop"), TEXT("") } }), &Assigned);

		const FOscuMIDIBinding* Stop = Find(*Map, TEXT("laser"), TEXT("Stop"));
		if (TestNotNull(TEXT("The new function was bound"), Stop) && Stop->Sources.Num() == 1)
		{
			TestEqual(TEXT("It stepped past the note somebody else had claimed"),
				static_cast<int32>(Stop->Sources[0].ResolvedNote), 37);
		}
	}

	// Two tags can share a channel, which is the whole reason the old structure was
	// replaced. Nothing about a tag reserves a channel any more.
	{
		UOscuMIDIMap* Map = MakeMap();
		AddRule(*Map, TEXT("laser"), 1, TEXT(""), 36);
		AddRule(*Map, TEXT("cube"), 1, TEXT(""), 60);

		int32 Assigned = 0;
		Map->MergeFunctions(FName("laser"), { FName("Stop") }, Signatures({ { TEXT("Stop"), TEXT("") } }), &Assigned);
		Map->MergeFunctions(FName("cube"), { FName("Stop") }, Signatures({ { TEXT("Stop"), TEXT("") } }), &Assigned);

		TestEqual(TEXT("Both tags were assigned"), Assigned, 2);
		TestEqual(TEXT("The first tag took its own first note"),
			static_cast<int32>(Find(*Map, TEXT("laser"), TEXT("Stop"))->Sources[0].ResolvedNote), 36);
		TestEqual(TEXT("The second took its own, on the same channel"),
			static_cast<int32>(Find(*Map, TEXT("cube"), TEXT("Stop"))->Sources[0].ResolvedNote), 60);
	}

	Settings->MiddleCOctave = SavedMiddleC;
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Validation, against a real world with real reflected functions.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIValidateTest,
	"OSCulator.MIDI.Validate",
	OscuTest::Flags)

bool FOscuMIDIValidateTest::RunTest(const FString& Parameters)
{
	using namespace OscuMIDIBindingTest;

	OscuTest::FScopedTestWorld Scoped;
	Scoped.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scoped.BeginPlay();

	UOscuMIDIMap* Map = MakeMap();

	auto Add = [Map](const TCHAR* Tag, const TCHAR* Function, bool bWithSource)
	{
		FOscuMIDIBinding& Binding = Map->Bindings.AddDefaulted_GetRef();
		Binding.Tag = FName(Tag);
		Binding.FunctionName = FName(Function);
		if (bWithSource)
		{
			FOscuMIDISource& Source = Binding.Sources.AddDefaulted_GetRef();
			Source.Channel = 1;
			Source.Note = TEXT("36");
		}
	};

	Add(TEXT("laser"), TEXT("Stop"), true);             // exists on the actor
	Add(TEXT("laser"), TEXT("RenamedAway"), true);      // the case this feature exists for
	Add(TEXT("nosuchtag"), TEXT("Stop"), true);         // another level's business
	Add(TEXT("laser"), TEXT("SetIntensity"), false);    // listed, not yet assigned
	Map->Refresh();

	const UOscuMIDIMap::FOscuMIDIValidation Result = Map->Validate(Scoped.World);

	TestEqual(TEXT("Every binding was judged"), Result.Total(), 4);
	TestEqual(TEXT("The live functions are found"), Result.Found, 2);
	TestEqual(TEXT("The dead function is reported missing"), Result.FunctionMissing, 1);
	TestEqual(TEXT("The row pointing at an absent tag is reported separately"), Result.TagMissing, 1);
	TestEqual(TEXT("The unassigned binding is counted, not faulted"), Result.Unassigned, 1);

	TestEqual(TEXT("The dead binding is marked function-missing"),
		static_cast<int32>(Map->Bindings[1].Status), static_cast<int32>(EOscuMIDIRowStatus::FunctionMissing));
	TestEqual(TEXT("The absent-tag binding is marked tag-missing"),
		static_cast<int32>(Map->Bindings[2].Status), static_cast<int32>(EOscuMIDIRowStatus::TagMissing));

	TestTrue(TEXT("A broken binding says so in its title"),
		Map->Bindings[1].Summary.Contains(TEXT("FUNCTION NOT FOUND")));

	// Three bindings share note 36 on channel 1. Legal -- that is how one pad drives
	// several actors -- but reported, because an accidental collision looks identical
	// from the inside.
	TestTrue(TEXT("A shared input is reported"), Result.Shared.Num() > 0);

	// AOscuTestActor is a C++ class, so it has no Blueprint-authored functions at all.
	// Its Stop() is found anyway, above -- bindings are judged against everything
	// exposed, because a hand-made binding to a native function is legal and Prune must
	// never delete one. The suggestion list is the opposite view and must stay empty
	// here: drawn from the same set it would offer two hundred inherited engine
	// functions per tag and be unreadable.
	TestEqual(TEXT("A native-only actor suggests nothing to map"), Result.Unclaimed.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
