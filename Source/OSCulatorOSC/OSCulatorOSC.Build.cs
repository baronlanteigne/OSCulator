// Copyright Baron Lanteigne. All Rights Reserved.

using UnrealBuildTool;

public class OSCulatorOSC : ModuleRules
{
	public OSCulatorOSC(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"OSCulatorCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Sockets",
			"Networking",
		});
	}
}
