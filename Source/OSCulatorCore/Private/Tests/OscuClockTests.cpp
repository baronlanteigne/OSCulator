// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuClockSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuPulseComponent.h"
#include "Tests/OscuTestActor.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

namespace OscuClockTest
{
	/**
	 * The engine's one clock, on a time source the test moves by hand, at 120 BPM in 4/4
	 * -- two beats a second, so every step below is exact. Put back to real time and the
	 * settings when the test ends, whatever happened in between.
	 */
	struct FScopedClock
	{
		UOscuClockSubsystem* Clock = nullptr;
		UOscuClockTestListener* Listener = nullptr;
		double FakeNow = 1000.0;

		FScopedClock()
		{
			Clock = UOscuClockSubsystem::Get();
			if (Clock == nullptr)
			{
				return;
			}

			Clock->ResetForTest();
			Clock->SetTimeSourceForTest([this]() { return FakeNow; });
			Clock->SetBPM(120.0f);
			Clock->SetBeatsPerBar(4);

			Listener = NewObject<UOscuClockTestListener>();
			Clock->OnBeat.AddDynamic(Listener, &UOscuClockTestListener::HandleBeat);
			Clock->OnBar.AddDynamic(Listener, &UOscuClockTestListener::HandleBar);
			Clock->OnStart.AddDynamic(Listener, &UOscuClockTestListener::HandleStart);
			Clock->OnStop.AddDynamic(Listener, &UOscuClockTestListener::HandleStop);
		}

		~FScopedClock()
		{
			if (Clock != nullptr)
			{
				Clock->OnBeat.RemoveAll(Listener);
				Clock->OnBar.RemoveAll(Listener);
				Clock->OnStart.RemoveAll(Listener);
				Clock->OnStop.RemoveAll(Listener);
				Clock->ResetForTest();
			}
		}

		/** Moves time on and lets the clock fire what is due, as a frame would. */
		void Step(double Seconds)
		{
			FakeNow += Seconds;
			Clock->Advance();
		}
	};
}

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuClockBeatsTest,
	"OSCulator.Clock.Beats",
	OscuTest::Flags)

bool FOscuClockBeatsTest::RunTest(const FString& Parameters)
{
	OscuClockTest::FScopedClock Scope;
	if (!TestNotNull(TEXT("The clock subsystem exists"), Scope.Clock))
	{
		return false;
	}
	UOscuClockSubsystem& Clock = *Scope.Clock;
	const UOscuClockTestListener& Heard = *Scope.Listener;

	TestFalse(TEXT("Stopped to begin with"), Clock.IsRunning());

	// Start is the downbeat: bar 0 and beat 0 at once, not a frame later.
	Clock.Start();
	TestTrue(TEXT("Start runs it"), Clock.IsRunning());
	TestEqual(TEXT("Start is heard"), Heard.StartCount, 1);
	TestEqual(TEXT("Start fires bar 0"), Heard.LastBar, 0);
	TestEqual(TEXT("Start fires beat 0"), Heard.LastBeat, 0);
	TestEqual(TEXT("Beat 0 is the downbeat"), Heard.LastBeatInBar, 0);
	TestEqual(TEXT("Position starts at 0"), Clock.GetPosition(), 0.0);

	// Half a beat: no event, half a phase.
	Scope.Step(0.25);
	TestEqual(TEXT("Half a beat fires nothing"), Heard.BeatCount, 1);
	TestEqual(TEXT("Half a beat is phase 0.5"), Clock.GetBeatPhase(), 0.5f);

	Scope.Step(0.25);
	TestEqual(TEXT("A whole beat fires beat 1"), Heard.LastBeat, 1);
	TestEqual(TEXT("Beat 1 is the second beat of the bar"), Heard.LastBeatInBar, 1);
	TestEqual(TEXT("No new bar inside the bar"), Heard.BarCount, 1);

	// To beat 4, the first beat of bar 1, one frame per beat.
	Scope.Step(0.5);
	Scope.Step(0.5);
	Scope.Step(0.5);
	TestEqual(TEXT("Beat 4 is in bar 1"), Heard.LastBeatBar, 1);
	TestEqual(TEXT("Beat 4 is a downbeat"), Heard.LastBeatInBar, 0);
	TestEqual(TEXT("The bar line fired"), Heard.LastBar, 1);
	TestEqual(TEXT("Every beat so far fired once"), Heard.BeatCount, 5);

	// A five-second hitch skips ten beats: one OnBeat for the latest, one OnBar.
	const int32 BeatsBefore = Heard.BeatCount;
	const int32 BarsBefore = Heard.BarCount;
	Scope.Step(5.0);
	TestEqual(TEXT("A hitch fires one beat, not ten"), Heard.BeatCount, BeatsBefore + 1);
	TestEqual(TEXT("...and it is the latest beat"), Heard.LastBeat, 14);
	TestEqual(TEXT("A hitch across bar lines fires one bar"), Heard.BarCount, BarsBefore + 1);
	TestEqual(TEXT("...the bar it landed in"), Heard.LastBar, 3);
	TestEqual(TEXT("Bar from the getter agrees"), Clock.GetBar(), 3);
	TestEqual(TEXT("Beat in bar from the getter agrees"), Clock.GetBeatInBar(), 2);

	// Start again always means beat 0.
	Clock.Start();
	TestEqual(TEXT("Start again is back at 0"), Clock.GetPosition(), 0.0);
	TestEqual(TEXT("...and fires beat 0 again"), Heard.LastBeat, 0);

	return true;
}

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuClockTempoTest,
	"OSCulator.Clock.Tempo",
	OscuTest::Flags)

bool FOscuClockTempoTest::RunTest(const FString& Parameters)
{
	OscuClockTest::FScopedClock Scope;
	if (!TestNotNull(TEXT("The clock subsystem exists"), Scope.Clock))
	{
		return false;
	}
	UOscuClockSubsystem& Clock = *Scope.Clock;

	Clock.Start();
	Scope.Step(1.0);
	TestEqual(TEXT("One second at 120 is two beats"), Clock.GetPosition(), 2.0);

	// A tempo change keeps what has been counted and changes only the rate from now.
	Clock.SetBPM(60.0f);
	TestEqual(TEXT("Changing tempo keeps the position"), Clock.GetPosition(), 2.0);
	Scope.Step(1.0);
	TestEqual(TEXT("One second at 60 is one more beat"), Clock.GetPosition(), 3.0);

	Clock.SetBPM(5.0f);
	TestEqual(TEXT("Tempo is clamped low"), Clock.GetBPM(), 20.0f);
	Clock.SetBPM(5000.0f);
	TestEqual(TEXT("Tempo is clamped high"), Clock.GetBPM(), 999.0f);

	Clock.SetBeatsPerBar(0);
	TestEqual(TEXT("A bar has at least one beat"), Clock.GetBeatsPerBar(), 1);

	return true;
}

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuClockStopContinueTest,
	"OSCulator.Clock.StopContinue",
	OscuTest::Flags)

bool FOscuClockStopContinueTest::RunTest(const FString& Parameters)
{
	OscuClockTest::FScopedClock Scope;
	if (!TestNotNull(TEXT("The clock subsystem exists"), Scope.Clock))
	{
		return false;
	}
	UOscuClockSubsystem& Clock = *Scope.Clock;
	const UOscuClockTestListener& Heard = *Scope.Listener;

	Clock.Start();
	Scope.Step(1.25);
	TestEqual(TEXT("Running to 2.5"), Clock.GetPosition(), 2.5);

	Clock.Stop();
	TestFalse(TEXT("Stop stops it"), Clock.IsRunning());
	TestEqual(TEXT("Stop is heard"), Heard.StopCount, 1);

	Scope.Step(10.0);
	TestEqual(TEXT("Stopped, time passing moves nothing"), Clock.GetPosition(), 2.5);

	Clock.Stop();
	TestEqual(TEXT("Stopping twice is heard once"), Heard.StopCount, 1);

	// Continue resumes mid-beat: beat 2 already fired, so nothing until beat 3.
	const int32 BeatsBefore = Heard.BeatCount;
	Clock.Continue();
	TestTrue(TEXT("Continue runs it"), Clock.IsRunning());
	TestEqual(TEXT("Continue mid-beat fires nothing yet"), Heard.BeatCount, BeatsBefore);
	Scope.Step(0.25);
	TestEqual(TEXT("...then beat 3 on time"), Heard.LastBeat, 3);

	// Song Position while stopped parks the clock there; Continue starts on that beat.
	Clock.Stop();
	Clock.SetPosition(8.0);
	TestEqual(TEXT("Set Position while stopped moves it"), Clock.GetPosition(), 8.0);
	TestFalse(TEXT("...without starting it"), Clock.IsRunning());
	Clock.Continue();
	TestEqual(TEXT("Continuing on an exact beat fires that beat at once"), Heard.LastBeat, 8);
	TestEqual(TEXT("...and its bar line"), Heard.LastBar, 2);

	Clock.SetPosition(-4.0);
	TestEqual(TEXT("Position never goes below 0"), Clock.GetPosition(), 0.0);

	return true;
}

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuClockDivisionsTest,
	"OSCulator.Clock.Divisions",
	OscuTest::Flags)

bool FOscuClockDivisionsTest::RunTest(const FString& Parameters)
{
	using Q = EQuartzCommandQuantization;
	constexpr float Tolerance = 1e-4f;

	OscuClockTest::FScopedClock Scope;
	if (!TestNotNull(TEXT("The clock subsystem exists"), Scope.Clock))
	{
		return false;
	}
	UOscuClockSubsystem& Clock = *Scope.Clock;

	// ---- Lengths, in beats: Beat is a quarter, Bar is Beats Per Bar ----

	TestEqual(TEXT("1/8 is half a beat"), Clock.GetBeatsPerPulse(Q::EighthNote), 0.5f);
	TestEqual(TEXT("Beat is a quarter note"), Clock.GetBeatsPerPulse(Q::Beat), Clock.GetBeatsPerPulse(Q::QuarterNote));
	TestEqual(TEXT("Dotted 1/8 is three sixteenths"), Clock.GetBeatsPerPulse(Q::DottedEighthNote), 0.75f);
	TestEqual(TEXT("1/8 triplet is a third of a beat"), Clock.GetBeatsPerPulse(Q::EighthNoteTriplet), 1.0f / 3.0f, Tolerance);
	TestEqual(TEXT("Tick is a 1/32, as in Quartz"), Clock.GetBeatsPerPulse(Q::Tick), 0.125f);
	TestEqual(TEXT("Bar is Beats Per Bar"), Clock.GetBeatsPerPulse(Q::Bar), 4.0f);
	TestEqual(TEXT("None has no length"), Clock.GetBeatsPerPulse(Q::None), 0.0f);
	TestEqual(TEXT("...and no seconds"), Clock.GetSecondsPerPulse(Q::None), 0.0f);
	TestEqual(TEXT("Every division lines up again after 12 beats in 4/4"), OscuClock::AlignmentPeriodBeats(4), 12);
	TestEqual(TEXT("...and after 60 in 5/4"), OscuClock::AlignmentPeriodBeats(5), 60);

	// ---- Seconds per pulse at two tempos ----

	TestEqual(TEXT("120 BPM: a 1/8 is 0.25 s"), Clock.GetSecondsPerPulse(Q::EighthNote), 0.25f, Tolerance);
	TestEqual(TEXT("120 BPM: a dotted 1/8 is 0.375 s"), Clock.GetSecondsPerPulse(Q::DottedEighthNote), 0.375f, Tolerance);
	TestEqual(TEXT("120 BPM: a 1/8 triplet is 1/6 s"), Clock.GetSecondsPerPulse(Q::EighthNoteTriplet), 1.0f / 6.0f, Tolerance);
	TestEqual(TEXT("120 BPM: a bar is 2 s"), Clock.GetSecondsPerPulse(Q::Bar), 2.0f, Tolerance);

	Clock.SetBPM(90.0f);
	TestEqual(TEXT("90 BPM: a beat is 2/3 s"), Clock.GetSecondsPerPulse(Q::Beat), 2.0f / 3.0f, Tolerance);
	TestEqual(TEXT("90 BPM: a dotted 1/4 is exactly 1 s"), Clock.GetSecondsPerPulse(Q::DottedQuarterNote), 1.0f, Tolerance);
	TestEqual(TEXT("90 BPM: a 1/4 triplet is 4/9 s"), Clock.GetSecondsPerPulse(Q::QuarterNoteTriplet), 4.0f / 9.0f, Tolerance);
	Clock.SetBPM(120.0f);

	// ---- Index and phase, from the position ----

	Clock.Start();
	TestEqual(TEXT("At Start every division is on pulse 0"), Clock.GetPulseIndex(Q::DottedEighthNote), 0);
	TestEqual(TEXT("...at phase 0"), Clock.GetPulsePhase(Q::EighthNoteTriplet), 0.0f, Tolerance);

	// 0.6 s at 120 BPM is 1.2 beats.
	Scope.Step(0.6);
	TestEqual(TEXT("1.2 beats is 1/8 number 2"), Clock.GetPulseIndex(Q::EighthNote), 2);
	TestEqual(TEXT("...0.4 through it"), Clock.GetPulsePhase(Q::EighthNote), 0.4f, Tolerance);
	TestEqual(TEXT("1.2 beats is dotted 1/8 number 1"), Clock.GetPulseIndex(Q::DottedEighthNote), 1);
	TestEqual(TEXT("...0.6 through it"), Clock.GetPulsePhase(Q::DottedEighthNote), 0.6f, Tolerance);
	TestEqual(TEXT("1.2 beats is 1/8 triplet number 3"), Clock.GetPulseIndex(Q::EighthNoteTriplet), 3);
	TestEqual(TEXT("...0.6 through it"), Clock.GetPulsePhase(Q::EighthNoteTriplet), 0.6f, Tolerance);
	TestEqual(TEXT("1.2 beats is still bar 0"), Clock.GetPulseIndex(Q::Bar), 0);
	TestEqual(TEXT("...0.3 through it"), Clock.GetPulsePhase(Q::Bar), 0.3f, Tolerance);

	// A tempo change mid-pulse keeps the phase; only the rate changes.
	Clock.SetBPM(60.0f);
	TestEqual(TEXT("A tempo change mid-pulse keeps the index"), Clock.GetPulseIndex(Q::EighthNote), 2);
	TestEqual(TEXT("...and the phase"), Clock.GetPulsePhase(Q::EighthNote), 0.4f, Tolerance);
	TestEqual(TEXT("...and a 1/8 is now 0.5 s"), Clock.GetSecondsPerPulse(Q::EighthNote), 0.5f, Tolerance);
	Scope.Step(0.3);
	TestEqual(TEXT("0.3 s at 60 BPM finishes that 1/8: number 3"), Clock.GetPulseIndex(Q::EighthNote), 3);
	TestEqual(TEXT("...at phase 0"), Clock.GetPulsePhase(Q::EighthNote), 0.0f, Tolerance);

	// Exactly on a triplet boundary floating point lands a hair under it; it still counts.
	Clock.Start();
	Clock.SetBPM(120.0f);
	Scope.Step(0.5);
	TestEqual(TEXT("Beat 1 exactly is 1/8 triplet 3, not 2"), Clock.GetPulseIndex(Q::EighthNoteTriplet), 3);
	TestEqual(TEXT("...at phase 0, not nearly 1"), Clock.GetPulsePhase(Q::EighthNoteTriplet), 0.0f, Tolerance);

	return true;
}

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuClockPulseComponentTest,
	"OSCulator.Clock.PulseComponent",
	OscuTest::Flags)

bool FOscuClockPulseComponentTest::RunTest(const FString& Parameters)
{
	OscuClockTest::FScopedClock Scope;
	if (!TestNotNull(TEXT("The clock subsystem exists"), Scope.Clock))
	{
		return false;
	}
	UOscuClockSubsystem& Clock = *Scope.Clock;
	const UOscuClockTestListener& Heard = *Scope.Listener;

	// No world: the test calls what BeginPlay's binding and the tick would.
	UOscuPulseComponent* Pulse = NewObject<UOscuPulseComponent>();
	Pulse->Division = EQuartzCommandQuantization::EighthNote;
	Pulse->OnPulse.AddDynamic(Scope.Listener, &UOscuClockTestListener::HandlePulse);

	Pulse->Evaluate();
	TestEqual(TEXT("Stopped, nothing fires"), Heard.PulseCount, 0);

	Clock.Start();
	Pulse->HandleClockStart();
	TestEqual(TEXT("Start fires pulse 0 at once"), Heard.PulseCount, 1);
	TestEqual(TEXT("...and it is pulse 0"), Heard.LastPulse, 0);
	Pulse->Evaluate();
	TestEqual(TEXT("A second look in the same frame fires nothing more"), Heard.PulseCount, 1);

	// 1/8 at 120 BPM is 0.25 s.
	Scope.Step(0.1);
	Pulse->Evaluate();
	TestEqual(TEXT("Partway through a pulse, nothing"), Heard.PulseCount, 1);
	Scope.Step(0.15);
	Pulse->Evaluate();
	TestEqual(TEXT("The next 1/8 fires"), Heard.LastPulse, 1);
	Pulse->Evaluate();
	TestEqual(TEXT("...once"), Heard.PulseCount, 2);

	// A two-second hitch skips eight 1/8s: one fire, for the latest.
	Scope.Step(2.0);
	Pulse->Evaluate();
	TestEqual(TEXT("A hitch fires once, not eight times"), Heard.PulseCount, 3);
	TestEqual(TEXT("...for the latest pulse"), Heard.LastPulse, 9);

	// Half a pulse of Offset puts it between the 1/8s: counting starts afresh.
	Pulse->Offset = 0.5f;
	Pulse->Evaluate();
	TestEqual(TEXT("Changing Offset on a pulse line does not fire"), Heard.PulseCount, 3);
	Scope.Step(0.125);
	Pulse->Evaluate();
	TestEqual(TEXT("Half a 1/8 later the offset pulse fires"), Heard.PulseCount, 4);
	TestEqual(TEXT("...numbered like the pulse it follows"), Heard.LastPulse, 9);
	TestEqual(TEXT("...and the phase counts from there"), Pulse->GetPhase(), 0.0f, 1e-4f);
	Pulse->Offset = 0.0f;

	// Stopped, nothing; Continue mid-pulse waits for the next one.
	Clock.Stop();
	Scope.Step(1.0);
	Pulse->Evaluate();
	TestEqual(TEXT("Nothing fires while stopped"), Heard.PulseCount, 4);
	Clock.Continue();
	Pulse->HandleClockStart();
	TestEqual(TEXT("Continue mid-pulse fires nothing yet"), Heard.PulseCount, 4);
	Scope.Step(0.125);
	Pulse->Evaluate();
	TestEqual(TEXT("...then the next 1/8 on time"), Heard.LastPulse, 10);

	// Disabled, nothing.
	Pulse->bEnabled = false;
	Scope.Step(0.25);
	Pulse->Evaluate();
	TestEqual(TEXT("Disabled, nothing fires"), Heard.PulseCount, 5);
	Pulse->bEnabled = true;

	// Start again goes back to pulse 0, and fires it.
	Clock.Start();
	Pulse->HandleClockStart();
	TestEqual(TEXT("Start again fires pulse 0"), Heard.LastPulse, 0);
	TestEqual(TEXT("...once"), Heard.PulseCount, 6);

	// Set Position back without a Start: counted from there, not stuck waiting.
	Scope.Step(1.0);
	Pulse->Evaluate();
	TestEqual(TEXT("A second on, pulse 4"), Heard.LastPulse, 4);
	Clock.SetPosition(0.5);
	Pulse->Evaluate();
	TestEqual(TEXT("Moving back onto pulse 1 fires it"), Heard.LastPulse, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
