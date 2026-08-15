// Copyright Baron Lanteigne. All Rights Reserved.

using UnrealBuildTool;

public class OSCulatorMIDI : ModuleRules
{
	public OSCulatorMIDI(ReadOnlyTargetRules Target) : base(Target)
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
			"MIDIDevice",
		});

		if (Target.bBuildEditor)
		{
			// Auto-populate walks the editor's current level; Learn writes back into
			// the asset. Both are editor-only and live behind WITH_EDITOR.
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
