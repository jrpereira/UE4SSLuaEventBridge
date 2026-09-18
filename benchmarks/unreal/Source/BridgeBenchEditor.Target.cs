using UnrealBuildTool;
public class BridgeBenchEditorTarget : TargetRules
{
    public BridgeBenchEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.Add("BridgeBench");
    }
}