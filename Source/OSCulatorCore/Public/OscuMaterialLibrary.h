// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OscuMaterialLibrary.generated.h"

/**
 * Which material parameter setter a resolved value belongs to.
 *
 * The two useful branches are the two MID setters. Invalid exists so an empty
 * array has somewhere to go that is not a silent black.
 */
UENUM(BlueprintType)
enum class EOscuMaterialParamForm : uint8
{
	/** One number. Feed Set Scalar Parameter Value. */
	Scalar,

	/** A colour, alpha filled in when the sender omitted it. Feed Set Vector Parameter Value. */
	Color,

	/** Nothing usable arrived. Normally left unwired. */
	Invalid
};

/**
 * Helpers for driving material parameters from a variable-length message.
 *
 * Nothing here is OSC- or MIDI-specific, and nothing here touches a material: these
 * only interpret the float array that a variadic event hands you, so the same node
 * works whichever transport filled it. See Docs/HELPERS.md.
 */
UCLASS()
class OSCULATORCORE_API UOscuMaterialLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Splits a variable-length float array into "this is a scalar" or "this is a colour".
	 *
	 * A trailing array parameter swallows every argument the fixed parameters did not
	 * take, so one event can serve a scalar parameter and a colour parameter without
	 * the author writing two of them. What arrived is known only by its length:
	 *
	 *   0    Invalid, and a warning
	 *   1    Scalar, Values[0]
	 *   2    Scalar, Values[0], and a warning -- 2 is neither, so it is read as a
	 *        scalar with a stray value after it rather than dropped
	 *   3    Color, alpha 1.0
	 *   4    Color
	 *   5+   Color from the first four, and a warning
	 *
	 * Alpha defaults to opaque rather than to the zero the marshaller would otherwise
	 * leave, because a sender that omits it means "opaque", never "invisible".
	 *
	 * Both value outputs are written on every branch, so the pin you did not take
	 * never carries a stale value from an earlier call.
	 *
	 * Values is a double array because a Blueprint "Float" pin is a double in UE5. A
	 * single-precision array pin would refuse to connect to your event's array --
	 * Blueprint autocasts scalars between float and double, but not arrays.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Material",
		meta = (DisplayName = "Resolve Material Parameter Value", ExpandEnumAsExecs = "OutForm"))
	static void ResolveMaterialParameterValue(
		const TArray<double>& Values,
		EOscuMaterialParamForm& OutForm,
		double& OutScalar,
		FLinearColor& OutColor);
};
