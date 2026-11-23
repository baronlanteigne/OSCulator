// Copyright Baron Lanteigne. All Rights Reserved.

#include "OSCulatorCore.h"
#include "OscuPlayerMappings.h"
#include "OscuPlayerMenuSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, OSCulatorPlayerMenu)

namespace
{
	void ForEachMenu(TFunctionRef<void(UOscuPlayerMenuSubsystem&)> Visit)
	{
		if (GEngine == nullptr)
		{
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UGameInstance* GameInstance = Context.OwningGameInstance;
			if (UOscuPlayerMenuSubsystem* Menu = GameInstance != nullptr ? GameInstance->GetSubsystem<UOscuPlayerMenuSubsystem>() : nullptr)
			{
				Visit(*Menu);
			}
		}
	}

	void ListRemaps(FOutputDevice& Ar)
	{
		const TArray<FOscuPlayerOverride>& Stored = FOscuPlayerMappings::Get().GetStored();
		const TArray<FOscuPlayerOverride> Active = FOscuPlayerMappings::Get().GetActive();

		Ar.Logf(TEXT("Player menu: %d remap(s) stored, %d in effect. %s"),
			Stored.Num(), Active.Num(),
			FOscuPlayerMappings::ShouldPersist()
				? *FString::Printf(TEXT("Saved in %s."), *FOscuPlayerMappings::GetPath())
				: TEXT("Session only (editor, Save Changes Made In PIE off)."));

		for (const FOscuPlayerOverride& Override : Stored)
		{
			FString MIDI = TEXT("default");
			if (Override.HasMIDI())
			{
				const TCHAR* Kind = Override.MIDIKind == EOscuPlayerMIDIKind::Note ? TEXT("note")
					: Override.MIDIKind == EOscuPlayerMIDIKind::ControlChange ? TEXT("CC") : TEXT("program");
				MIDI = Override.MIDIKind == EOscuPlayerMIDIKind::Off
					? FString(TEXT("off"))
					: FString::Printf(TEXT("ch %d %s %d"), Override.MIDIChannel, Kind, Override.MIDINumber);
			}
			Ar.Logf(TEXT("  %s%s   OSC %s   MIDI %s"),
				Override.bOutput ? TEXT("out ") : TEXT(""),
				*FOscuPlayerMappings::DefaultOSCAddress(Override.Tag, Override.FunctionName),
				Override.HasOSC() ? *Override.OSCAddress : TEXT("default"), *MIDI);
		}
	}

	FAutoConsoleCommand GToggleCommand(
		TEXT("OSCulator.PlayerMenu"),
		TEXT("Opens or closes the player menu (Project Settings > OSCulator > Player Menu must be enabled)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			ForEachMenu([](UOscuPlayerMenuSubsystem& Menu) { Menu.Toggle(); });
		}));

	FAutoConsoleCommandWithOutputDevice GListCommand(
		TEXT("OSCulator.PlayerMenu.List"),
		TEXT("Lists what the player menu has remapped."),
		FConsoleCommandWithOutputDeviceDelegate::CreateStatic(&ListRemaps));

	FAutoConsoleCommand GResetCommand(
		TEXT("OSCulator.PlayerMenu.Reset"),
		TEXT("Puts every player remap back to the project's own mapping."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			FOscuPlayerMappings::Get().ResetAll();
			UE_LOG(LogOSCulator, Log, TEXT("Player menu: every remap reset."));
		}));
}
