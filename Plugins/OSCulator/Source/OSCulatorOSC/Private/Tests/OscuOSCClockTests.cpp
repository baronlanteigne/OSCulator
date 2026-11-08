// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuClockSubsystem.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

//////////////////////////////////////////////////////////////////////////
// Tempo over OSC reaches the clock. No socket: ApplyTempoMessage is everything the
// drain does with a message at the tempo address.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuOSCClockTempoTest,
	"OSCulator.OSC.ClockTempo",
	OscuTest::Flags)

bool FOscuOSCClockTempoTest::RunTest(const FString& Parameters)
{
	UOscuClockSubsystem* Clock = UOscuClockSubsystem::Get();
	if (!TestNotNull(TEXT("The clock exists"), Clock))
	{
		return false;
	}

	Clock->ResetForTest();
	ON_SCOPE_EXIT { Clock->ResetForTest(); };

	const FString Address = TEXT("/clock/SetBPM");

	FOscuMessage Tempo;
	Tempo.Address = Address;
	Tempo.Args.Add(FOscuValue::MakeFloat(128.0));

	TestTrue(TEXT("The tempo address is answered"), OscuOSC::ApplyTempoMessage(Tempo, Address));
	TestEqual(TEXT("...and sets the BPM"), Clock->GetBPM(), 128.0f);

	Tempo.Args[0] = FOscuValue::MakeInt(96);
	OscuOSC::ApplyTempoMessage(Tempo, Address);
	TestEqual(TEXT("An int works as well as a float"), Clock->GetBPM(), 96.0f);

	Tempo.Args[0] = FOscuValue::MakeFloat(0.0);
	TestTrue(TEXT("0 is still answered, not routed"), OscuOSC::ApplyTempoMessage(Tempo, Address));
	TestEqual(TEXT("...but means no tempo yet, so the BPM stays"), Clock->GetBPM(), 96.0f);

	FOscuMessage Empty;
	Empty.Address = Address;
	AddExpectedMessagePlain(TEXT("needs one number"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("No argument is answered too"), OscuOSC::ApplyTempoMessage(Empty, Address));
	TestEqual(TEXT("...and changes nothing"), Clock->GetBPM(), 96.0f);

	FOscuMessage Other;
	Other.Address = TEXT("/aesthesis/SetBPM");
	Other.Args.Add(FOscuValue::MakeFloat(140.0));
	TestFalse(TEXT("Another address is left for the router"), OscuOSC::ApplyTempoMessage(Other, Address));
	TestEqual(TEXT("...and does not touch the tempo"), Clock->GetBPM(), 96.0f);

	TestFalse(TEXT("With no tempo address set, nothing is answered"), OscuOSC::ApplyTempoMessage(Tempo, FString()));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
