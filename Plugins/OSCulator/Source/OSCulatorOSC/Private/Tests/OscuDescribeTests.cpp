// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDescribeTest,
	"OSCulator.OSC.Describe",
	OscuTest::Flags)

bool FOscuDescribeTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;

	FScopedTestWorld Scope;
	Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(Scope.World);
	if (!TestNotNull(TEXT("The OSC subsystem exists"), OSC))
	{
		return false;
	}

	TArray<FOscuMessage> Reply;
	OSC->BuildDescribeReply(Reply);

	// Always bracketed, so a listener knows when the description is complete even
	// though UDP gives no delivery guarantee.
	if (!TestTrue(TEXT("The reply is bracketed"), Reply.Num() >= 2))
	{
		return false;
	}
	TestEqual(TEXT("It opens with begin"), Reply[0].Address, FString(TEXT("/_describe/begin")));
	TestEqual(TEXT("It closes with end"), Reply.Last().Address, FString(TEXT("/_describe/end")));

	// The test actor is native, so nothing it exposes is Blueprint-authored and the
	// description is empty. That is the point: describing a couple of hundred
	// inherited engine functions would drown whatever asked.
	const int32 FunctionCount = Reply.Num() - 2;
	TestEqual(TEXT("The end message reports the count"), Reply.Last().Args[0].AsInt(), static_cast<int64>(FunctionCount));
	TestEqual(TEXT("A native-only actor describes nothing"), FunctionCount, 0);

	// Every entry between the brackets is flat and self-describing.
	for (int32 Index = 1; Index < Reply.Num() - 1; ++Index)
	{
		const FOscuMessage& Entry = Reply[Index];
		TestEqual(TEXT("Entries share one address"), Entry.Address, FString(TEXT("/_describe/function")));
		TestEqual(TEXT("Entries carry six fields"), Entry.Args.Num(), 6);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
