// Copyright Baron Lanteigne. All Rights Reserved.

using UnrealBuildTool;

public class OSCulatorEditor : ModuleRules
{
	public OSCulatorEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// The panel itself.
			"Slate",
			"SlateCore",
			"InputCore",
			"PropertyEditor",

			// GEditor, for the world the level is open in.
			"UnrealEd",

			// The asset being edited, and the registry it is validated against.
			"OSCulatorCore",
			"OSCulatorMIDI",
		});
	}
}
