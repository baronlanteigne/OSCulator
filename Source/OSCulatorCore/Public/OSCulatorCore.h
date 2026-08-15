// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

/** Every log line the plugin emits, across all three modules, goes here. */
OSCULATORCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogOSCulator, Log, All);

/**
 * "stat OSCulator" in the console.
 *
 * Exists so the question "is OSC costing me frame time?" has an answer in
 * milliseconds rather than an argument about what stat unit is showing. The
 * plugin's own cost is separated from whatever the called Blueprint event does,
 * which is usually the larger number by far.
 */
DECLARE_STATS_GROUP(TEXT("OSCulator"), STATGROUP_OSCulator, STATCAT_Advanced);
