using UnrealBuildTool;
using System.IO;

// Development-only module. The canonical release descriptor never declares it.
public class P4MapKitTests : ModuleRules
{
    public P4MapKitTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivatePCHHeaderFile = "Private/P4MapKitTestsPCH.h";
        bUseUnity = false;
        CppStandard = CppStandardVersion.Cpp17;
        bEnableExceptions = true;
        bEnforceIWYU = false;
        PrivateDefinitions.Add("P4_MAPKIT_TESTS=1");
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "UnrealEd", "Kadan", "P4BlueprintImport",
            "Json", "PlacementMode", "AssetRegistry", "RHI", "MeshDescription", "StaticMeshDescription",
            "RenderCore", "ImageWrapper", "ShaderCompilerCommon", "RawMesh", "BlueprintGraph", "Foliage", "Landscape", "NavigationSystem", "Navmesh", "AIModule",
            "DerivedDataCache", "Slate", "SlateCore", "ApplicationCore", "LevelEditor", "DesktopPlatform", "ContentBrowser", "ContentBrowserData", "ContentBrowserAssetDataSource",
            "EditorStyle", "MaterialEditor", "InputCore", "AssetTools", "PropertyEditor", "GraphEditor",
            "UMGEditor", "UMG", "Kismet", "KismetCompiler", "AnimGraph", "AnimGraphRuntime",
            "MovieScene", "MovieSceneTracks", "MovieSceneTools", "LevelSequence", "Projects", "Sockets", "Networking"
        });
        // Tests link the product DLLs. They never compile copies of product translation units.
        PrivateIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory,"../Kadan/Private")));
        PrivateIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory,"../P4BlueprintImport/Private")));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory,"Private/Authoring"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory,"Private/Blueprints"));
        if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.Add("bcrypt.lib");
    }
}
