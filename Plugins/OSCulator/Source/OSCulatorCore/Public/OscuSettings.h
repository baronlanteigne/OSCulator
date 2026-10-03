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
 * To cut continuous controller traffic specifically, reach for a device's Ignored
 * Channels first: dropping a channel you do not map costs you nothing, while unticking
 * Control Change costs you every controller on every channel.
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

/**
 * One MIDI controller to listen to, and what to throw away from it.
 *
 * Both filters live here rather than once for the whole project, because both are
 * statements about a specific piece of hardware. A channel number means something
 * different on every box -- channel 10 is drums on one and a lighting desk on the next
 * -- and so does a message type: the sequencer flooding the wire with clock is one
 * device, while the knob box next to it sends nothing but control change and must keep
 * it. A project-wide setting could only ever be the intersection of what every device
 * happens to agree on, which means one noisy device drags the filter tighter for
 * everything else, or nothing gets filtered at all.
 *
 * PortMidi applies both per stream, so this costs nothing to do properly: the mask and
 * the filter were always per port, and only the settings pretended otherwise.
 */
USTRUCT()
struct OSCULATORCORE_API FOscuMIDIInputDevice
{
	GENERATED_BODY()

	/**
	 * The device name as the OS reports it. Run OSCulator.MIDIDevices to list them.
	 *
	 * By name, never by enumeration order: device exclusivity conflicts between plugins
	 * are real, and grabbing whatever happens to be first is how you end up fighting
	 * another plugin for a port.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input Device")
	FString Name;

	/**
	 * Which kinds of message to accept from this device. See FOscuMIDIInputMessages.
	 *
	 * Per device because the traffic is: untick Clock on the sequencer that floods the
	 * wire with it without taking Program Change away from the box that needs it. The
	 * defaults suit a controller -- everything playable ticked, clock and transport and
	 * sysex not -- so a device added and left alone behaves.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input Device",
		meta = (DisplayName = "Listen For"))
	FOscuMIDIInputMessages ListenFor;

	/**
	 * Channels to DISCARD from this device, 1-16. Empty -- the default -- keeps all of
	 * them.
	 *
	 * An exclusion list, not a selection: every device starts hearing everything it
	 * sends, and each entry here is a deliberate "I will never map this, stop paying for
	 * it". Discarded at the port by PortMidi's channel mask, so a muted channel never
	 * takes a queue slot and never reaches the game thread. That is the cheapest filter
	 * OSCulator has, and unlike Listen For it costs nothing you might want -- a channel
	 * you do not map is a channel you do not use.
	 *
	 * Exclusion is also the only direction that fails safely. A freshly added array row
	 * starts at 0, which is outside 1-16: here it means "ignore nothing yet" and the
	 * device keeps working, where a selection list read the same row as "listen to
	 * channel 0" and silently muted the hardware.
	 *
	 * Entries outside 1-16 are skipped with a log line. Listing all sixteen is taken at
	 * face value -- the device is opened and hears no channel messages at all -- but it
	 * is called out in the log, because at that point unticking the device is clearer.
	 *
	 * Learn lifts this entirely while a row is armed, so an input can still be taught
	 * from a channel that is normally muted. It would be a long afternoon otherwise.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input Device",
		meta = (DisplayName = "Ignored Channels"))
	TArray<int32> IgnoredChannels;
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
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "OSCulator"))
class OSCULATORCORE_API UOscuSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	static const UOscuSettings* Get();

	/**
	 * True when a resolved signature fact may be remembered instead of re-derived.
	 *
	 * The only reason to re-derive per message is that a Blueprint could be recompiled
	 * underneath us. A packaged build cannot recompile anything, and Performance Mode
	 * says you are not going to -- so both answers are the same question, asked once
	 * here rather than reasoned about separately at each call site.
	 */
	static bool ShouldCacheSignatureFacts()
	{
#if WITH_EDITOR
		return Get()->bPerformanceMode;
#else
		// Nothing can recompile in a cooked build, so re-resolving per message was only
		// ever waste there.
		return true;
#endif
	}

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

	// ---- Performance ----

	/**
	 * Switches off everything that exists only to help you AUTHOR the project.
	 *
	 * For running a show. The authoring features are already absent from a packaged
	 * build -- they live behind WITH_EDITOR -- but a show run FROM the editor, which is
	 * the normal way to do this, has every one of them live. This is the switch that
	 * says "I have finished editing; stop paying for the editing."
	 *
	 * What it turns off, exactly:
	 *
	 *   - MIDI Learn. It will not arm, and the per-message check that looks for an armed
	 *     row is skipped. Arming is refused with a log line rather than silently doing
	 *     nothing, because a Learn button that quietly stopped working would be a
	 *     miserable thing to debug.
	 *   - The Blueprint-recompile hooks that keep the registry's view of each class
	 *     fresh. Nothing recompiles during a show, and a cache rebuild triggered
	 *     mid-performance is a hitch nobody asked for.
	 *   - Per-message parameter-slot resolution. A binding that names a parameter
	 *     normally re-resolves the name against the live signature on EVERY message, so
	 *     that a recompile which reorders parameters is followed. With recompiles ruled
	 *     out, the answer is resolved once and remembered.
	 *
	 * What it does NOT touch: anything that affects what your show does. No filter, no
	 * channel, no mapping, no value shaping. Turning it on must never change a single
	 * value that reaches a function -- only how much work was done to get it there.
	 *
	 * Validate and Auto-Map are unaffected: they only run when their button is pressed,
	 * so they cost nothing during a show whatever this says.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Performance",
		meta = (DisplayName = "Performance Mode (disable authoring features)"))
	bool bPerformanceMode = false;

	// ---- MIDI Input ----

	/** Off by default; the transport lands with Phase 6. */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input")
	bool bEnableMIDIIn = false;

	/**
	 * Every controller to listen to, each with its own list of channels to ignore.
	 *
	 * A list because MIDI devices are opened individually -- unlike OSC input,
	 * where one socket already hears every sender. Events from all listed devices
	 * are merged into one stream, so two controllers sending the same channel and
	 * note both trigger the same mapping; a binding that must distinguish them names
	 * its Device.
	 *
	 * A device that is missing or already open is skipped with a log line rather
	 * than taking the others down with it.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input",
		meta = (EditCondition = "bEnableMIDIIn", EditConditionHides,
				TitleProperty = "Name"))
	TArray<FOscuMIDIInputDevice> MIDIInputDevices;

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
