// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OscuPlayerMenuLibrary.generated.h"

/**
 * Opens the player menu from your own UI, for projects that want their own button
 * instead of (or as well as) the corner button and the key. Nothing happens unless
 * Project Settings > OSCulator > Player Menu is enabled.
 */
UCLASS()
class OSCULATORPLAYERMENU_API UOscuPlayerMenuLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Player Menu", meta = (WorldContext = "WorldContextObject"))
	static void OpenPlayerMenu(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "OSCulator|Player Menu", meta = (WorldContext = "WorldContextObject"))
	static void ClosePlayerMenu(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "OSCulator|Player Menu", meta = (WorldContext = "WorldContextObject"))
	static void TogglePlayerMenu(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "OSCulator|Player Menu", meta = (WorldContext = "WorldContextObject"))
	static bool IsPlayerMenuOpen(const UObject* WorldContextObject);

	/** Every control back to the project's own OSC address and MIDI input. The port and devices stay. */
	UFUNCTION(BlueprintCallable, Category = "OSCulator|Player Menu")
	static void ResetPlayerRemaps();
};
