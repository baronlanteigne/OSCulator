// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDISubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuMIDILibrary.h"
#include "Tests/OscuTestWorld.h"

#include "Misc/AutomationTest.h"

//////////////////////////////////////////////////////////////////////////
// The channel asymmetry in UE's own plugin

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIWireChannelTest,
	"OSCulator.MIDI.WireChannel",
	OscuTest::Flags)

bool FOscuMIDIWireChannelTest::RunTest(const FString& Parameters)
{
	// MIDIDeviceInputController reports channels as (Status % 16) + 1, i.e. 1-16.
	// MIDIDeviceOutputController ORs the channel straight into the status byte and
	// so wants 0-15. OSCulator is 1-16 everywhere, so output subtracts one.
	//
	// Getting this backwards puts every outgoing note one channel high, which is
	// invisible until someone looks at a DAW and wonders why channel 1 arrives on 2.
	TestEqual(TEXT("Channel 1 goes out as 0"), UOscuMIDISubsystem::ToWireChannel(1), 0);
	TestEqual(TEXT("Channel 10 goes out as 9"), UOscuMIDISubsystem::ToWireChannel(10), 9);
	TestEqual(TEXT("Channel 16 goes out as 15"), UOscuMIDISubsystem::ToWireChannel(16), 15);

	// Out-of-range input is clamped rather than allowed to corrupt the status byte.
	TestEqual(TEXT("Channel 0 clamps up to 1, so 0"), UOscuMIDISubsystem::ToWireChannel(0), 0);
	TestEqual(TEXT("Channel 17 clamps down to 16, so 15"), UOscuMIDISubsystem::ToWireChannel(17), 15);
	TestEqual(TEXT("A negative channel clamps too"), UOscuMIDISubsystem::ToWireChannel(-5), 0);

	// The round trip: what input reports, output must put back on the same wire
	// channel it came from.
	for (int32 Channel = 1; Channel <= 16; ++Channel)
	{
		TestEqual(FString::Printf(TEXT("Channel %d round trips"), Channel),
			UOscuMIDISubsystem::ToWireChannel(Channel) + 1, Channel);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Auto note-off bookkeeping. No hardware needed: what matters is that a
// scheduled release is never lost.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDINoteOffSchedulingTest,
	"OSCulator.MIDI.NoteOffScheduling",
	OscuTest::Flags)

bool FOscuMIDINoteOffSchedulingTest::RunTest(const FString& Parameters)
{
	UOscuMIDISubsystem* MIDI = UOscuMIDISubsystem::Get();
	if (!TestNotNull(TEXT("The MIDI subsystem exists"), MIDI))
	{
		return false;
	}

	// Start clean; another test or a real device may have left something pending.
	MIDI->FlushPendingNoteOffs();
	TestEqual(TEXT("Nothing pending to begin with"), MIDI->GetPendingNoteOffCount(), 0);

	// A long duration, so nothing can fire during the test.
	MIDI->SendNote(1, 60, 127, 3600.0f);
	TestEqual(TEXT("Sending a note schedules its release"), MIDI->GetPendingNoteOffCount(), 1);

	MIDI->SendNote(2, 64, 100, 3600.0f);
	MIDI->SendNote(3, 67, 80, 3600.0f);
	TestEqual(TEXT("Each note schedules its own"), MIDI->GetPendingNoteOffCount(), 3);

	// The release is scheduled even with no device open, so the bookkeeping stays
	// honest and a device opened in between still gets told.
	MIDI->FlushPendingNoteOffs();
	TestEqual(TEXT("Flushing releases everything at once"), MIDI->GetPendingNoteOffCount(), 0);

	// A duration of zero means the caller is handling the release.
	MIDI->SendNote(1, 60, 127, 0.0f);
	TestEqual(TEXT("Zero duration schedules nothing"), MIDI->GetPendingNoteOffCount(), 0);

	MIDI->SendNote(1, 60, 127, -1.0f);
	TestEqual(TEXT("A negative duration schedules nothing either"), MIDI->GetPendingNoteOffCount(), 0);

	// A plain note on never schedules; that is what SendNote is for.
	MIDI->SendNoteOn(1, 60, 127);
	TestEqual(TEXT("Note On alone schedules nothing"), MIDI->GetPendingNoteOffCount(), 0);

	// The Blueprint surface reaches the same state.
	UOscuMIDILibrary::SendMIDINote(UOscuMIDILibrary::AllDevices, 1, 60, 127, 3600.0f);
	TestEqual(TEXT("The Blueprint node schedules too"), UOscuMIDILibrary::GetPendingMIDINoteOffCount(), 1);

	UOscuMIDILibrary::ReleaseAllMIDINotes();
	TestEqual(TEXT("Release All clears it"), UOscuMIDILibrary::GetPendingMIDINoteOffCount(), 0);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Note name helpers on the Blueprint surface

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDILibraryNoteNameTest,
	"OSCulator.MIDI.LibraryNoteNames",
	OscuTest::Flags)

bool FOscuMIDILibraryNoteNameTest::RunTest(const FString& Parameters)
{
	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	const int32 SavedMiddleC = Settings->MiddleCOctave;
	Settings->MiddleCOctave = 3;

	TestEqual(TEXT("C3 reads as 60"), UOscuMIDILibrary::MIDINoteFromName(TEXT("C3")), 60);
	TestEqual(TEXT("A bare number passes through"), UOscuMIDILibrary::MIDINoteFromName(TEXT("61")), 61);
	TestEqual(TEXT("60 prints as C3"), UOscuMIDILibrary::MIDINoteToName(60), FString(TEXT("C3")));

	// Unusable input reports rather than guessing.
	TestEqual(TEXT("Nonsense returns -1"), UOscuMIDILibrary::MIDINoteFromName(TEXT("banana")), INDEX_NONE);
	TestEqual(TEXT("An out-of-range note prints empty"), UOscuMIDILibrary::MIDINoteToName(200), FString());

	Settings->MiddleCOctave = SavedMiddleC;
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
