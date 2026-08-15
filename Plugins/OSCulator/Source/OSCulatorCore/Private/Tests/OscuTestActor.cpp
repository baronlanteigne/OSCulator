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

void AOscuTestActor::Stop()
{
	++CallCount;
	LastCalled = TEXT("Stop");
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
