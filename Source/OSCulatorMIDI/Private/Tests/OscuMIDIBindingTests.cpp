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

	/**
	 * One parameter, described the way the registry would describe it.
	 *
	 * The label-to-shape mapping is spelled out rather than inferred, so this helper
	 * cannot drift into being a second copy of the real type table in OscuMarshal: a
	 * test that quietly disagreed with ClassifyParam about what "float" means would pass
	 * while the product was wrong. These are stand-in signatures, nothing more.
	 */
	FOscuExposedParamInfo Param(const TCHAR* Name, const TCHAR* TypeLabel)
	{
		FOscuExposedParamInfo Info;
		Info.Name = FName(Name);
		Info.TypeLabel = TypeLabel;

		const FString Label(TypeLabel);
		if (Label == TEXT("float") || Label == TEXT("int") || Label == TEXT("byte"))
		{
			Info.ArgCount = 1;
			Info.bContinuous = true;
		}
		else if (Label == TEXT("vec3"))
		{
			Info.ArgCount = 3;
		}
		else
		{
			// string, name, bool, enum: one slot, and not a magnitude.
			Info.ArgCount = 1;
		}

		return Info;
	}

	/** A Blueprint output pin: present in the signature, costing no argument. */
	FOscuExposedParamInfo OutParam(const TCHAR* Name, const TCHAR* TypeLabel)
	{
		FOscuExposedParamInfo Info = Param(Name, TypeLabel);
		Info.bOutputOnly = true;
		Info.bContinuous = false;
		Info.ArgCount = 0;
		return Info;
	}

	/**
	 * Triggers take nothing; a continuous function takes a number first.
	 *
	 * The single-type shorthand the older assertions were written against: one parameter
	 * of the named type, or none at all for an empty label.
	 */
	TMap<FName, TArray<FOscuExposedParamInfo>> Signatures(std::initializer_list<TPair<const TCHAR*, const TCHAR*>> Pairs)
	{
		TMap<FName, TArray<FOscuExposedParamInfo>> Out;
		for (const TPair<const TCHAR*, const TCHAR*>& Pair : Pairs)
		{
			TArray<FOscuExposedParamInfo>& Params = Out.Add(FName(Pair.Key));
			if (!FString(Pair.Value).IsEmpty())
			{
				Params.Add(Param(TEXT("Value"), Pair.Value));
			}
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
	const TMap<FName, TArray<FOscuExposedParamInfo>> Types = Signatures({
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

//////////////////////////////////////////////////////////////////////////
// Which parameters velocity and pitch default to.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIDefaultValueParamsTest,
	"OSCulator.MIDI.DefaultValueParams",
	OscuTest::Flags)

bool FOscuMIDIDefaultValueParamsTest::RunTest(const FString& Parameters)
{
	using namespace OscuMIDIBindingTest;

	// ---- The first two continuous parameters, in order ----
	{
		FOscuMIDIBinding Binding;
		const bool bWrote = UOscuMIDIMap::AssignDefaultValueParams(Binding, {
			Param(TEXT("Speed"), TEXT("float")),
			Param(TEXT("Depth"), TEXT("float")),
			Param(TEXT("Extra"), TEXT("float")),
		});

		TestTrue(TEXT("it wrote something"), bWrote);

		// Velocity takes the FIRST. That keeps the common case identical to the old
		// positional default, where velocity went to the first parameter.
		TestEqual(TEXT("velocity takes the first"), Binding.VelocityParam, FName(TEXT("Speed")));
		TestEqual(TEXT("pitch takes the second"), Binding.PitchParam, FName(TEXT("Depth")));
	}

	// ---- int and byte count; string, vector, bool and enum do not ----
	{
		FOscuMIDIBinding Binding;
		UOscuMIDIMap::AssignDefaultValueParams(Binding, {
			Param(TEXT("Label"), TEXT("string")),      // skipped
			Param(TEXT("Where"), TEXT("vec3")),        // skipped
			Param(TEXT("Enabled"), TEXT("bool")),      // skipped
			Param(TEXT("Mode"), TEXT("enum(EThing)")), // skipped
			Param(TEXT("Count"), TEXT("int")),         // first magnitude
			Param(TEXT("Shade"), TEXT("byte")),        // second magnitude
		});

		TestEqual(TEXT("an int is a magnitude"), Binding.VelocityParam, FName(TEXT("Count")));
		TestEqual(TEXT("so is a byte"), Binding.PitchParam, FName(TEXT("Shade")));
	}

	// ---- They are found past a multi-slot parameter ----
	{
		FOscuMIDIBinding Binding;
		UOscuMIDIMap::AssignDefaultValueParams(Binding, {
			Param(TEXT("Origin"), TEXT("vec3")),
			Param(TEXT("Pitch"), TEXT("float")),
			Param(TEXT("Level"), TEXT("float")),
		});

		// Exactly the signature a positional scheme could not reach into.
		TestEqual(TEXT("velocity reaches behind a vec3"), Binding.VelocityParam, FName(TEXT("Pitch")));
		TestEqual(TEXT("and so does pitch"), Binding.PitchParam, FName(TEXT("Level")));
	}

	// ---- One continuous parameter means velocity only ----
	{
		FOscuMIDIBinding Binding;
		TestTrue(TEXT("it wrote something"),
			UOscuMIDIMap::AssignDefaultValueParams(Binding, { Param(TEXT("Intensity"), TEXT("float")) }));

		TestEqual(TEXT("velocity is set"), Binding.VelocityParam, FName(TEXT("Intensity")));
		TestTrue(TEXT("pitch is left empty rather than doubling up"), Binding.PitchParam.IsNone());
	}

	// ---- A trigger gets nothing, and stays on the untouched path ----
	{
		FOscuMIDIBinding Binding;
		TestFalse(TEXT("a trigger writes nothing"),
			UOscuMIDIMap::AssignDefaultValueParams(Binding, {}));

		TestTrue(TEXT("velocity stays empty"), Binding.VelocityParam.IsNone());
		TestTrue(TEXT("pitch stays empty"), Binding.PitchParam.IsNone());
	}

	// ---- Nothing continuous means nothing assigned ----
	{
		FOscuMIDIBinding Binding;
		TestFalse(TEXT("a signature with no magnitudes writes nothing"),
			UOscuMIDIMap::AssignDefaultValueParams(Binding, {
				Param(TEXT("Label"), TEXT("string")),
				Param(TEXT("Where"), TEXT("vec3")),
			}));

		TestTrue(TEXT("velocity stays empty"), Binding.VelocityParam.IsNone());
	}

	// ---- An output pin is not assignable, however numeric it looks ----
	{
		FOscuMIDIBinding Binding;
		UOscuMIDIMap::AssignDefaultValueParams(Binding, {
			OutParam(TEXT("OutResult"), TEXT("float")),
			Param(TEXT("In"), TEXT("float")),
		});

		// The output pin is written by the call, so pointing velocity at it would mean
		// the message overwrote a return value.
		TestEqual(TEXT("the output pin is skipped"), Binding.VelocityParam, FName(TEXT("In")));
		TestTrue(TEXT("and leaves pitch with nowhere to go"), Binding.PitchParam.IsNone());
	}

	// ---- Additive only: a deliberate choice is never touched ----
	{
		FOscuMIDIBinding Binding;
		Binding.VelocityParam = FName(TEXT("MyChoice"));

		TestFalse(TEXT("a half-filled pair is left alone"),
			UOscuMIDIMap::AssignDefaultValueParams(Binding, {
				Param(TEXT("Speed"), TEXT("float")),
				Param(TEXT("Depth"), TEXT("float")),
			}));

		TestEqual(TEXT("the existing choice survives"), Binding.VelocityParam, FName(TEXT("MyChoice")));

		// And pitch is NOT completed for them. Someone who named one and left the other
		// empty said something, and finishing the sentence would be guessing.
		TestTrue(TEXT("and pitch is not filled in behind their back"), Binding.PitchParam.IsNone());
	}

	// ---- Auto-map writes them, and running it twice changes nothing ----
	{
		UOscuMIDIMap* Map = MakeMap();

		TMap<FName, TArray<FOscuExposedParamInfo>> Params;
		Params.Add(FName("Sweep"), {
			Param(TEXT("Origin"), TEXT("vec3")),
			Param(TEXT("Pitch"), TEXT("float")),
			Param(TEXT("Level"), TEXT("float")),
		});

		int32 Assigned = 0;
		Map->MergeFunctions(FName("laser"), { FName("Sweep") }, Params, &Assigned);

		const FOscuMIDIBinding* Sweep = Find(*Map, TEXT("laser"), TEXT("Sweep"));
		if (TestNotNull(TEXT("the binding was listed"), Sweep))
		{
			TestEqual(TEXT("velocity was defaulted"), Sweep->VelocityParam, FName(TEXT("Pitch")));
			TestEqual(TEXT("pitch was defaulted"), Sweep->PitchParam, FName(TEXT("Level")));
		}

		// Hand-edit one, then run auto-map again. Nothing may move: that is the
		// additive-only rule the whole asset depends on.
		Map->Bindings[0].VelocityParam = FName(TEXT("Level"));
		Map->Bindings[0].PitchParam = FName(TEXT("Pitch"));

		Map->MergeFunctions(FName("laser"), { FName("Sweep") }, Params, &Assigned);

		TestEqual(TEXT("a second run leaves a hand edit alone"),
			Map->Bindings[0].VelocityParam, FName(TEXT("Level")));
		TestEqual(TEXT("both halves of it"),
			Map->Bindings[0].PitchParam, FName(TEXT("Pitch")));
		TestEqual(TEXT("and adds no duplicate binding"), Map->Bindings.Num(), 1);
	}

	// ---- An existing unnamed binding picks the defaults up ----
	{
		UOscuMIDIMap* Map = MakeMap();

		// Stands in for an asset authored before any of this existed: the row is there,
		// with no parameter assignment at all.
		FOscuMIDIBinding& Existing = Map->Bindings.AddDefaulted_GetRef();
		Existing.Tag = FName("laser");
		Existing.FunctionName = FName("SetIntensity");

		TMap<FName, TArray<FOscuExposedParamInfo>> Params;
		Params.Add(FName("SetIntensity"), { Param(TEXT("Intensity"), TEXT("float")) });

		int32 Assigned = 0;
		const int32 Added = Map->MergeFunctions(FName("laser"), { FName("SetIntensity") }, Params, &Assigned);

		TestEqual(TEXT("no binding was added"), Added, 0);
		TestEqual(TEXT("but the existing one was filled in"),
			Map->Bindings[0].VelocityParam, FName(TEXT("Intensity")));
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Type moving from the source to the binding, and the assets that predate it.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIBindingTypeTest,
	"OSCulator.MIDI.BindingType",
	OscuTest::Flags)

bool FOscuMIDIBindingTypeTest::RunTest(const FString& Parameters)
{
	using namespace OscuMIDIBindingTest;

	// An asset saved before Type moved up: the kind is on each SOURCE, and the binding's
	// own Type deserialises to the enum default, Note. Nothing else records what the row
	// meant, so if the mirror ran first every controller row would silently become a note
	// row and CC 7 would be reinterpreted as note 7.
	auto MakeLegacyRow = [](UOscuMIDIMap& Map, const TCHAR* Function, EOscuMIDIInputType SourceType, uint8 Number)
		-> FOscuMIDIBinding&
	{
		FOscuMIDIBinding& Binding = Map.Bindings.AddDefaulted_GetRef();
		Binding.Tag = FName("laser");
		Binding.FunctionName = FName(Function);
		// Left at the default on purpose: this is what a legacy load produces.
		Binding.Type = EOscuMIDIInputType::Note;

		FOscuMIDISource& Source = Binding.Sources.AddDefaulted_GetRef();
		Source.Channel = 1;
		Source.Type = SourceType;
		if (SourceType == EOscuMIDIInputType::ControlChange)
		{
			Source.ControlNumber = Number;
		}
		else
		{
			Source.Note = FString::FromInt(Number);
		}
		return Binding;
	};

	// ---- A legacy controller row is recovered, not flattened ----
	{
		UOscuMIDIMap* Map = MakeMap();
		MakeLegacyRow(*Map, TEXT("SetIntensity"), EOscuMIDIInputType::ControlChange, 7);

		Map->PostLoad();

		TestEqual(TEXT("the binding adopted its source's kind"),
			static_cast<int32>(Map->Bindings[0].Type),
			static_cast<int32>(EOscuMIDIInputType::ControlChange));

		// And the source still answers to CC 7 rather than note 7.
		TestEqual(TEXT("the source stayed a controller"),
			static_cast<int32>(Map->Bindings[0].Sources[0].Type),
			static_cast<int32>(EOscuMIDIInputType::ControlChange));
		TestTrue(TEXT("and still claims CC 7"),
			Map->IsInputTaken(1, EOscuMIDIInputType::ControlChange, 7));
		TestFalse(TEXT("not note 7"),
			Map->IsInputTaken(1, EOscuMIDIInputType::Note, 7));
	}

	// ---- A legacy note row is left exactly as it was ----
	{
		UOscuMIDIMap* Map = MakeMap();
		MakeLegacyRow(*Map, TEXT("Stop"), EOscuMIDIInputType::Note, 36);

		Map->PostLoad();

		TestEqual(TEXT("a note row stays a note row"),
			static_cast<int32>(Map->Bindings[0].Type),
			static_cast<int32>(EOscuMIDIInputType::Note));
		TestTrue(TEXT("and still claims note 36"),
			Map->IsInputTaken(1, EOscuMIDIInputType::Note, 36));
	}

	// ---- A legacy MIXED row is resolved to its first source, and says so ----
	{
		AddExpectedError(TEXT("more than one kind"), EAutomationExpectedErrorFlags::Contains, 0);

		UOscuMIDIMap* Map = MakeMap();
		FOscuMIDIBinding& Binding = MakeLegacyRow(*Map, TEXT("SetIntensity"), EOscuMIDIInputType::ControlChange, 7);

		// The pad-and-knob row that used to be legal. There is no answer for what it
		// should become, so the first source decides and the rest is reported.
		FOscuMIDISource& Second = Binding.Sources.AddDefaulted_GetRef();
		Second.Channel = 1;
		Second.Type = EOscuMIDIInputType::Note;
		Second.Note = TEXT("40");

		Map->PostLoad();

		TestEqual(TEXT("the first source decided the row"),
			static_cast<int32>(Map->Bindings[0].Type),
			static_cast<int32>(EOscuMIDIInputType::ControlChange));
		TestEqual(TEXT("and the other source was re-pointed to match"),
			static_cast<int32>(Map->Bindings[0].Sources[1].Type),
			static_cast<int32>(EOscuMIDIInputType::ControlChange));
	}

	// ---- The mirror runs on every edit, so a row re-points in one step ----
	{
		UOscuMIDIMap* Map = MakeMap();
		FOscuMIDIBinding& Binding = Map->Bindings.AddDefaulted_GetRef();
		Binding.Tag = FName("laser");
		Binding.FunctionName = FName("SetIntensity");
		Binding.Type = EOscuMIDIInputType::Note;

		for (int32 Index = 0; Index < 3; ++Index)
		{
			FOscuMIDISource& Source = Binding.Sources.AddDefaulted_GetRef();
			Source.Channel = 1;
			Source.Note = FString::FromInt(40 + Index);
		}

		Map->Refresh();
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestEqual(TEXT("every source starts as a note"),
				static_cast<int32>(Map->Bindings[0].Sources[Index].Type),
				static_cast<int32>(EOscuMIDIInputType::Note));
		}

		// Flip the row. This is the case that would have been destroyed if the migration
		// lived in Refresh: a legitimate edit makes the binding disagree with its sources
		// for exactly as long as it takes the mirror to run, which is indistinguishable
		// from a legacy load by inspection alone. Migration is in PostLoad for this reason.
		Map->Bindings[0].Type = EOscuMIDIInputType::ControlChange;
		Map->Refresh();

		TestEqual(TEXT("the edit survived Refresh"),
			static_cast<int32>(Map->Bindings[0].Type),
			static_cast<int32>(EOscuMIDIInputType::ControlChange));

		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestEqual(TEXT("and every source followed it"),
				static_cast<int32>(Map->Bindings[0].Sources[Index].Type),
				static_cast<int32>(EOscuMIDIInputType::ControlChange));
		}
	}

	// ---- An unassigned row has nothing to recover from, and is left alone ----
	{
		UOscuMIDIMap* Map = MakeMap();
		FOscuMIDIBinding& Binding = Map->Bindings.AddDefaulted_GetRef();
		Binding.Tag = FName("laser");
		Binding.FunctionName = FName("Stop");
		Binding.Type = EOscuMIDIInputType::ControlChange;

		Map->PostLoad();

		TestEqual(TEXT("a sourceless row keeps the type it was given"),
			static_cast<int32>(Map->Bindings[0].Type),
			static_cast<int32>(EOscuMIDIInputType::ControlChange));
	}

	// ---- A controller row gets no pitch parameter by default ----
	{
		FOscuMIDIBinding Knob;
		Knob.Type = EOscuMIDIInputType::ControlChange;

		UOscuMIDIMap::AssignDefaultValueParams(Knob, {
			Param(TEXT("Speed"), TEXT("float")),
			Param(TEXT("Depth"), TEXT("float")),
		});

		TestEqual(TEXT("velocity is still pointed somewhere"), Knob.VelocityParam, FName(TEXT("Speed")));

		// A controller has no pitch. Naming one would put a constant into Depth and hide
		// the very field that would have explained where it came from.
		TestTrue(TEXT("but a controller row gets no pitch parameter"), Knob.PitchParam.IsNone());

		FOscuMIDIBinding Pad;
		Pad.Type = EOscuMIDIInputType::Note;
		UOscuMIDIMap::AssignDefaultValueParams(Pad, {
			Param(TEXT("Speed"), TEXT("float")),
			Param(TEXT("Depth"), TEXT("float")),
		});
		TestEqual(TEXT("whereas a note row does"), Pad.PitchParam, FName(TEXT("Depth")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
