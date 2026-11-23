// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDISubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuMIDIMap.h"
#include "OscuPlayerMappings.h"
#include "Tests/OscuScopedPlayerMenu.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

namespace OscuPlayerMIDITest
{
	/** Puts the subsystem's source maps back however the test leaves. */
	struct FScopedMaps
	{
		UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
		TArray<TObjectPtr<UOscuMIDIMap>> Saved;

		explicit FScopedMaps(UOscuMIDIMap* Map)
		{
			Saved = MIDI->GetSourceMaps();
			MIDI->SetActiveMap(Map);
		}

		~FScopedMaps() { MIDI->SetActiveMaps(Saved); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuPlayerMIDIRemapTest,
	"OSCulator.PlayerMenu.MIDIRemap",
	OscuTest::Flags)

bool FOscuPlayerMIDIRemapTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuPlayerMIDITest;

	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	if (!TestNotNull(TEXT("The MIDI engine subsystem exists"), MIDI))
	{
		return false;
	}

	FScopedPlayerMenu Menu({
		FScopedPlayerMenu::Entry(TEXT("laser"), TEXT("SetIntensity")),
		FScopedPlayerMenu::Entry(TEXT("laser"), TEXT("Stop")),
		FScopedPlayerMenu::Entry(TEXT("laser"), TEXT("Fire"), /*bOSC*/ true, /*bMIDI*/ false),
	});

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuMIDIMap* Map = NewObject<UOscuMIDIMap>(GetTransientPackage());
	Map->DefaultDevice = FName("Box A");
	{
		FOscuMIDIBinding& Intensity = Map->Bindings.AddDefaulted_GetRef();
		Intensity.Tag = FName("laser");
		Intensity.FunctionName = FName("SetIntensity");
		Intensity.Type = EOscuMIDIInputType::ControlChange;
		Intensity.OutMin = 0.0f;
		Intensity.OutMax = 10.0f;
		FOscuMIDISource& Source = Intensity.Sources.AddDefaulted_GetRef();
		Source.Channel = 1;
		Source.ControlNumber = 7;
	}
	Map->Refresh();
	FScopedMaps Maps(Map);

	TestEqual(TEXT("The project's own input works before any remap"),
		MIDI->IngestControlChange(1, 7, 127, FName("Box A")), 1);
	TestEqual(TEXT("With no remap, dispatch reads the asset itself"), MIDI->GetActiveMaps().Num(), 1);

	FOscuPlayerMappings::Get().SetMIDI(FName("laser"), FName("SetIntensity"), EOscuPlayerMIDIKind::ControlChange, 2, 20);

	TestEqual(TEXT("The project's input no longer fires the remapped function"),
		MIDI->IngestControlChange(1, 7, 127, FName("Box A")), 0);
	TestEqual(TEXT("The player's input fires it, from any device"),
		MIDI->IngestControlChange(2, 20, 127, FName("Some Other Box")), 1);
	TestEqual(TEXT("...with the binding's own value range"), Laser->LastIntensity, 10.0, 0.0001);
	TestEqual(TEXT("The asset itself is not edited"), static_cast<int32>(Map->Bindings[0].Sources[0].ControlNumber), 7);
	TestEqual(TEXT("FindProjectBinding answers from the asset"),
		static_cast<int32>(MIDI->FindProjectBinding(FName("laser"), FName("SetIntensity"))->Sources[0].ControlNumber), 7);

	// A function the project's maps never bound.
	FOscuPlayerMappings::Get().SetMIDI(FName("laser"), FName("Stop"), EOscuPlayerMIDIKind::Note, 1, 40);
	Laser->LastCalled = NAME_None;
	TestEqual(TEXT("A remap can bind a function no map binds"), MIDI->IngestNote(1, 40, 100, true), 1);
	TestEqual(TEXT("...and calls it"), Laser->LastCalled, FName("Stop"));

	// The menu does not offer MIDI for Fire, so a stored remap for it is ignored.
	FOscuPlayerMappings::Get().SetMIDI(FName("laser"), FName("Fire"), EOscuPlayerMIDIKind::Note, 1, 41);
	TestEqual(TEXT("A remap the menu does not allow is ignored"), MIDI->IngestNote(1, 41, 100, true), 0);

	// Learn: the next press or movement is handed over, not dispatched.
	{
		FOscuMIDICapture Got;
		int32 Callbacks = 0;
		MIDI->ArmCapture([&Got, &Callbacks](const FOscuMIDICapture& Capture) { Got = Capture; ++Callbacks; });

		TestEqual(TEXT("A note off is not a gesture"), MIDI->IngestNote(3, 50, 0, false), 0);
		TestEqual(TEXT("...so nothing is captured yet"), Callbacks, 0);
		TestEqual(TEXT("A captured message is not dispatched"), MIDI->IngestControlChange(2, 20, 64), 0);
		TestEqual(TEXT("Capture fires once"), Callbacks, 1);
		TestEqual(TEXT("...with the channel"), Got.Channel, 2);
		TestEqual(TEXT("...the number"), Got.Number, 20);
		TestTrue(TEXT("...and the kind"), Got.Type == EOscuMIDIInputType::ControlChange);
		TestFalse(TEXT("Capture disarms itself"), MIDI->IsCapturing());
	}

	FOscuPlayerMappings::Get().ResetAll();
	TestEqual(TEXT("Reset puts the project's input back"), MIDI->IngestControlChange(1, 7, 127, FName("Box A")), 1);
	TestEqual(TEXT("...and drops the transient maps"), MIDI->GetActiveMaps().Num(), 1);

	return true;
}

#endif
