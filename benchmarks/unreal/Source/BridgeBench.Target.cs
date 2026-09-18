using UnrealBuildTool;
public class BridgeBenchTarget : TargetRules
{
    public BridgeBenchTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.Add("BridgeBench");
    }
}