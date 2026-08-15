// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OscuIntrospection.h"
#include "OscuValue.h"
#include "OscuMarshal.generated.h"

class FProperty;
class UFunction;

/** Sentinel ArgCount meaning "consumes every argument still on the message". */
inline constexpr int32 OscuVariadicArgCount = -1;

/** What to do when the message's argument count disagrees with the signature. */
UENUM()
enum class EOscuArgPolicy : uint8
{
	/** The count must match exactly. OSC input uses this. */
	Strict,

	/**
	 * Fill what was supplied and leave the rest zero-initialised.
	 *
	 * MIDI input always uses this, because MIDI supplies one value -- velocity --
	 * regardless of what the signature wants. That is the entire reason MIDI needs
	 * no marshalling code of its own.
	 */
	Lenient
};

/**
 * What one function parameter costs on the wire, and what to call it.
 *
 * This is the single source of truth for the §6 type table. Both halves of the
 * plugin read it: introspection uses ArgCount and TypeLabel to print a signature,
 * and the marshaller uses ArgCount to decide how many values to eat. Writing the
 * table twice is how the two drift apart.
 */
struct FOscuParamClass
{
	/** False means the containing function is excluded from the registry entirely. */
	bool bMarshallable = false;

	/** Arguments consumed. OscuVariadicArgCount for a trailing array. */
	int32 ArgCount = 0;

	/** How the parameter is described to a sender: "float", "vec3", "rot(pitch,yaw,roll)". */
	FString TypeLabel;

	/** Why it was rejected. Only meaningful when bMarshallable is false. */
	FString RejectReason;
};

namespace OscuMarshal
{
	/**
	 * Classifies one parameter against the supported type table.
	 *
	 * Never call this on a return parameter -- filter those out first.
	 */
	OSCULATORCORE_API FOscuParamClass ClassifyParam(const FProperty* Property);

	/**
	 * True for a parameter that is written back but never read: a Blueprint output
	 * pin. It occupies a slot in the frame but consumes no incoming argument.
	 *
	 * A UPARAM(ref) parameter is NOT pure output -- it carries a value in as well,
	 * so it does consume one.
	 */
	OSCULATORCORE_API bool IsPureOutputParam(const FProperty* Property);

	/**
	 * Checks the supplied argument count against the signature.
	 *
	 * On failure OutError carries the whole user-facing line, generated from the
	 * reflection data we already have:
	 *   "/laser/fire expects 5 args (vec3, name, float) -- got 4. Ignored."
	 */
	OSCULATORCORE_API bool CheckArgCount(
		const FString& Address,
		const FOscuExposedFunctionInfo& Info,
		int32 SuppliedCount,
		EOscuArgPolicy Policy,
		FString& OutError);

	/**
	 * Writes arguments into an already-initialised parameter frame.
	 *
	 * The frame must have been zeroed and had InitializeValue_InContainer called on
	 * every parameter, and must have DestroyValue_InContainer called on every
	 * parameter afterwards. See BuildAndCall, which owns that whole dance.
	 *
	 * Coercion is permissive throughout: a String asked for a number parses itself,
	 * a number asked for a string formats itself. Under Lenient, parameters with no
	 * argument left to fill them keep whatever the zeroed frame gave them.
	 */
	OSCULATORCORE_API void FillFrame(
		const UFunction* Function,
		const TArray<FOscuValue>& Args,
		EOscuArgPolicy Policy,
		uint8* Frame);
}
