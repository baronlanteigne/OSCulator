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
 * The channel mask.
 *
 * PortMidi counts channels from zero and OSCulator counts from one, which is exactly
 * the kind of difference that produces a rig answering to the wrong channel.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDIChannelMaskTest,
	"OSCulator.MIDI.ChannelMask",
	OscuMIDIFilterTest::Flags)

bool FOscuMIDIChannelMaskTest::RunTest(const FString& Parameters)
{
	// Channel 1 is PortMidi's bit 0. Off by one here and every mapping listens to the
	// wrong instrument.
	TestEqual(TEXT("channel 1 is bit 0"), OscuMIDI::BuildChannelMask({ 1 }), 0x0001);
	TestEqual(TEXT("channel 2 is bit 1"), OscuMIDI::BuildChannelMask({ 2 }), 0x0002);
	TestEqual(TEXT("channel 16 is bit 15"), OscuMIDI::BuildChannelMask({ 16 }), 0x8000);

	TestEqual(TEXT("several channels combine"), OscuMIDI::BuildChannelMask({ 1, 2, 16 }), 0x8003);
	TestEqual(TEXT("order does not matter"), OscuMIDI::BuildChannelMask({ 16, 2, 1 }), 0x8003);
	TestEqual(TEXT("a repeat is harmless"), OscuMIDI::BuildChannelMask({ 7, 7 }), OscuMIDI::BuildChannelMask({ 7 }));

	// An empty list means everything. This is the default, so getting it wrong would
	// mute every device out of the box.
	TestEqual(TEXT("an empty list admits all 16"), OscuMIDI::BuildChannelMask({}), 0xFFFF);
	TestEqual(TEXT("all 16 listed is the same as none listed"),
		OscuMIDI::BuildChannelMask({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 }), 0xFFFF);

	// Out-of-range entries are ignored, and a list of nothing else falls back to all
	// channels rather than silently listening to none. A new array row starts at 0, so
	// this is the common case rather than the exotic one.
	AddExpectedError(TEXT("ignoring"), EAutomationExpectedErrorFlags::Contains, 0);
	TestEqual(TEXT("out of range entries are ignored"), OscuMIDI::BuildChannelMask({ 0, 1, 17, -3 }), 0x0001);
	TestEqual(TEXT("a list of only bad entries admits all 16"), OscuMIDI::BuildChannelMask({ 0, 17, 99 }), 0xFFFF);

	TestEqual(TEXT("all channels describes itself"),
		OscuMIDI::DescribeChannelMask(OscuMIDI::BuildChannelMask({})),
		FString(TEXT("all channels")));
	TestEqual(TEXT("a narrowed mask names its channels"),
		OscuMIDI::DescribeChannelMask(OscuMIDI::BuildChannelMask({ 2, 7 })),
		FString(TEXT("channel(s) 2, 7 only")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
