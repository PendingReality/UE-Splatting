// SPDX-License-Identifier: MIT

using UnrealBuildTool;

public class UESplattingDemo : ModuleRules
{
	public UESplattingDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UESplatting" });

		RuntimeDependencies.Add(
			"$(ProjectDir)/samples/Data/UESplatting_Demo.ply",
			StagedFileType.NonUFS
		);
	}
}
