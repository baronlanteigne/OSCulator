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
			// Quartz: the clock hands out a UQuartzClockHandle.
			"AudioMixer",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Only for OSCulator.Export.
			"Json",
			// The .uproject's module list: which native component functions are the project's own.
			"Projects",
		});

		if (Target.bBuildEditor)
		{
			// Only for the "could not write OSCulator.ini" toast.
			PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		}
	}
}
