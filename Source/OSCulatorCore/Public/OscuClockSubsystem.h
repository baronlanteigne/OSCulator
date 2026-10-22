// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "Subsystems/EngineSubsystem.h"
#include "OscuClockSubsystem.generated.h"

class UAudioComponent;
class UGameInstance;
class UQuartzClockHandle;
class UWorld;
struct FActorsInitializedParams;

/**
 * The division math, free of any clock state, so the subsystem, the pulse component and
 * the tests all count pulses the same way.
 */
namespace OscuClock
{
	/**
	 * How many beats one pulse of Division lasts. Beat is a quarter note, as Quartz counts
	 * it in x/4; Bar is BeatsPerBar beats; Tick is a 1/32, as Quartz has it. None and
	 * anything else that is not a length gives 0.
	 */
	OSCULATORCORE_API double BeatsPerPulse(EQuartzCommandQuantization Division, int32 BeatsPerBar);

	/**
	 * Whole pulses in a fractional pulse count. Floating point puts beat 1 in thirds at
	 * 2.9999999999999996; that is pulse 3, not pulse 2, so a hair under a whole number
	 * counts as the whole number.
	 */
	OSCULATORCORE_API int64 FloorPulses(double Pulses);

	/** 0-1 through the current pulse, under the same rounding as FloorPulses. */
	OSCULATORCORE_API double PulsePhase(double Pulses);

	/**
	 * The last pulse that has already fired at this count: a count on an exact pulse has
	 * not fired that pulse yet, a count past one has.
	 */
	OSCULATORCORE_API int64 LastPulseBefore(double Pulses);

	/**
	 * Beats between points where every division lines up again -- straight, dotted,
	 * triplet and the bar. 12 beats in 4/4. The Quartz clock is re-aligned on one of these
	 * after a Continue or Set Position, when it can't simply restart from 0.
	 */
	OSCULATORCORE_API int32 AlignmentPeriodBeats(int32 BeatsPerBar);
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOscuClockBeatEvent, int32, Beat, int32, Bar, int32, BeatInBar);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOscuClockBarEvent, int32, Bar);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOscuClockTempoEvent, float, BPM);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOscuClockTransportEvent);

/**
 * A local tempo clock: beats and bars at a BPM, counted on this machine.
 *
 * It is NOT slaved to MIDI clock and never reads it. It runs in parallel with whatever
 * else is keeping time -- Ableton, a lighting desk -- and takes only two things from
 * outside: the tempo, and transport (start, stop, continue, song position). Two clocks
 * at the same tempo started by the same Start drift apart slowly, and the next Start
 * or Continue lines them up again. Following clock instead would cost a 24-per-beat
 * message stream, the one that overflows input queues, and a jittery tempo estimate.
 *
 * Counted in real time, never game time: pause, global time dilation and slow motion
 * do not touch it, because the other clock in the room does not slow down either.
 * Position is computed from the time since the last tempo change rather than added up
 * frame by frame, so it does not drift with frame rate.
 *
 * An engine subsystem rather than a world one, for the same reason as MIDI: transport
 * can arrive while only the editor is up, and a clock that Ableton started before you
 * pressed Play should already be in phase when play begins.
 *
 * Beat events fire on the first frame after the beat, so up to a frame late. After a
 * hitch that skipped several beats, OnBeat fires once, for the latest beat -- a burst of
 * stale beats in one frame is worse than a missing one. OnBar fires if any bar line was
 * crossed.
 *
 * It is the project's one musical clock. Anything that repeats at a fraction of the
 * tempo reads it: Get Pulse Index / Phase / Seconds Per Pulse for a Division, or an
 * OSCu Pulse component for an event per pulse. Divisions are Quartz's own
 * EQuartzCommandQuantization, so Blueprint and Quartz share one vocabulary.
 *
 * For sample-accurate audio it owns one Quartz clock per game world (Settings > Clock >
 * Quartz Clock, "Main" by default), made when play starts and removed at world cleanup.
 * That clock follows the tempo, restarts from 0 on Start, and re-aligns after Continue or
 * Set Position. It keeps running while transport is stopped, so queued sounds never hang.
 * Get it with Get Quartz Clock, or let Play On Pulse do the quantizing.
 *
 * From Blueprint: Get Engine Subsystem (OSCu Clock Subsystem).
 */
UCLASS(BlueprintType, meta = (DisplayName = "OSCu Clock Subsystem"))
class OSCULATORCORE_API UOscuClockSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UOscuClockSubsystem* Get();

	// ---- Transport ----

	/** Runs from beat 0. MIDI Start does the same. */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Clock")
	void Start();

	/** Stops and holds the current position, so Continue resumes from it. */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Clock")
	void Stop();

	/** Runs from the current position -- where Stop left it, or where Set Position put it. */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Clock")
	void Continue();

	/**
	 * Moves to a position in beats. Running, it carries on from there; stopped, it waits
	 * there for Continue. MIDI Song Position does this.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Clock")
	void SetPosition(double Beats);

	// ---- Tempo ----

	/** Clamped to 20-999. Changing it mid-beat keeps the position; only the rate changes. */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Clock")
	void SetBPM(float NewBPM);

	UFUNCTION(BlueprintCallable, Category = "OSCulator|Clock")
	void SetBeatsPerBar(int32 NewBeatsPerBar);

	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	float GetBPM() const { return BPM; }

	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	int32 GetBeatsPerBar() const { return BeatsPerBar; }

	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	bool IsRunning() const { return bRunning; }

	// ---- Position ----

	/** Beats since beat 0, fractional. 2.5 is halfway through the third beat. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	double GetPosition() const;

	/** Whole beats since beat 0, counting from 0. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	int32 GetBeat() const;

	/** Whole bars since beat 0, counting from 0. A DAW's "bar 1" is 0 here. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	int32 GetBar() const;

	/** 0 to Beats Per Bar - 1. 0 is the downbeat. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	int32 GetBeatInBar() const;

	/** 0-1 through the current beat. Drive anything that should pulse with the tempo. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	float GetBeatPhase() const;

	/** 0-1 through the current bar. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	float GetBarPhase() const;

	// ---- Divisions ----

	/**
	 * Whole pulses of Division since beat 0, counting from 0. Counted from beat 0, not from
	 * each bar, so a dotted 1/8 runs straight across bar lines, as Quartz's Transport
	 * Relative does. 0 for None.
	 */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	int32 GetPulseIndex(EQuartzCommandQuantization Division) const;

	/** 0-1 through the current pulse of Division. Drive anything that should move with it. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	float GetPulsePhase(EQuartzCommandQuantization Division) const;

	/** How long one pulse of Division lasts at the current tempo. 0 for None. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	float GetSecondsPerPulse(EQuartzCommandQuantization Division) const;

	/** How many beats one pulse of Division lasts: 1/8 is 0.5, a bar is Beats Per Bar. */
	UFUNCTION(BlueprintPure, Category = "OSCulator|Clock")
	float GetBeatsPerPulse(EQuartzCommandQuantization Division) const;

	// ---- Quartz ----

	/**
	 * This world's Quartz clock, kept at this clock's tempo and bar lines. Made now if play
	 * has not made it yet. Null outside a game world, or with Quartz Clock set to None.
	 * Use it for Play Quantized and anything else that wants a clock handle.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Clock", meta = (WorldContext = "WorldContextObject"))
	UQuartzClockHandle* GetQuartzClock(const UObject* WorldContextObject);

	/**
	 * Plays the component's sound on the next pulse of Division, sample-accurate, on the
	 * Quartz clock in its world. Counted from beat 0 like Get Pulse Index, so it lands on
	 * the pulse a Pulse component with the same Division fires for. Without a Quartz clock
	 * it plays at once, with a warning.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Clock", meta = (AutoCreateRefTerm = "Delegate", AdvancedDisplay = "Delegate"))
	void PlayOnPulse(UAudioComponent* AudioComponent, EQuartzCommandQuantization Division, const FOnQuartzCommandEventBP& Delegate);

	// ---- Events ----

	/** Every beat, including beat 0 on Start. Beat counts from 0; BeatInBar 0 is the downbeat. */
	UPROPERTY(BlueprintAssignable, Category = "OSCulator|Clock")
	FOscuClockBeatEvent OnBeat;

	/** Every bar line, including bar 0 on Start. Fires before that downbeat's OnBeat. */
	UPROPERTY(BlueprintAssignable, Category = "OSCulator|Clock")
	FOscuClockBarEvent OnBar;

	/** Start and Continue both, after the position is set. */
	UPROPERTY(BlueprintAssignable, Category = "OSCulator|Clock")
	FOscuClockTransportEvent OnStart;

	UPROPERTY(BlueprintAssignable, Category = "OSCulator|Clock")
	FOscuClockTransportEvent OnStop;

	UPROPERTY(BlueprintAssignable, Category = "OSCulator|Clock")
	FOscuClockTempoEvent OnBPMChanged;

	// ---- For transports and tests ----

	/** Fires whatever beats are due now. Called every frame; public so a test can step it. */
	void Advance();

	/**
	 * Replaces the time source, in seconds. Null restores FPlatformTime::Seconds. For
	 * tests, which need time to move exactly when told to.
	 */
	void SetTimeSourceForTest(TFunction<double()> Source);

	/** Stopped at beat 0, real time, tempo and bar length back to the settings. For tests. */
	void ResetForTest();

	/** The last beat OnBeat fired for, or -1 before the first. For tests. */
	int32 GetLastFiredBeat() const { return LastFiredBeat; }

private:
	double Now() const;

	/** Puts the anchor at Beats as of now, so the position keeps counting from there. */
	void Reanchor(double Beats);

	bool Tick(float DeltaTime);

	void ApplySettings();
	void HandleGameInstanceStart(UGameInstance* GameInstance);
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void HandleWorldInitializedActors(const FActorsInitializedParams& Params);

	/** The Quartz clock in this world, made, started and set to this tempo first when bCreate. Null when Quartz Clock is None. */
	UQuartzClockHandle* FindOrMakeQuartzClock(UWorld& World, bool bCreate);

	/** Sets every game world's Quartz clock to this tempo. */
	void PushTempoToQuartz();

	/** Sets every game world's Quartz clock to Beats Per Bar beats a bar. */
	void PushTimeSignatureToQuartz();

	/** Puts every game world's Quartz clock back to bar 0, beat 0. */
	void ResetQuartzTransport();

	/**
	 * After a Continue or Set Position the Quartz clock can't jump to a fractional
	 * position, so it restarts on our next beat (beats line up) and again on the next
	 * alignment period (every division lines up). Called from Advance when a beat fires.
	 */
	void AlignQuartzOnBeat(int32 Beat);

	/** Our beat on which the Quartz clock restarts next, or -1 when it is already aligned. */
	int32 PendingQuartzAlignBeat = -1;

	float BPM = 120.0f;
	int32 BeatsPerBar = 4;
	bool bRunning = false;

	/**
	 * Position = AnchorBeats + (Now - AnchorTime) * BPM / 60 while running, AnchorBeats
	 * while stopped. Re-anchored on every tempo change, so a change applies from now
	 * and the beats already counted stay counted.
	 */
	double AnchorBeats = 0.0;
	double AnchorTime = 0.0;

	/** Beat numbers below this have had their OnBeat. -1 lets beat 0 fire. */
	int32 LastFiredBeat = -1;

	/** True once anything other than settings has set the tempo, so a settings edit stops overriding it. */
	bool bBPMSetAtRuntime = false;

	/** Started by Start With Play rather than by transport or Blueprint, so ending play stops it. */
	bool bStartedByPlay = false;

	TFunction<double()> TimeSource;

	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle SettingsChangedHandle;
	FDelegateHandle GameInstanceStartHandle;
	FDelegateHandle WorldCleanupHandle;
	FDelegateHandle WorldInitializedActorsHandle;
};
