// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UOscuSettings;

/** One Key=Value line of OSCulator.ini, with the line it came from for warnings. */
struct FOscuIniKey
{
	FString Key;
	FString Value;
	int32 Line = 0;
};

/** One [section] of OSCulator.ini, keys in file order. */
struct FOscuIniSection
{
	FString Name;
	int32 Line = 0;
	TArray<FOscuIniKey> Keys;
};

/**
 * OSCulator.ini: the endpoint settings (OSC in/out, targets, MIDI devices, Performance
 * Mode) as a file that can be edited without the editor, including next to a packaged
 * exe. Project Settings stays the in-memory truth; the file is applied over it at
 * launch and rewritten whenever Project Settings changes. See Docs/SETUP.md.
 */
namespace OscuEndpointFile
{
	/** Splits text into sections. Anything it cannot read becomes a warning, never an error. */
	OSCULATORCORE_API TArray<FOscuIniSection> Parse(const FString& Text, TArray<FString>& OutWarnings);

	/**
	 * The endpoint fields of Settings in OSCulator.ini form, comments included. Targets
	 * and devices with a blank or repeated name are left out (a section cannot hold
	 * them), each with a LogOSCulator warning.
	 */
	OSCULATORCORE_API FString Write(const UOscuSettings& Settings);

	/**
	 * Applies Text over Settings. Present lines win, missing lines keep the settings
	 * value, and [OSC Target] / [MIDI In <device>] sections replace those lists.
	 * A file with no sections at all (empty, truncated) is ignored with one warning.
	 * Returns one human-readable line per value that changed, in file order.
	 */
	OSCULATORCORE_API TArray<FString> Apply(const FString& Text, UOscuSettings& Settings, TArray<FString>& OutWarnings);

	/** Lines describing every difference between two files OSCulator wrote. */
	OSCULATORCORE_API TArray<FString> Diff(const FString& BeforeText, const FString& AfterText);

	/**
	 * Where OSCulator.ini lives. Uncooked (editor, -game): <Project>/OSCulator/.
	 * Cooked: next to the exe (<Package>/OSCulator.ini), falling back to
	 * <Package>/<Project>/OSCulator/ if the staging remap did not move it.
	 */
	OSCULATORCORE_API FString GetPath();

	/** Writes Settings to Path, creating the folder. False if the file cannot be written. */
	OSCULATORCORE_API bool Save(const UOscuSettings& Settings, const FString& Path);

	/**
	 * Missing file: written from Settings (bOutCreated). Present: applied over Settings,
	 * changes in OutOverrides, warnings logged. Only <Path>.tmp present (a save cut
	 * short): moved into place, then applied. OutStatus is the one-line summary; the
	 * caller decides whether it becomes DescribeLastLoad(). False only if the file
	 * could not be read or created.
	 */
	OSCULATORCORE_API bool LoadOrCreate(UOscuSettings& Settings, const FString& Path, TArray<FString>& OutOverrides, bool& bOutCreated, FString& OutStatus);

	/** Called once on the settings CDO, before any transport opens. */
	void LoadAtStartup(UOscuSettings& Settings);

	/** True in the editor proper: never in -game, packaged builds or commandlets. */
	bool ShouldSyncDefaultConfig();

	/** One line for OSCulator.Status: which file loaded and what it changed. */
	OSCULATORCORE_API FString DescribeLastLoad();
}
