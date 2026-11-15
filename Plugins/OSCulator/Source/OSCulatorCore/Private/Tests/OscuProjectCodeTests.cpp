// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuProjectCode.h"

#include "Misc/AutomationTest.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuProjectCodeTest,
	"OSCulator.Exposure.ProjectCode",
	OscuTest::Flags)

bool FOscuProjectCodeTest::RunTest(const FString& Parameters)
{
	using OscuProjectCode::IsDeclaredIn;

	const UClass* Component = UOscuTestComponent::StaticClass();
	const UFunction* Nudge = Component->FindFunctionByName(TEXT("Nudge"));
	const UFunction* Activate = Component->FindFunctionByName(TEXT("Activate"));
	if (!TestNotNull(TEXT("Nudge found"), Nudge) || !TestNotNull(TEXT("Activate found"), Activate))
	{
		return false;
	}

	// The test component lives in OSCulatorCore, so that module stands in for "the project".
	const TSet<FName> Project = { FName(TEXT("OSCulatorCore")) };
	const TSet<FName> Elsewhere = { FName(TEXT("SomeGame")) };

	TestTrue(TEXT("A function the component declares counts when its module is the project's"), IsDeclaredIn(Nudge, Project));
	TestFalse(TEXT("...and not when it is someone else's module"), IsDeclaredIn(Nudge, Elsewhere));
	TestFalse(TEXT("An engine function the component inherits never counts"), IsDeclaredIn(Activate, Project));
	TestFalse(TEXT("No modules, nothing counts"), IsDeclaredIn(Nudge, TSet<FName>()));
	TestFalse(TEXT("No function, nothing counts"), IsDeclaredIn(nullptr, Project));

	// The real list: an engine module is never one of the project's own.
	TestFalse(TEXT("Engine is not a project module"), OscuProjectCode::GetProjectModuleNames().Contains(FName(TEXT("Engine"))));

	return true;
}

#endif
