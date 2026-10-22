// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuPulseComponent.h"

#include "OscuClockSubsystem.h"

UOscuPulseComponent::UOscuPulseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// The clock counts real time and does not stop for pause, so neither do its pulses.
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

void UOscuPulseComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UOscuClockSubsystem* Clock = UOscuClockSubsystem::Get())
	{
		Clock->OnStart.AddDynamic(this, &UOscuPulseComponent::HandleClockStart);
	}
}

void UOscuPulseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The clock outlives every world; it must not hold on to this.
	if (UOscuClockSubsystem* Clock = UOscuClockSubsystem::Get())
	{
		Clock->OnStart.RemoveDynamic(this, &UOscuPulseComponent::HandleClockStart);
	}

	Super::EndPlay(EndPlayReason);
}

void UOscuPulseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Evaluate();
}

bool UOscuPulseComponent::GetPulses(double& OutPulses) const
{
	const UOscuClockSubsystem* Clock = UOscuClockSubsystem::Get();
	if (Clock == nullptr)
	{
		return false;
	}

	const double PulseBeats = OscuClock::BeatsPerPulse(Division, Clock->GetBeatsPerBar());
	if (PulseBeats <= 0.0)
	{
		return false;
	}

	OutPulses = Clock->GetPosition() / PulseBeats - Offset;
	return true;
}

float UOscuPulseComponent::GetPhase() const
{
	double Pulses = 0.0;
	return GetPulses(Pulses) ? static_cast<float>(OscuClock::PulsePhase(Pulses)) : 0.0f;
}

void UOscuPulseComponent::HandleClockStart()
{
	bCounting = false;

	// Start is pulse 0: fire it now, with the clock's own beat 0, not a frame later.
	Evaluate();
}

void UOscuPulseComponent::Evaluate()
{
	const UOscuClockSubsystem* Clock = UOscuClockSubsystem::Get();
	double Pulses = 0.0;
	if (!bEnabled || Clock == nullptr || !Clock->IsRunning() || !GetPulses(Pulses))
	{
		// Picked up again from wherever the clock is when it next runs.
		bCounting = false;
		return;
	}

	// Counting starts from here: a pulse exactly now is due, one already under way is not.
	// A new Division or Offset is a new grid, so it counts afresh as well.
	if (!bCounting || CountedDivision != Division || CountedOffset != Offset)
	{
		LastFiredIndex = OscuClock::LastPulseBefore(Pulses);
		CountedDivision = Division;
		CountedOffset = Offset;
		bCounting = true;
	}

	const int64 Index = OscuClock::FloorPulses(Pulses);

	// The position went back (Set Position). Count from there, as if just started.
	if (Index < LastFiredIndex)
	{
		LastFiredIndex = OscuClock::LastPulseBefore(Pulses);
	}

	if (Index <= LastFiredIndex)
	{
		return;
	}

	// Only the latest pulse after a hitch. Set before broadcasting, so a handler that
	// moves the clock is not overwritten when this returns.
	LastFiredIndex = Index;
	OnPulse.Broadcast(static_cast<int32>(FMath::Clamp<int64>(Index, MIN_int32, MAX_int32)));
}
