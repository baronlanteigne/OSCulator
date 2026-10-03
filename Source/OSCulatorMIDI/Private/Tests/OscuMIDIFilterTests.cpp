// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDIPort.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "OscuSettings.h"
#include "portmidi.h"

namespace OscuMIDIFilterTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** Nothing wanted. The starting point for testing one switch at a time. */
	static FOscuMIDIInputMessages None()
	{
		FOscuMIDIInputMessages Listen;
		Listen.bNotes = false;
		Listen.bControlChange = false;
		Listen.bProgramChange = false;
		Listen.bPitchBend = false;
		Listen.bAftertouch = false;
		Listen.bClock = false;
		Listen.bTransport = false;
		Listen.bSysEx = false;
		return Listen;
	}
}

/**
 * The message mask, checked without hardware.
 *
 * The settings say what to keep and PortMidi wants what to throw away, so every one of
 * these is an inversion -- which is exactly the kind of thing that is wrong in one
 * place and right in seven. A wrong bit here is invisible until a show: too few and the
 * clock flood returns, one too many and notes silently stop arriving.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIFilterMaskTest,
	"OSCulator.MIDI.FilterMask",
	OscuMIDIFilterTest::Flags)

bool FOscuMIDIFilterMaskTest::RunTest(const FString& Parameters)
{
	auto ExpectDropped = [this](const FString& What, int32 Mask, int32 Bit)
	{
		TestTrue(FString::Printf(TEXT("%s is dropped"), *What), (Mask & Bit) == Bit);
	};

	auto ExpectPassed = [this](const FString& What, int32 Mask, int32 Bit)
	{
		TestTrue(FString::Printf(TEXT("%s is passed through"), *What), (Mask & Bit) == 0);
	};

	// ---- Ticking one thing admits that thing, and only that thing ----

	{
		FOscuMIDIInputMessages Listen = OscuMIDIFilterTest::None();
		Listen.bNotes = true;

		const int32 Mask = OscuMIDI::BuildFilterMask(Listen);
		ExpectPassed(TEXT("notes, with only notes wanted"), Mask, PM_FILT_NOTE);
		ExpectDropped(TEXT("control change, with only notes wanted"), Mask, PM_FILT_CONTROL);
		ExpectDropped(TEXT("clock, with only notes wanted"), Mask, PM_FILT_CLOCK);
		ExpectDropped(TEXT("sysex, with only notes wanted"), Mask, PM_FILT_SYSEX);
	}

	{
		FOscuMIDIInputMessages Listen = OscuMIDIFilterTest::None();
		Listen.bControlChange = true;

		const int32 Mask = OscuMIDI::BuildFilterMask(Listen);
		ExpectPassed(TEXT("control change, with only CC wanted"), Mask, PM_FILT_CONTROL);

		// Notes are no longer unconditional -- they have their own switch now -- so
		// this asserts the switches really are independent rather than that notes are
		// special.
		ExpectDropped(TEXT("notes, with only CC wanted"), Mask, PM_FILT_NOTE);
	}

	{
		FOscuMIDIInputMessages Listen = OscuMIDIFilterTest::None();
		Listen.bProgramChange = true;

		// The one Baron accidentally switched off when the panel meant the opposite.
		const int32 Mask = OscuMIDI::BuildFilterMask(Listen);
		ExpectPassed(TEXT("program change, with only program change wanted"), Mask, PM_FILT_PROGRAM);
		ExpectDropped(TEXT("control change, with only program change wanted"), Mask, PM_FILT_CONTROL);
	}

	{
		FOscuMIDIInputMessages Listen = OscuMIDIFilterTest::None();
		Listen.bAftertouch = true;

		// Both kinds, together. Channel aftertouch without poly would be a very
		// confusing half-measure.
		const int32 Mask = OscuMIDI::BuildFilterMask(Listen);
		ExpectPassed(TEXT("channel aftertouch"), Mask, PM_FILT_CHANNEL_AFTERTOUCH);
		ExpectPassed(TEXT("poly aftertouch"), Mask, PM_FILT_POLY_AFTERTOUCH);
	}

	{
		FOscuMIDIInputMessages Listen = OscuMIDIFilterTest::None();
		Listen.bClock = true;

		// All three continuous timing streams, together, for the same reason.
		const int32 Mask = OscuMIDI::BuildFilterMask(Listen);
		ExpectPassed(TEXT("clock"), Mask, PM_FILT_CLOCK);
		ExpectPassed(TEXT("tick"), Mask, PM_FILT_TICK);
		ExpectPassed(TEXT("timecode"), Mask, PM_FILT_MTC);
		ExpectDropped(TEXT("transport, with only clock wanted"), Mask, PM_FILT_PLAY);
	}

	{
		FOscuMIDIInputMessages Listen = OscuMIDIFilterTest::None();
		Listen.bTransport = true;

		const int32 Mask = OscuMIDI::BuildFilterMask(Listen);
		ExpectPassed(TEXT("start/stop/continue"), Mask, PM_FILT_PLAY);
		ExpectPassed(TEXT("song position"), Mask, PM_FILT_SONG_POSITION);
		ExpectPassed(TEXT("song select"), Mask, PM_FILT_SONG_SELECT);
		ExpectDropped(TEXT("clock, with only transport wanted"), Mask, PM_FILT_CLOCK);
	}

	{
		FOscuMIDIInputMessages Listen = OscuMIDIFilterTest::None();
		Listen.bPitchBend = true;
		const int32 Mask = OscuMIDI::BuildFilterMask(Listen);
		ExpectPassed(TEXT("pitch bend"), Mask, PM_FILT_PITCHBEND);
	}

	{
		FOscuMIDIInputMessages Listen = OscuMIDIFilterTest::None();
		Listen.bSysEx = true;
		const int32 Mask = OscuMIDI::BuildFilterMask(Listen);
		ExpectPassed(TEXT("sysex"), Mask, PM_FILT_SYSEX);
	}

	// ---- The shipped defaults ----

	{
		const FOscuMIDIInputMessages Defaults;
		const int32 Mask = OscuMIDI::BuildFilterMask(Defaults);

		// Everything that carries a playable value arrives out of the box. A device
		// that needs configuring before it does anything is a device that looks broken.
		ExpectPassed(TEXT("notes, by default"), Mask, PM_FILT_NOTE);
		ExpectPassed(TEXT("control change, by default"), Mask, PM_FILT_CONTROL);
		ExpectPassed(TEXT("program change, by default"), Mask, PM_FILT_PROGRAM);
		ExpectPassed(TEXT("pitch bend, by default"), Mask, PM_FILT_PITCHBEND);
		ExpectPassed(TEXT("aftertouch, by default"), Mask, PM_FILT_CHANNEL_AFTERTOUCH);

		// And the two that nothing can act on, plus the flood, do not.
		ExpectDropped(TEXT("clock, by default"), Mask, PM_FILT_CLOCK);
		ExpectDropped(TEXT("transport, by default"), Mask, PM_FILT_PLAY);
		ExpectDropped(TEXT("sysex, by default"), Mask, PM_FILT_SYSEX);
	}

	// ---- Invariants, whatever the settings say ----

	{
		const int32 Everything = OscuMIDI::BuildFilterMask([]
		{
			FOscuMIDIInputMessages Listen;
			Listen.bClock = true;
			Listen.bTransport = true;
			Listen.bSysEx = true;
			return Listen;
		}());

		const int32 Nothing = OscuMIDI::BuildFilterMask(OscuMIDIFilterTest::None());

		// Pm_SetFilter replaces PortMidi's default mask rather than adding to it, so
		// active sensing must be asked for explicitly every time. Forgetting it makes
		// ticking everything noisier than never having filtered at all.
		ExpectDropped(TEXT("active sensing, with everything wanted"), Everything, PM_FILT_ACTIVE);
		ExpectDropped(TEXT("active sensing, with nothing wanted"), Nothing, PM_FILT_ACTIVE);

		// Reset and the undefined real-time messages have no switch: nothing can
		// subscribe to them, so they always go.
		ExpectDropped(TEXT("reset, with everything wanted"), Everything, PM_FILT_RESET);
		ExpectDropped(TEXT("undefined real-time, with everything wanted"), Everything, PM_FILT_UNDEFINED);
	}

	// ---- What the log will say ----

	TestEqual(TEXT("the default mask describes itself"),
		OscuMIDI::DescribeFilterMask(OscuMIDI::BuildFilterMask(FOscuMIDIInputMessages())),
		FString(TEXT("clock, timecode, transport, sysex")));

	// Losing notes is shouted about, because it is the one setting that stops every
	// mapping from firing.
	TestTrue(TEXT("a mask without notes says so loudly"),
		OscuMIDI::DescribeFilterMask(OscuMIDI::BuildFilterMask(OscuMIDIFilterTest::None())).Contains(TEXT("NOTES")));

	return true;
}

/**
 * The per-device channel mask.
 *
 * Two things to get wrong here. PortMidi counts channels from zero and OSCulator counts
 * from one, which is exactly the kind of difference that produces a rig answering to the
 * wrong channel. And the list is an EXCLUSION list -- what to throw away -- while the
 * mask it produces is an admission mask, so the inversion is checked explicitly rather
 * than assumed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIChannelMaskTest,
	"OSCulator.MIDI.ChannelMask",
	OscuMIDIFilterTest::Flags)

bool FOscuMIDIChannelMaskTest::RunTest(const FString& Parameters)
{
	const FString Device(TEXT("TestDevice"));

	// Nothing ignored is every channel. This is the default for a device with no
	// configuration at all, so getting it wrong mutes every rig out of the box.
	TestEqual(TEXT("ignoring nothing admits all 16"),
		OscuMIDI::BuildChannelMaskIgnoring({}, Device), 0xFFFF);

	// Channel 1 is PortMidi's bit 0. Off by one here and the wrong instrument goes quiet.
	TestEqual(TEXT("ignoring channel 1 clears bit 0"),
		OscuMIDI::BuildChannelMaskIgnoring({ 1 }, Device), 0xFFFE);
	TestEqual(TEXT("ignoring channel 2 clears bit 1"),
		OscuMIDI::BuildChannelMaskIgnoring({ 2 }, Device), 0xFFFD);
	TestEqual(TEXT("ignoring channel 16 clears bit 15"),
		OscuMIDI::BuildChannelMaskIgnoring({ 16 }, Device), 0x7FFF);

	TestEqual(TEXT("several ignored channels combine"),
		OscuMIDI::BuildChannelMaskIgnoring({ 1, 2, 16 }, Device), 0x7FFC);
	TestEqual(TEXT("order does not matter"),
		OscuMIDI::BuildChannelMaskIgnoring({ 16, 2, 1 }, Device), 0x7FFC);
	TestEqual(TEXT("a repeat is harmless"),
		OscuMIDI::BuildChannelMaskIgnoring({ 7, 7 }, Device),
		OscuMIDI::BuildChannelMaskIgnoring({ 7 }, Device));

	// The reason the list is an exclusion list: a freshly added array row sits at 0, and
	// in this direction that ignores nothing instead of muting the device. Out-of-range
	// entries are reported, because a row that looks set and does nothing is confusing
	// on its own terms.
	AddExpectedError(TEXT("outside 1-16"), EAutomationExpectedErrorFlags::Contains, 0);
	TestEqual(TEXT("a new row at 0 changes nothing"),
		OscuMIDI::BuildChannelMaskIgnoring({ 0 }, Device), 0xFFFF);
	TestEqual(TEXT("out of range entries are skipped, valid ones still apply"),
		OscuMIDI::BuildChannelMaskIgnoring({ 0, 1, 17, -3 }, Device), 0xFFFE);

	// All sixteen is taken at face value rather than second-guessed into meaning
	// "everything", which is what the old selection list had to do.
	AddExpectedError(TEXT("all sixteen channels"), EAutomationExpectedErrorFlags::Contains, 0);
	TestEqual(TEXT("ignoring all 16 admits nothing"),
		OscuMIDI::BuildChannelMaskIgnoring(
			{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }, Device), 0x0000);

	TestEqual(TEXT("all channels describes itself"),
		OscuMIDI::DescribeChannelMask(OscuMIDI::BuildChannelMaskIgnoring({}, Device)),
		FString(TEXT("all channels")));
	TestEqual(TEXT("a narrowed mask names what it drops"),
		OscuMIDI::DescribeChannelMask(OscuMIDI::BuildChannelMaskIgnoring({ 2, 7 }, Device)),
		FString(TEXT("all channels except 2, 7")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
