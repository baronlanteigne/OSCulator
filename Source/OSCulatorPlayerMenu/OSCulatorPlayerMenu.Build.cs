// Copyright Baron Lanteigne. All Rights Reserved.

using UnrealBuildTool;

public class OSCulatorPlayerMenu : ModuleRules
{
	public OSCulatorPlayerMenu(ReadOnlyTargetRules Target) : base(Target)
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
			"Slate",
			"SlateCore",
			"InputCore",
			"Sockets",

			"OSCulatorOSC",
			"OSCulatorMIDI",
		});
	}
}
