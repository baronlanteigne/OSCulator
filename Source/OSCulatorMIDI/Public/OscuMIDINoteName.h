// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Note names in both directions.
 *
 * Note 60 is C3 in Ableton and Logic, and C4 in scientific pitch notation. The
 * MIDI specification defines no octave naming at all, so there is no correct
 * answer to pick -- MiddleCOctave exposes the choice instead. 3 gives C3 = 60.
 *
 * There is deliberately no 128-entry UENUM for this. Enum identifiers cannot
 * contain '#' or '-', so it would need 128 UMETA(DisplayName=...) lines to be
 * readable and would still present as an enormous dropdown.
 */
namespace OscuMIDINoteName
{
	/**
	 * Accepts either a bare integer 0-127, or a name: an A-G letter, an optional
	 * '#' or 'b', then an octave number that may be negative. "C3", "C#2", "Db2"
	 * and "61" are all valid.
	 *
	 * Returns false with a reason for anything unparseable or outside 0-127.
	 */
	OSCULATORMIDI_API bool Parse(const FString& Text, int32 MiddleCOctave, uint8& OutNote, FString& OutError);

	/**
	 * The inverse, normalised to sharps: 61 becomes "C#3" rather than "Db3".
	 *
	 * Used by Learn and auto-populate so a written-back name matches what Parse
	 * would read, and by the details panel so a row reads the same after editing.
	 */
	OSCULATORMIDI_API FString ToString(uint8 Note, int32 MiddleCOctave);
}
