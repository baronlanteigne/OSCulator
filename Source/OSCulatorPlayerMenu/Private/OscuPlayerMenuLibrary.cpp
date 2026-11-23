// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuPlayerMenuLibrary.h"

#include "OscuPlayerMappings.h"
#include "OscuPlayerMenuSubsystem.h"

void UOscuPlayerMenuLibrary::OpenPlayerMenu(const UObject* WorldContextObject)
{
	if (UOscuPlayerMenuSubsystem* Menu = UOscuPlayerMenuSubsystem::Get(WorldContextObject))
	{
		Menu->Open();
	}
}

void UOscuPlayerMenuLibrary::ClosePlayerMenu(const UObject* WorldContextObject)
{
	if (UOscuPlayerMenuSubsystem* Menu = UOscuPlayerMenuSubsystem::Get(WorldContextObject))
	{
		Menu->Close();
	}
}

void UOscuPlayerMenuLibrary::TogglePlayerMenu(const UObject* WorldContextObject)
{
	if (UOscuPlayerMenuSubsystem* Menu = UOscuPlayerMenuSubsystem::Get(WorldContextObject))
	{
		Menu->Toggle();
	}
}

bool UOscuPlayerMenuLibrary::IsPlayerMenuOpen(const UObject* WorldContextObject)
{
	const UOscuPlayerMenuSubsystem* Menu = UOscuPlayerMenuSubsystem::Get(WorldContextObject);
	return Menu != nullptr && Menu->IsOpen();
}

void UOscuPlayerMenuLibrary::ResetPlayerRemaps()
{
	FOscuPlayerMappings::Get().ResetAll();
}
