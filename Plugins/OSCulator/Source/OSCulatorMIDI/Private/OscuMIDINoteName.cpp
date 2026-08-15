// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDINoteName.h"

namespace
{
	/** Semitones above C for each letter. Not evenly spaced -- there are no
	 *  black keys between E/F and B/C. */
	bool PitchClassForLetter(TCHAR Letter, int32& OutPitchClass)
	{
		switch (FChar::ToUpper(Letter))
		{
		case TEXT('C'): OutPitchClass = 0;  return true;
		case TEXT('D'): OutPitchClass = 2;  return true;
		case TEXT('E'): OutPitchClass = 4;  return true;
		case TEXT('F'): OutPitchClass = 5;  return true;
		case TEXT('G'): OutPitchClass = 7;  return true;
		case TEXT('A'): OutPitchClass = 9;  return true;
		case TEXT('B'): OutPitchClass = 11; return true;
		default: return false;
		}
	}

	const TCHAR* GSharpNames[12] =
	{
		TEXT("C"), TEXT("C#"), TEXT("D"), TEXT("D#"), TEXT("E"), TEXT("F"),
		TEXT("F#"), TEXT("G"), TEXT("G#"), TEXT("A"), TEXT("A#"), TEXT("B")
	};

	/** How many octaves to shift so that note 60 lands in MiddleCOctave. */
	int32 OctaveOffset(int32 MiddleCOctave)
	{
		return 5 - MiddleCOctave;
	}
}

namespace OscuMIDINoteName
{
	bool Parse(const FString& Text, int32 MiddleCOctave, uint8& OutNote, FString& OutError)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			OutError = TEXT("empty");
			return false;
		}

		// A bare number is the escape hatch for anyone who thinks in note numbers,
		// and is what the wire actually carries.
		if (Trimmed.IsNumeric())
		{
			const int32 AsInt = FCString::Atoi(*Trimmed);
			if (AsInt < 0 || AsInt > 127)
			{
				OutError = FString::Printf(TEXT("note %d is outside 0-127"), AsInt);
				return false;
			}
			OutNote = static_cast<uint8>(AsInt);
			return true;
		}

		int32 Index = 0;
		int32 PitchClass = 0;
		if (!PitchClassForLetter(Trimmed[Index], PitchClass))
		{
			OutError = FString::Printf(TEXT("'%s' does not start with a note letter A-G"), *Trimmed);
			return false;
		}
		++Index;

		// Only a lower-case 'b' is a flat. An upper-case 'B' here would be a second
		// note letter, which is meaningless.
		int32 Accidental = 0;
		if (Index < Trimmed.Len())
		{
			if (Trimmed[Index] == TEXT('#'))
			{
				Accidental = 1;
				++Index;
			}
			else if (Trimmed[Index] == TEXT('b'))
			{
				Accidental = -1;
				++Index;
			}
		}

		if (Index >= Trimmed.Len())
		{
			OutError = FString::Printf(TEXT("'%s' has no octave number"), *Trimmed);
			return false;
		}

		// The octave may be negative: with MiddleCOctave 3, note 0 is C-2.
		bool bNegative = false;
		if (Trimmed[Index] == TEXT('-'))
		{
			bNegative = true;
			++Index;
		}

		if (Index >= Trimmed.Len())
		{
			OutError = FString::Printf(TEXT("'%s' has no octave number"), *Trimmed);
			return false;
		}

		int32 Octave = 0;
		for (; Index < Trimmed.Len(); ++Index)
		{
			if (!FChar::IsDigit(Trimmed[Index]))
			{
				OutError = FString::Printf(TEXT("'%s' has trailing characters after the octave"), *Trimmed);
				return false;
			}
			Octave = Octave * 10 + (Trimmed[Index] - TEXT('0'));
		}
		if (bNegative)
		{
			Octave = -Octave;
		}

		const int32 Note = (Octave + OctaveOffset(MiddleCOctave)) * 12 + PitchClass + Accidental;
		if (Note < 0 || Note > 127)
		{
			OutError = FString::Printf(TEXT("'%s' resolves to %d, outside 0-127"), *Trimmed, Note);
			return false;
		}

		OutNote = static_cast<uint8>(Note);
		return true;
	}

	FString ToString(uint8 Note, int32 MiddleCOctave)
	{
		const int32 PitchClass = Note % 12;
		const int32 Octave = (Note / 12) - OctaveOffset(MiddleCOctave);
		return FString::Printf(TEXT("%s%d"), GSharpNames[PitchClass], Octave);
	}
}
