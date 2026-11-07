using UnrealBuildTool;

public class JMEditorTools : ModuleRules
{
	public JMEditorTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "UnrealEd",
			"Json", "JsonUtilities", "DeveloperSettings", "JM"
		});

		// AnimAssetBuilder(MC4.2): AnimGraph 노드·레이어 인터페이스·몽타주 생성, 포즈 샘플링
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AnimGraph", "AnimGraphRuntime", "BlueprintGraph", "Kismet", "KismetCompiler",
			"AssetTools", "EditorScriptingUtilities", "AnimationBlueprintLibrary"
		});
	}
}
