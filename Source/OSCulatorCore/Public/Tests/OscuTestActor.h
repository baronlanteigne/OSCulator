// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OscuTestActor.generated.h"

/**
 * Fixtures for the OSCulator automation tests.
 *
 * These are declared unconditionally rather than behind WITH_DEV_AUTOMATION_TESTS
 * because UHT has to see a UCLASS in every configuration. The cost is a few
 * hundred bytes of class registration; the benefit is that the reflection tests
 * run against a real UClass built by the real header tool, which is the only way
 * to be sure the property flags are what the marshaller assumes they are.
 *
 * Every function records what it was handed, so dispatch can be checked on values
 * rather than on the mere fact that something got called.
 */

UENUM()
enum class EOscuTestMode : uint8
{
	Idle,
	Burst,
	Sweep
};

UCLASS(NotBlueprintable, NotPlaceable, Hidden)
class AOscuTestActor : public AActor
{
	GENERATED_BODY()

public:
	/** vec3(3) + name(1) + float(1) == 5 arguments. */
	UFUNCTION()
	void Fire(FVector Dir, FName Mode, float Power);

	/** A trigger. Zero arguments, and therefore never coalesced later on. */
	UFUNCTION()
	void Stop();

	/** Blueprint "Float" pins are doubles in UE5; this is the common case. */
	UFUNCTION()
	void SetIntensity(double Intensity);

	/** Consumed Pitch, Yaw, Roll -- struct member order, not X/Y/Z. */
	UFUNCTION()
	void Aim(FRotator Rotation);

	/** loc(3) + rot(3) + scale(3) == 9. */
	UFUNCTION()
	void Place(FTransform Where);

	UFUNCTION()
	void Configure(bool bEnabled, const FString& Label, int32 Count);

	UFUNCTION()
	void SetMode(EOscuTestMode Mode);

	UFUNCTION()
	void Tint(FLinearColor Colour);

	/** Trailing array: 1 fixed argument, then everything else. */
	UFUNCTION()
	void Chase(float Speed, const TArray<float>& Points);

	/** A plain non-const reference is a Blueprint OUTPUT pin. It occupies a frame
	 *  slot but consumes no incoming argument, so this expects 1 argument, not 2. */
	UFUNCTION()
	void Query(float In, float& OutResult);

	/** UPARAM(ref) is an INPUT passed by reference, so this one does consume an
	 *  argument. The distinction is the whole reason IsPureOutputParam exists. */
	UFUNCTION()
	void Accumulate(UPARAM(ref) float& Running);

	// ---- Must be rejected at registration ----

	/** Object reference: not marshallable from a flat message. */
	UFUNCTION()
	void Attach(AActor* Other);

	/** An array that is not the final parameter makes the split ambiguous. */
	UFUNCTION()
	void BadArray(const TArray<float>& Values, float Trailing);

	/**
	 * Stands in for the Blueprint compiler's ExecuteUbergraph_<Name>, which has this
	 * exact shape: one int32 bytecode offset into the whole event graph. It is
	 * perfectly marshallable and must still never be exposed.
	 */
	UFUNCTION()
	void ExecuteUbergraph_OscuTestActor(int32 EntryPoint);

	// ---- What the last call was handed ----

	UPROPERTY() int32 CallCount = 0;
	UPROPERTY() FName LastCalled;

	UPROPERTY() FVector LastDir = FVector::ZeroVector;
	UPROPERTY() FName LastMode;
	UPROPERTY() float LastPower = 0.0f;

	UPROPERTY() double LastIntensity = 0.0;
	UPROPERTY() FRotator LastRotation = FRotator::ZeroRotator;
	UPROPERTY() FTransform LastTransform;
	UPROPERTY() FLinearColor LastColour = FLinearColor::Black;

	UPROPERTY() bool bLastEnabled = false;
	UPROPERTY() FString LastLabel;
	UPROPERTY() int32 LastCount = 0;
	UPROPERTY() EOscuTestMode LastEnumMode = EOscuTestMode::Idle;

	UPROPERTY() float LastSpeed = 0.0f;
	UPROPERTY() TArray<float> LastPoints;

	UPROPERTY() float LastAccumulated = 0.0f;
};

/** Carries its tag in the class defaults, the way a Blueprint with tags set in
 *  Class Defaults does, so spawning it at runtime exercises the spawn handler. */
UCLASS(NotBlueprintable, NotPlaceable, Hidden)
class AOscuPreTaggedTestActor : public AActor
{
	GENERATED_BODY()

public:
	AOscuPreTaggedTestActor();

	UFUNCTION()
	void Pulse(float Amount);

	UPROPERTY() int32 CallCount = 0;
	UPROPERTY() float LastAmount = 0.0f;
};
