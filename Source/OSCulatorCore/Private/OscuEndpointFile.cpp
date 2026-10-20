// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuEndpointFile.h"
#include "OSCulatorCore.h"
#include "OscuSettings.h"

namespace
{
	const TCHAR* const OSCTargetPrefix = TEXT("OSC Target ");
	const TCHAR* const MIDIInDevicePrefix = TEXT("MIDI In ");

	/** About the section as a whole: points at its [header] line. */
	void Warn(TArray<FString>& Warnings, const FOscuIniSection& Section, const FString& Message)
	{
		Warnings.Add(FString::Printf(TEXT("line %d [%s]: %s"), Section.Line, *Section.Name, *Message));
	}

	/** About one key: points at that key's own line. */
	void WarnAt(TArray<FString>& Warnings, const FOscuIniSection& Section, const FOscuIniKey& KV, const FString& Message)
	{
		Warnings.Add(FString::Printf(TEXT("line %d [%s]: %s"), KV.Line, *Section.Name, *Message));
	}

	void WarnBadValue(TArray<FString>& Warnings, const FOscuIniSection& Section, const FOscuIniKey& KV, const TCHAR* Expected)
	{
		WarnAt(Warnings, Section, KV, FString::Printf(TEXT("%s=%s is not %s; keeping the Project Settings value."), *KV.Key, *KV.Value, Expected));
	}

	bool ReadBool(const FString& Value, bool& Out)
	{
		if (Value == TEXT("True") || Value == TEXT("1")) { Out = true; return true; }
		if (Value == TEXT("False") || Value == TEXT("0")) { Out = false; return true; }
		return false;
	}

	/** Digits only: "1.5", "-3" and "8000abc" are all refused rather than half-read. */
	bool ReadInt(const FString& Value, int32 Min, int32 Max, int32& Out)
	{
		if (Value.IsEmpty() || Value.Len() > 9)
		{
			return false;
		}
		for (const TCHAR C : Value)
		{
			if (!FChar::IsDigit(C))
			{
				return false;
			}
		}
		const int32 Parsed = FCString::Atoi(*Value);
		if (Parsed < Min || Parsed > Max)
		{
			return false;
		}
		Out = Parsed;
		return true;
	}

	TArray<FString> ReadList(const FString& Value)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), /*InCullEmpty*/ true);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
		}
		Parts.RemoveAll([](const FString& Part) { return Part.IsEmpty(); });
		return Parts;
	}

	void ReadBoolKey(const FOscuIniSection& Section, const FOscuIniKey& KV, bool& Field, TArray<FString>& Warnings)
	{
		if (!ReadBool(KV.Value, Field))
		{
			WarnBadValue(Warnings, Section, KV, TEXT("True or False"));
		}
	}

	void ReadPortKey(const FOscuIniSection& Section, const FOscuIniKey& KV, int32& Field, TArray<FString>& Warnings)
	{
		if (!ReadInt(KV.Value, 1, 65535, Field))
		{
			WarnBadValue(Warnings, Section, KV, TEXT("a port from 1 to 65535"));
		}
	}

	void WarnUnknownKey(const FOscuIniSection& Section, const FOscuIniKey& KV, TArray<FString>& Warnings)
	{
		WarnAt(Warnings, Section, KV, FString::Printf(TEXT("unknown key '%s' ignored."), *KV.Key));
	}

	void ApplyOSCTarget(const FOscuIniSection& Section, FOscuOSCTarget& Target, TArray<FString>& Warnings)
	{
		for (const FOscuIniKey& KV : Section.Keys)
		{
			if (KV.Key == TEXT("Host"))         { Target.Host = KV.Value; }
			else if (KV.Key == TEXT("Port"))    { ReadPortKey(Section, KV, Target.Port, Warnings); }
			else if (KV.Key == TEXT("Enabled")) { ReadBoolKey(Section, KV, Target.bEnabled, Warnings); }
			else                                { WarnUnknownKey(Section, KV, Warnings); }
		}
	}

	void ApplyMIDIInDevice(const FOscuIniSection& Section, FOscuMIDIInputDevice& Device, TArray<FString>& Warnings)
	{
		FOscuMIDIInputMessages& L = Device.ListenFor;
		for (const FOscuIniKey& KV : Section.Keys)
		{
			const FString& Key = KV.Key;
			if (Key == TEXT("Notes"))              { ReadBoolKey(Section, KV, L.bNotes, Warnings); }
			else if (Key == TEXT("ControlChange")) { ReadBoolKey(Section, KV, L.bControlChange, Warnings); }
			else if (Key == TEXT("ProgramChange")) { ReadBoolKey(Section, KV, L.bProgramChange, Warnings); }
			else if (Key == TEXT("PitchBend"))     { ReadBoolKey(Section, KV, L.bPitchBend, Warnings); }
			else if (Key == TEXT("Aftertouch"))    { ReadBoolKey(Section, KV, L.bAftertouch, Warnings); }
			else if (Key == TEXT("Clock"))         { ReadBoolKey(Section, KV, L.bClock, Warnings); }
			else if (Key == TEXT("Transport"))     { ReadBoolKey(Section, KV, L.bTransport, Warnings); }
			else if (Key == TEXT("SysEx"))         { ReadBoolKey(Section, KV, L.bSysEx, Warnings); }
			else if (Key == TEXT("IgnoredChannels"))
			{
				TArray<int32> Channels;
				for (const FString& Entry : ReadList(KV.Value))
				{
					int32 Channel = 0;
					if (ReadInt(Entry, 1, 16, Channel))
					{
						Channels.Add(Channel);
					}
					else
					{
						WarnAt(Warnings, Section, KV, FString::Printf(TEXT("IgnoredChannels entry '%s' is not a channel from 1 to 16; skipped."), *Entry));
					}
				}
				Device.IgnoredChannels = MoveTemp(Channels);
			}
			else { WarnUnknownKey(Section, KV, Warnings); }
		}
	}

	/** "Section|Key" -> value, plus the section names and the keys in file order, for Diff. */
	void Flatten(const FString& Text, TMap<FString, FString>& OutValues, TArray<FString>& OutSections, TArray<FString>& OutKeyOrder)
	{
		TArray<FString> Ignored;
		for (const FOscuIniSection& Section : OscuEndpointFile::Parse(Text, Ignored))
		{
			OutSections.AddUnique(Section.Name);
			for (const FOscuIniKey& KV : Section.Keys)
			{
				const FString FlatKey = Section.Name + TEXT("|") + KV.Key;
				OutValues.Add(FlatKey, KV.Value);
				OutKeyOrder.AddUnique(FlatKey);
			}
		}
	}
}

TArray<FOscuIniSection> OscuEndpointFile::Parse(const FString& Text, TArray<FString>& OutWarnings)
{
	TArray<FOscuIniSection> Sections;

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const int32 LineNumber = Index + 1;
		const FString Line = Lines[Index].TrimStartAndEnd();

		if (Line.IsEmpty() || Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("#")))
		{
			continue;
		}

		if (Line.StartsWith(TEXT("[")))
		{
			if (!Line.EndsWith(TEXT("]")))
			{
				OutWarnings.Add(FString::Printf(TEXT("line %d: '%s' has no closing ']'; ignored."), LineNumber, *Line));
				continue;
			}
			FOscuIniSection& Section = Sections.AddDefaulted_GetRef();
			Section.Name = Line.Mid(1, Line.Len() - 2).TrimStartAndEnd();
			Section.Line = LineNumber;
			continue;
		}

		int32 Equals = INDEX_NONE;
		if (!Line.FindChar(TEXT('='), Equals))
		{
			OutWarnings.Add(FString::Printf(TEXT("line %d: '%s' is not Key=Value; ignored."), LineNumber, *Line));
			continue;
		}
		if (Sections.Num() == 0)
		{
			OutWarnings.Add(FString::Printf(TEXT("line %d: '%s' is outside any [section]; ignored."), LineNumber, *Line));
			continue;
		}
		FOscuIniKey& KV = Sections.Last().Keys.AddDefaulted_GetRef();
		KV.Key = Line.Left(Equals).TrimEnd();
		KV.Value = Line.Mid(Equals + 1).TrimStart();
		KV.Line = LineNumber;
	}

	return Sections;
}

FString OscuEndpointFile::Write(const UOscuSettings& S)
{
	FString Out;
	auto L = [&Out](const FString& Line) { Out += Line; Out += LINE_TERMINATOR; };
	auto B = [](bool bValue) { return bValue ? TEXT("True") : TEXT("False"); };

	L(TEXT("; OSCulator endpoints. Values here override Project Settings at launch."));
	L(TEXT("; After editing: restart, or run OSCulator.ReloadConfig."));
	L(TEXT("; Each [OSC Target ...] and [MIDI In <device>] section is one entry: delete the"));
	L(TEXT("; section to remove it, or set Enabled=False to mute it."));
	L(TEXT("; OSCulator rewrites this file when Project Settings change: own comments are lost."));

	L(TEXT(""));
	L(TEXT("[OSC In]"));
	L(FString::Printf(TEXT("Enabled=%s"), B(S.bEnableOSCIn)));
	L(FString::Printf(TEXT("BindAddress=%s"), *S.BindAddress));
	L(FString::Printf(TEXT("ListenPort=%d"), S.ListenPort));
	L(TEXT("; Comma-separated. Empty = accept every sender."));
	L(FString::Printf(TEXT("AllowedSenderIPs=%s"), *FString::Join(S.AllowedSenderIPs, TEXT(","))));

	L(TEXT(""));
	L(TEXT("[OSC Out]"));
	L(FString::Printf(TEXT("Enabled=%s"), B(S.bEnableOSCOut)));

	// A section header is the only place a name lives, so an entry with no name, or a
	// name an earlier entry already took, cannot be written without losing or merging it.
	// Compared trimmed and case-insensitively, the way Apply reads the names back.
	TArray<FString> WrittenTargets;
	for (int32 Index = 0; Index < S.OSCTargets.Num(); ++Index)
	{
		const FOscuOSCTarget& Target = S.OSCTargets[Index];
		const FString Name = Target.Name.IsNone() ? FString() : Target.Name.ToString().TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			UE_LOG(LogOSCulator, Warning, TEXT("OSCulator.ini: OSC target #%d not written: its name is blank."), Index);
			continue;
		}
		if (WrittenTargets.Contains(Name))
		{
			UE_LOG(LogOSCulator, Warning, TEXT("OSCulator.ini: OSC target #%d '%s' not written: its name repeats an earlier target."), Index, *Name);
			continue;
		}
		WrittenTargets.Add(Name);

		L(TEXT(""));
		L(FString::Printf(TEXT("[%s%s]"), OSCTargetPrefix, *Name));
		L(FString::Printf(TEXT("Host=%s"), *Target.Host));
		L(FString::Printf(TEXT("Port=%d"), Target.Port));
		L(FString::Printf(TEXT("Enabled=%s"), B(Target.bEnabled)));
	}

	L(TEXT(""));
	L(TEXT("[MIDI In]"));
	L(FString::Printf(TEXT("Enabled=%s"), B(S.bEnableMIDIIn)));

	TArray<FString> WrittenDevices;
	for (int32 Index = 0; Index < S.MIDIInputDevices.Num(); ++Index)
	{
		const FOscuMIDIInputDevice& Device = S.MIDIInputDevices[Index];
		const FString Name = Device.Name.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			UE_LOG(LogOSCulator, Warning, TEXT("OSCulator.ini: MIDI In device #%d not written: its name is blank."), Index);
			continue;
		}
		if (WrittenDevices.Contains(Name))
		{
			UE_LOG(LogOSCulator, Warning, TEXT("OSCulator.ini: MIDI In device #%d '%s' not written: its name repeats an earlier device."), Index, *Name);
			continue;
		}
		WrittenDevices.Add(Name);

		const FOscuMIDIInputMessages& M = Device.ListenFor;
		L(TEXT(""));
		L(FString::Printf(TEXT("[%s%s]"), MIDIInDevicePrefix, *Name));
		L(FString::Printf(TEXT("Notes=%s"), B(M.bNotes)));
		L(FString::Printf(TEXT("ControlChange=%s"), B(M.bControlChange)));
		L(FString::Printf(TEXT("ProgramChange=%s"), B(M.bProgramChange)));
		L(FString::Printf(TEXT("PitchBend=%s"), B(M.bPitchBend)));
		L(FString::Printf(TEXT("Aftertouch=%s"), B(M.bAftertouch)));
		L(FString::Printf(TEXT("Clock=%s"), B(M.bClock)));
		L(FString::Printf(TEXT("Transport=%s"), B(M.bTransport)));
		L(FString::Printf(TEXT("SysEx=%s"), B(M.bSysEx)));
		L(TEXT("; Channels 1-16 to discard, comma-separated."));
		TArray<FString> Channels;
		for (const int32 Channel : Device.IgnoredChannels)
		{
			Channels.Add(FString::FromInt(Channel));
		}
		L(FString::Printf(TEXT("IgnoredChannels=%s"), *FString::Join(Channels, TEXT(","))));
	}

	L(TEXT(""));
	L(TEXT("[MIDI Out]"));
	L(FString::Printf(TEXT("Enabled=%s"), B(S.bEnableMIDIOut)));
	L(TEXT("; Comma-separated device names."));
	L(FString::Printf(TEXT("Devices=%s"), *FString::Join(S.MIDIOutputDeviceNames, TEXT(","))));

	L(TEXT(""));
	L(TEXT("[Performance]"));
	L(FString::Printf(TEXT("PerformanceMode=%s"), B(S.bPerformanceMode)));

	return Out;
}

TArray<FString> OscuEndpointFile::Apply(const FString& Text, UOscuSettings& S, TArray<FString>& OutWarnings)
{
	const TArray<FOscuIniSection> Sections = Parse(Text, OutWarnings);
	if (Sections.Num() == 0)
	{
		// Empty or cut short: applying it would empty the target and device lists.
		OutWarnings.Add(TEXT("OSCulator.ini has no [sections]; ignored, Project Settings kept."));
		return {};
	}

	const FString Before = Write(S);

	// Sections are the list: rebuilt in file order, each starting from the existing
	// entry of the same name so a missing line keeps its Project Settings value.
	TArray<FOscuOSCTarget> Targets;
	TArray<FOscuMIDIInputDevice> Devices;
	TSet<FString> SeenSections;

	for (const FOscuIniSection& Section : Sections)
	{
		bool bAlreadyInSet = false;
		SeenSections.Add(Section.Name, &bAlreadyInSet);
		if (bAlreadyInSet)
		{
			Warn(OutWarnings, Section, TEXT("appears more than once; later values win."));
		}

		if (Section.Name == TEXT("OSC In"))
		{
			for (const FOscuIniKey& KV : Section.Keys)
			{
				if (KV.Key == TEXT("Enabled"))               { ReadBoolKey(Section, KV, S.bEnableOSCIn, OutWarnings); }
				else if (KV.Key == TEXT("BindAddress"))      { S.BindAddress = KV.Value; }
				else if (KV.Key == TEXT("ListenPort"))       { ReadPortKey(Section, KV, S.ListenPort, OutWarnings); }
				else if (KV.Key == TEXT("AllowedSenderIPs")) { S.AllowedSenderIPs = ReadList(KV.Value); }
				else                                         { WarnUnknownKey(Section, KV, OutWarnings); }
			}
		}
		else if (Section.Name == TEXT("OSC Out"))
		{
			for (const FOscuIniKey& KV : Section.Keys)
			{
				if (KV.Key == TEXT("Enabled")) { ReadBoolKey(Section, KV, S.bEnableOSCOut, OutWarnings); }
				else                           { WarnUnknownKey(Section, KV, OutWarnings); }
			}
		}
		else if (Section.Name == TEXT("MIDI In"))
		{
			for (const FOscuIniKey& KV : Section.Keys)
			{
				if (KV.Key == TEXT("Enabled")) { ReadBoolKey(Section, KV, S.bEnableMIDIIn, OutWarnings); }
				else                           { WarnUnknownKey(Section, KV, OutWarnings); }
			}
		}
		else if (Section.Name == TEXT("MIDI Out"))
		{
			for (const FOscuIniKey& KV : Section.Keys)
			{
				if (KV.Key == TEXT("Enabled"))      { ReadBoolKey(Section, KV, S.bEnableMIDIOut, OutWarnings); }
				else if (KV.Key == TEXT("Devices")) { S.MIDIOutputDeviceNames = ReadList(KV.Value); }
				else                                { WarnUnknownKey(Section, KV, OutWarnings); }
			}
		}
		else if (Section.Name == TEXT("Performance"))
		{
			for (const FOscuIniKey& KV : Section.Keys)
			{
				if (KV.Key == TEXT("PerformanceMode")) { ReadBoolKey(Section, KV, S.bPerformanceMode, OutWarnings); }
				else                                   { WarnUnknownKey(Section, KV, OutWarnings); }
			}
		}
		else if (Section.Name.StartsWith(OSCTargetPrefix))
		{
			const FName Name(*Section.Name.Mid(FCString::Strlen(OSCTargetPrefix)).TrimStartAndEnd());
			// FName == here, like FString == for devices below, ignores case on purpose: [osc target totd] is toTD.
			auto SameName = [Name](const FOscuOSCTarget& T) { return T.Name == Name; };

			FOscuOSCTarget* Target = Targets.FindByPredicate(SameName);
			if (Target == nullptr)
			{
				const FOscuOSCTarget* Existing = S.OSCTargets.FindByPredicate(SameName);
				Target = &Targets.Add_GetRef(Existing ? *Existing : FOscuOSCTarget());
				Target->Name = Name;
			}
			ApplyOSCTarget(Section, *Target, OutWarnings);
		}
		else if (Section.Name.StartsWith(MIDIInDevicePrefix))
		{
			const FString Name = Section.Name.Mid(FCString::Strlen(MIDIInDevicePrefix)).TrimStartAndEnd();
			auto SameName = [&Name](const FOscuMIDIInputDevice& D) { return D.Name == Name; };

			FOscuMIDIInputDevice* Device = Devices.FindByPredicate(SameName);
			if (Device == nullptr)
			{
				const FOscuMIDIInputDevice* Existing = S.MIDIInputDevices.FindByPredicate(SameName);
				Device = &Devices.Add_GetRef(Existing ? *Existing : FOscuMIDIInputDevice());
				Device->Name = Name;
			}
			ApplyMIDIInDevice(Section, *Device, OutWarnings);
		}
		else
		{
			Warn(OutWarnings, Section, TEXT("unknown section ignored."));
		}
	}

	S.OSCTargets = MoveTemp(Targets);
	S.MIDIInputDevices = MoveTemp(Devices);

	return Diff(Before, Write(S));
}

TArray<FString> OscuEndpointFile::Diff(const FString& BeforeText, const FString& AfterText)
{
	TMap<FString, FString> Before, After;
	TArray<FString> BeforeSections, AfterSections, BeforeOrder, AfterOrder;
	Flatten(BeforeText, Before, BeforeSections, BeforeOrder);
	Flatten(AfterText, After, AfterSections, AfterOrder);

	TArray<FString> Lines;
	for (const FString& Section : BeforeSections)
	{
		if (!AfterSections.Contains(Section))
		{
			Lines.Add(FString::Printf(TEXT("[%s] removed"), *Section));
		}
	}
	for (const FString& Section : AfterSections)
	{
		if (!BeforeSections.Contains(Section))
		{
			Lines.Add(FString::Printf(TEXT("[%s] added"), *Section));
		}
	}
	// Walk the keys in file order: TMap order is not something to rely on.
	for (const FString& FlatKey : AfterOrder)
	{
		const FString& New = After.FindChecked(FlatKey);
		const FString* Old = Before.Find(FlatKey);
		if (Old != nullptr && !Old->Equals(New, ESearchCase::CaseSensitive))
		{
			FString Section, Key;
			FlatKey.Split(TEXT("|"), &Section, &Key);
			Lines.Add(FString::Printf(TEXT("[%s] %s: %s -> %s"), *Section, *Key, **Old, *New));
		}
	}
	return Lines;
}
