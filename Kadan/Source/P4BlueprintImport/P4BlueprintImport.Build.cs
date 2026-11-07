using UnrealBuildTool;

public class P4BlueprintImport : ModuleRules
{
	public P4BlueprintImport(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		// Development test seams (Private/P4BlueprintTestSeams.h) only for a build with tests. Release builds set 0.
		if (System.Environment.GetEnvironmentVariable("P4_MAPKIT_TESTS") == "1") PrivateDefinitions.Add("P4_BLUEPRINT_TEST_SEAMS=1");
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AssetRegistry", "Json", "BlueprintGraph", "Kismet", "KismetCompiler", "MeshDescription", "StaticMeshDescription", "UMG", "UMGEditor", "GraphEditor", "Slate", "SlateCore", "RenderCore", "AnimGraph", "AnimGraphRuntime", "MovieScene", "MovieSceneTracks", "MovieSceneTools", "LevelSequence" });
	}
}
