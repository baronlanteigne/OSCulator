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

#endif // WITH_DEV_AUTOMATION_TESTS
