// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMaterialLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuMarshal.h"
#include "OscuValue.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

namespace OscuMaterialTest
{
	/**
	 * TestEqual's generic template needs LexToString, which a UENUM class does not
	 * have. The rest of the suite casts to int32; comparing names instead costs the
	 * same and makes a failure say "Scalar, expected Color" rather than "0, expected 1".
	 */
	const TCHAR* FormName(EOscuMaterialParamForm Form)
	{
		switch (Form)
		{
		case EOscuMaterialParamForm::Scalar:  return TEXT("Scalar");
		case EOscuMaterialParamForm::Color:   return TEXT("Color");
		default:                              return TEXT("Invalid");
		}
	}

	/** One call, unpacked into something a test can assert on in one line. */
	struct FResolved
	{
		EOscuMaterialParamForm Form = EOscuMaterialParamForm::Invalid;
		double Scalar = 0.0;
		FLinearColor Color = FLinearColor::Black;

		const TCHAR* FormAsName() const { return FormName(Form); }
	};

	FResolved Resolve(std::initializer_list<double> Values)
	{
		FResolved Out;
		UOscuMaterialLibrary::ResolveMaterialParameterValue(
			TArray<double>(Values), Out.Form, Out.Scalar, Out.Color);
		return Out;
	}
}

//////////////////////////////////////////////////////////////////////////
// The core claim: length alone decides which material setter the value belongs to

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMaterialResolveFormTest,
	"OSCulator.Material.ResolveForm",
	OscuTest::Flags)

bool FOscuMaterialResolveFormTest::RunTest(const FString& Parameters)
{
	using namespace OscuMaterialTest;

	// One number is a scalar.
	{
		const FResolved R = Resolve({ 0.7 });
		TestEqual(TEXT("1 value is a scalar"), R.FormAsName(), TEXT("Scalar"));
		TestEqual(TEXT("Scalar value"), R.Scalar, 0.7);
	}

	// Three is a colour, and the alpha the sender left out is opaque -- not the zero
	// the marshaller would otherwise leave, which would be an invisible colour.
	{
		const FResolved R = Resolve({ 0.1, 1.0, 1.0 });
		TestEqual(TEXT("3 values is a colour"), R.FormAsName(), TEXT("Color"));
		TestEqual(TEXT("RGB carried through"), R.Color, FLinearColor(0.1f, 1.0f, 1.0f, 1.0f));
		TestEqual(TEXT("Omitted alpha is opaque"), R.Color.A, 1.0f);
	}

	// Four is a colour with the alpha the sender actually meant, including zero.
	{
		const FResolved R = Resolve({ 1.0, 0.0, 0.0, 0.25 });
		TestEqual(TEXT("4 values is a colour"), R.FormAsName(), TEXT("Color"));
		TestEqual(TEXT("Supplied alpha wins"), R.Color, FLinearColor(1.0f, 0.0f, 0.0f, 0.25f));
	}

	{
		const FResolved R = Resolve({ 1.0, 1.0, 1.0, 0.0 });
		TestEqual(TEXT("An explicit zero alpha is not overwritten"), R.Color.A, 0.0f);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Lengths that are neither: tolerated, but never silently

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMaterialResolveOddLengthsTest,
	"OSCulator.Material.ResolveOddLengths",
	OscuTest::Flags)

bool FOscuMaterialResolveOddLengthsTest::RunTest(const FString& Parameters)
{
	using namespace OscuMaterialTest;

	// Nothing usable. The only length that refuses to produce a value.
	{
		const FResolved R = Resolve({});
		TestEqual(TEXT("Empty is Invalid"), R.FormAsName(), TEXT("Invalid"));
	}

	// Two is neither, and is read as a scalar with a stray value after it. A sender
	// with one channel too many keeps working; the warning says what was assumed.
	{
		const FResolved R = Resolve({ 0.4, 99.0 });
		TestEqual(TEXT("2 values falls back to scalar"), R.FormAsName(), TEXT("Scalar"));
		TestEqual(TEXT("First value is the scalar"), R.Scalar, 0.4);
	}

	// Surplus past a colour is dropped, matching what CheckArgCount does with a
	// surplus everywhere else -- a CHOP emits every channel it has.
	{
		const FResolved R = Resolve({ 1.0, 0.5, 0.25, 0.125, 42.0, 43.0 });
		TestEqual(TEXT("6 values is still a colour"), R.FormAsName(), TEXT("Color"));
		TestEqual(TEXT("Built from the first 4"), R.Color, FLinearColor(1.0f, 0.5f, 0.25f, 0.125f));
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// The branch not taken must not carry a value from the previous call

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMaterialResolveNoStaleOutputTest,
	"OSCulator.Material.ResolveNoStaleOutput",
	OscuTest::Flags)

bool FOscuMaterialResolveNoStaleOutputTest::RunTest(const FString& Parameters)
{
	using namespace OscuMaterialTest;

	// Blueprint reuses the node's output storage between calls, so the pin on the
	// branch that did not fire has to be cleared rather than left alone.
	EOscuMaterialParamForm Form = EOscuMaterialParamForm::Invalid;
	double Scalar = 0.0;
	FLinearColor Color = FLinearColor::Black;

	UOscuMaterialLibrary::ResolveMaterialParameterValue(
		TArray<double>({ 1.0, 1.0, 1.0, 1.0 }), Form, Scalar, Color);
	TestEqual(TEXT("First call is a colour"), FormName(Form), TEXT("Color"));
	TestEqual(TEXT("White"), Color, FLinearColor::White);

	// Same outputs, now down the scalar branch. The colour must not still be white.
	UOscuMaterialLibrary::ResolveMaterialParameterValue(
		TArray<double>({ 0.5 }), Form, Scalar, Color);
	TestEqual(TEXT("Second call is a scalar"), FormName(Form), TEXT("Scalar"));
	TestEqual(TEXT("Scalar value"), Scalar, 0.5);
	TestEqual(TEXT("Colour output was reset, not left white"), Color, FLinearColor::Black);

	// And the reverse: a colour call must not leave the earlier scalar behind.
	UOscuMaterialLibrary::ResolveMaterialParameterValue(
		TArray<double>({ 0.0, 1.0, 0.0 }), Form, Scalar, Color);
	TestEqual(TEXT("Third call is a colour"), FormName(Form), TEXT("Color"));
	TestEqual(TEXT("Scalar output was reset"), Scalar, 0.0);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// End to end: a trailing array really does carry a variable-length payload

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMaterialVariadicTailTest,
	"OSCulator.Material.VariadicTail",
	OscuTest::Flags)

bool FOscuMaterialVariadicTailTest::RunTest(const FString& Parameters)
{
	using namespace OscuTest;
	using namespace OscuMaterialTest;

	FScopedTestWorld Scope;
	AOscuTestActor* Laser = Scope.SpawnTaggedAs<AOscuTestActor>({ TEXT("OSC_laser") });
	Scope.BeginPlay();

	UOscuRouterSubsystem* Router = Scope.Router();
	if (!TestNotNull(TEXT("Router subsystem exists"), Router))
	{
		return false;
	}

	// Chase(float Speed, TArray<float> Points) has the shape a material event wants:
	// fixed parameters first, then one trailing array that swallows the rest. Three
	// values after the fixed one, so the payload is an RGB with no alpha.
	FOscuMessage Message;
	Message.Address = TEXT("/laser/Chase");
	Message.Args = {
		FOscuValue::MakeFloat(0.5),
		FOscuValue::MakeFloat(1.0), FOscuValue::MakeFloat(0.0), FOscuValue::MakeFloat(0.0)
	};

	FString Error;
	const int32 Calls = Router->DispatchMessage(Message, EOscuArgPolicy::Strict, &Error);

	TestTrue(FString::Printf(TEXT("Dispatch succeeded (%s)"), *Error), Error.IsEmpty());
	TestEqual(TEXT("One actor called"), Calls, 1);
	TestEqual(TEXT("Fixed parameter took the first argument"), Laser->LastSpeed, 0.5f);
	TestEqual(TEXT("Array swallowed the remaining 3"), Laser->LastPoints.Num(), 3);

	// What the Blueprint would then do with that array.
	EOscuMaterialParamForm Form = EOscuMaterialParamForm::Invalid;
	double Scalar = 0.0;
	FLinearColor Color = FLinearColor::Black;
	UOscuMaterialLibrary::ResolveMaterialParameterValue(
		TArray<double>(Laser->LastPoints), Form, Scalar, Color);

	TestEqual(TEXT("Resolves to a colour"), FormName(Form), TEXT("Color"));
	TestEqual(TEXT("Red, opaque"), Color, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));

	// The same address with one value is a scalar instead, which is the whole point:
	// one event, one address, both kinds of material parameter.
	Message.Args = { FOscuValue::MakeFloat(0.5), FOscuValue::MakeFloat(0.8) };
	Router->DispatchMessage(Message, EOscuArgPolicy::Strict, &Error);

	TestEqual(TEXT("Array swallowed 1"), Laser->LastPoints.Num(), 1);
	UOscuMaterialLibrary::ResolveMaterialParameterValue(
		TArray<double>(Laser->LastPoints), Form, Scalar, Color);
	TestEqual(TEXT("Resolves to a scalar"), FormName(Form), TEXT("Scalar"));
	TestEqual(TEXT("Scalar value"), Scalar, 0.8, 1.e-6);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
