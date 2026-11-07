using UnrealBuildTool;
public class KadanShaderFormat : ModuleRules
{
    public KadanShaderFormat(ReadOnlyTargetRules Target) : base(Target)
    {
        // Kadan's GLSL_SWITCH shader format. UE finds shader formats by module name ("*ShaderFormat*"), so this stays a
        // module of its own; the compiler itself is the Kadan module's (IKadanSwitchShaderCompiler). It also holds the
        // game's own shader classes (KadanGameShaderTypes.cpp), which is why it loads at PostConfigInit.
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        CppStandard = CppStandardVersion.Cpp17;
        PublicDependencyModuleNames.AddRange(new string[] { "Core" });
        PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI", "Engine", "Renderer", "TargetPlatform", "ShaderCompilerCommon", "ShaderPreprocessor" });
        AddEngineThirdPartyPrivateStaticDependencies(Target, "HLSLCC");
    }
}
