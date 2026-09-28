// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"
#include "OscuSettings.generated.h"

/**
 * One place OSCulator sends to.
 *
 * Output needs a list where input does not. A receiving socket bound to 0.0.0.0
 * already hears every machine on the network at once, because UDP is
 * connectionless -- but sending is point-to-point, so reaching three devices
 * means three targets.
 */
USTRUCT()
struct OSCULATORCORE_API FOscuOSCTarget
{
	GENERATED_BODY()

	/** Referenced from a Send node. Leaving a send's target blank hits them all. */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Target")
	FName Name;

	UPROPERTY(EditAnywhere, Config, Category = "OSC Target")
	FString Host = TEXT("127.0.0.1");

	UPROPERTY(EditAnywhere, Config, Category = "OSC Target", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 Port = 9000;

	/** Silences one destination without deleting its configuration. */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Target")
	bool bEnabled = true;
};

/**
 * Project settings for OSCulator, under Project Settings > Plugins > OSCulator.
 *
 * Four transport branches, each behind its own checkbox. Every property in a
 * branch carries the EditCondition/EditConditionHides pair, so an unchecked branch
 * does not grey out -- it VANISHES from the panel. Someone who only does OSC input
 * should see one checkbox and a handful of fields, not twenty.
 *
 * The same booleans gate whether anything is opened at runtime, so a disabled
 * branch costs nothing at all: no socket, no thread, no tick hook.
 */
/**
 * Which kinds of MIDI message an input device listens for.
 *
 * Ticked means "I want this". Anything left unticked is discarded at the port, before
 * it is ever queued, and that is the point: UE's MIDI input queue is drained once per
 * game thread frame, so any hitch -- hitting Play, a shader compile -- is a window in
 * which nothing drains and the queue fills. When it fills, PortMidi throws away
 * everything in it, so a stream of messages nobody asked for takes the notes down
 * with it. Measured on a sequencer running at tempo, clock alone was 68% of the
 * traffic on the wire.
 *
 * Clock, transport and sysex start unticked because nothing in OSCulator can act on
 * them today. Everything that carries a playable value starts ticked, so a device
 * works as expected out of the box and narrowing it is a deliberate act.
 *
 * To cut continuous controller traffic specifically, reach for MIDIInputChannels
 * first: dropping a channel you do not map costs you nothing, while unticking Control
 * Change costs you every controller on every channel.
 */
USTRUCT()
struct FOscuMIDIInputMessages
{
	GENERATED_BODY()

	/**
	 * Note on and note off: everything a map asset can currently address.
	 *
	 * Untick it and OSCulator receives nothing it knows what to do with, and Learn
	 * stops working. It is offered anyway, because a list of "what I need" that leaves
	 * out the main thing is its own kind of confusing -- and because a project driving
	 * only continuous controllers is a reasonable thing to want later. Unticking it is
	 * called out in the log at startup rather than left to be discovered.
	 */
	UPROPERTY(EditAnywhere, Category = "Listen For")
	bool bNotes = true;

	/** Control change. Continuous while anything is modulating -- often the largest share of the traffic after clock. */
	UPROPERTY(EditAnywhere, Category = "Listen For")
	bool bControlChange = true;

	/** Program change. Rare on the wire, and cheap to keep. */
	UPROPERTY(EditAnywhere, Category = "Listen For")
	bool bProgramChange = true;

	/** Pitch bend. Continuous while a wheel moves. */
	UPROPERTY(EditAnywhere, Category = "Listen For")
	bool bPitchBend = true;

	/** Channel and polyphonic aftertouch. Continuous while keys are held. */
	UPROPERTY(EditAnywhere, Category = "Listen For")
	bool bAftertouch = true;

	/**
	 * Clock, tick and MTC quarter frame: the continuous timing streams.
	 *
	 * Around fifty messages a second at any ordinary tempo, and a hundred for
	 * timecode, arriving whether or not anyone is playing. This is the one that
	 * overflows queues. Tick it only to count clock yourself.
	 */
	UPROPERTY(EditAnywhere, Category = "Listen For")
	bool bClock = false;

	/** Start, stop, continue, song position, song select. A handful per button press, so cheap to keep. */
	UPROPERTY(EditAnywhere, Category = "Listen For")
	bool bTransport = false;

	/** System exclusive. Bursty -- a patch dump is thousands of bytes and can fill the queue by itself. */
	UPROPERTY(EditAnywhere, Category = "Listen For")
	bool bSysEx = false;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "OSCulator"))
class OSCULATORCORE_API UOscuSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	static const UOscuSettings* Get();

	/**
	 * Broadcast after any of these settings is edited.
	 *
	 * Transports subscribe so that changing a device name or a port takes effect
	 * immediately, rather than needing an editor restart to be believed.
	 *
	 * A delegate rather than a direct call because the transport modules depend on
	 * this one, never the other way round.
	 */
	DECLARE_MULTICAST_DELEGATE(FOscuSettingsChanged);
	// No API macro here: the class itself is already exported, and repeating it on a
	// member is an error rather than a redundancy.
	static FOscuSettingsChanged OnSettingsChanged;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ---- Registry ----

	/**
	 * Actor tags beginning with this are registered. The remainder becomes the
	 * address, so an actor tagged "OSC_laser" answers to /laser/<function>.
	 *
	 * Matching is case-insensitive, which is free typo tolerance.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Registry")
	FString TagPrefix = TEXT("OSC_");

	// ---- Function Exposure ----

	/**
	 * Expose every function on the actor's class, including inherited engine
	 * functions like K2_SetActorLocation and K2_DestroyActor.
	 *
	 * This is a deliberate trade: it hands anything on the network free transform
	 * control over tagged actors, in exchange for needing no per-function opt-in.
	 * Set a FunctionPrefix instead if that trade is wrong for a given project.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Function Exposure")
	bool bExposeAllFunctions = true;

	/**
	 * When non-empty, only functions starting with this are exposed, and the
	 * prefix is stripped from the address: OSC_fire becomes /laser/fire.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Function Exposure")
	FString FunctionPrefix;

	/** Always exposed, whatever the prefix says. Names are matched as written. */
	UPROPERTY(EditAnywhere, Config, Category = "Function Exposure")
	TArray<FName> ExtraAllowedFunctions;

	// ---- OSC Input ----

	/**
	 * Gates the whole branch. Unchecked, no socket is opened and no receive thread
	 * is started, so a disabled branch costs nothing at runtime.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Input")
	bool bEnableOSCIn = true;

	/**
	 * 0.0.0.0 listens on every interface, which is what you want -- the sender is
	 * usually on another machine. 127.0.0.1 would only ever hear this one.
	 *
	 * One socket is enough however many machines are sending: UDP is connectionless,
	 * so every sender on the network arrives here already, each message carrying its
	 * own SourceIP. A second input would only separate traffic by port number.
	 * Restrict WHICH machines with AllowedSenderIPs rather than by adding sockets.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Input",
		meta = (EditCondition = "bEnableOSCIn", EditConditionHides))
	FString BindAddress = TEXT("0.0.0.0");

	UPROPERTY(EditAnywhere, Config, Category = "OSC Input",
		meta = (EditCondition = "bEnableOSCIn", EditConditionHides, ClampMin = "1", ClampMax = "65535"))
	int32 ListenPort = 8000;

	/**
	 * Empty accepts everything. Listing addresses drops packets from anywhere else,
	 * which removes the "wrong machine on the network" failure entirely.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Input",
		meta = (EditCondition = "bEnableOSCIn", EditConditionHides))
	TArray<FString> AllowedSenderIPs;

	/** Generous by design: a sender that bursts should not lose packets to the OS. */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Input",
		meta = (EditCondition = "bEnableOSCIn", EditConditionHides, ClampMin = "4096"))
	int32 ReceiveBufferSize = 1024 * 1024;

	/**
	 * Addresses where every hit matters, exempted from last-wins coalescing.
	 *
	 * Zero-argument messages are never coalesced regardless of this list: a no-arg
	 * function is a trigger, and triggers are meant to fire every time.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Input",
		meta = (EditCondition = "bEnableOSCIn", EditConditionHides))
	TArray<FString> NoCoalesceAddresses;

	// ---- OSC Output ----

	/** Off by default: sending is opt-in, and the transport lands with Phase 7. */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Output")
	bool bEnableOSCOut = false;

	/**
	 * Every destination to send to. A Send node with no target named hits all the
	 * enabled ones; naming a target hits just that one.
	 *
	 * Deliberately a list, unlike OSC input, which needs only the one socket.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Output",
		meta = (EditCondition = "bEnableOSCOut", EditConditionHides,
				TitleProperty = "{Name}  {Host}:{Port}"))
	TArray<FOscuOSCTarget> OSCTargets;

	/**
	 * Receiving this address makes OSCulator serialise its own introspection and
	 * send it back, so a Max patch or TouchDesigner network can auto-populate its
	 * senders instead of being told the addresses by hand.
	 *
	 * The "/_" prefix marks it as control traffic rather than an actor address.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "OSC Output",
		meta = (EditCondition = "bEnableOSCOut", EditConditionHides))
	FString DescribeAddress = TEXT("/_describe");

	// ---- MIDI Input ----

	/** Off by default; the transport lands with Phase 6. */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input")
	bool bEnableMIDIIn = false;

	/**
	 * Every controller to listen to. Chosen by name, never by enumeration order:
	 * device exclusivity conflicts between plugins are real, and grabbing whatever
	 * happens to be first is how you end up fighting another plugin for a port.
	 *
	 * A list because MIDI devices are opened individually -- unlike OSC input,
	 * where one socket already hears every sender. Events from all listed devices
	 * are merged into one stream, so two controllers sending the same channel and
	 * note both trigger the same mapping.
	 *
	 * A device that is missing or already open is skipped with a log line rather
	 * than taking the others down with it.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input",
		meta = (EditCondition = "bEnableMIDIIn", EditConditionHides))
	TArray<FString> MIDIInputDeviceNames;

	/**
	 * Which mapping assets are active. All of them, at once.
	 *
	 * A list rather than one, so the split is yours to choose: one asset per device,
	 * one per show, or one for everything. Per-device is the reason this is a list --
	 * an asset with its Default Device set names its hardware once at the top and every
	 * source below inherits it -- but nothing forces that, because a function driven
	 * from two controllers is better as one binding with two sources than as two
	 * bindings in two assets with their value settings kept in sync by hand.
	 *
	 * Soft paths with an AllowedClasses filter rather than typed pointers, because
	 * UOscuMIDIMap lives in OSCulatorMIDI and this settings object lives in
	 * OSCulatorCore -- a typed reference would make the dependency circular. The filter
	 * still gives a properly restricted asset picker in the details panel.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input",
		meta = (EditCondition = "bEnableMIDIIn", EditConditionHides,
				AllowedClasses = "/Script/OSCulatorMIDI.OscuMIDIMap"))
	TArray<FSoftObjectPath> MIDIMaps;

	/**
	 * Which octave number note 60 is called. 3 gives C3 = 60, matching Ableton and
	 * Logic; 4 gives C4 = 60, matching scientific pitch notation.
	 *
	 * The MIDI spec defines no octave naming at all, so this cannot be settled --
	 * only exposed.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input",
		meta = (EditCondition = "bEnableMIDIIn", EditConditionHides, ClampMin = "0", ClampMax = "5"))
	int32 MiddleCOctave = 3;

	/**
	 * Which kinds of message to listen for. Anything unticked is discarded at the
	 * port. See FOscuMIDIInputMessages.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input",
		meta = (EditCondition = "bEnableMIDIIn", EditConditionHides, DisplayName = "Listen For"))
	FOscuMIDIInputMessages MIDIInputMessages;

	/**
	 * Which MIDI channels to listen on, 1-16. Empty means all of them.
	 *
	 * The sharpest tool here, and the one to reach for first. A MIDI interface carries
	 * a whole rig, while a map usually answers to two or three channels; everything on
	 * the others is discarded at the port instead of taking a queue slot. Unlike the
	 * type filter it costs nothing you might want -- a channel you do not map is a
	 * channel you do not use.
	 *
	 * Learn ignores this while a row is armed, so a note can still be learned from a
	 * channel that is normally muted. It would be a long afternoon otherwise.
	 *
	 * Entries outside 1-16 are ignored with a log line, and a list containing nothing
	 * valid falls back to every channel: a new row starts at 0, and silently muting the
	 * device the moment one is added would look exactly like broken hardware.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input",
		meta = (EditCondition = "bEnableMIDIIn", EditConditionHides))
	TArray<int32> MIDIInputChannels;

	/**
	 * How many messages each input device can queue between frames.
	 *
	 * Sized in messages, not bytes. The default is what the engine's own MIDI plugin
	 * intends and then fails to pass on -- it hands PortMidi a zero and gets a much
	 * smaller fallback queue, which is why a busy device overflows within seconds of
	 * pressing Play. At a hundred messages a second, 1024 absorbs about ten seconds of
	 * stalled game thread; the fallback managed under three.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input",
		meta = (EditCondition = "bEnableMIDIIn", EditConditionHides, ClampMin = "64", ClampMax = "16384"))
	int32 MIDIInputQueueSize = 1024;

	// ---- MIDI Output ----

	/** Off by default; the transport lands with Phase 7. */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Output")
	bool bEnableMIDIOut = false;

	/** Every device to send to. A list for the same reason as the input side. */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Output",
		meta = (EditCondition = "bEnableMIDIOut", EditConditionHides))
	TArray<FString> MIDIOutputDeviceNames;
};
