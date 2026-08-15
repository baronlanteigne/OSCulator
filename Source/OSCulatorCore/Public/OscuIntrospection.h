// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OscuIntrospection.generated.h"

class AActor;

/** Which slice of the registry to describe. */
UENUM()
enum class EOscuIntrospectFilter : uint8
{
	/** Every exposed function, inherited engine functions included. */
	All,

	/** Blueprint-authored functions only. A fully-exposed actor buries its own
	 *  handful of events under 200-odd inherited ones; this is the useful view. */
	Custom,

	/** Tags and the actors under them. No functions. */
	ActorsOnly
};

USTRUCT()
struct OSCULATORCORE_API FOscuExposedParamInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FName Name;

	/** "float", "vec3", "rot(pitch,yaw,roll)", "array<float>". */
	UPROPERTY()
	FString TypeLabel;

	/** Arguments consumed. OscuVariadicArgCount (-1) for a trailing array. */
	UPROPERTY()
	int32 ArgCount = 0;

	/** A Blueprint output pin: filled by the call, not by the message. */
	UPROPERTY()
	bool bOutputOnly = false;
};

USTRUCT()
struct OSCULATORCORE_API FOscuExposedFunctionInfo
{
	GENERATED_BODY()

	UPROPERTY()
	FName FunctionName;

	/** The address a sender uses, e.g. "/laser/fire". */
	UPROPERTY()
	FString Address;

	UPROPERTY()
	TArray<FOscuExposedParamInfo> Params;

	/** Arguments this signature consumes. With bVariadic, the minimum. */
	UPROPERTY()
	int32 TotalArgCount = 0;

	/** The last parameter is an array, so any extra arguments are swallowed by it. */
	UPROPERTY()
	bool bVariadic = false;

	/** Authored in a Blueprint rather than inherited from a native class. */
	UPROPERTY()
	bool bBlueprintAuthored = false;

	/** Has a non-return out parameter, so its frame cannot be shared between actors. */
	UPROPERTY()
	bool bHasOutParams = false;

	/** "vec3, string, float" -- the parenthesised part of an argument-count error. */
	FString GetSignatureString() const;
};

USTRUCT()
struct OSCULATORCORE_API FOscuExposedTagInfo
{
	GENERATED_BODY()

	/** The tag with its prefix stripped: "laser", from an actor tagged "OSC_laser". */
	UPROPERTY()
	FName Tag;

	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> Actors;

	UPROPERTY()
	TArray<FOscuExposedFunctionInfo> Functions;
};
