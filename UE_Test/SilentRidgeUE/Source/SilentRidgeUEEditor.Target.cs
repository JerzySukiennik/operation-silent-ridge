// Editor target for Operation Silent Ridge (UE 5.7).
using UnrealBuildTool;

public class SilentRidgeUEEditorTarget : TargetRules
{
	public SilentRidgeUEEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("SilentRidgeUE");
	}
}
