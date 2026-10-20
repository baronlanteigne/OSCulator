// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UOscuSettings;

/** One [section] of OSCulator.ini, keys in file order. */
struct FOscuIniSection
{
	FString Name;
	int32 Line = 0;
	TArray<TPair<FString, FString>> Keys;
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

	/** The endpoint fields of Settings in OSCulator.ini form, comments included. */
	OSCULATORCORE_API FString Write(const UOscuSettings& Settings);

	/**
	 * Applies Text over Settings. Present lines win, missing lines keep the settings
	 * value, and [OSC Target] / [MIDI In <device>] sections replace those lists.
	 * Returns one human-readable line per value that changed.
	 */
	OSCULATORCORE_API TArray<FString> Apply(const FString& Text, UOscuSettings& Settings, TArray<FString>& OutWarnings);

	/** Lines describing every difference between two files OSCulator wrote. */
	OSCULATORCORE_API TArray<FString> Diff(const FString& BeforeText, const FString& AfterText);
}
