// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OscuValue.h"
#include "OscuOSCLibrary.generated.h"

/**
 * The Blueprint face of OSC output.
 *
 * The conversion functions carry BlueprintAutocast so that a float or an FVector
 * can be wired straight into a Make Array feeding Send OSC, with no explicit
 * conversion node in between.
 *
 * Note the connection order that makes that work: attach Make Array's OUTPUT to
 * the Send node's Args pin FIRST. Make Array is a wildcard node whose element type
 * is fixed by whatever connects first, so wiring a float in before the output is
 * connected produces an array of floats, and no autocast is ever consulted.
 */
UCLASS()
class OSCULATOROSC_API UOscuOSCLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Sends one message.
	 *
	 * Leave Target empty to reach every enabled target; name one to reach only it.
	 * Returns how many targets accepted the message.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|OSC",
		meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "Args", DisplayName = "Send OSC"))
	static int32 SendOSC(const UObject* WorldContextObject,
		UPARAM(meta = (GetOptions = "GetOSCTargetOptions")) FName Target,
		const FString& Address, const TArray<FOscuValue>& Args);

	/**
	 * Fills the Target dropdown from Project Settings, read live so adding a target
	 * updates the pin without a recompile. "(all)" means every enabled target.
	 */
	UFUNCTION()
	static TArray<FString> GetOSCTargetOptions();

	/** The dropdown entry meaning "every enabled target". */
	static const FName AllTargets;

	/**
	 * Sends a message whose arguments are all floats.
	 *
	 * The common case by a wide margin, and it needs no Make Array wildcard dance.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|OSC",
		meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "Values", DisplayName = "Send OSC (Floats)"))
	static int32 SendOSCFloats(const UObject* WorldContextObject,
		UPARAM(meta = (GetOptions = "GetOSCTargetOptions")) FName Target,
		const FString& Address, const TArray<float>& Values);

	/** True when at least one target is open. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|OSC", meta = (WorldContext = "WorldContextObject"))
	static bool IsOSCOutputReady(const UObject* WorldContextObject);

	// ---- Conversions ----
	//
	// One argument in, one FOscuValue out, marked BlueprintAutocast so the compiler
	// inserts them silently when a pin type does not match.

	UFUNCTION(BlueprintPure, Category = "OSCulator|OSC",
		meta = (BlueprintAutocast, CompactNodeTitle = "->", DisplayName = "To OSC Value (Float)"))
	static FOscuValue Conv_DoubleToOscuValue(double Value);

	UFUNCTION(BlueprintPure, Category = "OSCulator|OSC",
		meta = (BlueprintAutocast, CompactNodeTitle = "->", DisplayName = "To OSC Value (Integer)"))
	static FOscuValue Conv_IntToOscuValue(int32 Value);

	UFUNCTION(BlueprintPure, Category = "OSCulator|OSC",
		meta = (BlueprintAutocast, CompactNodeTitle = "->", DisplayName = "To OSC Value (Boolean)"))
	static FOscuValue Conv_BoolToOscuValue(bool Value);

	UFUNCTION(BlueprintPure, Category = "OSCulator|OSC",
		meta = (BlueprintAutocast, CompactNodeTitle = "->", DisplayName = "To OSC Value (String)"))
	static FOscuValue Conv_StringToOscuValue(const FString& Value);

	UFUNCTION(BlueprintPure, Category = "OSCulator|OSC",
		meta = (BlueprintAutocast, CompactNodeTitle = "->", DisplayName = "To OSC Value (Name)"))
	static FOscuValue Conv_NameToOscuValue(FName Value);

	/** Flattens to three floats on the wire. The whole reason the Vector type exists. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|OSC",
		meta = (BlueprintAutocast, CompactNodeTitle = "->", DisplayName = "To OSC Value (Vector)"))
	static FOscuValue Conv_VectorToOscuValue(FVector Value);
};
