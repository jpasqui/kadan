using UnrealBuildTool;
public class Kadan : ModuleRules
{
    public Kadan(ReadOnlyTargetRules Target) : base(Target)
    {
        // Source readers require precise IEEE semantics, which the engine's shared precompiled
        // headers do not have (/fp:fast). The module's own header applies the numeric contract
        // before any engine header. Each source file is compiled on its own, so a change to one
        // file compiles one file.
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivatePCHHeaderFile = "Private/P4MapKitPCH.h";
        bUseUnity = false;
        CppStandard = CppStandardVersion.Cpp17;
        // Portable processing returns precise validation failures via exceptions.
        bEnableExceptions = true;
        // Several sources include an engine header before their own, on purpose.
        bEnforceIWYU = false;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "Foliage" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Json", "PlacementMode", "AssetRegistry", "RHI", "MeshDescription", "StaticMeshDescription", "RawMesh", "RenderCore", "BlueprintGraph", "Foliage", "NavigationSystem", "Navmesh", "Landscape", "Sockets", "Networking", "JsonUtilities", "DerivedDataCache", "Slate", "SlateCore", "LevelEditor", "DesktopPlatform", "ContentBrowser", "EditorStyle", "MaterialEditor", "MaterialShaderQualitySettings", "P4BlueprintImport", "InputCore", "ToolMenus", "AssetTools", "PropertyEditor", "GraphEditor", "UMGEditor", "UMG", "Projects" });
        // The GLSL_SWITCH format module's compiler interface (header only; the format module finds Kadan at run time).
        PrivateIncludePathModuleNames.Add("KadanShaderFormat");
        PrivateDependencyModuleNames.Add("PhysicsCore");
        PrivateDependencyModuleNames.Add("ApplicationCore");
        if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.Add("bcrypt.lib");
        // Automation translation units are staged only in the separate development Tests module.
    }
}
