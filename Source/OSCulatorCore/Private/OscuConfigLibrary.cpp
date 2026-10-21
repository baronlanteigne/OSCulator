// Copyright Baron Lanteigne. All Rights Reserved.

#include "OscuConfigLibrary.h"
#include "OscuEndpointFile.h"

bool UOscuConfigLibrary::ReloadOSCulatorConfig()
{
	return OscuEndpointFile::Reload();
}
