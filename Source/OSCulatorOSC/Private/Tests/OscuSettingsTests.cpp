// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuOSCSubsystem.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

namespace OscuSettingsTest
{
	/** Restores whatever it changed, however the test leaves. */
	struct FScopedListenPort
	{
		UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
		int32 Saved = 0;

		explicit FScopedListenPort(int32 Port)
		{
			Saved = Settings->ListenPort;
			Settings->ListenPort = Port;
		}

		~FScopedListenPort() { Settings->ListenPort = Saved; }
	};
}

//////////////////////////////////////////////////////////////////////////
// The gate is real: unchecked means nothing is opened

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuSettingsOSCInGateTest,
	"OSCulator.Settings.OSCInputGate",
	OscuTest::Flags)

bool FOscuSettingsOSCInGateTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;

	{
		// Disabled. The subsystem still exists -- it just does nothing.
		FScopedTestWorld Scope(/*bEnableOSCInput*/ false);
		Scope.BeginPlay();

		UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(Scope.World);
		if (!TestNotNull(TEXT("The OSC subsystem exists in a game world"), OSC))
		{
			return false;
		}
		TestFalse(TEXT("Disabled: no socket was opened"), OSC->IsListening());
		TestEqual(TEXT("Disabled: no port was bound"), OSC->GetListenPort(), 0);
		TestEqual(TEXT("Disabled: nothing was received"), OSC->GetPacketsReceived(), static_cast<uint64>(0));
	}

	{
		// Enabled, on an ephemeral port so the test cannot collide with a real
		// sender or with a previous run still holding the configured port.
		OscuSettingsTest::FScopedListenPort Port(0);

		FScopedTestWorld Scope(/*bEnableOSCInput*/ true);
		Scope.BeginPlay();

		UOscuOSCSubsystem* OSC = UOscuOSCSubsystem::Get(Scope.World);
		if (!TestNotNull(TEXT("The OSC subsystem exists"), OSC))
		{
			return false;
		}
		TestTrue(TEXT("Enabled: the socket opened"), OSC->IsListening());
		TestTrue(TEXT("Enabled: a real port was bound"), OSC->GetListenPort() > 0);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// There is deliberately no test asserting the header's default values.
//
// A Config property loads from DefaultGame.ini straight into the class default
// object, so GetDefault<>() returns whatever the project has saved. There is no
// separate "compile-time default" left to read once config has been applied --
// GetClass()->GetDefaultObject() is the very object config was loaded into.
//
// Such a test therefore asserts the contents of the user's ini rather than the
// behaviour of this code, and fails the moment someone legitimately ticks a box in
// Project Settings. What is worth testing is that a setting is HONOURED, which is
// what the gate test above does: it sets the value itself, then checks what the
// runtime did about it.

#endif // WITH_DEV_AUTOMATION_TESTS
