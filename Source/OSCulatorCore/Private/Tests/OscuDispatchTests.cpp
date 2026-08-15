// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuRouterSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuMarshal.h"
#include "OscuValue.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

namespace OscuDispatchTest
{
	FOscuMessage Make(const TCHAR* Address, std::initializer_list<FOscuValue> Args)
	{
		FOscuMessage Message;
		Message.Address = Address;
		Message.Args.Append(Args);
		return Message;
	}

	/** Numbers arrive as floats overwhelmingly often, so that is the default here. */
	FOscuValue N(double Value) { return FOscuValue::MakeFloat(Value); }
	FOscuValue S(const TCHAR* Value) { return FOscuValue::MakeString(Value); }
}

//////////////////////////////////////////////////////////////////////////
// The core claim: the signature is the schema

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDispatchValuesTest,
	"OSCulator.Dispatch.Values",
	OscuTest::Flags)

bool FOscuDispatchValuesTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuDispatchTest;

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	// Five flat values become an FVector, an FName and a float, purely because
	// that is what Fire's parameter list says. The sender declared no types.
	FString Error;
	const int32 Calls = Router->DispatchMessage(
		Make(TEXT("/laser/Fire"), { N(0.0), N(0.0), N(1.0), S(TEXT("burst")), N(0.5) }),
		EOscuArgPolicy::Strict, &Error);

	TestTrue(FString::Printf(TEXT("Dispatch succeeded (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("One actor called"), Calls, 1);
	TestEqual(TEXT("Called Fire"), Laser->LastCalled, FName("Fire"));
	TestEqual(TEXT("Call count"), Laser->CallCount, 1);

	// The vector was assembled from three consecutive numbers by the marshaller;
	// the codec knows nothing about vectors on the receive side.
	TestEqual(TEXT("FVector assembled from 3 floats"), Laser->LastDir, FVector(0.0, 0.0, 1.0));
	TestEqual(TEXT("FName built from a string"), Laser->LastMode, FName("burst"));
	TestEqual(TEXT("Trailing float"), Laser->LastPower, 0.5f);

	// A zero-argument trigger.
	Router->DispatchMessage(Make(TEXT("/laser/Stop"), {}), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Zero-arg trigger fired"), Laser->LastCalled, FName("Stop"));
	TestEqual(TEXT("Call count advanced"), Laser->CallCount, 2);

	// Blueprint "Float" pins are doubles; this must not fall through the table.
	Router->DispatchMessage(Make(TEXT("/laser/SetIntensity"), { N(0.75) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Double parameter"), Laser->LastIntensity, 0.75);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Structs reassembled from consecutive numbers

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDispatchStructTest,
	"OSCulator.Dispatch.Structs",
	OscuTest::Flags)

bool FOscuDispatchStructTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuDispatchTest;

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();
	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	FString Error;

	// FRotator is consumed in struct member order: Pitch, Yaw, Roll. Anyone coming
	// from TouchDesigner thinks X/Y/Z, which would be Roll/Pitch/Yaw -- the reverse.
	Router->DispatchMessage(Make(TEXT("/laser/Aim"), { N(10.0), N(20.0), N(30.0) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Rotator pitch is the FIRST argument"), Laser->LastRotation.Pitch, 10.0);
	TestEqual(TEXT("Rotator yaw is the second"), Laser->LastRotation.Yaw, 20.0);
	TestEqual(TEXT("Rotator roll is the third"), Laser->LastRotation.Roll, 30.0);

	// FLinearColor components are float, not double like the vector types.
	Router->DispatchMessage(Make(TEXT("/laser/Tint"), { N(0.25), N(0.5), N(0.75), N(1.0) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Colour R"), Laser->LastColour.R, 0.25f);
	TestEqual(TEXT("Colour G"), Laser->LastColour.G, 0.5f);
	TestEqual(TEXT("Colour B"), Laser->LastColour.B, 0.75f);
	TestEqual(TEXT("Colour A"), Laser->LastColour.A, 1.0f);

	// Transform: loc(3), rot as pitch/yaw/roll(3), scale(3).
	Router->DispatchMessage(
		Make(TEXT("/laser/Place"), { N(1.0), N(2.0), N(3.0), N(10.0), N(20.0), N(30.0), N(2.0), N(2.0), N(2.0) }),
		EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Place dispatched (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("Transform location"), Laser->LastTransform.GetLocation(), FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("Transform scale"), Laser->LastTransform.GetScale3D(), FVector(2.0, 2.0, 2.0));
	TestEqual(TEXT("Transform rotation pitch"), Laser->LastTransform.Rotator().Pitch, 10.0, 0.01);
	TestEqual(TEXT("Transform rotation yaw"), Laser->LastTransform.Rotator().Yaw, 20.0, 0.01);
	TestEqual(TEXT("Transform rotation roll"), Laser->LastTransform.Rotator().Roll, 30.0, 0.01);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Every actor sharing a tag fires

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDispatchMultipleActorsTest,
	"OSCulator.Dispatch.MultipleActors",
	OscuTest::Flags)

bool FOscuDispatchMultipleActorsTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuDispatchTest;

	FScopedTestWorld Scope;
	AOscuTestActor* A = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	AOscuTestActor* B = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	AOscuTestActor* Unrelated = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_fog") });
	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	FString Error;
	const int32 Calls = Router->DispatchMessage(
		Make(TEXT("/laser/Fire"), { N(1.0), N(0.0), N(0.0), S(TEXT("wide")), N(0.9) }),
		EOscuArgPolicy::Strict, &Error);

	TestEqual(TEXT("Both tagged actors were called"), Calls, 2);
	TestEqual(TEXT("First actor fired"), A->CallCount, 1);
	TestEqual(TEXT("Second actor fired"), B->CallCount, 1);
	TestEqual(TEXT("Both got the same values"), A->LastDir, B->LastDir);
	TestEqual(TEXT("Differently-tagged actor untouched"), Unrelated->CallCount, 0);

	// The shared frame carries FName and FVector parameters, so if the frame were
	// destroyed between the two calls this would be reading freed memory.
	TestEqual(TEXT("Shared frame survived both calls"), B->LastMode, FName("wide"));

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Strict rejects, and says what it wanted

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDispatchStrictTest,
	"OSCulator.Dispatch.Strict",
	OscuTest::Flags)

bool FOscuDispatchStrictTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuDispatchTest;

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();
	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	// Four arguments where the signature consumes five.
	FString Error;
	const int32 Calls = Router->DispatchMessage(
		Make(TEXT("/laser/Fire"), { N(0.0), N(0.0), N(1.0), S(TEXT("burst")) }),
		EOscuArgPolicy::Strict, &Error);

	TestEqual(TEXT("Nothing was called"), Calls, 0);
	TestEqual(TEXT("The actor never saw it"), Laser->CallCount, 0);

	// The expected signature is generated from the reflection data, so the sender
	// is told what to fix rather than merely that something broke.
	TestTrue(FString::Printf(TEXT("Error names the address: %s"), *Error), Error.Contains(TEXT("/laser/Fire")));
	TestTrue(FString::Printf(TEXT("Error states the expected count: %s"), *Error), Error.Contains(TEXT("expects 5 args")));
	TestTrue(FString::Printf(TEXT("Error prints the signature: %s"), *Error), Error.Contains(TEXT("(vec3, name, float)")));
	TestTrue(FString::Printf(TEXT("Error states what arrived: %s"), *Error), Error.Contains(TEXT("got 4")));

	// Too many is NOT equally wrong. Too few means running on values the sender
	// never supplied; too many means running on exactly the values asked for, with
	// ignorable data trailing. Senders append surplus routinely -- a TouchDesigner
	// CHOP emits every channel it has -- so the extra is discarded and the call
	// goes ahead.
	Error.Reset();
	const int32 SurplusCalls = Router->DispatchMessage(
		Make(TEXT("/laser/Fire"), { N(0.0), N(0.0), N(1.0), S(TEXT("burst")), N(0.5), N(9.0), N(9.0) }),
		EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Surplus arguments are tolerated (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("It was called"), SurplusCalls, 1);
	TestEqual(TEXT("The declared parameters got the right values"), Laser->LastDir, FVector(0.0, 0.0, 1.0));
	TestEqual(TEXT("...including the last declared one"), Laser->LastPower, 0.5f);

	// The case that matters most: a zero-argument trigger reached by a sender that
	// cannot emit an empty message.
	Error.Reset();
	const int32 TriggerCalls = Router->DispatchMessage(
		Make(TEXT("/laser/Stop"), { N(0.0) }), EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("A trigger fires despite a surplus argument (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("The trigger ran"), TriggerCalls, 1);
	TestEqual(TEXT("...and it was Stop"), Laser->LastCalled, FName("Stop"));

	// Unknown addresses are refused rather than silently swallowed.
	Error.Reset();
	Router->DispatchMessage(Make(TEXT("/laser/NoSuchFunction"), {}), EOscuArgPolicy::Strict, &Error);
	TestFalse(TEXT("Unknown function is reported"), Error.IsEmpty());

	Error.Reset();
	Router->DispatchMessage(Make(TEXT("/laser/Fire/extra"), {}), EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Malformed address is reported: %s"), *Error), Error.Contains(TEXT("/tag/function")));

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Lenient fills what it can and zeroes the rest -- this is MIDI's mode

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDispatchLenientTest,
	"OSCulator.Dispatch.Lenient",
	OscuTest::Flags)

bool FOscuDispatchLenientTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuDispatchTest;

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Laser->LastPower = 99.0f;
	Laser->LastMode = TEXT("stale");
	Scope.BeginPlay();
	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	// One value against a five-argument signature. This is exactly what MIDI does:
	// it supplies velocity and nothing else, whatever the signature wants.
	FString Error;
	const int32 Calls = Router->DispatchMessage(
		Make(TEXT("/laser/Fire"), { N(0.6) }), EOscuArgPolicy::Lenient, &Error);

	TestEqual(TEXT("It was called anyway"), Calls, 1);
	TestEqual(TEXT("The supplied value landed in the first slot"), Laser->LastDir.X, 0.6);

	// Everything unfilled is whatever the zeroed, initialised frame gave it. In
	// particular NOT the Blueprint parameter default -- those live in editor-only
	// metadata and are baked into the call node, so ProcessEvent never applies them.
	TestEqual(TEXT("Unfilled vector components are zero"), Laser->LastDir.Y, 0.0);
	TestEqual(TEXT("Unfilled vector components are zero"), Laser->LastDir.Z, 0.0);
	TestEqual(TEXT("Unfilled name is empty, not stale"), Laser->LastMode, FName());
	TestEqual(TEXT("Unfilled float is zero, not stale"), Laser->LastPower, 0.0f);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Trailing arrays, out parameters, and permissive coercion

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDispatchEdgeCaseTest,
	"OSCulator.Dispatch.EdgeCases",
	OscuTest::Flags)

bool FOscuDispatchEdgeCaseTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuDispatchTest;

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();
	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	FString Error;

	// A trailing array swallows everything left over.
	Router->DispatchMessage(
		Make(TEXT("/laser/Chase"), { N(2.0), N(10.0), N(20.0), N(30.0) }), EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Variadic dispatched (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("Fixed argument"), Laser->LastSpeed, 2.0f);
	if (TestEqual(TEXT("Array took the remainder"), Laser->LastPoints.Num(), 3))
	{
		TestEqual(TEXT("Array element 0"), Laser->LastPoints[0], 10.0f);
		TestEqual(TEXT("Array element 2"), Laser->LastPoints[2], 30.0f);
	}

	// A variadic signature is satisfied by its fixed arguments alone.
	Error.Reset();
	Router->DispatchMessage(Make(TEXT("/laser/Chase"), { N(5.0) }), EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Empty remainder is legal (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("Array is empty"), Laser->LastPoints.Num(), 0);

	// An output parameter consumes nothing, so this is a one-argument call.
	Error.Reset();
	Router->DispatchMessage(Make(TEXT("/laser/Query"), { N(4.0) }), EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Out-param function dispatched (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("Its input arrived"), Laser->LastPower, 4.0f);

	// Coercion is permissive: a string asked for a number parses itself.
	Error.Reset();
	Router->DispatchMessage(Make(TEXT("/laser/SetIntensity"), { S(TEXT("0.25")) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("String coerced to a number"), Laser->LastIntensity, 0.25);

	// ...and a number asked for a string formats itself.
	Error.Reset();
	Router->DispatchMessage(
		Make(TEXT("/laser/Configure"), { N(1.0), N(42.0), N(7.9) }), EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Configure dispatched (%s)"), *Error), Error.IsEmpty());
	TestTrue(TEXT("Non-zero number coerced to true"), Laser->bLastEnabled);
	TestTrue(TEXT("Number coerced to a string"), Laser->LastLabel.Contains(TEXT("42")));
	TestEqual(TEXT("Float truncated toward zero for an int"), Laser->LastCount, 7);

	// An enum accepts its name as readily as its index.
	Error.Reset();
	Router->DispatchMessage(Make(TEXT("/laser/SetMode"), { S(TEXT("Sweep")) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Enum resolved by name"), static_cast<int32>(Laser->LastEnumMode), static_cast<int32>(EOscuTestMode::Sweep));

	Error.Reset();
	Router->DispatchMessage(Make(TEXT("/laser/SetMode"), { N(1.0) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Enum resolved by index"), static_cast<int32>(Laser->LastEnumMode), static_cast<int32>(EOscuTestMode::Burst));

	// Excluded at registration, and therefore not reachable by dispatch either.
	Error.Reset();
	const int32 Calls = Router->DispatchMessage(
		Make(TEXT("/laser/ExecuteUbergraph_OscuTestActor"), { N(4172.0) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("ExecuteUbergraph_* cannot be dispatched"), Calls, 0);
	TestFalse(TEXT("...and the refusal is reported"), Error.IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
