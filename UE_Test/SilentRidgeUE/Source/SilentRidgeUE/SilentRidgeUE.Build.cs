// Runtime module: flight model, pawn, camera, HUD, audio, autotest.
using UnrealBuildTool;

public class SilentRidgeUE : ModuleRules
{
	public SilentRidgeUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "LevelSequence", "MovieScene", "ProceduralMeshComponent", "RHI" });
	}
}
