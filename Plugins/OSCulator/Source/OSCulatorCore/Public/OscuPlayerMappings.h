// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FOscuPlayerMenuEntry;

/**
 * Which MIDI message a player override listens for (an input) or sends (an event
 * output). None leaves the project's own; Off, for outputs only, sends no MIDI at all.
 */
enum class EOscuPlayerMIDIKind : uint8
{
	None,
	Note,
	ControlChange,
	ProgramChange,
	Off,
};

/**
 * What the player changed about one menu entry.
 *
 * An empty OSCAddress keeps /<Tag>/<FunctionName>; MIDIKind None keeps whatever the
 * project's MIDI map assets bind. Learned MIDI inputs are device-agnostic: they fire
 * from any open device, so switching controllers in the menu does not orphan them.
 */
struct OSCULATORCORE_API FOscuPlayerOverride
{
	FName Tag;

	/** The function for an input, the event dispatcher for an output. */
	FName FunctionName;

	/** An event output (OSCulator sends) rather than an input (OSCulator receives). */
	bool bOutput = false;

	FString OSCAddress;

	EOscuPlayerMIDIKind MIDIKind = EOscuPlayerMIDIKind::None;
	int32 MIDIChannel = 1;
	int32 MIDINumber = 0;

	bool HasOSC() const { return !OSCAddress.IsEmpty(); }
	bool HasMIDI() const { return MIDIKind != EOscuPlayerMIDIKind::None; }
	bool IsEmpty() const { return !HasOSC() && !HasMIDI(); }
	bool Matches(FName InTag, FName InFunction, bool bInOutput = false) const
	{
		return Tag == InTag && FunctionName == InFunction && bOutput == bInOutput;
	}
};

/**
 * The player menu's remaps, and the one place they are saved.
 *
 * Two kinds of change, stored in two places:
 *   - Remaps (OSC address, MIDI input per function) go to
 *     Saved/OSCulator/PlayerMappings.json. The project's own mapping is never edited:
 *     OSC rewrites the address on arrival, MIDI works on transient copies of the maps.
 *   - The OSC listen port and MIDI input devices are endpoint settings, so they go to
 *     OSCulator.ini like any other edit of those (see OscuEndpointFile).
 *
 * In the editor, both are session-only unless Project Settings > Player Menu > Save
 * Changes Made In PIE is on: EndSession puts everything back when play stops.
 *
 * An override only applies while the menu is enabled and its function is still listed
 * there with that column allowed, so a file left over from an older build cannot keep
 * re-pointing something the menu no longer offers.
 *
 * Game thread only.
 */
class OSCULATORCORE_API FOscuPlayerMappings
{
public:
	static FOscuPlayerMappings& Get();

	/** Every stored override, applicable or not. */
	const TArray<FOscuPlayerOverride>& GetStored();

	/** The overrides that apply now, already filtered by the menu's entries. */
	TArray<FOscuPlayerOverride> GetActive();

	/** The stored override for one function (or, with bOutput, one event output), or null. */
	const FOscuPlayerOverride* Find(FName Tag, FName FunctionName, bool bOutput = false);

	/** Empty Address goes back to the default. */
	void SetOSCAddress(FName Tag, FName FunctionName, const FString& Address, bool bOutput = false);

	/** Kind None goes back to the default. Channel 1-16, Number 0-127. */
	void SetMIDI(FName Tag, FName FunctionName, EOscuPlayerMIDIKind Kind, int32 Channel, int32 Number, bool bOutput = false);

	/** The theme the player picked (an EOscuPlayerMenuTheme value), or INDEX_NONE for the project's. */
	int32 GetPlayerTheme();
	void SetPlayerTheme(int32 Theme);

	/** Every remap back to the project's. Endpoints are left as they are. */
	void ResetAll();

	/** Sets OSC Input's listen port and reopens the socket. False with a reason if it could not be saved. */
	bool SetListenPort(int32 Port, FString& OutError);

	/** Adds or removes one device from MIDI Input Devices and reopens the ports. */
	bool SetMIDIInputDeviceEnabled(const FString& DeviceName, bool bEnabled, FString& OutError);

	/**
	 * Points the first OSC target (the one the player menu shows) at Host:Port, adding
	 * one named "Player" if there are none.
	 */
	bool SetOSCOutTarget(const FString& Host, int32 Port, FString& OutError);

	/** Adds or removes one device from MIDI Output Devices and reopens the ports. */
	bool SetMIDIOutputDeviceEnabled(const FString& DeviceName, bool bEnabled, FString& OutError);

	/** Puts back a set taken from GetStored, without saving. For tests. */
	void RestoreStored(const TArray<FOscuPlayerOverride>& Stored);

	/** Play stopped in the editor: put back what the session changed, unless it is being saved. */
	void EndSession();

	/** False in the editor (PIE included) unless Save Changes Made In PIE is on. */
	static bool ShouldPersist();

	/** <Saved>/OSCulator/PlayerMappings.json. */
	static FString GetPath();

	/** "/tag/function", the address the router answers to by default. */
	static FString DefaultOSCAddress(FName Tag, FName FunctionName);

	/**
	 * The comparable form of an address: lower case, no trailing slash, and with
	 * everything after the tag joined by '_' as the router does (/a/b/c reaches b_c).
	 */
	static FString RoutingKey(const FString& Address);

	/** The comparable form of a player's address: lower case, no trailing slash. */
	static FString AliasKey(const FString& Address);

	/** Broadcast whenever a remap changes (not for endpoints: those use OnSettingsChanged). */
	DECLARE_MULTICAST_DELEGATE(FOnPlayerMappingsChanged);
	static FOnPlayerMappingsChanged OnChanged;

private:
	void EnsureLoaded();
	void Load();
	void Commit();
	FOscuPlayerOverride& FindOrAdd(FName Tag, FName FunctionName, bool bOutput);
	void TakeEndpointSnapshot();
	bool CommitEndpoints(FString& OutError);

	TArray<FOscuPlayerOverride> Overrides;
	int32 PlayerTheme = INDEX_NONE;
	bool bLoaded = false;

	/** Set by any change this session, so EndSession knows whether to undo anything. */
	bool bRemapsChangedThisSession = false;

	/** OSCulator.ini text from before the first endpoint change, when not persisting. */
	FString EndpointSnapshot;
	bool bHasEndpointSnapshot = false;
};
