// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OscuMIDIMap.h"

struct FOscuMIDIInputMessages;

/** One decoded message, in the terms a binding is written in. */
struct FOscuMIDIMessage
{
	EOscuMIDIInputType Type = EOscuMIDIInputType::Note;

	/** 1-16, matching the map and every DAW. */
	int32 Channel = 1;

	/** The note number, or the CC number. */
	int32 Number = 0;

	/** Velocity, or the controller value. 0-127. */
	int32 Value = 0;

	/** Note sources only. A note on with zero velocity arrives here as false. */
	bool bNoteOn = false;
};

/**
 * One MIDI input device, owned outright.
 *
 * OSCulator reads MIDI input through its own copy of PortMidi rather than through
 * UE's MIDIDevice plugin. That is not a preference; the engine plugin has three
 * defects that cannot be reached from outside it:
 *
 *   - It opens every stream with PortMidi's default message filter, which discards
 *     active sensing and nothing else. Clock measured 68% of the traffic from a
 *     sequencer at tempo, and every message of it costs a queue slot on its way to
 *     being read and thrown away, because the engine's event switch has no case for
 *     system messages.
 *   - UMIDIDeviceInputController::StartupDevice passes its own zeroed MIDIBufferSize
 *     member to Pm_OpenInput instead of the size it was handed, so the queue is
 *     PortMidi's small fallback no matter what any caller asks for. Identical in
 *     every engine version from 5.3 to 5.8.
 *   - Neither the filter nor the channel mask is exposed, and PortMidi links
 *     statically with no exported symbols, so there is no way in from another module.
 *     Trying it -- calling Pm_SetFilter on a stream the engine opened -- reaches an
 *     uninitialised device table in a second private copy of the library and takes
 *     the editor down.
 *
 * So this module initialises its own copy and owns the whole path: open, filter,
 * mask, read, close. Two copies of PortMidi then coexist in the process, ours
 * driving input and the engine's driving output. They share no state -- verified by
 * making the engine terminate and reinitialise its copy while a stream of ours was
 * open and reading, which it did not notice.
 *
 * MIDI output still goes through UMIDIDeviceOutputController, which has none of these
 * problems.
 */
struct FOscuMIDIPort
{
	FOscuMIDIPort() = default;

	/**
	 * Move-only, and it closes itself.
	 *
	 * A PortMidi stream is not a managed resource: nothing reclaims it, and a leaked
	 * one holds the device for the life of the process, so no other application can
	 * have it. That makes the destructor worth having even though every path closes
	 * explicitly. Copying is deleted rather than defined, because two copies of one
	 * stream pointer would mean closing it twice -- and moving nulls the source, so
	 * TArray can reallocate without either losing or double-closing a port.
	 */
	~FOscuMIDIPort() { Close(); }

	FOscuMIDIPort(const FOscuMIDIPort&) = delete;
	FOscuMIDIPort& operator=(const FOscuMIDIPort&) = delete;

	FOscuMIDIPort(FOscuMIDIPort&& Other) noexcept
		: Name(MoveTemp(Other.Name))
		, DeviceId(Other.DeviceId)
		, DeviceID(Other.DeviceID)
		, UnusedMessages(Other.UnusedMessages)
		, Overflows(Other.Overflows)
		, Stream(Other.Stream)
		, FilterMask(Other.FilterMask)
		, ChannelMask(Other.ChannelMask)
		, ConfiguredChannelMask(Other.ConfiguredChannelMask)
	{
		Other.Stream = nullptr;
		Other.DeviceID = INDEX_NONE;
	}

	FOscuMIDIPort& operator=(FOscuMIDIPort&& Other) noexcept
	{
		if (this != &Other)
		{
			Close();

			Name = MoveTemp(Other.Name);
			DeviceId = Other.DeviceId;
			DeviceID = Other.DeviceID;
			UnusedMessages = Other.UnusedMessages;
			Overflows = Other.Overflows;
			Stream = Other.Stream;
			FilterMask = Other.FilterMask;
			ChannelMask = Other.ChannelMask;
			ConfiguredChannelMask = Other.ConfiguredChannelMask;

			Other.Stream = nullptr;
			Other.DeviceID = INDEX_NONE;
		}
		return *this;
	}

	/** The name this port was configured under, as typed in settings. */
	FString Name;

	/** The same name, for matching a source's Device without rebuilding it per frame. */
	FName DeviceId;

	/** PortMidi's device index in OUR copy's enumeration. Meaningless to the engine's. */
	int32 DeviceID = INDEX_NONE;

	/**
	 * Opens by name. False on failure, with a reason in OutError fit for a log line.
	 *
	 * QueueSize is in messages. FilterMask and ChannelMask come from BuildFilterMask and
	 * BuildChannelMaskIgnoring; passing them in rather than reading settings here keeps
	 * this testable and keeps the policy in one place. Both are per device, so two ports
	 * opened in the same restart routinely get different ones -- which is free, since
	 * PortMidi applies both per stream anyway.
	 */
	bool Open(const FString& DeviceName, int32 QueueSize, int32 FilterMask, int32 ChannelMask, FString& OutError);

	/** Idempotent. Safe on a port that never opened. */
	void Close();

	bool IsOpen() const { return Stream != nullptr; }

	/** Replaces the channel mask on an open port. Used to lift it while Learn is armed. */
	void SetChannelMask(int32 ChannelMask);

	/**
	 * Puts back the mask this port was opened with, after Learn lifted it.
	 *
	 * The port remembers it rather than the caller recomputing it, because each device
	 * has its own ignored-channel list now -- restoring from settings would mean
	 * re-matching every open port against its configured entry by name.
	 */
	void RestoreChannelMask();

	/**
	 * Reads everything queued and reports what it understood.
	 *
	 * Returns the number of raw messages consumed, which is not the number reported:
	 * anything that survived the filter but is neither a note nor a control change is
	 * counted and dropped.
	 */
	int32 Drain(const TFunctionRef<void(const FOscuMIDIMessage&)>& OnMessage);

	/** Messages read that were neither notes nor CC. A high count means a loose filter. */
	uint64 UnusedMessages = 0;

	/** Times PortMidi reported its queue had overflowed and been flushed. */
	uint64 Overflows = 0;

	int32 GetFilterMask() const { return FilterMask; }
	int32 GetChannelMask() const { return ChannelMask; }

private:
	/** PmStream*, kept as void* so portmidi.h stays out of this header. */
	void* Stream = nullptr;

	int32 FilterMask = 0;

	/** What is live on the stream right now. Lifted to 0xFFFF while Learn is armed. */
	int32 ChannelMask = 0;

	/** What Open was given, so RestoreChannelMask has something to go back to. */
	int32 ConfiguredChannelMask = 0xFFFF;
};

/** One device as our own copy of PortMidi sees it. */
struct FOscuMIDIDeviceInfo
{
	FString Name;
	int32 DeviceID = INDEX_NONE;
	bool bInput = false;
	bool bOutput = false;

	/** Open in OUR copy. PortMidi's opened flag is per-process-copy; it cannot see other applications. */
	bool bOpenedByUs = false;
};

namespace OscuMIDI
{
	/**
	 * Starts our copy of PortMidi if it is not already running. Idempotent.
	 *
	 * Deliberately never paired with a Terminate except at module shutdown. The engine
	 * plugin re-initialises its copy on every device enumeration, which is how it ends
	 * up restarting controllers behind its own back; doing it once is both cheaper and
	 * far easier to reason about.
	 */
	bool EnsureInitialised(FString& OutError);

	/** Called once, from module shutdown, after every port is closed. */
	void Shutdown();

	/** Enumerates through our copy. Empty if initialisation failed. */
	void EnumerateDevices(TArray<FOscuMIDIDeviceInfo>& OutDevices);

	/**
	 * The PortMidi filter mask for one device's "listen for" setting.
	 *
	 * Note the inversion: the settings say what to keep, PortMidi wants what to throw
	 * away. Pure, so a test can check it without hardware.
	 */
	int32 BuildFilterMask(const FOscuMIDIInputMessages& Listen);

	/**
	 * The PortMidi channel mask for one device's list of channels to IGNORE, numbered
	 * 1-16.
	 *
	 * Exclusion, not selection: the mask starts at all sixteen and each listed channel
	 * clears a bit, so an empty list -- the default -- admits everything with no special
	 * case, and a row still sitting at its freshly-added 0 changes nothing. DeviceName is
	 * only for the log lines this emits on an out-of-range entry or an all-sixteen list.
	 */
	int32 BuildChannelMaskIgnoring(const TArray<int32>& IgnoredChannels, const FString& DeviceName);

	/** What a filter mask throws away, in words, for a log line. */
	FString DescribeFilterMask(int32 Mask);

	/** What a channel mask drops, in words, for a log line. */
	FString DescribeChannelMask(int32 Mask);
}
