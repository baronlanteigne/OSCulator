// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuEndpointFile.h"
#include "OscuSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/OscuTestWorld.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace OscuEndpointFileTest
{
	/**
	 * A settings object that is NOT the CDO, filled with known values, so no test
	 * reads or writes the project's real configuration.
	 */
	UOscuSettings* MakeSettings()
	{
		UOscuSettings* S = NewObject<UOscuSettings>(GetTransientPackage());

		S->bEnableOSCIn = true;
		S->BindAddress = TEXT("0.0.0.0");
		S->ListenPort = 8000;
		S->AllowedSenderIPs.Reset();

		S->bEnableOSCOut = true;
		S->OSCTargets.Reset();
		FOscuOSCTarget TD;
		TD.Name = FName(TEXT("toTD"));
		TD.Host = TEXT("127.0.0.1");
		TD.Port = 8008;
		S->OSCTargets.Add(TD);
		FOscuOSCTarget Pad;
		Pad.Name = FName(TEXT("toiPad"));
		Pad.Host = TEXT("192.168.8.150");
		Pad.Port = 8001;
		S->OSCTargets.Add(Pad);

		S->bEnableMIDIIn = true;
		S->MIDIInputDevices.Reset();
		FOscuMIDIInputDevice Device;
		Device.Name = TEXT("rtpMIDI");
		Device.IgnoredChannels = { 10, 16 };
		S->MIDIInputDevices.Add(Device);

		S->bEnableMIDIOut = true;
		S->MIDIOutputDeviceNames = { TEXT("rtpMIDI") };

		S->bPerformanceMode = false;
		return S;
	}

	FString TempPath()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("OSCulatorTests"), TEXT("OSCulator.ini")));
	}
}

//////////////////////////////////////////////////////////////////////////
// Settings -> file -> settings loses nothing

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointRoundTripTest,
	"OSCulator.EndpointFile.RoundTrip",
	OscuTest::Flags)

bool FOscuEndpointRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	const FString Text = OscuEndpointFile::Write(*MakeSettings());

	// Start from something different on every field, so a field Apply forgets shows up.
	UOscuSettings* Other = NewObject<UOscuSettings>(GetTransientPackage());
	Other->bEnableOSCIn = false;
	Other->BindAddress = TEXT("127.0.0.1");
	Other->ListenPort = 9999;
	Other->AllowedSenderIPs = { TEXT("10.0.0.1") };
	Other->bEnableOSCOut = false;
	Other->OSCTargets.Reset();
	Other->bEnableMIDIIn = false;
	Other->MIDIInputDevices.Reset();
	Other->bEnableMIDIOut = false;
	Other->MIDIOutputDeviceNames.Reset();
	Other->bPerformanceMode = true;

	TArray<FString> Warnings;
	OscuEndpointFile::Apply(Text, *Other, Warnings);

	TestEqual(TEXT("No warnings for a file OSCulator wrote itself"), Warnings.Num(), 0);
	TestEqual(TEXT("Writing the applied settings gives back the same file"), OscuEndpointFile::Write(*Other), Text);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// A changed value is applied and reported

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointOverrideTest,
	"OSCulator.EndpointFile.Override",
	OscuTest::Flags)

bool FOscuEndpointOverrideTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	UOscuSettings* S = MakeSettings();
	const FString Text = OscuEndpointFile::Write(*S).Replace(TEXT("Port=8008"), TEXT("Port=8010"));

	TArray<FString> Warnings;
	const TArray<FString> Overrides = OscuEndpointFile::Apply(Text, *S, Warnings);

	TestEqual(TEXT("toTD port taken from the file"), S->OSCTargets[0].Port, 8010);
	TestEqual(TEXT("Exactly one override"), Overrides.Num(), 1);
	if (Overrides.Num() == 1)
	{
		TestEqual(TEXT("Override wording"), Overrides[0], FString(TEXT("[OSC Target toTD] Port: 8008 -> 8010")));
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
// A missing line keeps the settings value

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointMissingLineTest,
	"OSCulator.EndpointFile.MissingLineKeepsSetting",
	OscuTest::Flags)

bool FOscuEndpointMissingLineTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	UOscuSettings* S = MakeSettings();
	const FString Text = OscuEndpointFile::Write(*S).Replace(TEXT("ListenPort=8000"), TEXT(""));
	S->ListenPort = 8123;

	TArray<FString> Warnings;
	const TArray<FString> Overrides = OscuEndpointFile::Apply(Text, *S, Warnings);

	TestEqual(TEXT("ListenPort kept from settings"), S->ListenPort, 8123);
	TestEqual(TEXT("Nothing overridden"), Overrides.Num(), 0);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// A bad value is skipped, the rest still loads

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointBadValueTest,
	"OSCulator.EndpointFile.BadValueSkipped",
	OscuTest::Flags)

bool FOscuEndpointBadValueTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	UOscuSettings* S = MakeSettings();
	const FString Text = OscuEndpointFile::Write(*S)
		.Replace(TEXT("ListenPort=8000"), TEXT("ListenPort=70000"))
		.Replace(TEXT("Port=8008"), TEXT("Port=abc"))
		.Replace(TEXT("192.168.8.150"), TEXT("10.0.0.5"));

	TArray<FString> Warnings;
	OscuEndpointFile::Apply(Text, *S, Warnings);

	TestEqual(TEXT("Out-of-range ListenPort ignored"), S->ListenPort, 8000);
	TestEqual(TEXT("Non-numeric port ignored"), S->OSCTargets[0].Port, 8008);
	TestEqual(TEXT("A later good value still loads"), S->OSCTargets[1].Host, FString(TEXT("10.0.0.5")));
	TestEqual(TEXT("One warning per bad value"), Warnings.Num(), 2);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Sections are the list

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointDeletedSectionTest,
	"OSCulator.EndpointFile.DeletedSectionRemovesTarget",
	OscuTest::Flags)

bool FOscuEndpointDeletedSectionTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	UOscuSettings* WithoutPad = MakeSettings();
	WithoutPad->OSCTargets.RemoveAt(1);
	const FString Text = OscuEndpointFile::Write(*WithoutPad);

	UOscuSettings* S = MakeSettings();
	TArray<FString> Warnings;
	const TArray<FString> Overrides = OscuEndpointFile::Apply(Text, *S, Warnings);

	TestEqual(TEXT("Only toTD left"), S->OSCTargets.Num(), 1);
	TestTrue(TEXT("Removal is reported"), Overrides.Contains(FString(TEXT("[OSC Target toiPad] removed"))));
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Typos are called out, not silently ignored

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointUnknownTest,
	"OSCulator.EndpointFile.UnknownSectionsAndKeysWarn",
	OscuTest::Flags)

bool FOscuEndpointUnknownTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	UOscuSettings* S = MakeSettings();
	const FString Text = OscuEndpointFile::Write(*S)
		+ TEXT("\n[OSC Taget toX]\nHost=1.2.3.4\n")
		+ TEXT("\n[OSC In]\nListenPrt=9\n");

	TArray<FString> Warnings;
	OscuEndpointFile::Apply(Text, *S, Warnings);

	TestEqual(TEXT("The misspelled section added no target"), S->OSCTargets.Num(), 2);
	TestTrue(TEXT("Unknown section warned"),
		Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("OSC Taget toX")); }));
	TestTrue(TEXT("Unknown key warned"),
		Warnings.ContainsByPredicate([](const FString& W) { return W.Contains(TEXT("ListenPrt")); }));
	return true;
}

//////////////////////////////////////////////////////////////////////////
// An empty or truncated file wipes nothing

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointEmptyFileTest,
	"OSCulator.EndpointFile.EmptyFileKeepsSettings",
	OscuTest::Flags)

bool FOscuEndpointEmptyFileTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	UOscuSettings* S = MakeSettings();
	TArray<FString> Warnings;
	const TArray<FString> Overrides = OscuEndpointFile::Apply(TEXT(""), *S, Warnings);

	TestEqual(TEXT("Targets kept"), S->OSCTargets.Num(), 2);
	TestEqual(TEXT("MIDI In devices kept"), S->MIDIInputDevices.Num(), 1);
	TestEqual(TEXT("One warning"), Warnings.Num(), 1);
	TestEqual(TEXT("Nothing overridden"), Overrides.Num(), 0);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Entries the file cannot name are left out of it, loudly

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointBlankAndDuplicateNamesTest,
	"OSCulator.EndpointFile.BlankAndDuplicateNamesSkipped",
	OscuTest::Flags)

bool FOscuEndpointBlankAndDuplicateNamesTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	UOscuSettings* S = MakeSettings();
	FOscuOSCTarget Duplicate;
	Duplicate.Name = FName(TEXT("toTD"));
	Duplicate.Port = 9100;
	S->OSCTargets.Add(Duplicate);
	S->OSCTargets.Add(FOscuOSCTarget()); // Name is None
	FOscuMIDIInputDevice Blank;
	Blank.Name = TEXT("");
	S->MIDIInputDevices.Add(Blank);

	AddExpectedMessagePlain(TEXT("repeats an earlier"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("name is blank"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	const FString Text = OscuEndpointFile::Write(*S);

	const FString TDHeader = TEXT("[OSC Target toTD]");
	const int32 First = Text.Find(TDHeader);
	TestTrue(TEXT("toTD written once"), First != INDEX_NONE
		&& Text.Find(TDHeader, ESearchCase::IgnoreCase, ESearchDir::FromStart, First + TDHeader.Len()) == INDEX_NONE);
	TestFalse(TEXT("No None target"), Text.Contains(TEXT("[OSC Target None]")));
	TestFalse(TEXT("No blank device"), Text.Contains(TEXT("[MIDI In ]")));

	TArray<FString> Warnings;
	OscuEndpointFile::Apply(Text, *MakeSettings(), Warnings);
	TestEqual(TEXT("The written file reads back without warnings"), Warnings.Num(), 0);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Warnings point at the offending line, not its section header

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointKeyLineTest,
	"OSCulator.EndpointFile.WarningNamesKeyLine",
	OscuTest::Flags)

bool FOscuEndpointKeyLineTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	UOscuSettings* S = MakeSettings();
	const FString Text = OscuEndpointFile::Write(*S).Replace(TEXT("Port=8008"), TEXT("Port=abc"));

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, /*InCullEmpty*/ false);
	const int32 BadLine = Lines.IndexOfByKey(FString(TEXT("Port=abc"))) + 1;

	TArray<FString> Warnings;
	OscuEndpointFile::Apply(Text, *S, Warnings);

	TestEqual(TEXT("One warning"), Warnings.Num(), 1);
	if (Warnings.Num() == 1)
	{
		const FString Expected = FString::Printf(TEXT("line %d [OSC Target toTD]"), BadLine);
		TestTrue(FString::Printf(TEXT("'%s' starts with '%s'"), *Warnings[0], *Expected), Warnings[0].StartsWith(Expected));
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Changes are reported in file order

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointDiffOrderTest,
	"OSCulator.EndpointFile.DiffInFileOrder",
	OscuTest::Flags)

bool FOscuEndpointDiffOrderTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	UOscuSettings* S = MakeSettings();
	const FString Text = OscuEndpointFile::Write(*S)
		.Replace(TEXT("PerformanceMode=False"), TEXT("PerformanceMode=True"))
		.Replace(TEXT("Port=8008"), TEXT("Port=8010"))
		.Replace(TEXT("ListenPort=8000"), TEXT("ListenPort=8001"));

	TArray<FString> Warnings;
	const TArray<FString> Overrides = OscuEndpointFile::Apply(Text, *S, Warnings);

	const TArray<FString> Expected = {
		TEXT("[OSC In] ListenPort: 8000 -> 8001"),
		TEXT("[OSC Target toTD] Port: 8008 -> 8010"),
		TEXT("[Performance] PerformanceMode: False -> True"),
	};
	TestEqual(TEXT("Overrides in file order"), FString::Join(Overrides, TEXT(" | ")), FString::Join(Expected, TEXT(" | ")));
	return true;
}

//////////////////////////////////////////////////////////////////////////
// A missing file is created from the settings; an existing one is applied

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointLoadOrCreateTest,
	"OSCulator.EndpointFile.LoadOrCreate",
	OscuTest::Flags)

bool FOscuEndpointLoadOrCreateTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	const FString Path = TempPath();
	IFileManager::Get().Delete(*Path, /*RequireExists*/ false, /*EvenReadOnly*/ true);

	const FString StatusBefore = OscuEndpointFile::DescribeLastLoad();

	UOscuSettings* S = MakeSettings();
	TArray<FString> Overrides;
	bool bCreated = false;
	FString Status;
	TestTrue(TEXT("Missing file handled"), OscuEndpointFile::LoadOrCreate(*S, Path, Overrides, bCreated, Status));
	TestTrue(TEXT("It was created"), bCreated);
	TestTrue(TEXT("Status says created"), Status.Contains(TEXT("created")));

	FString OnDisk;
	TestTrue(TEXT("File exists"), FFileHelper::LoadFileToString(OnDisk, *Path));
	TestEqual(TEXT("Contents are the settings"), OnDisk, OscuEndpointFile::Write(*S));

	// Second pass: the file now exists and differs from the settings.
	FFileHelper::SaveStringToFile(OnDisk.Replace(TEXT("ListenPort=8000"), TEXT("ListenPort=8100")), *Path);
	TestTrue(TEXT("Existing file handled"), OscuEndpointFile::LoadOrCreate(*S, Path, Overrides, bCreated, Status));
	TestFalse(TEXT("Not re-created"), bCreated);
	TestEqual(TEXT("Applied"), S->ListenPort, 8100);
	TestEqual(TEXT("One override"), Overrides.Num(), 1);
	TestTrue(TEXT("Status counts the override"), Status.Contains(TEXT("1 override(s)")));

	TestEqual(TEXT("The startup status is left alone"), OscuEndpointFile::DescribeLastLoad(), StatusBefore);

	IFileManager::Get().Delete(*Path, false, true);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// A save cut short between delete and move leaves only the .tmp: it is recovered

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOscuEndpointRecoverTempTest,
	"OSCulator.EndpointFile.RecoverInterruptedSave",
	OscuTest::Flags)

bool FOscuEndpointRecoverTempTest::RunTest(const FString& Parameters)
{
	using namespace OscuEndpointFileTest;

	const FString Path = TempPath();
	const FString Temp = Path + TEXT(".tmp");
	IFileManager::Get().Delete(*Path, false, true);
	IFileManager::Get().Delete(*Temp, false, true);

	UOscuSettings* S = MakeSettings();
	const FString Text = OscuEndpointFile::Write(*S).Replace(TEXT("ListenPort=8000"), TEXT("ListenPort=8100"));
	TestTrue(TEXT("Only the .tmp is written"), FFileHelper::SaveStringToFile(Text, *Temp));

	AddExpectedMessagePlain(TEXT("recovered OSCulator.ini from an interrupted save"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	TArray<FString> Overrides;
	bool bCreated = true;
	FString Status;
	TestTrue(TEXT("Handled"), OscuEndpointFile::LoadOrCreate(*S, Path, Overrides, bCreated, Status));
	TestFalse(TEXT("Not created from the settings"), bCreated);
	TestTrue(TEXT("File is back"), FPaths::FileExists(Path));
	TestFalse(TEXT(".tmp is gone"), FPaths::FileExists(Temp));
	TestEqual(TEXT("The .tmp values were applied"), S->ListenPort, 8100);

	IFileManager::Get().Delete(*Path, false, true);
	IFileManager::Get().Delete(*Temp, false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
