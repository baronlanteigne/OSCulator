// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuEventOutputSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuOSCSubsystem.h"
#include "OscuPlayerMappings.h"
#include "OscuSettings.h"
#include "Tests/OscuScopedPlayerMenu.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

namespace OscuEventOutputTest
{
	/** Event outputs for the test's duration, with both transports off so nothing leaves the machine. */
	struct FScopedOutputs
	{
		explicit FScopedOutputs(std::initializer_list<FOscuEventOutput> Outputs)
		{
			Settings = GetMutableDefault<UOscuSettings>();
			Saved = Settings->EventOutputs;
			bSavedOSCOut = Settings->bEnableOSCOut;
			bSavedMIDIOut = Settings->bEnableMIDIOut;
			Settings->EventOutputs = Outputs;
			Settings->bEnableOSCOut = false;
			Settings->bEnableMIDIOut = false;
		}

		~FScopedOutputs()
		{
			Settings->EventOutputs = Saved;
			Settings->bEnableOSCOut = bSavedOSCOut;
			Settings->bEnableMIDIOut = bSavedMIDIOut;
		}

		UOscuSettings* Settings = nullptr;
		TArray<FOscuEventOutput> Saved;
		bool bSavedOSCOut = false;
		bool bSavedMIDIOut = false;
	};

	FOscuEventOutput Output(const TCHAR* Tag, const TCHAR* Dispatcher, EOscuEventMIDIKind Kind = EOscuEventMIDIKind::None,
		int32 Channel = 1, int32 Number = 0, float Min = 0.0f, float Max = 1.0f)
	{
		FOscuEventOutput Out;
		Out.Tag = FName(Tag);
		Out.Dispatcher = FName(Dispatcher);
		Out.MIDIKind = Kind;
		Out.MIDIChannel = Channel;
		Out.MIDINumber = Number;
		Out.ValueMin = Min;
		Out.ValueMax = Max;
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEventOutputSendTest,
	"OSCulator.EventOutputs.Send",
	OscuTest::Flags)

bool FOscuEventOutputSendTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuEventOutputTest;

	FScopedPlayerMenu Menu({});
	FScopedOutputs Outputs({
		Output(TEXT("laser"), TEXT("OnHit"), EOscuEventMIDIKind::ControlChange, 2, 21, 0.0f, 10.0f),
		Output(TEXT("laser"), TEXT("OnScored"), EOscuEventMIDIKind::Note, 1, 60),
		Output(TEXT("laser"), TEXT("OnPing")),
		Output(TEXT("laser"), TEXT("NoSuchDispatcher")),
	});

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_other") });
	Scope.BeginPlay();

	UOscuEventOutputSubsystem* Events = UOscuEventOutputSubsystem::Get(Scope.World);
	if (!TestNotNull(TEXT("The event output subsystem exists in a game world"), Events))
	{
		return false;
	}
	TestEqual(TEXT("Three dispatchers bound on the tagged actor; the missing one and the other tag are skipped"),
		Events->GetBoundCount(), 3);

	// One float: the OSC argument, and the MIDI value through Value Min / Max.
	Laser->OnHit.Broadcast(5.0f);
	{
		const FOscuEventSend& Sent = Events->GetLastSend();
		TestEqual(TEXT("Calling the dispatcher sends once"), Events->GetSendCount(), 1);
		TestTrue(TEXT("It sends OSC by default"), Sent.bOSC);
		TestEqual(TEXT("...to /tag/dispatcher"), Sent.Message.Address, FString(TEXT("/laser/OnHit")));
		TestEqual(TEXT("...with the parameter as its argument"), Sent.Message.Args.Num(), 1);
		TestEqual(TEXT("...unchanged"), Sent.Message.Args[0].AsNumber(), 5.0, 0.0001);
		TestTrue(TEXT("MIDI is the configured controller"), Sent.MIDIKind == EOscuPlayerMIDIKind::ControlChange);
		TestEqual(TEXT("...on its channel"), Sent.MIDIChannel, 2);
		TestEqual(TEXT("...and number"), Sent.MIDINumber, 21);
		TestEqual(TEXT("5 in 0-10 scales to 64"), Sent.MIDIValue, 64);
	}

	// Two parameters, the number second: still the MIDI value.
	Laser->OnScored.Broadcast(FName("Ada"), 1);
	{
		const FOscuEventSend& Sent = Events->GetLastSend();
		TestEqual(TEXT("A name and an int are two arguments"), Sent.Message.Args.Num(), 2);
		TestEqual(TEXT("...the name as a string"), Sent.Message.Args[0].Str, FString(TEXT("Ada")));
		TestTrue(TEXT("A note output sends a note"), Sent.MIDIKind == EOscuPlayerMIDIKind::Note);
		TestEqual(TEXT("1 at the top of 0-1 is full velocity"), Sent.MIDIValue, 127);
	}

	// No parameters: a trigger.
	Laser->OnPing.Broadcast();
	{
		const FOscuEventSend& Sent = Events->GetLastSend();
		TestEqual(TEXT("A trigger sends to its address"), Sent.Message.Address, FString(TEXT("/laser/OnPing")));
		TestEqual(TEXT("...with no arguments"), Sent.Message.Args.Num(), 0);
		TestTrue(TEXT("...and no MIDI when none is set"), Sent.MIDIKind == EOscuPlayerMIDIKind::None);
	}

	// The player re-points one. Only listed outputs take a remap, which these all are.
	FOscuPlayerMappings::Get().SetOSCAddress(FName("laser"), FName("OnHit"), TEXT("/score/hit"), /*bOutput*/ true);
	FOscuPlayerMappings::Get().SetMIDI(FName("laser"), FName("OnHit"), EOscuPlayerMIDIKind::Note, 3, 40, /*bOutput*/ true);
	Laser->OnHit.Broadcast(10.0f);
	{
		const FOscuEventSend& Sent = Events->GetLastSend();
		TestEqual(TEXT("The player's address is used"), Sent.Message.Address, FString(TEXT("/score/hit")));
		TestTrue(TEXT("...and the player's MIDI message"), Sent.MIDIKind == EOscuPlayerMIDIKind::Note);
		TestEqual(TEXT("...on their channel"), Sent.MIDIChannel, 3);
		TestEqual(TEXT("...and number"), Sent.MIDINumber, 40);
	}

	if (UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(Scope.World))
	{
		TestEqual(TEXT("An output remap is not an input remap"), OSC->GetAddressAliases().Num(), 0);
	}

	FOscuPlayerMappings::Get().SetMIDI(FName("laser"), FName("OnHit"), EOscuPlayerMIDIKind::Off, 1, 0, true);
	Laser->OnHit.Broadcast(10.0f);
	TestTrue(TEXT("\"off\" sends no MIDI"), Events->GetLastSend().MIDIKind == EOscuPlayerMIDIKind::None);

	// Test button: the first number at Value Max.
	{
		const FOscuEventSend Sent = Events->SendTest(0);
		TestEqual(TEXT("A test send fills the first number with Value Max"), Sent.Message.Args[0].AsNumber(), 10.0, 0.0001);
	}

	// Unbound when the world goes, so a later broadcast reaches nothing stale.
	const int32 Before = Events->GetSendCount();
	Events->Rebind();
	Laser->OnPing.Broadcast();
	TestEqual(TEXT("Rebinding does not double up"), Events->GetSendCount(), Before + 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEventOutputParseTest,
	"OSCulator.EventOutputs.ParseMIDI",
	OscuTest::Flags)

bool FOscuEventOutputParseTest::RunTest(const FString& Parameters)
{
	const auto Parse = [](const TCHAR* Text, EOscuPlayerMIDIKind& Kind, int32& Channel, int32& Number)
	{
		FString Error;
		return UOscuEventOutputSubsystem::ParseMIDIOut(Text, Kind, Channel, Number, Error);
	};

	EOscuPlayerMIDIKind Kind = EOscuPlayerMIDIKind::None;
	int32 Channel = 1;
	int32 Number = 0;

	TestTrue(TEXT("cc 21 reads"), Parse(TEXT("cc 21"), Kind, Channel, Number));
	TestTrue(TEXT("...as a controller"), Kind == EOscuPlayerMIDIKind::ControlChange);
	TestEqual(TEXT("...number 21"), Number, 21);

	TestTrue(TEXT("ch 2 cc 7 reads"), Parse(TEXT("ch 2 cc 7"), Kind, Channel, Number));
	TestEqual(TEXT("...channel 2"), Channel, 2);
	TestEqual(TEXT("...number 7"), Number, 7);

	TestTrue(TEXT("The menu's own spelling reads back"), Parse(TEXT("Ch 4 · CC 30"), Kind, Channel, Number));
	TestEqual(TEXT("...channel 4"), Channel, 4);
	TestEqual(TEXT("...number 30"), Number, 30);

	TestTrue(TEXT("A bare number keeps the kind and channel"), Parse(TEXT("12"), Kind, Channel, Number));
	TestTrue(TEXT("...still a controller"), Kind == EOscuPlayerMIDIKind::ControlChange);
	TestEqual(TEXT("...still channel 4"), Channel, 4);

	TestTrue(TEXT("note 60 reads"), Parse(TEXT("note 60"), Kind, Channel, Number));
	TestTrue(TEXT("...as a note"), Kind == EOscuPlayerMIDIKind::Note);
	TestEqual(TEXT("...number 60"), Number, 60);

	TestTrue(TEXT("off reads"), Parse(TEXT("off"), Kind, Channel, Number));
	TestTrue(TEXT("...as off"), Kind == EOscuPlayerMIDIKind::Off);

	TestFalse(TEXT("Channel 17 is refused"), Parse(TEXT("ch 17 cc 1"), Kind, Channel, Number));
	TestFalse(TEXT("CC 200 is refused"), Parse(TEXT("cc 200"), Kind, Channel, Number));
	TestFalse(TEXT("Nonsense is refused"), Parse(TEXT("banana"), Kind, Channel, Number));
	return true;
}

#endif
