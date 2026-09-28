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


			// PortMidi links statically and MIDIDevice exports none of its symbols,
			// so this is a SECOND, private copy of the library -- not a route into
			// the one MIDIDevice uses. That is only safe because this module starts
			// and owns its copy end to end: Pm_Initialize here, Pm_OpenInput here,
			// Pm_Close here. Borrowing a stream MIDIDevice opened and calling into
			// this copy dereferences an uninitialised device table, which is exactly
			// how the Pm_SetFilter crash happened.
			"portmidi",
		});

		if (Target.bBuildEditor)
		{
			// Auto-populate walks the editor's current level; Learn writes back into
			// the asset. Both are editor-only and live behind WITH_EDITOR.
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
