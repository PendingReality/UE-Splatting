// SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class UESplattingEditor : ModuleRules
{
	public UESplattingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UESplatting"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"AssetTools",
				"EditorFramework",
				"RenderCore",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd"
			}
		);
	}
}
