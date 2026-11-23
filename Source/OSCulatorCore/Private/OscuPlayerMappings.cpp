// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuPlayerMappings.h"

#include "OSCulatorCore.h"
#include "OscuEndpointFile.h"
#include "OscuSettings.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FOscuPlayerMappings::FOnPlayerMappingsChanged FOscuPlayerMappings::OnChanged;

namespace
{
	const TCHAR* KindToString(const EOscuPlayerMIDIKind Kind)
	{
		switch (Kind)
		{
		case EOscuPlayerMIDIKind::Note:          return TEXT("note");
		case EOscuPlayerMIDIKind::ControlChange: return TEXT("cc");
		case EOscuPlayerMIDIKind::ProgramChange: return TEXT("program");
		case EOscuPlayerMIDIKind::Off:           return TEXT("off");
		default:                                 return TEXT("none");
		}
	}

	EOscuPlayerMIDIKind KindFromString(const FString& Text)
	{
		if (Text.Equals(TEXT("note"), ESearchCase::IgnoreCase))    { return EOscuPlayerMIDIKind::Note; }
		if (Text.Equals(TEXT("cc"), ESearchCase::IgnoreCase))      { return EOscuPlayerMIDIKind::ControlChange; }
		if (Text.Equals(TEXT("program"), ESearchCase::IgnoreCase)) { return EOscuPlayerMIDIKind::ProgramChange; }
		if (Text.Equals(TEXT("off"), ESearchCase::IgnoreCase))     { return EOscuPlayerMIDIKind::Off; }
		return EOscuPlayerMIDIKind::None;
	}

	const FOscuPlayerMenuEntry* FindEntry(const UOscuSettings& Settings, const FName Tag, const FName FunctionName)
	{
		return Settings.PlayerMenuEntries.FindByPredicate([Tag, FunctionName](const FOscuPlayerMenuEntry& Entry)
		{
			return Entry.Tag == Tag && Entry.FunctionName == FunctionName;
		});
	}

	const FOscuEventOutput* FindOutput(const UOscuSettings& Settings, const FName Tag, const FName Dispatcher)
	{
		return Settings.EventOutputs.FindByPredicate([Tag, Dispatcher](const FOscuEventOutput& Output)
		{
			return Output.Tag == Tag && Output.Dispatcher == Dispatcher;
		});
	}
}

FOscuPlayerMappings& FOscuPlayerMappings::Get()
{
	static FOscuPlayerMappings Instance;
	return Instance;
}

bool FOscuPlayerMappings::ShouldPersist()
{
	return !GIsEditor || UOscuSettings::Get()->bPlayerMenuSaveInPIE;
}

FString FOscuPlayerMappings::GetPath()
{
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OSCulator"), TEXT("PlayerMappings.json")));
}

FString FOscuPlayerMappings::DefaultOSCAddress(const FName Tag, const FName FunctionName)
{
	return FString::Printf(TEXT("/%s/%s"), *Tag.ToString(), *FunctionName.ToString());
}

FString FOscuPlayerMappings::AliasKey(const FString& Address)
{
	FString Key = Address.TrimStartAndEnd().ToLower();
	while (Key.Len() > 1 && Key.EndsWith(TEXT("/")))
	{
		Key.LeftChopInline(1);
	}
	return Key;
}

FString FOscuPlayerMappings::RoutingKey(const FString& Address)
{
	TArray<FString> Segments;
	AliasKey(Address).ParseIntoArray(Segments, TEXT("/"), /*CullEmpty*/ true);
	if (Segments.Num() == 0)
	{
		return FString();
	}

	FString Key = TEXT("/") + Segments[0];
	if (Segments.Num() > 1)
	{
		Key += TEXT("/");
		Key += FString::Join(TArrayView<const FString>(Segments).RightChop(1), TEXT("_"));
	}
	return Key;
}

const TArray<FOscuPlayerOverride>& FOscuPlayerMappings::GetStored()
{
	EnsureLoaded();
	return Overrides;
}

TArray<FOscuPlayerOverride> FOscuPlayerMappings::GetActive()
{
	EnsureLoaded();

	TArray<FOscuPlayerOverride> Active;
	const UOscuSettings& Settings = *UOscuSettings::Get();
	if (!Settings.bEnablePlayerMenu)
	{
		return Active;
	}

	for (const FOscuPlayerOverride& Stored : Overrides)
	{
		bool bAllowOSC = false;
		bool bAllowMIDI = false;
		if (Stored.bOutput)
		{
			const FOscuEventOutput* Output = FindOutput(Settings, Stored.Tag, Stored.FunctionName);
			if (Output == nullptr || !Output->bInPlayerMenu)
			{
				continue;
			}
			bAllowOSC = Output->bPlayerOSC;
			bAllowMIDI = Output->bPlayerMIDI;
		}
		else
		{
			const FOscuPlayerMenuEntry* Entry = FindEntry(Settings, Stored.Tag, Stored.FunctionName);
			if (Entry == nullptr)
			{
				continue;
			}
			bAllowOSC = Entry->bOSC;
			bAllowMIDI = Entry->bMIDI;
		}

		FOscuPlayerOverride Applied = Stored;
		if (!bAllowOSC)
		{
			Applied.OSCAddress.Reset();
		}
		if (!bAllowMIDI)
		{
			Applied.MIDIKind = EOscuPlayerMIDIKind::None;
		}
		if (!Applied.IsEmpty())
		{
			Active.Add(MoveTemp(Applied));
		}
	}
	return Active;
}

const FOscuPlayerOverride* FOscuPlayerMappings::Find(const FName Tag, const FName FunctionName, const bool bOutput)
{
	EnsureLoaded();
	return Overrides.FindByPredicate([Tag, FunctionName, bOutput](const FOscuPlayerOverride& Override)
	{
		return Override.Matches(Tag, FunctionName, bOutput);
	});
}

FOscuPlayerOverride& FOscuPlayerMappings::FindOrAdd(const FName Tag, const FName FunctionName, const bool bOutput)
{
	EnsureLoaded();
	if (FOscuPlayerOverride* Existing = Overrides.FindByPredicate([Tag, FunctionName, bOutput](const FOscuPlayerOverride& Override)
		{
			return Override.Matches(Tag, FunctionName, bOutput);
		}))
	{
		return *Existing;
	}

	FOscuPlayerOverride& Added = Overrides.AddDefaulted_GetRef();
	Added.Tag = Tag;
	Added.FunctionName = FunctionName;
	Added.bOutput = bOutput;
	return Added;
}

void FOscuPlayerMappings::SetOSCAddress(const FName Tag, const FName FunctionName, const FString& Address, const bool bOutput)
{
	FString Clean = Address.TrimStartAndEnd();
	if (!Clean.IsEmpty() && !Clean.StartsWith(TEXT("/")))
	{
		Clean.InsertAt(0, TEXT('/'));
	}

	// Typing the default back in is the same as resetting it.
	FString Default = DefaultOSCAddress(Tag, FunctionName);
	if (bOutput)
	{
		if (const FOscuEventOutput* Output = FindOutput(*UOscuSettings::Get(), Tag, FunctionName))
		{
			Default = Output->GetDefaultOSCAddress();
		}
	}
	if (RoutingKey(Clean) == RoutingKey(Default))
	{
		Clean.Reset();
	}

	FindOrAdd(Tag, FunctionName, bOutput).OSCAddress = Clean;
	Commit();
}

void FOscuPlayerMappings::SetMIDI(const FName Tag, const FName FunctionName, const EOscuPlayerMIDIKind Kind, const int32 Channel, const int32 Number, const bool bOutput)
{
	FOscuPlayerOverride& Override = FindOrAdd(Tag, FunctionName, bOutput);
	Override.MIDIKind = Kind;
	Override.MIDIChannel = FMath::Clamp(Channel, 1, 16);
	Override.MIDINumber = FMath::Clamp(Number, 0, 127);
	Commit();
}

int32 FOscuPlayerMappings::GetPlayerTheme()
{
	EnsureLoaded();
	return PlayerTheme;
}

void FOscuPlayerMappings::SetPlayerTheme(const int32 Theme)
{
	EnsureLoaded();
	PlayerTheme = Theme;
	Commit();
}

void FOscuPlayerMappings::ResetAll()
{
	EnsureLoaded();
	Overrides.Reset();
	Commit();
}

void FOscuPlayerMappings::RestoreStored(const TArray<FOscuPlayerOverride>& Stored)
{
	EnsureLoaded();
	Overrides = Stored;
	OnChanged.Broadcast();
}

void FOscuPlayerMappings::EnsureLoaded()
{
	if (bLoaded)
	{
		return;
	}
	bLoaded = true;

	if (ShouldPersist())
	{
		Load();
	}
}

void FOscuPlayerMappings::Load()
{
	Overrides.Reset();

	const FString Path = GetPath();
	FString Text;
	if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToString(Text, *Path))
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogOSCulator, Warning, TEXT("Player menu: %s is not valid JSON and was ignored."), *Path);
		return;
	}

	int32 Theme = INDEX_NONE;
	if (Root->TryGetNumberField(TEXT("Theme"), Theme))
	{
		PlayerTheme = Theme;
	}

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Root->TryGetArrayField(TEXT("Overrides"), Items))
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& Item : *Items)
	{
		const TSharedPtr<FJsonObject> Object = Item.IsValid() ? Item->AsObject() : nullptr;
		if (!Object.IsValid())
		{
			continue;
		}

		FOscuPlayerOverride Override;
		Override.Tag = FName(*Object->GetStringField(TEXT("Tag")));
		Override.FunctionName = FName(*Object->GetStringField(TEXT("Function")));
		Object->TryGetStringField(TEXT("OSC"), Override.OSCAddress);
		Object->TryGetBoolField(TEXT("Output"), Override.bOutput);

		const TSharedPtr<FJsonObject>* MIDI = nullptr;
		if (Object->TryGetObjectField(TEXT("MIDI"), MIDI) && MIDI != nullptr && MIDI->IsValid())
		{
			Override.MIDIKind = KindFromString((*MIDI)->GetStringField(TEXT("Kind")));
			Override.MIDIChannel = FMath::Clamp(static_cast<int32>((*MIDI)->GetNumberField(TEXT("Channel"))), 1, 16);
			Override.MIDINumber = FMath::Clamp(static_cast<int32>((*MIDI)->GetNumberField(TEXT("Number"))), 0, 127);
		}

		if (!Override.Tag.IsNone() && !Override.FunctionName.IsNone() && !Override.IsEmpty())
		{
			Overrides.Add(MoveTemp(Override));
		}
	}

	UE_LOG(LogOSCulator, Log, TEXT("Player menu: %d remap(s) loaded from %s."), Overrides.Num(), *Path);
}

void FOscuPlayerMappings::Commit()
{
	Overrides.RemoveAll([](const FOscuPlayerOverride& Override) { return Override.IsEmpty(); });
	bRemapsChangedThisSession = true;

	if (ShouldPersist())
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("Version"), 1);
		if (PlayerTheme != INDEX_NONE)
		{
			Root->SetNumberField(TEXT("Theme"), PlayerTheme);
		}

		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FOscuPlayerOverride& Override : Overrides)
		{
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("Tag"), Override.Tag.ToString());
			Object->SetStringField(TEXT("Function"), Override.FunctionName.ToString());
			if (Override.bOutput)
			{
				Object->SetBoolField(TEXT("Output"), true);
			}
			if (Override.HasOSC())
			{
				Object->SetStringField(TEXT("OSC"), Override.OSCAddress);
			}
			if (Override.HasMIDI())
			{
				const TSharedRef<FJsonObject> MIDI = MakeShared<FJsonObject>();
				MIDI->SetStringField(TEXT("Kind"), KindToString(Override.MIDIKind));
				MIDI->SetNumberField(TEXT("Channel"), Override.MIDIChannel);
				MIDI->SetNumberField(TEXT("Number"), Override.MIDINumber);
				Object->SetObjectField(TEXT("MIDI"), MIDI);
			}
			Items.Add(MakeShared<FJsonValueObject>(Object));
		}
		Root->SetArrayField(TEXT("Overrides"), Items);

		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Root, Writer);

		const FString Path = GetPath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);
		if (!FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogOSCulator, Warning, TEXT("Player menu: could not write %s; remaps last until the game closes."), *Path);
		}
	}

	OnChanged.Broadcast();
}

void FOscuPlayerMappings::TakeEndpointSnapshot()
{
	if (!ShouldPersist() && !bHasEndpointSnapshot)
	{
		EndpointSnapshot = OscuEndpointFile::Write(*UOscuSettings::Get());
		bHasEndpointSnapshot = true;
	}
}

bool FOscuPlayerMappings::CommitEndpoints(FString& OutError)
{
	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	bool bSaved = true;

	if (ShouldPersist())
	{
		const FString Path = OscuEndpointFile::GetPath();
		if (!OscuEndpointFile::Save(*Settings, Path))
		{
			OutError = FString::Printf(TEXT("Could not write %s. The change lasts until the game closes."), *Path);
			UE_LOG(LogOSCulator, Warning, TEXT("Player menu: %s"), *OutError);
			bSaved = false;
		}
#if WITH_EDITOR
		else if (OscuEndpointFile::ShouldSyncDefaultConfig())
		{
			Settings->TryUpdateDefaultConfigFile();
		}
#endif
	}

	UOscuSettings::OnSettingsChanged.Broadcast();
	return bSaved;
}

bool FOscuPlayerMappings::SetListenPort(const int32 Port, FString& OutError)
{
	if (Port < 1 || Port > 65535)
	{
		OutError = TEXT("A port is a number from 1 to 65535.");
		return false;
	}

	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	if (Settings->ListenPort == Port)
	{
		return true;
	}

	TakeEndpointSnapshot();
	Settings->ListenPort = Port;
	UE_LOG(LogOSCulator, Log, TEXT("Player menu: OSC listen port set to %d."), Port);
	return CommitEndpoints(OutError);
}

bool FOscuPlayerMappings::SetMIDIInputDeviceEnabled(const FString& DeviceName, const bool bEnabled, FString& OutError)
{
	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	const auto SameName = [&DeviceName](const FOscuMIDIInputDevice& Device)
	{
		return Device.Name.Equals(DeviceName, ESearchCase::IgnoreCase);
	};

	const bool bListed = Settings->MIDIInputDevices.ContainsByPredicate(SameName);
	if (bListed == bEnabled)
	{
		return true;
	}

	TakeEndpointSnapshot();
	if (bEnabled)
	{
		FOscuMIDIInputDevice& Added = Settings->MIDIInputDevices.AddDefaulted_GetRef();
		Added.Name = DeviceName;
	}
	else
	{
		Settings->MIDIInputDevices.RemoveAll(SameName);
	}

	UE_LOG(LogOSCulator, Log, TEXT("Player menu: MIDI input '%s' %s."), *DeviceName, bEnabled ? TEXT("added") : TEXT("removed"));
	return CommitEndpoints(OutError);
}

bool FOscuPlayerMappings::SetOSCOutTarget(const FString& Host, const int32 Port, FString& OutError)
{
	const FString CleanHost = Host.TrimStartAndEnd();
	if (CleanHost.IsEmpty())
	{
		OutError = TEXT("Enter the address of the machine to send to.");
		return false;
	}
	if (Port < 1 || Port > 65535)
	{
		OutError = TEXT("A port is a number from 1 to 65535.");
		return false;
	}

	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	if (Settings->OSCTargets.Num() > 0
		&& Settings->OSCTargets[0].Host == CleanHost && Settings->OSCTargets[0].Port == Port)
	{
		return true;
	}

	TakeEndpointSnapshot();
	if (Settings->OSCTargets.Num() == 0)
	{
		FOscuOSCTarget& Added = Settings->OSCTargets.AddDefaulted_GetRef();
		Added.Name = FName("Player");
	}
	Settings->OSCTargets[0].Host = CleanHost;
	Settings->OSCTargets[0].Port = Port;
	Settings->OSCTargets[0].bEnabled = true;

	UE_LOG(LogOSCulator, Log, TEXT("Player menu: OSC out set to %s:%d."), *CleanHost, Port);
	return CommitEndpoints(OutError);
}

bool FOscuPlayerMappings::SetMIDIOutputDeviceEnabled(const FString& DeviceName, const bool bEnabled, FString& OutError)
{
	UOscuSettings* Settings = GetMutableDefault<UOscuSettings>();
	const auto SameName = [&DeviceName](const FString& Name) { return Name.Equals(DeviceName, ESearchCase::IgnoreCase); };

	const bool bListed = Settings->MIDIOutputDeviceNames.ContainsByPredicate(SameName);
	if (bListed == bEnabled)
	{
		return true;
	}

	TakeEndpointSnapshot();
	if (bEnabled)
	{
		Settings->MIDIOutputDeviceNames.Add(DeviceName);
	}
	else
	{
		Settings->MIDIOutputDeviceNames.RemoveAll(SameName);
	}

	UE_LOG(LogOSCulator, Log, TEXT("Player menu: MIDI output '%s' %s."), *DeviceName, bEnabled ? TEXT("added") : TEXT("removed"));
	return CommitEndpoints(OutError);
}

void FOscuPlayerMappings::EndSession()
{
	if (ShouldPersist())
	{
		bRemapsChangedThisSession = false;
		return;
	}

	if (bHasEndpointSnapshot)
	{
		TArray<FString> Warnings;
		OscuEndpointFile::Apply(EndpointSnapshot, *GetMutableDefault<UOscuSettings>(), Warnings);
		bHasEndpointSnapshot = false;
		EndpointSnapshot.Reset();
		UE_LOG(LogOSCulator, Log, TEXT("Player menu: play stopped, OSC port and MIDI devices put back (Save Changes Made In PIE is off)."));
		UOscuSettings::OnSettingsChanged.Broadcast();
	}

	if (bRemapsChangedThisSession)
	{
		bRemapsChangedThisSession = false;
		Overrides.Reset();
		PlayerTheme = INDEX_NONE;
		UE_LOG(LogOSCulator, Log, TEXT("Player menu: play stopped, remaps cleared (Save Changes Made In PIE is off)."));
		OnChanged.Broadcast();
	}
}
