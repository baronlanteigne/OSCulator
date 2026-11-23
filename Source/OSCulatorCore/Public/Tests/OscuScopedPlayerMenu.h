// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "OscuPlayerMappings.h"
#include "OscuSettings.h"

namespace OscuTest
{
	/**
	 * Enables the player menu with the given controls for the test's duration, and puts
	 * the settings and the stored remaps back afterwards. Save Changes Made In PIE is
	 * forced off, so nothing the test does reaches PlayerMappings.json.
	 */
	struct FScopedPlayerMenu
	{
		explicit FScopedPlayerMenu(std::initializer_list<FOscuPlayerMenuEntry> Entries)
		{
			Settings = GetMutableDefault<UOscuSettings>();
			bSavedEnable = Settings->bEnablePlayerMenu;
			bSavedSaveInPIE = Settings->bPlayerMenuSaveInPIE;
			SavedEntries = Settings->PlayerMenuEntries;
			SavedStored = FOscuPlayerMappings::Get().GetStored();

			Settings->bPlayerMenuSaveInPIE = false;
			Settings->bEnablePlayerMenu = true;
			Settings->PlayerMenuEntries = Entries;
			FOscuPlayerMappings::Get().RestoreStored({});
		}

		~FScopedPlayerMenu()
		{
			Settings->bEnablePlayerMenu = bSavedEnable;
			Settings->PlayerMenuEntries = SavedEntries;
			FOscuPlayerMappings::Get().RestoreStored(SavedStored);
			Settings->bPlayerMenuSaveInPIE = bSavedSaveInPIE;
		}

		static FOscuPlayerMenuEntry Entry(const TCHAR* Tag, const TCHAR* Function, bool bOSC = true, bool bMIDI = true)
		{
			FOscuPlayerMenuEntry Out;
			Out.Tag = FName(Tag);
			Out.FunctionName = FName(Function);
			Out.bOSC = bOSC;
			Out.bMIDI = bMIDI;
			return Out;
		}

		FScopedPlayerMenu(const FScopedPlayerMenu&) = delete;
		FScopedPlayerMenu& operator=(const FScopedPlayerMenu&) = delete;

	private:
		UOscuSettings* Settings = nullptr;
		bool bSavedEnable = false;
		bool bSavedSaveInPIE = false;
		TArray<FOscuPlayerMenuEntry> SavedEntries;
		TArray<FOscuPlayerOverride> SavedStored;
	};
}

#endif
