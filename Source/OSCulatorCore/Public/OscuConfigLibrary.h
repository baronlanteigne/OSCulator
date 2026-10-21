// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OscuConfigLibrary.generated.h"

/** Blueprint access to OSCulator.ini. Shipping builds have no console, so this is the reload button there. */
UCLASS()
class OSCULATORCORE_API UOscuConfigLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Re-reads OSCulator.ini and reopens OSC sockets and MIDI ports with its values.
	 * Same as the OSCulator.ReloadConfig console command. False if the file is missing
	 * or unreadable, in which case nothing changed.
	 * Briefly closes and reopens OSC sockets and MIDI ports; expect a short hitch.
	 */
	UFUNCTION(BlueprintCallable, Category = "OSCulator", meta = (DisplayName = "Reload OSCulator Config"))
	static bool ReloadOSCulatorConfig();
};
