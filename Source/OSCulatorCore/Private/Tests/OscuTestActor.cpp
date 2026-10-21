// Copyright Baron Lanteigne. All Rights Reserved.

#include "Tests/OscuTestActor.h"

void AOscuTestActor::Fire(FVector Dir, FName Mode, float Power)
{
	++CallCount;
	LastCalled = TEXT("Fire");
	LastDir = Dir;
	LastMode = Mode;
	LastPower = Power;
}

void AOscuTestActor::Sweep(FVector Origin, float Pitch, float Level)
{
	++CallCount;
	LastCalled = TEXT("Sweep");
	LastSweepOrigin = Origin;
	LastSweepPitch = Pitch;
	LastSweepLevel = Level;
}

void AOscuTestActor::Stop()
{
	++CallCount;
	LastCalled = TEXT("Stop");
}

void AOscuTestActor::RunHook()
{
	++CallCount;
	LastCalled = TEXT("RunHook");
	if (Hook)
	{
		Hook();
	}
}

void AOscuTestActor::SetIntensity(double Intensity)
{
	++CallCount;
	LastCalled = TEXT("SetIntensity");
	LastIntensity = Intensity;
}

void AOscuTestActor::Aim(FRotator Rotation)
{
	++CallCount;
	LastCalled = TEXT("Aim");
	LastRotation = Rotation;
}

void AOscuTestActor::Place(FTransform Where)
{
	++CallCount;
	LastCalled = TEXT("Place");
	LastTransform = Where;
}

void AOscuTestActor::Configure(bool bEnabled, const FString& Label, int32 Count)
{
	++CallCount;
	LastCalled = TEXT("Configure");
	bLastEnabled = bEnabled;
	LastLabel = Label;
	LastCount = Count;
}

void AOscuTestActor::SetMode(EOscuTestMode Mode)
{
	++CallCount;
	LastCalled = TEXT("SetMode");
	LastEnumMode = Mode;
}

void AOscuTestActor::Tint(FLinearColor Colour)
{
	++CallCount;
	LastCalled = TEXT("Tint");
	LastColour = Colour;
}

void AOscuTestActor::Chase(float Speed, const TArray<float>& Points)
{
	++CallCount;
	LastCalled = TEXT("Chase");
	LastSpeed = Speed;
	LastPoints = Points;
}

void AOscuTestActor::Query(float In, float& OutResult)
{
	++CallCount;
	LastCalled = TEXT("Query");
	LastPower = In;
	OutResult = In * 2.0f;
}

void AOscuTestActor::Accumulate(float& Running)
{
	++CallCount;
	LastCalled = TEXT("Accumulate");
	LastAccumulated = Running;
	Running += 1.0f;
}

void AOscuTestActor::Beam_Width(float Width)
{
	++CallCount;
	LastCalled = TEXT("Beam_Width");
	LastWidth = Width;
}

void AOscuTestActor::MatParam(FName Name, float Duration, const TArray<float>& Values)
{
	++CallCount;
	LastCalled = TEXT("MatParam");
	LastMatName = Name;
	LastMatDuration = Duration;
	LastMatValues = Values;
}

void AOscuTestActor::Attach(AActor* Other) {}
void AOscuTestActor::BadArray(const TArray<float>& Values, float Trailing) {}
void AOscuTestActor::ExecuteUbergraph_OscuTestActor(int32 EntryPoint) {}

AOscuPreTaggedTestActor::AOscuPreTaggedTestActor()
{
	Tags.Add(FName(TEXT("OSC_runtime")));
}

void AOscuPreTaggedTestActor::Pulse(float Amount)
{
	++CallCount;
	LastAmount = Amount;
}

void UOscuTestComponent::Nudge(FVector Offset, float Duration)
{
	++NudgeCount;
	LastOffset = Offset;
	LastDuration = Duration;
}

void UOscuTestComponent::Ping()
{
	++PingCount;
}

AOscuComponentHostTestActor::AOscuComponentHostTestActor()
{
	First = CreateDefaultSubobject<UOscuTestComponent>(TEXT("First"));
	Second = CreateDefaultSubobject<UOscuTestComponent>(TEXT("Second"));
}

void AOscuComponentHostTestActor::Ping()
{
	++PingCount;
}
