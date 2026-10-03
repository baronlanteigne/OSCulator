// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuRouterSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuIntrospection.h"
#include "OscuMarshal.h"
#include "OscuSettings.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

//////////////////////////////////////////////////////////////////////////
// Tag scanning: prefix stripping, several actors per tag, several tags per actor

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuRegistryTagScanTest,
	"OSCulator.Registry.TagScan",
	OscuTest::Flags)

bool FOscuRegistryTagScanTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	FScopedTestWorld Scope;

	// Two actors sharing one tag, plus one actor answering to two tags.
	Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("OSC_laser") });
	Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("OSC_laser") });
	Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("OSC_alpha"), TEXT("OSC_beta") });

	// Not prefixed, and so not ours.
	Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("Gameplay"), TEXT("Lighting") });

	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists in a game world"), Router))
	{
		return false;
	}

	TestEqual(TEXT("Three tags registered"), Router->GetNumTags(), 3);

	TArray<AActor*> Actors;
	Router->GatherActors(FName("laser"), Actors);
	TestEqual(TEXT("Both actors registered under 'laser'"), Actors.Num(), 2);

	Router->GatherActors(FName("alpha"), Actors);
	TestEqual(TEXT("One actor under 'alpha'"), Actors.Num(), 1);
	Router->GatherActors(FName("beta"), Actors);
	TestEqual(TEXT("The same actor also answers to 'beta'"), Actors.Num(), 1);

	Router->GatherActors(FName("Gameplay"), Actors);
	TestEqual(TEXT("Unprefixed tags are ignored"), Actors.Num(), 0);

	// FName comparison is case-insensitive, which is free typo tolerance.
	Router->GatherActors(FName("LASER"), Actors);
	TestEqual(TEXT("Tag lookup is case-insensitive"), Actors.Num(), 2);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Actors spawned after BeginPlay join the registry

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuRegistryRuntimeSpawnTest,
	"OSCulator.Registry.RuntimeSpawn",
	OscuTest::Flags)

bool FOscuRegistryRuntimeSpawnTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	FScopedTestWorld Scope;

	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}
	TestEqual(TEXT("Registry starts empty"), Router->GetNumTags(), 0);

	// AOscuPreTaggedTestActor sets its tag in the constructor, the way a Blueprint
	// with tags set in Class Defaults does. The spawn handler fires after the actor
	// is constructed, so the tag is already there to be seen -- a tag added by
	// gameplay code AFTER spawning would arrive too late and not register.
	Scope.World->SpawnActor<AOscuPreTaggedTestActor>();

	TestEqual(TEXT("Runtime-spawned actor joined the registry"), Router->GetNumTags(), 1);

	TArray<AActor*> Actors;
	Router->GatherActors(FName("runtime"), Actors);
	TestEqual(TEXT("Found under its class-default tag"), Actors.Num(), 1);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Destroyed actors are purged lazily, on read

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuRegistryStalePurgeTest,
	"OSCulator.Registry.StalePurge",
	OscuTest::Flags)

bool FOscuRegistryStalePurgeTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	FScopedTestWorld Scope;

	AActor* First = Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("OSC_laser") });
	Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	TArray<AActor*> Actors;
	Router->GatherActors(FName("laser"), Actors);
	TestEqual(TEXT("Two actors before the destroy"), Actors.Num(), 2);

	First->Destroy();

	Router->GatherActors(FName("laser"), Actors);
	TestEqual(TEXT("The destroyed actor is gone"), Actors.Num(), 1);
	TestTrue(TEXT("The survivor is still valid"), IsValid(Actors[0]));

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Signatures: the §6 type table, as introspection reports it

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuRegistrySignatureTest,
	"OSCulator.Registry.Signatures",
	OscuTest::Flags)

bool FOscuRegistrySignatureTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	FScopedTestWorld Scope;

	Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	const TArray<FOscuExposedTagInfo> Tags = Router->Introspect(EOscuIntrospectFilter::All, FName("laser"));
	if (!TestEqual(TEXT("One tag described"), Tags.Num(), 1))
	{
		return false;
	}
	const FOscuExposedTagInfo& Laser = Tags[0];

	auto Expect = [this, &Laser](const TCHAR* Name, int32 ExpectedArgs, const TCHAR* ExpectedSignature)
	{
		const FOscuExposedFunctionInfo* Info = FindFunction(Laser, Name);
		if (Info == nullptr)
		{
			AddError(FString::Printf(TEXT("%s is missing from the registry"), Name));
			return;
		}
		TestEqual(FString::Printf(TEXT("%s argument count"), Name), Info->TotalArgCount, ExpectedArgs);
		TestEqual(FString::Printf(TEXT("%s signature"), Name), Info->GetSignatureString(), FString(ExpectedSignature));
		TestEqual(FString::Printf(TEXT("%s address"), Name), Info->Address, FString::Printf(TEXT("/laser/%s"), Name));
	};

	Expect(TEXT("Fire"), 5, TEXT("vec3, name, float"));
	Expect(TEXT("Stop"), 0, TEXT(""));
	Expect(TEXT("Aim"), 3, TEXT("rot(pitch,yaw,roll)"));
	Expect(TEXT("Place"), 9, TEXT("transform(loc3,rot(pitch,yaw,roll),scale3)"));
	Expect(TEXT("Configure"), 3, TEXT("bool, string, int"));
	Expect(TEXT("SetMode"), 1, TEXT("enum(EOscuTestMode)"));
	Expect(TEXT("Tint"), 4, TEXT("color(r,g,b,a)"));

	// A Blueprint "Float" pin is a double in UE5, and must classify as one argument
	// rather than falling off the end of the table.
	Expect(TEXT("SetIntensity"), 1, TEXT("float"));

	// Trailing array: one fixed argument, then everything else.
	if (const FOscuExposedFunctionInfo* Chase = FindFunction(Laser, TEXT("Chase")))
	{
		TestTrue(TEXT("Chase is variadic"), Chase->bVariadic);
		TestEqual(TEXT("Chase fixed argument count"), Chase->TotalArgCount, 1);
	}
	else
	{
		AddError(TEXT("Chase is missing from the registry"));
	}

	// A plain float& is a Blueprint OUTPUT pin: it takes a frame slot but consumes
	// no argument off the message.
	if (const FOscuExposedFunctionInfo* Query = FindFunction(Laser, TEXT("Query")))
	{
		TestEqual(TEXT("Query consumes only its input"), Query->TotalArgCount, 1);
		TestEqual(TEXT("Query still lists both parameters"), Query->Params.Num(), 2);
		TestTrue(TEXT("Query's second parameter is output-only"), Query->Params[1].bOutputOnly);
		TestTrue(TEXT("Query needs a per-actor frame"), Query->bHasOutParams);
	}
	else
	{
		AddError(TEXT("Query is missing from the registry"));
	}

	// UPARAM(ref) is an INPUT passed by reference, so it does consume an argument --
	// but it is still written back, so the frame cannot be shared between actors.
	if (const FOscuExposedFunctionInfo* Accumulate = FindFunction(Laser, TEXT("Accumulate")))
	{
		TestEqual(TEXT("Accumulate consumes its ref parameter"), Accumulate->TotalArgCount, 1);
		TestFalse(TEXT("Accumulate's parameter is not output-only"), Accumulate->Params[0].bOutputOnly);
		TestTrue(TEXT("Accumulate still needs a per-actor frame"), Accumulate->bHasOutParams);
	}
	else
	{
		AddError(TEXT("Accumulate is missing from the registry"));
	}

	// Unmarshallable functions are excluded outright rather than failing at call time.
	TestNull(TEXT("Attach (object reference) is excluded"), FindFunction(Laser, TEXT("Attach")));
	TestNull(TEXT("BadArray (array not last) is excluded"), FindFunction(Laser, TEXT("BadArray")));

	// Marshallable, and still excluded: a Blueprint's compiled event graph must not
	// be reachable by sending an integer.
	TestNull(TEXT("ExecuteUbergraph_* is excluded"), FindFunction(Laser, TEXT("ExecuteUbergraph_OscuTestActor")));

	// With bExposeAllFunctions on, inherited engine functions come along too.
	TestNotNull(TEXT("Inherited engine functions are exposed"), FindFunction(Laser, TEXT("K2_DestroyActor")));

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Filters

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuRegistryFilterTest,
	"OSCulator.Registry.Filters",
	OscuTest::Flags)

bool FOscuRegistryFilterTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	FScopedTestWorld Scope;

	Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("OSC_laser") });
	Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("OSC_fog") });
	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	const TArray<FOscuExposedTagInfo> All = Router->Introspect(EOscuIntrospectFilter::All);
	TestEqual(TEXT("All: both tags"), All.Num(), 2);
	TestTrue(TEXT("All: a fully-exposed actor drags in a lot of inherited surface"), All[0].Functions.Num() > 20);

	// The test actor is native, so nothing it exposes is Blueprint-authored. This is
	// what makes 'List Custom' the useful view in a real project.
	const TArray<FOscuExposedTagInfo> Custom = Router->Introspect(EOscuIntrospectFilter::Custom);
	TestEqual(TEXT("Custom: tags are still listed"), Custom.Num(), 2);
	TestEqual(TEXT("Custom: no Blueprint-authored functions on a native class"), Custom[0].Functions.Num(), 0);

	const TArray<FOscuExposedTagInfo> ActorsOnly = Router->Introspect(EOscuIntrospectFilter::ActorsOnly);
	TestEqual(TEXT("Actors: both tags"), ActorsOnly.Num(), 2);
	TestEqual(TEXT("Actors: no functions gathered"), ActorsOnly[0].Functions.Num(), 0);
	TestEqual(TEXT("Actors: actor still reported"), ActorsOnly[0].Actors.Num(), 1);

	// Tags come back sorted, so console output is reproducible between runs.
	TestEqual(TEXT("Tags are sorted"), All[0].Tag, FName("fog"));
	TestEqual(TEXT("Tags are sorted"), All[1].Tag, FName("laser"));

	const TArray<FOscuExposedTagInfo> Missing = Router->Introspect(EOscuIntrospectFilter::All, FName("nosuchtag"));
	TestEqual(TEXT("An unknown tag describes nothing"), Missing.Num(), 0);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Forgetting a class's signature.
//
// The editor loop is edit, compile, try again, so a reflection cache that only ever
// filled was guaranteed to go stale -- and did. A deleted function kept being offered to
// auto-map and a newly added one kept being missed, both because nothing ever asked the
// cache to forget. Intermittently, too: a Blueprint recompile often reuses the same
// UClass object with new contents, so a cache keyed on the class pointer cannot notice
// on its own.
//
// A Blueprint cannot be recompiled from an automation test, so what is checked here is
// the mechanism the recompile hook drives: that invalidation actually discards the entry,
// that it reaches derived classes, and that a re-scan includes it. The editor delegate
// wiring itself is not covered.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuRegistryStaleExposureTest,
	"OSCulator.Registry.StaleExposure",
	OscuTest::Flags)

bool FOscuRegistryStaleExposureTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	FScopedTestWorld Scope;
	Scope.SpawnTagged(AOscuTestActor::StaticClass(), { TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("the registry exists"), Router))
	{
		return false;
	}

	UClass* const ActorClass = AOscuTestActor::StaticClass();

	// Reflection walks, not object addresses. The obvious test -- compare the address of
	// the returned exposure before and after -- is unreliable: freeing a cached entry and
	// immediately building its replacement can land on the same address, which reads as a
	// cache hit that never happened. Counting the walks measures the real question.
	auto Builds = [Router] { return Router->GetClassExposureBuildCount(); };

	// ---- A warm entry is answered without looking again ----
	{
		Router->GetClassExposure(ActorClass);
		const uint64 Before = Builds();

		Router->GetClassExposure(ActorClass);
		Router->GetClassExposure(ActorClass);

		TestEqual(TEXT("a cached class is not walked again"), Builds(), Before);
	}

	// ---- Invalidating the class forces the next ask to look again ----
	{
		const uint64 Before = Builds();
		Router->InvalidateClassExposure(ActorClass);

		// Nothing is rebuilt eagerly; the cost is paid on next use.
		TestEqual(TEXT("invalidation itself walks nothing"), Builds(), Before);

		const FOscuClassExposure& Rebuilt = Router->GetClassExposure(ActorClass);
		TestEqual(TEXT("the next ask walks it once"), Builds(), Before + 1);

		// And it is correct, not merely rebuilt.
		TestTrue(TEXT("the rebuilt exposure still finds a known function"),
			Rebuilt.ByAddressName.Contains(FName(TEXT("Stop"))));
	}

	// ---- Invalidating a PARENT reaches the child ----
	{
		Router->GetClassExposure(ActorClass);
		const uint64 Before = Builds();

		// A child Blueprint's callable surface is built from everything it inherits, so a
		// parent that gained or lost a function changes what the child offers without the
		// child being recompiled at all. AActor is a real ancestor of the test actor.
		Router->InvalidateClassExposure(AActor::StaticClass());

		Router->GetClassExposure(ActorClass);
		TestEqual(TEXT("invalidating an ancestor discards the descendant"), Builds(), Before + 1);
	}

	// ---- An unrelated class is left alone ----
	{
		Router->GetClassExposure(ActorClass);
		const uint64 Before = Builds();

		// Not an ancestor of the actor, so the actor's entry must survive. Invalidation
		// that quietly cleared everything would hide its own bugs.
		Router->InvalidateClassExposure(UOscuRouterSubsystem::StaticClass());

		Router->GetClassExposure(ActorClass);
		TestEqual(TEXT("invalidating an unrelated class keeps the entry"), Builds(), Before);
	}

	// ---- THE REGRESSION: a re-scan forgets signatures, not just actors ----
	{
		Router->GetClassExposure(ActorClass);
		const uint64 Before = Builds();

		// ScanWorld is what both editor buttons -- Auto-Map From Level and Validate
		// Against Level -- come through. It used to reset the actor list and the trigger
		// cache while leaving the signature cache untouched, which meant there was
		// nothing a user could run to pick up a function they had just added or deleted:
		// a deleted one kept being offered, a new one kept being missed. "Re-scan" has to
		// mean all of it.
		Router->ScanWorld();

		Router->GetClassExposure(ActorClass);
		TestEqual(TEXT("ScanWorld discards cached signatures too"), Builds(), Before + 1);
	}

	// ---- Introspect still works across a re-scan, which is auto-map's actual path ----
	{
		Router->ScanWorld();

		const TArray<FOscuExposedTagInfo> Tags = Router->Introspect(EOscuIntrospectFilter::All, FName(TEXT("laser")));
		if (TestEqual(TEXT("the tag is still found after a re-scan"), Tags.Num(), 1))
		{
			TestTrue(TEXT("and still reports its functions"), Tags[0].Functions.Num() > 0);

			const bool bFoundStop = Tags[0].Functions.ContainsByPredicate(
				[](const FOscuExposedFunctionInfo& Info) { return Info.FunctionName == FName(TEXT("Stop")); });
			TestTrue(TEXT("including a known one"), bFoundStop);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
