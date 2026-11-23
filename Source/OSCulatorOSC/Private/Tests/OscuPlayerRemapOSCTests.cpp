// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuOSCSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuPlayerMappings.h"
#include "Tests/OscuScopedPlayerMenu.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

namespace OscuPlayerOSCTest
{
	FOscuMessage Msg(const TCHAR* Address, std::initializer_list<double> Values = {})
	{
		FOscuMessage Out;
		Out.Address = Address;
		for (const double Value : Values)
		{
			Out.Args.Add(FOscuValue::MakeFloat(Value));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuPlayerKeysTest,
	"OSCulator.PlayerMenu.AddressKeys",
	OscuTest::Flags)

bool FOscuPlayerKeysTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Alias keys ignore case and a trailing slash"),
		FOscuPlayerMappings::AliasKey(TEXT(" /1/Fader1/ ")), FString(TEXT("/1/fader1")));
	TestEqual(TEXT("Routing keys join deeper segments the way the router does"),
		FOscuPlayerMappings::RoutingKey(TEXT("/Laser/Beam/Width")), FString(TEXT("/laser/beam_width")));
	TestEqual(TEXT("...and match the underscore spelling"),
		FOscuPlayerMappings::RoutingKey(TEXT("/laser/Beam_Width")), FString(TEXT("/laser/beam_width")));
	TestEqual(TEXT("The default address is /tag/function"),
		FOscuPlayerMappings::DefaultOSCAddress(FName("laser"), FName("Fire")), FString(TEXT("/laser/Fire")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuPlayerOSCRemapTest,
	"OSCulator.PlayerMenu.OSCRemap",
	OscuTest::Flags)

bool FOscuPlayerOSCRemapTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuPlayerOSCTest;

	FScopedPlayerMenu Menu({
		FScopedPlayerMenu::Entry(TEXT("laser"), TEXT("SetIntensity")),
		FScopedPlayerMenu::Entry(TEXT("laser"), TEXT("Beam_Width"), /*bOSC*/ false),
	});

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(Scope.World);
	if (!TestNotNull(TEXT("The OSC subsystem exists in a game world"), OSC))
	{
		return false;
	}

	FOscuPlayerMappings::Get().SetOSCAddress(FName("laser"), FName("SetIntensity"), TEXT("/1/fader1"));
	FOscuPlayerMappings::Get().SetOSCAddress(FName("laser"), FName("Beam_Width"), TEXT("/1/fader2"));
	TestEqual(TEXT("Only the entry that allows OSC is in effect"), OSC->GetAddressAliases().Num(), 1);

	{
		TArray<FOscuMessage> Batch = {
			Msg(TEXT("/1/FADER1/"), { 0.5 }),
			Msg(TEXT("/laser/SetIntensity"), { 0.2 }),
			Msg(TEXT("/laser/Fire")),
			Msg(TEXT("/1/fader2"), { 0.3 }),
			Msg(TEXT("/laser/Beam/Width"), { 0.4 }),
		};
		OSC->ApplyPlayerRemaps(Batch);

		TestEqual(TEXT("The function's own address was dropped, nothing else"), Batch.Num(), 4);
		TestEqual(TEXT("The player's address is rewritten to the function's"), Batch[0].Address, FString(TEXT("/laser/SetIntensity")));
		TestEqual(TEXT("Its value is untouched"), Batch[0].Args[0].AsNumber(), 0.5);
		TestEqual(TEXT("Unrelated addresses pass through"), Batch[1].Address, FString(TEXT("/laser/Fire")));
		TestEqual(TEXT("A remap the menu does not allow is ignored"), Batch[2].Address, FString(TEXT("/1/fader2")));
		TestEqual(TEXT("...and its function keeps its own address"), Batch[3].Address, FString(TEXT("/laser/Beam/Width")));

		const int32 Calls = Scope.Router()->DispatchMessage(Batch[0], EOscuArgPolicy::Strict);
		TestEqual(TEXT("The rewritten message reaches the actor"), Calls, 1);
		TestEqual(TEXT("...with the player's value"), Laser->LastIntensity, 0.5, 0.0001);
	}

	// Listen: the next address is handed over and not routed.
	{
		FString Captured;
		int32 Callbacks = 0;
		OSC->ArmAddressCapture([&Captured, &Callbacks](const FString& Address) { Captured = Address; ++Callbacks; });

		TArray<FOscuMessage> Batch = { Msg(TEXT("/_describe")), Msg(TEXT("/2/push3"), { 1.0 }), Msg(TEXT("/1/toggle"), { 1.0 }) };
		OSC->ApplyPlayerRemaps(Batch);

		TestEqual(TEXT("Listen takes the first routable address"), Captured, FString(TEXT("/2/push3")));
		TestEqual(TEXT("...once"), Callbacks, 1);
		TestEqual(TEXT("...and only that message is consumed"), Batch.Num(), 2);
		TestEqual(TEXT("Control traffic is never captured"), Batch[0].Address, FString(TEXT("/_describe")));
		TestFalse(TEXT("Listen disarms itself"), OSC->IsCapturingAddress());
	}

	// Typing the default back in is a reset.
	FOscuPlayerMappings::Get().SetOSCAddress(FName("laser"), FName("SetIntensity"), TEXT("/laser/setintensity"));
	TestEqual(TEXT("Setting the default address clears the remap"), OSC->GetAddressAliases().Num(), 0);
	{
		TArray<FOscuMessage> Batch = { Msg(TEXT("/laser/SetIntensity"), { 0.2 }) };
		OSC->ApplyPlayerRemaps(Batch);
		TestEqual(TEXT("...and the function answers to its own address again"), Batch.Num(), 1);
	}

	return true;
}

#endif
