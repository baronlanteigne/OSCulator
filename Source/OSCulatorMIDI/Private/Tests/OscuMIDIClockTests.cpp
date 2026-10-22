// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDISubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuClockSubsystem.h"
#include "OscuSettings.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

//////////////////////////////////////////////////////////////////////////
// Transport and tempo from MIDI input reach the clock. No hardware: IngestTransport
// and IngestControlChange are everything a real message does after the port.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIClockTransportTest,
	"OSCulator.MIDI.ClockTransport",
	OscuTest::Flags)

bool FOscuMIDIClockTransportTest::RunTest(const FString& Parameters)
{
	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	UOscuClockSubsystem* Clock = UOscuClockSubsystem::Get();
	if (!TestNotNull(TEXT("The MIDI subsystem exists"), MIDI) || !TestNotNull(TEXT("The clock exists"), Clock))
	{
		return false;
	}

	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	const bool bSavedFollow = Settings->bClockFollowsMIDITransport;
	const FString SavedDevice = Settings->ClockMIDIDevice;
	const bool bSavedFromCC = Settings->bClockBPMFromCC;
	const int32 SavedChannel = Settings->ClockBPMChannel;
	const int32 SavedControl = Settings->ClockBPMControl;
	const float SavedMin = Settings->ClockBPMMin;
	const float SavedMax = Settings->ClockBPMMax;

	double FakeNow = 1000.0;
	Clock->ResetForTest();
	Clock->SetTimeSourceForTest([&FakeNow]() { return FakeNow; });

	ON_SCOPE_EXIT
	{
		Settings->bClockFollowsMIDITransport = bSavedFollow;
		Settings->ClockMIDIDevice = SavedDevice;
		Settings->bClockBPMFromCC = bSavedFromCC;
		Settings->ClockBPMChannel = SavedChannel;
		Settings->ClockBPMControl = SavedControl;
		Settings->ClockBPMMin = SavedMin;
		Settings->ClockBPMMax = SavedMax;
		Clock->ResetForTest();
	};

	Settings->bClockFollowsMIDITransport = true;
	Settings->ClockMIDIDevice.Reset();

	// ---- Transport ----

	MIDI->IngestTransport(EOscuMIDITransport::Start);
	TestTrue(TEXT("MIDI Start starts the clock"), Clock->IsRunning());
	TestEqual(TEXT("...from beat 0"), Clock->GetPosition(), 0.0);

	// Sixteenth notes: 32 of them is 8 beats.
	MIDI->IngestTransport(EOscuMIDITransport::SongPosition, 32);
	TestEqual(TEXT("Song Position is sixteenths, so 32 is beat 8"), Clock->GetPosition(), 8.0);

	MIDI->IngestTransport(EOscuMIDITransport::Stop);
	TestFalse(TEXT("MIDI Stop stops it"), Clock->IsRunning());

	// The usual sequence for playing from mid-song: Song Position while stopped, then Continue.
	MIDI->IngestTransport(EOscuMIDITransport::SongPosition, 16);
	MIDI->IngestTransport(EOscuMIDITransport::Continue);
	TestTrue(TEXT("MIDI Continue runs it"), Clock->IsRunning());
	TestEqual(TEXT("...from the Song Position"), Clock->GetPosition(), 4.0);

	const uint64 DispatchedBefore = MIDI->GetMessagesDispatched();
	MIDI->IngestTransport(EOscuMIDITransport::Stop);
	TestEqual(TEXT("Transport never reaches a map"), MIDI->GetMessagesDispatched(), DispatchedBefore);

	// Not following: transport is ignored.
	Settings->bClockFollowsMIDITransport = false;
	MIDI->IngestTransport(EOscuMIDITransport::Start);
	TestFalse(TEXT("With Follow MIDI Transport off, Start is ignored"), Clock->IsRunning());

	// Following one named device: another device's Start is ignored, that one's is not.
	Settings->bClockFollowsMIDITransport = true;
	Settings->ClockMIDIDevice = TEXT("Sequencer");
	MIDI->IngestTransport(EOscuMIDITransport::Start, 0, FName(TEXT("Controller")));
	TestFalse(TEXT("Another device's Start is ignored"), Clock->IsRunning());
	MIDI->IngestTransport(EOscuMIDITransport::Start, 0, FName(TEXT("sequencer")));
	TestTrue(TEXT("The named device's Start counts, case aside"), Clock->IsRunning());

	// ---- Tempo from a control change ----

	Settings->ClockMIDIDevice.Reset();
	Settings->bClockBPMFromCC = true;
	Settings->ClockBPMChannel = 1;
	Settings->ClockBPMControl = 20;
	Settings->ClockBPMMin = 60.0f;
	Settings->ClockBPMMax = 187.0f;

	TestEqual(TEXT("CC 0 is BPM Min"), Settings->ClockBPMFromCCValue(0), 60.0f);
	TestEqual(TEXT("CC 127 is BPM Max"), Settings->ClockBPMFromCCValue(127), 187.0f);

	MIDI->IngestControlChange(1, 20, 60);
	TestEqual(TEXT("The BPM CC sets the tempo, one BPM per step"), Clock->GetBPM(), 120.0f);

	MIDI->IngestControlChange(1, 21, 0);
	MIDI->IngestControlChange(2, 20, 0);
	TestEqual(TEXT("Another CC or channel leaves it alone"), Clock->GetBPM(), 120.0f);

	Settings->bClockBPMFromCC = false;
	MIDI->IngestControlChange(1, 20, 0);
	TestEqual(TEXT("With BPM From CC off, the CC is ignored"), Clock->GetBPM(), 120.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
