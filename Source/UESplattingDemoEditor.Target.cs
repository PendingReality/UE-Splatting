// SPDX-License-Identifier: MIT

using UnrealBuildTool;
using System.Collections.Generic;

public class UESplattingDemoEditorTarget : TargetRules
{
	public UESplattingDemoEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("UESplattingDemo");
	}
}
