// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
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

	/**
	 * Two scalars sitting BEHIND a multi-slot parameter: vec3(3), then slots 3 and 4.
	 *
	 * The target for per-parameter MIDI assignment. Neither scalar is reachable by
	 * declaration order, which is the whole point -- a positional scheme can only ever
	 * fill Origin here, so this signature is what proves a named assignment works.
	 */
	UFUNCTION()
	void Sweep(FVector Origin, float Pitch, float Level);

	/** A trigger. Zero arguments, and therefore never coalesced later on. */
	UFUNCTION()
	void Stop();

	/** No arguments; runs Hook. Lets a test act from inside a dispatch (reload mid-drain). */
	UFUNCTION()
	void RunHook();

	/** Not reflected: plain C++ the test sets before sending the message. */
	TFunction<void()> Hook;

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

	/** Answers the nested /laser/Beam/Width as well as the flat /laser/Beam_Width. */
	UFUNCTION()
	void Beam_Width(float Width);

	/** Text first, so the address-leaf fallback reaches it as /laser/MatParam/<name>. */
	UFUNCTION()
	void MatParam(FName Name, float Duration, const TArray<float>& Values);

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

	UPROPERTY() float LastWidth = 0.0f;

	UPROPERTY() FName LastMatName;
	UPROPERTY() float LastMatDuration = 0.0f;
	UPROPERTY() TArray<float> LastMatValues;

	UPROPERTY() FVector LastSweepOrigin = FVector::ZeroVector;
	UPROPERTY() float LastSweepPitch = 0.0f;
	UPROPERTY() float LastSweepLevel = 0.0f;
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

/**
 * A component with functions of its own, reached through its owner's tag.
 *
 * Native, so the blanket bExposeAllFunctions does not reach it: on a component that
 * blanket covers Blueprint-authored functions only. Tests opt it in through
 * ExtraAllowedFunctions, which is also how they prove the default keeps it shut.
 */
UCLASS(NotBlueprintable, Hidden)
class UOscuTestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** vec3(3) + float(1): the shape of a tweened move. */
	UFUNCTION()
	void Nudge(FVector Offset, float Duration);

	/** Also declared on the host actor, which is asked first and must win. */
	UFUNCTION()
	void Ping();

	UPROPERTY() int32 NudgeCount = 0;
	UPROPERTY() int32 PingCount = 0;
	UPROPERTY() FVector LastOffset = FVector::ZeroVector;
	UPROPERTY() float LastDuration = 0.0f;
};

/** Hosts two test components, so one message is seen to reach both. */
UCLASS(NotBlueprintable, NotPlaceable, Hidden)
class AOscuComponentHostTestActor : public AActor
{
	GENERATED_BODY()

public:
	AOscuComponentHostTestActor();

	/** Shadows UOscuTestComponent::Ping. */
	UFUNCTION()
	void Ping();

	UPROPERTY() TObjectPtr<UOscuTestComponent> First;
	UPROPERTY() TObjectPtr<UOscuTestComponent> Second;

	UPROPERTY() int32 PingCount = 0;
};

/** Records what the clock broadcasts, so a test can check events rather than just state. */
UCLASS(NotBlueprintable, Hidden)
class UOscuClockTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleBeat(int32 Beat, int32 Bar, int32 BeatInBar);

	UFUNCTION()
	void HandleBar(int32 Bar);

	UFUNCTION()
	void HandleStart();

	UFUNCTION()
	void HandleStop();

	UFUNCTION()
	void HandlePulse(int32 Index);

	UPROPERTY() int32 PulseCount = 0;
	UPROPERTY() int32 LastPulse = -1;
	UPROPERTY() int32 BeatCount = 0;
	UPROPERTY() int32 LastBeat = -1;
	UPROPERTY() int32 LastBeatBar = -1;
	UPROPERTY() int32 LastBeatInBar = -1;
	UPROPERTY() int32 BarCount = 0;
	UPROPERTY() int32 LastBar = -1;
	UPROPERTY() int32 StartCount = 0;
	UPROPERTY() int32 StopCount = 0;
};
