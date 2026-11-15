// Copyright Baron Lanteigne. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UFunction;

/**
 * "The project's own code": the C++ modules the .uproject lists. On a component, the
 * blanket bExposeAllFunctions reaches functions declared there as well as Blueprint-
 * authored ones, so a project's native components need no opt-in while engine and
 * plugin components (PhysicsControl, StaticMesh...) stay shut.
 */
namespace OscuProjectCode
{
	/** Short names of the modules the current .uproject lists. Read once; empty with no project. */
	const TSet<FName>& GetProjectModuleNames();

	/** Whether Function is declared -- not inherited -- by a class in one of Modules. */
	bool IsDeclaredIn(const UFunction* Function, const TSet<FName>& Modules);
}
