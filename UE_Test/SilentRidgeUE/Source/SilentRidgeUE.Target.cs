// Game target for Operation Silent Ridge (UE 5.7).
using UnrealBuildTool;

public class SilentRidgeUETarget : TargetRules
{
	public SilentRidgeUETarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("SilentRidgeUE");
	}
}
