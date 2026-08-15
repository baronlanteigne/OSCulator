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
	 * Which mapping asset is active. What it CONTAINS lives in the asset, so a
	 * different show is a different asset rather than a different build.
	 *
	 * A soft path with an AllowedClasses filter rather than a typed pointer,
	 * because UOscuMIDIMap lives in OSCulatorMIDI and this settings object lives in
	 * OSCulatorCore -- a typed reference would make the dependency circular. The
	 * filter still gives a properly restricted asset picker in the details panel.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Input",
		meta = (EditCondition = "bEnableMIDIIn", EditConditionHides,
				AllowedClasses = "/Script/OSCulatorMIDI.OscuMIDIMap"))
	FSoftObjectPath MIDIMap;

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

	// ---- MIDI Output ----

	/** Off by default; the transport lands with Phase 7. */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Output")
	bool bEnableMIDIOut = false;

	/** Every device to send to. A list for the same reason as the input side. */
	UPROPERTY(EditAnywhere, Config, Category = "MIDI Output",
		meta = (EditCondition = "bEnableMIDIOut", EditConditionHides))
	TArray<FString> MIDIOutputDeviceNames;
};
