// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System.Collections.Generic;

public class UESplattingDemoTarget : TargetRules
{
	public UESplattingDemoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UESplattingDemo");
	}
}
