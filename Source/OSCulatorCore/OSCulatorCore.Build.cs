// Copyright Baron Lanteigne. All Rights Reserved.

using UnrealBuildTool;

public class OSCulatorCore : ModuleRules
{
	public OSCulatorCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Only for OSCulator.Export.
			"Json",
		});
	}
}
