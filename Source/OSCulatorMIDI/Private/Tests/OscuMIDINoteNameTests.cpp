// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuMIDINoteName.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace OscuMIDINameTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuMIDINoteNameTest,
	"OSCulator.MIDI.NoteNames",
	OscuMIDINameTest::Flags)

bool FOscuMIDINoteNameTest::RunTest(const FString& Parameters)
{
	auto ExpectNote = [this](const TCHAR* Text, int32 MiddleC, int32 Expected)
	{
		uint8 Note = 0;
		FString Error;
		if (!OscuMIDINoteName::Parse(Text, MiddleC, Note, Error))
		{
			AddError(FString::Printf(TEXT("'%s' (middle C = %d) failed to parse: %s"), Text, MiddleC, *Error));
			return;
		}
		TestEqual(FString::Printf(TEXT("'%s' with middle C = %d"), Text, MiddleC), static_cast<int32>(Note), Expected);
	};

	auto ExpectRejected = [this](const TCHAR* Text, int32 MiddleC)
	{
		uint8 Note = 0;
		FString Error;
		const bool bParsed = OscuMIDINoteName::Parse(Text, MiddleC, Note, Error);
		TestFalse(FString::Printf(TEXT("'%s' is rejected"), Text), bParsed);
		if (!bParsed)
		{
			TestFalse(FString::Printf(TEXT("'%s' rejection carries a reason"), Text), Error.IsEmpty());
		}
	};

	// The three anchors from the spec, all with middle C = 3 (Ableton, Logic).
	ExpectNote(TEXT("C3"), 3, 60);
	ExpectNote(TEXT("C-2"), 3, 0);
	ExpectNote(TEXT("G8"), 3, 127);

	// Scientific pitch notation puts the same note an octave up in NAME only.
	ExpectNote(TEXT("C4"), 4, 60);
	ExpectNote(TEXT("C-1"), 4, 0);

	// Accidentals, including the two spellings of the same key.
	ExpectNote(TEXT("C#3"), 3, 61);
	ExpectNote(TEXT("Db3"), 3, 61);
	ExpectNote(TEXT("B2"), 3, 59);
	ExpectNote(TEXT("Bb2"), 3, 58);

	// Letters are not evenly spaced -- no black key between E/F or B/C.
	ExpectNote(TEXT("E3"), 3, 64);
	ExpectNote(TEXT("F3"), 3, 65);

	// Case-insensitive on the letter, and whitespace-tolerant.
	ExpectNote(TEXT("c3"), 3, 60);
	ExpectNote(TEXT("  A3  "), 3, 69);

	// A bare number is the escape hatch, and is what the wire actually carries.
	ExpectNote(TEXT("60"), 3, 60);
	ExpectNote(TEXT("0"), 3, 0);
	ExpectNote(TEXT("127"), 3, 127);

	ExpectRejected(TEXT(""), 3);
	ExpectRejected(TEXT("H3"), 3);          // not a note letter
	ExpectRejected(TEXT("C"), 3);           // no octave
	ExpectRejected(TEXT("C#"), 3);          // accidental but no octave
	ExpectRejected(TEXT("C3x"), 3);         // trailing junk
	ExpectRejected(TEXT("128"), 3);         // out of range
	ExpectRejected(TEXT("-1"), 3);          // out of range
	ExpectRejected(TEXT("C9"), 3);          // resolves to 132
	ExpectRejected(TEXT("C-3"), 3);         // resolves to -12

	// The inverse, normalised to sharps.
	TestEqual(TEXT("60 -> C3"), OscuMIDINoteName::ToString(60, 3), FString(TEXT("C3")));
	TestEqual(TEXT("61 -> C#3 (not Db3)"), OscuMIDINoteName::ToString(61, 3), FString(TEXT("C#3")));
	TestEqual(TEXT("0 -> C-2"), OscuMIDINoteName::ToString(0, 3), FString(TEXT("C-2")));
	TestEqual(TEXT("127 -> G8"), OscuMIDINoteName::ToString(127, 3), FString(TEXT("G8")));
	TestEqual(TEXT("60 -> C4 in scientific pitch"), OscuMIDINoteName::ToString(60, 4), FString(TEXT("C4")));

	// Round trip every note, in both octave conventions. This is the assertion that
	// actually protects the pair -- a shared off-by-one would survive spot checks.
	for (int32 MiddleC = 0; MiddleC <= 5; ++MiddleC)
	{
		for (int32 RawNote = 0; RawNote <= 127; ++RawNote)
		{
			const FString Name = OscuMIDINoteName::ToString(static_cast<uint8>(RawNote), MiddleC);

			uint8 Parsed = 0;
			FString Error;
			if (!OscuMIDINoteName::Parse(Name, MiddleC, Parsed, Error))
			{
				AddError(FString::Printf(TEXT("Note %d formatted as '%s' (middle C = %d) but would not parse back: %s"),
					RawNote, *Name, MiddleC, *Error));
				continue;
			}
			if (Parsed != RawNote)
			{
				AddError(FString::Printf(TEXT("Note %d formatted as '%s' (middle C = %d) parsed back as %d"),
					RawNote, *Name, MiddleC, Parsed));
			}
		}
	}

	// Flats must land on the same key as their sharp spelling, everywhere they exist.
	for (int32 RawNote = 1; RawNote <= 127; ++RawNote)
	{
		const FString Sharp = OscuMIDINoteName::ToString(static_cast<uint8>(RawNote), 3);
		if (!Sharp.Contains(TEXT("#")))
		{
			continue;
		}

		// C#3 is the same key as Db3: take the next letter up and flatten it.
		const FString Natural = OscuMIDINoteName::ToString(static_cast<uint8>(RawNote + 1), 3);
		if (Natural.Contains(TEXT("#")))
		{
			continue;
		}

		const FString Flat = Natural.Left(1) + TEXT("b") + Natural.RightChop(1);

		uint8 FromFlat = 0;
		FString Error;
		if (OscuMIDINoteName::Parse(Flat, 3, FromFlat, Error))
		{
			TestEqual(FString::Printf(TEXT("%s and %s are the same key"), *Sharp, *Flat),
				static_cast<int32>(FromFlat), RawNote);
		}
		else
		{
			AddError(FString::Printf(TEXT("Flat spelling '%s' would not parse: %s"), *Flat, *Error));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
