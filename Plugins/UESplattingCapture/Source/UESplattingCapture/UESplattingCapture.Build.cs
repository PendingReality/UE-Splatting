// SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class UESplattingCapture : ModuleRules
{
	public UESplattingCapture(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UESplatting"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"UnrealEd",
				"ImageWrapper",
				"ImageCore",
				"Imath",
				"Json",
				"LevelSequence",
				"MovieScene",
				"MovieSceneTracks",
				"MovieRenderPipelineCore",
				"MovieRenderPipelineEditor",
				"MovieRenderPipelineRenderPasses",
				"PropertyEditor",
				"Projects",
				"RenderCore",
				"Sequencer",
				"ToolMenus",
				"UEOpenExr",
				"UEOpenExrRTTI"
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
