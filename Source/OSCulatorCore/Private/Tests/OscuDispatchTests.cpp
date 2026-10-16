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
	Router->DispatchMessage(Make(TEXT("/laser"), {}), EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Malformed address is reported: %s"), *Error), Error.Contains(TEXT("/tag/function")));

	// A deeper address is well formed -- it names Fire_extra -- and is refused only
	// because nothing declares that.
	Error.Reset();
	const int32 NestedCalls = Router->DispatchMessage(Make(TEXT("/laser/Fire/extra"), {}), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Nested address to a missing function calls nothing"), NestedCalls, 0);
	TestFalse(TEXT("...and is reported"), Error.IsEmpty());

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

//////////////////////////////////////////////////////////////////////////
// Deeper addresses join into one function name

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDispatchNestedAddressTest,
	"OSCulator.Dispatch.NestedAddress",
	OscuTest::Flags)

bool FOscuDispatchNestedAddressTest::RunTest(const FString& Parameters)
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

	// The grouped form a sender's parameter pages produce.
	FString Error;
	int32 Calls = Router->DispatchMessage(Make(TEXT("/laser/Beam/Width"), { N(0.25) }), EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Nested address dispatched (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("One actor called"), Calls, 1);
	TestEqual(TEXT("Segments joined with '_'"), Laser->LastCalled, FName("Beam_Width"));
	TestEqual(TEXT("Value arrived"), Laser->LastWidth, 0.25f);

	// The flat form reaches the same function.
	Error.Reset();
	Calls = Router->DispatchMessage(Make(TEXT("/laser/Beam_Width"), { N(0.5) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Flat address reaches the same function"), Calls, 1);
	TestEqual(TEXT("...with its value"), Laser->LastWidth, 0.5f);

	// Argument counting is unchanged by the nesting.
	Error.Reset();
	Calls = Router->DispatchMessage(Make(TEXT("/laser/Beam/Width"), {}), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Too few arguments still refused"), Calls, 0);
	TestFalse(TEXT("...and reported"), Error.IsEmpty());

	return true;
}

//////////////////////////////////////////////////////////////////////////
// The end of an address can be an argument

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDispatchAddressLeafTest,
	"OSCulator.Dispatch.AddressLeaf",
	OscuTest::Flags)

bool FOscuDispatchAddressLeafTest::RunTest(const FString& Parameters)
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

	// No MatParam_Emissive exists, so MatParam is called with the leaf in front.
	FString Error;
	int32 Calls = Router->DispatchMessage(
		Make(TEXT("/laser/MatParam/Emissive"), { N(0.5), N(0.7) }), EOscuArgPolicy::Strict, &Error);
	TestTrue(FString::Printf(TEXT("Leaf address dispatched (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("One actor called"), Calls, 1);
	TestEqual(TEXT("The shorter function answered"), Laser->LastCalled, FName("MatParam"));
	TestEqual(TEXT("The leaf arrived as the name"), Laser->LastMatName, FName("Emissive"));
	TestEqual(TEXT("The message's own arguments follow it"), Laser->LastMatDuration, 0.5f);
	if (TestEqual(TEXT("...into the trailing array"), Laser->LastMatValues.Num(), 1))
	{
		TestEqual(TEXT("...with its value"), Laser->LastMatValues[0], 0.7f);
	}

	// A leaf containing '_' is kept whole: MatParam_Base is not interned, so the peel
	// carries on to MatParam with Base_Color.
	Error.Reset();
	Calls = Router->DispatchMessage(
		Make(TEXT("/laser/MatParam/Base_Color"), { N(0.0), N(1.0), N(0.0), N(0.0) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Leaf with '_' dispatched"), Calls, 1);
	TestEqual(TEXT("...whole"), Laser->LastMatName, FName("Base_Color"));
	TestEqual(TEXT("...with a colour's worth of values"), Laser->LastMatValues.Num(), 3);

	// The flat form peels the same way, so a MIDI binding naming MatParam_Emissive works.
	Error.Reset();
	Calls = Router->DispatchMessage(Make(TEXT("/laser/MatParam_Emissive"), { N(0.25) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Flat leaf address dispatched"), Calls, 1);
	TestEqual(TEXT("...with the leaf"), Laser->LastMatName, FName("Emissive"));

	// Only text takes a leaf. Beam_Width wants a float, so the typo is refused rather
	// than called with a word in it.
	Laser->LastWidth = 0.75f;
	Error.Reset();
	Calls = Router->DispatchMessage(Make(TEXT("/laser/Beam/Width/extra"), { N(0.25) }), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("A non-text first parameter is not a leaf target"), Calls, 0);
	TestFalse(TEXT("...and the miss is reported"), Error.IsEmpty());
	TestEqual(TEXT("...and Beam_Width was not called"), Laser->LastWidth, 0.75f);

	// Argument counting includes the leaf: Name and Duration are both required.
	Error.Reset();
	Calls = Router->DispatchMessage(Make(TEXT("/laser/MatParam/Emissive"), {}), EOscuArgPolicy::Strict, &Error);
	TestEqual(TEXT("Too few arguments still refused"), Calls, 0);

	// MIDI asks for slots by the name it dispatches under. The leaf has no slot, and
	// the parameters after it count from zero, because the address fills it.
	const FName LaserTag = FName(TEXT("laser"));
	const FName Flat = FName(TEXT("MatParam_Emissive"));
	TestEqual(TEXT("Duration is slot 0 through the leaf"), Router->FindParamSlot(LaserTag, Flat, FName(TEXT("Duration"))), 0);
	TestEqual(TEXT("Values is slot 1 through the leaf"), Router->FindParamSlot(LaserTag, Flat, FName(TEXT("Values"))), 1);
	TestEqual(TEXT("The leaf itself has no slot"), Router->FindParamSlot(LaserTag, Flat, FName(TEXT("Name"))), INDEX_NONE);
	TestEqual(TEXT("Called directly, Name is slot 0"),
		Router->FindParamSlot(LaserTag, FName(TEXT("MatParam")), FName(TEXT("Name"))), 0);

	TArray<FOscuExposedParamInfo> Params;
	TArray<int32> Slots;
	if (TestTrue(TEXT("Described through the leaf"), Router->DescribeParams(LaserTag, Flat, Params, Slots)))
	{
		TestEqual(TEXT("...without the leaf parameter"), Params.Num(), 2);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Components answer for their actor

namespace OscuDispatchTest
{
	/** Restores ExtraAllowedFunctions, so an opt-in cannot leak into later tests. */
	struct FScopedExtraAllowed
	{
		explicit FScopedExtraAllowed(std::initializer_list<FName> Names)
		{
			Settings = GetMutableDefault<UOscuSettings>();
			Saved = Settings->ExtraAllowedFunctions;
			Settings->ExtraAllowedFunctions = TArray<FName>(Names);
		}

		~FScopedExtraAllowed()
		{
			Settings->ExtraAllowedFunctions = Saved;
		}

		FScopedExtraAllowed(const FScopedExtraAllowed&) = delete;
		FScopedExtraAllowed& operator=(const FScopedExtraAllowed&) = delete;

	private:
		UOscuSettings* Settings = nullptr;
		TArray<FName> Saved;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuDispatchComponentTest,
	"OSCulator.Dispatch.Components",
	OscuTest::Flags)

bool FOscuDispatchComponentTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuDispatchTest;

	// Default exposure: the blanket does not reach native component functions, whether
	// the component declares them or inherits them from the engine.
	{
		FScopedTestWorld Scope;
		AOscuComponentHostTestActor* Host = Scope.SpawnTaggedAs<AOscuComponentHostTestActor>({ TEXT("OSC_host") });
		Scope.BeginPlay();

		UOscuRouterSubsystem* Router = Scope.Router();
		if (!TestNotNull(TEXT("Router subsystem exists"), Router))
		{
			return false;
		}

		FString Error;
		int32 Calls = Router->DispatchMessage(
			Make(TEXT("/host/Nudge"), { N(1.0), N(2.0), N(3.0), N(0.5) }), EOscuArgPolicy::Strict, &Error);
		TestEqual(TEXT("Native component function stays shut under the blanket"), Calls, 0);
		TestEqual(TEXT("...so the component was not called"), Host->First->NudgeCount, 0);

		Error.Reset();
		Calls = Router->DispatchMessage(Make(TEXT("/host/Activate"), { N(0.0) }), EOscuArgPolicy::Strict, &Error);
		TestEqual(TEXT("Inherited engine component functions are not exposed"), Calls, 0);
	}

	// Opted in: what Blueprint-authored component functions get by default.
	{
		FScopedExtraAllowed Allow({ FName(TEXT("Nudge")), FName(TEXT("Ping")) });

		FScopedTestWorld Scope;
		AOscuComponentHostTestActor* Host = Scope.SpawnTaggedAs<AOscuComponentHostTestActor>({ TEXT("OSC_host") });
		Scope.BeginPlay();

		UOscuRouterSubsystem* Router = Scope.Router();
		if (!TestNotNull(TEXT("Router subsystem exists"), Router))
		{
			return false;
		}

		// The actor has no Nudge, so its components answer -- both of them.
		FString Error;
		int32 Calls = Router->DispatchMessage(
			Make(TEXT("/host/Nudge"), { N(1.0), N(2.0), N(3.0), N(0.5) }), EOscuArgPolicy::Strict, &Error);
		TestTrue(FString::Printf(TEXT("Component dispatch succeeded (%s)"), *Error), Error.IsEmpty());
		TestEqual(TEXT("Both components called"), Calls, 2);
		TestEqual(TEXT("First component called once"), Host->First->NudgeCount, 1);
		TestEqual(TEXT("Second component called once"), Host->Second->NudgeCount, 1);
		TestEqual(TEXT("Vector assembled for a component"), Host->First->LastOffset, FVector(1.0, 2.0, 3.0));
		TestEqual(TEXT("Trailing float for a component"), Host->Second->LastDuration, 0.5f);

		// The actor declares Ping too, and is asked first: an override, not a broadcast.
		Error.Reset();
		Calls = Router->DispatchMessage(Make(TEXT("/host/Ping"), {}), EOscuArgPolicy::Strict, &Error);
		TestEqual(TEXT("Only the actor answered"), Calls, 1);
		TestEqual(TEXT("Actor's Ping ran"), Host->PingCount, 1);
		TestEqual(TEXT("First component's Ping did not"), Host->First->PingCount, 0);
		TestEqual(TEXT("Second component's Ping did not"), Host->Second->PingCount, 0);

		// Argument counting applies to component targets as it does to actors.
		Error.Reset();
		Calls = Router->DispatchMessage(
			Make(TEXT("/host/Nudge"), { N(1.0), N(2.0), N(3.0) }), EOscuArgPolicy::Strict, &Error);
		TestEqual(TEXT("Too few arguments refused for a component"), Calls, 0);
		TestFalse(TEXT("...and reported"), Error.IsEmpty());

		// Listed under the owner's tag, which is what MIDI Auto-Map and /_describe read.
		const TArray<FOscuExposedTagInfo> Tags = Router->Introspect(EOscuIntrospectFilter::All, FName(TEXT("host")));
		if (TestEqual(TEXT("One tag described"), Tags.Num(), 1))
		{
			const FOscuExposedFunctionInfo* Nudge = FindFunction(Tags[0], TEXT("Nudge"));
			if (TestNotNull(TEXT("Component function introspected"), Nudge))
			{
				TestEqual(TEXT("...at the owner's address"), Nudge->Address, FString(TEXT("/host/Nudge")));
				TestEqual(TEXT("...with its signature"), Nudge->TotalArgCount, 4);
			}
			TestNull(TEXT("Inherited engine component functions are not introspected"), FindFunction(Tags[0], TEXT("Activate")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
