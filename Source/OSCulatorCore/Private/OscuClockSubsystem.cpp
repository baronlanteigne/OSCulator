// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuClockSubsystem.h"

#include "OSCulatorCore.h"
#include "OscuSettings.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Quartz/AudioMixerClockHandle.h"
#include "Quartz/QuartzSubsystem.h"

static TAutoConsoleVariable<int32> CVarOscuClockMonitor(
	TEXT("OSCulator.ClockMonitor"),
	0,
	TEXT("1 logs the clock's start, stop and tempo changes. 2 also logs every beat."),
	ECVF_Default);

namespace
{
	constexpr float MinBPM = 20.0f;
	constexpr float MaxBPM = 999.0f;

	/** The beat that should fire next from a position, minus one: a position on an exact
	 *  beat has not fired it yet, a position past one has. */
	int32 LastBeatBefore(double Beats)
	{
		return FMath::CeilToInt32(Beats) - 1;
	}

	/** PIE and packaged worlds. Quartz clocks belong to a world, and editor worlds have no use for one. */
	template <typename FunctionType>
	void ForEachGameWorld(FunctionType&& Function)
	{
		if (GEngine == nullptr)
		{
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World != nullptr && World->IsGameWorld())
			{
				Function(*World);
			}
		}
	}

	/** How far under a whole number of pulses floating point is allowed to land and still count as it. */
	constexpr double PulseEpsilon = 1e-9;
}

// ---- Division math ----

double OscuClock::BeatsPerPulse(EQuartzCommandQuantization Division, int32 BeatsPerBar)
{
	switch (Division)
	{
	case EQuartzCommandQuantization::Bar:					return FMath::Max(BeatsPerBar, 1);
	case EQuartzCommandQuantization::Beat:					return 1.0;
	case EQuartzCommandQuantization::Tick:
	case EQuartzCommandQuantization::ThirtySecondNote:		return 1.0 / 8.0;
	case EQuartzCommandQuantization::SixteenthNote:			return 1.0 / 4.0;
	case EQuartzCommandQuantization::EighthNote:			return 1.0 / 2.0;
	case EQuartzCommandQuantization::QuarterNote:			return 1.0;
	case EQuartzCommandQuantization::HalfNote:				return 2.0;
	case EQuartzCommandQuantization::WholeNote:				return 4.0;
	case EQuartzCommandQuantization::DottedSixteenthNote:	return 3.0 / 8.0;
	case EQuartzCommandQuantization::DottedEighthNote:		return 3.0 / 4.0;
	case EQuartzCommandQuantization::DottedQuarterNote:		return 3.0 / 2.0;
	case EQuartzCommandQuantization::DottedHalfNote:		return 3.0;
	case EQuartzCommandQuantization::DottedWholeNote:		return 6.0;
	case EQuartzCommandQuantization::SixteenthNoteTriplet:	return 1.0 / 6.0;
	case EQuartzCommandQuantization::EighthNoteTriplet:		return 1.0 / 3.0;
	case EQuartzCommandQuantization::QuarterNoteTriplet:	return 2.0 / 3.0;
	case EQuartzCommandQuantization::HalfNoteTriplet:		return 4.0 / 3.0;
	default:												return 0.0;
	}
}

int64 OscuClock::FloorPulses(double Pulses)
{
	return FMath::FloorToInt64(Pulses + PulseEpsilon);
}

double OscuClock::PulsePhase(double Pulses)
{
	return FMath::Clamp(Pulses - static_cast<double>(FloorPulses(Pulses)), 0.0, 1.0);
}

int64 OscuClock::LastPulseBefore(double Pulses)
{
	return FMath::CeilToInt64(Pulses - PulseEpsilon) - 1;
}

int32 OscuClock::AlignmentPeriodBeats(int32 BeatsPerBar)
{
	// Every division above is a whole number of 1/24 beats; the longest that all of them
	// divide into is 288 of those, 12 beats. A bar has to fit as well.
	constexpr int32 DivisionsPeriod = 12;
	BeatsPerBar = FMath::Max(BeatsPerBar, 1);
	return DivisionsPeriod / FMath::GreatestCommonDivisor(DivisionsPeriod, BeatsPerBar) * BeatsPerBar;
}

void UOscuClockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApplySettings();
	AnchorTime = Now();

	// Engine-level, like MIDI note-offs: the clock keeps counting while only the editor
	// is up, so a clock Ableton started before Play is already in phase when play begins.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UOscuClockSubsystem::Tick));

	SettingsChangedHandle = UOscuSettings::OnSettingsChanged.AddUObject(this, &UOscuClockSubsystem::ApplySettings);
	GameInstanceStartHandle = FWorldDelegates::OnStartGameInstance.AddUObject(this, &UOscuClockSubsystem::HandleGameInstanceStart);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UOscuClockSubsystem::HandleWorldCleanup);

	// Before any actor's BeginPlay, so one that asks for the Quartz clock finds it at this tempo.
	WorldInitializedActorsHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &UOscuClockSubsystem::HandleWorldInitializedActors);
}

void UOscuClockSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	UOscuSettings::OnSettingsChanged.Remove(SettingsChangedHandle);
	FWorldDelegates::OnStartGameInstance.Remove(GameInstanceStartHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitializedActorsHandle);

	Super::Deinitialize();
}

UOscuClockSubsystem* UOscuClockSubsystem::Get()
{
	return GEngine != nullptr ? GEngine->GetEngineSubsystem<UOscuClockSubsystem>() : nullptr;
}

// ---- Transport ----

void UOscuClockSubsystem::Start()
{
	bStartedByPlay = false;
	Reanchor(0.0);
	LastFiredBeat = -1;
	bRunning = true;

	// Beat 0 is on every division's grid, so restarting the Quartz clock now lines up all
	// of them at once.
	ResetQuartzTransport();
	PendingQuartzAlignBeat = -1;

	UE_CLOG(CVarOscuClockMonitor.GetValueOnGameThread() > 0, LogOSCulator, Log, TEXT("Clock: start at %.2f BPM"), BPM);
	OnStart.Broadcast();

	// Bar 0 and beat 0 now, not a frame from now: Start IS the downbeat.
	Advance();
}

void UOscuClockSubsystem::Stop()
{
	if (!bRunning)
	{
		return;
	}

	// Freeze where it is, so Continue picks up from here.
	AnchorBeats = GetPosition();
	bRunning = false;
	bStartedByPlay = false;

	UE_CLOG(CVarOscuClockMonitor.GetValueOnGameThread() > 0, LogOSCulator, Log, TEXT("Clock: stop at beat %.2f"), AnchorBeats);
	OnStop.Broadcast();
}

void UOscuClockSubsystem::Continue()
{
	if (bRunning)
	{
		// A second Continue while running would restart the count from the frozen anchor.
		return;
	}

	Reanchor(AnchorBeats);
	LastFiredBeat = LastBeatBefore(AnchorBeats);
	bRunning = true;

	// The Quartz clock kept running from wherever it was; line it up from our next beat.
	PendingQuartzAlignBeat = LastFiredBeat + 1;

	UE_CLOG(CVarOscuClockMonitor.GetValueOnGameThread() > 0, LogOSCulator, Log, TEXT("Clock: continue from beat %.2f"), AnchorBeats);
	OnStart.Broadcast();
	Advance();
}

void UOscuClockSubsystem::SetPosition(double Beats)
{
	Beats = FMath::Max(Beats, 0.0);
	Reanchor(Beats);
	LastFiredBeat = LastBeatBefore(Beats);

	if (bRunning)
	{
		PendingQuartzAlignBeat = LastFiredBeat + 1;
	}

	// Stopped, this only parks the position for Continue, which is what MIDI Song
	// Position before a Continue means.
	Advance();
}

// ---- Tempo ----

void UOscuClockSubsystem::SetBPM(float NewBPM)
{
	bBPMSetAtRuntime = true;

	NewBPM = FMath::Clamp(NewBPM, MinBPM, MaxBPM);
	if (FMath::IsNearlyEqual(NewBPM, BPM))
	{
		return;
	}

	// The beats already counted stay counted: only the rate from now on changes.
	Reanchor(GetPosition());
	BPM = NewBPM;

	PushTempoToQuartz();
	UE_CLOG(CVarOscuClockMonitor.GetValueOnGameThread() > 0, LogOSCulator, Log, TEXT("Clock: %.2f BPM"), BPM);
	OnBPMChanged.Broadcast(BPM);
}

void UOscuClockSubsystem::SetBeatsPerBar(int32 NewBeatsPerBar)
{
	NewBeatsPerBar = FMath::Clamp(NewBeatsPerBar, 1, 32);
	if (NewBeatsPerBar == BeatsPerBar)
	{
		return;
	}
	BeatsPerBar = NewBeatsPerBar;

	PushTimeSignatureToQuartz();
	if (bRunning)
	{
		// A new time signature can restart Quartz's bar count; line it up again.
		PendingQuartzAlignBeat = LastFiredBeat + 1;
	}
}

// ---- Divisions ----

float UOscuClockSubsystem::GetBeatsPerPulse(EQuartzCommandQuantization Division) const
{
	return static_cast<float>(OscuClock::BeatsPerPulse(Division, BeatsPerBar));
}

int32 UOscuClockSubsystem::GetPulseIndex(EQuartzCommandQuantization Division) const
{
	const double PulseBeats = OscuClock::BeatsPerPulse(Division, BeatsPerBar);
	if (PulseBeats <= 0.0)
	{
		return 0;
	}
	return static_cast<int32>(FMath::Clamp<int64>(OscuClock::FloorPulses(GetPosition() / PulseBeats), 0, MAX_int32));
}

float UOscuClockSubsystem::GetPulsePhase(EQuartzCommandQuantization Division) const
{
	const double PulseBeats = OscuClock::BeatsPerPulse(Division, BeatsPerBar);
	if (PulseBeats <= 0.0)
	{
		return 0.0f;
	}
	return static_cast<float>(OscuClock::PulsePhase(GetPosition() / PulseBeats));
}

float UOscuClockSubsystem::GetSecondsPerPulse(EQuartzCommandQuantization Division) const
{
	return static_cast<float>(OscuClock::BeatsPerPulse(Division, BeatsPerBar) * 60.0 / BPM);
}

// ---- Position ----

double UOscuClockSubsystem::GetPosition() const
{
	if (!bRunning)
	{
		return AnchorBeats;
	}
	return AnchorBeats + FMath::Max(Now() - AnchorTime, 0.0) * BPM / 60.0;
}

int32 UOscuClockSubsystem::GetBeat() const
{
	return FMath::FloorToInt32(GetPosition());
}

int32 UOscuClockSubsystem::GetBar() const
{
	return GetBeat() / BeatsPerBar;
}

int32 UOscuClockSubsystem::GetBeatInBar() const
{
	return GetBeat() % BeatsPerBar;
}

float UOscuClockSubsystem::GetBeatPhase() const
{
	const double Position = GetPosition();
	return static_cast<float>(Position - FMath::FloorToDouble(Position));
}

float UOscuClockSubsystem::GetBarPhase() const
{
	const double InBars = GetPosition() / BeatsPerBar;
	return static_cast<float>(InBars - FMath::FloorToDouble(InBars));
}

// ---- Ticking ----

void UOscuClockSubsystem::Advance()
{
	if (!bRunning)
	{
		return;
	}

	const int32 Beat = FMath::FloorToInt32(GetPosition());
	if (Beat <= LastFiredBeat)
	{
		return;
	}

	const int32 Previous = LastFiredBeat;

	// Set before broadcasting: a handler that calls Start or Set Position must not have
	// its new position overwritten when this returns.
	LastFiredBeat = Beat;

	// Before anyone hears the beat, so a sound queued on it lands on the aligned clock.
	AlignQuartzOnBeat(Beat);

	// Only the latest beat after a hitch, but any bar line crossed on the way still counts.
	const int32 Bar = Beat / BeatsPerBar;
	const int32 PreviousBar = Previous < 0 ? -1 : Previous / BeatsPerBar;

	if (Bar > PreviousBar)
	{
		OnBar.Broadcast(Bar);
	}

	// A bar handler may have moved the clock. Its own Advance has already fired what it
	// needed to, so this beat is stale.
	if (LastFiredBeat != Beat)
	{
		return;
	}

	// Counted from 1 here, as a DAW shows it, so the log reads against Ableton's bar display.
	UE_CLOG(CVarOscuClockMonitor.GetValueOnGameThread() > 1, LogOSCulator, Log, TEXT("Clock: %d.%d  (%.2f BPM)"),
		Bar + 1, Beat % BeatsPerBar + 1, BPM);
	OnBeat.Broadcast(Beat, Bar, Beat % BeatsPerBar);
}

bool UOscuClockSubsystem::Tick(float DeltaTime)
{
	Advance();
	return true;
}

void UOscuClockSubsystem::SetTimeSourceForTest(TFunction<double()> Source)
{
	// Keep the position continuous across the swap, as a tempo change would.
	const double Position = GetPosition();
	TimeSource = MoveTemp(Source);
	Reanchor(Position);
}

void UOscuClockSubsystem::ResetForTest()
{
	TimeSource = nullptr;
	bRunning = false;
	bStartedByPlay = false;
	bBPMSetAtRuntime = false;
	LastFiredBeat = -1;
	PulsePlayHolds.Reset();
	Reanchor(0.0);
	ApplySettings();
}

double UOscuClockSubsystem::Now() const
{
	// Real time, deliberately. Game time stops on pause and stretches with time dilation;
	// the clock this runs alongside does neither.
	return TimeSource ? TimeSource() : FPlatformTime::Seconds();
}

void UOscuClockSubsystem::Reanchor(double Beats)
{
	AnchorBeats = Beats;
	AnchorTime = Now();
}

// ---- Settings and play ----

void UOscuClockSubsystem::ApplySettings()
{
	const UOscuSettings& Settings = *UOscuSettings::Get();

	SetBeatsPerBar(Settings.ClockBeatsPerBar);

	// The settings value is a default. Once MIDI or a Blueprint has set the tempo, an
	// unrelated settings edit -- a device name, say -- must not snap it back.
	if (!bBPMSetAtRuntime)
	{
		const float NewBPM = FMath::Clamp(Settings.ClockBPM, MinBPM, MaxBPM);
		if (!FMath::IsNearlyEqual(NewBPM, BPM))
		{
			Reanchor(GetPosition());
			BPM = NewBPM;
			PushTempoToQuartz();
			OnBPMChanged.Broadcast(BPM);
		}
	}
}

void UOscuClockSubsystem::HandleGameInstanceStart(UGameInstance* GameInstance)
{
	if (!UOscuSettings::Get()->bClockStartWithPlay || bRunning)
	{
		return;
	}

	Start();
	bStartedByPlay = true;

	UE_LOG(LogOSCulator, Log, TEXT("Clock started with play at %.2f BPM, %d beats per bar."), BPM, BeatsPerBar);
}

void UOscuClockSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (World == nullptr || !World->IsGameWorld())
	{
		return;
	}

	// The world's Quartz clock goes with the world. Asked by name, so a clock made by
	// something else under the same name goes too -- there is only meant to be one.
	const FName ClockName = UOscuSettings::Get()->ClockQuartzName;
	UQuartzSubsystem* Quartz = UQuartzSubsystem::Get(World);
	if (!ClockName.IsNone() && Quartz != nullptr && Quartz->DoesClockExist(World, ClockName))
	{
		Quartz->DeleteClockByName(World, ClockName);
	}

	// Only a clock that play started stops with play. One started by MIDI Start keeps
	// running, because the DAW that started it is still playing.
	if (bStartedByPlay && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game))
	{
		Stop();
	}
}

// ---- Quartz ----

void UOscuClockSubsystem::HandleWorldInitializedActors(const FActorsInitializedParams& Params)
{
	if (Params.World != nullptr && Params.World->IsGameWorld())
	{
		FindOrMakeQuartzClock(*Params.World, true);
	}
}

UQuartzClockHandle* UOscuClockSubsystem::GetQuartzClock(const UObject* WorldContextObject)
{
	UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
		: nullptr;
	if (World == nullptr || !World->IsGameWorld())
	{
		return nullptr;
	}
	return FindOrMakeQuartzClock(*World, true);
}

bool UOscuClockSubsystem::ClaimPulsePlay(const UObject* Key, bool bStillPlaying, EQuartzCommandQuantization Division)
{
	// Quartz schedules ahead of the audio it renders; a sound queued just before a pulse can
	// land on the one after. The margin covers that, so a hold never ends while it waits.
	constexpr double SchedulingMargin = 0.05;

	const double NowSeconds = Now();
	if (const double* HoldUntil = PulsePlayHolds.Find(Key))
	{
		if (bStillPlaying && NowSeconds < *HoldUntil)
		{
			return false;
		}
	}

	// Components come and go with actors; drop the ones that have.
	if (PulsePlayHolds.Num() > 64)
	{
		for (auto It = PulsePlayHolds.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || It.Value() <= NowSeconds)
			{
				It.RemoveCurrent();
			}
		}
	}

	PulsePlayHolds.Add(Key, NowSeconds + GetSecondsPerPulse(Division) + SchedulingMargin);
	return true;
}

bool UOscuClockSubsystem::PlayOnPulse(UAudioComponent* AudioComponent, EQuartzCommandQuantization Division, const FOnQuartzCommandEventBP& Delegate)
{
	if (AudioComponent == nullptr)
	{
		return false;
	}

	UWorld* World = AudioComponent->GetWorld();
	UQuartzClockHandle* Handle = World != nullptr && World->IsGameWorld() ? FindOrMakeQuartzClock(*World, true) : nullptr;
	if (Handle == nullptr)
	{
		// Late beats a missing sound: whatever asked for it still hears it.
		UE_LOG(LogOSCulator, Warning, TEXT("Play On Pulse: no Quartz clock for '%s' (not in a game world, or Quartz Clock is None). Playing it now."),
			*AudioComponent->GetPathName());
		AudioComponent->Play();
		return true;
	}

	// Playing the component again would stop the sound it has queued for the pulse.
	if (!ClaimPulsePlay(AudioComponent, AudioComponent->IsPlaying(), Division))
	{
		return false;
	}

	// Transport Relative counts from the Quartz clock's 0, which is our beat 0 (or one of
	// the alignment points): the same grid Get Pulse Index counts on. Bar Relative would
	// restart a dotted 1/8 at every bar line instead.
	FQuartzQuantizationBoundary Boundary(Division, 1.0f, EQuarztQuantizationReference::TransportRelative);
	AudioComponent->PlayQuantized(World, Handle, Boundary, Delegate);
	return true;
}

UQuartzClockHandle* UOscuClockSubsystem::FindOrMakeQuartzClock(UWorld& World, bool bCreate)
{
	const FName ClockName = UOscuSettings::Get()->ClockQuartzName;
	if (ClockName.IsNone())
	{
		return nullptr;
	}

	UQuartzSubsystem* Quartz = UQuartzSubsystem::Get(&World);
	if (Quartz == nullptr)
	{
		return nullptr;
	}

	if (Quartz->DoesClockExist(&World, ClockName))
	{
		return Quartz->GetHandleForClock(&World, ClockName);
	}

	if (!bCreate)
	{
		return nullptr;
	}

	FQuartzClockSettings ClockSettings;
	ClockSettings.TimeSignature.NumBeats = BeatsPerBar;

	UQuartzClockHandle* Handle = Quartz->CreateNewClock(&World, ClockName, ClockSettings);
	if (Handle == nullptr)
	{
		return nullptr;
	}

	// Started even while transport is stopped, and never stopped by it: a sound queued on
	// a clock that is not running waits forever.
	FQuartzQuantizationBoundary Immediately;
	Handle->SetBeatsPerMinute(&World, Immediately, FOnQuartzCommandEventBP(), Handle, BPM);
	Handle->StartClock(&World, Handle);

	// Made mid-song, it starts from its own 0 rather than ours; line it up from our next beat.
	if (bRunning)
	{
		PendingQuartzAlignBeat = LastFiredBeat + 1;
	}

	UE_LOG(LogOSCulator, Log, TEXT("Quartz clock '%s' made at %.2f BPM, %d beats per bar."), *ClockName.ToString(), BPM, BeatsPerBar);
	return Handle;
}

void UOscuClockSubsystem::PushTempoToQuartz()
{
	ForEachGameWorld([this](UWorld& World)
	{
		if (UQuartzClockHandle* Handle = FindOrMakeQuartzClock(World, false))
		{
			FQuartzQuantizationBoundary Immediately;
			Handle->SetBeatsPerMinute(&World, Immediately, FOnQuartzCommandEventBP(), Handle, BPM);
		}
	});
}

void UOscuClockSubsystem::PushTimeSignatureToQuartz()
{
	const FName ClockName = UOscuSettings::Get()->ClockQuartzName;
	if (ClockName.IsNone())
	{
		return;
	}

	ForEachGameWorld([this, ClockName](UWorld& World)
	{
		UQuartzSubsystem* Quartz = UQuartzSubsystem::Get(&World);
		if (Quartz == nullptr || !Quartz->DoesClockExist(&World, ClockName))
		{
			return;
		}

		// Quartz has no setter for the time signature; making the clock again with
		// override on changes it in place, keeping the handle and the tempo.
		FQuartzClockSettings ClockSettings;
		ClockSettings.TimeSignature.NumBeats = BeatsPerBar;
		Quartz->CreateNewClock(&World, ClockName, ClockSettings, true);
	});
}

void UOscuClockSubsystem::ResetQuartzTransport()
{
	ForEachGameWorld([this](UWorld& World)
	{
		if (UQuartzClockHandle* Handle = FindOrMakeQuartzClock(World, false))
		{
			Handle->ResetTransport(&World, FOnQuartzCommandEventBP());
		}
	});
}

void UOscuClockSubsystem::AlignQuartzOnBeat(int32 Beat)
{
	if (PendingQuartzAlignBeat < 0 || Beat < PendingQuartzAlignBeat)
	{
		return;
	}

	// On one of our beats, so the Quartz beat lines up with ours from here.
	ResetQuartzTransport();

	// Its bars and its dotted and triplet grids count from this beat, though, not from our
	// 0. On an alignment point those all agree; anywhere else, do it again on the next one.
	const int32 Period = OscuClock::AlignmentPeriodBeats(BeatsPerBar);
	PendingQuartzAlignBeat = Beat % Period == 0 ? -1 : (Beat / Period + 1) * Period;
}
