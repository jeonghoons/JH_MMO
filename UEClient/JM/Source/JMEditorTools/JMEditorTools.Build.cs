using UnrealBuildTool;

public class JMEditorTools : ModuleRules
{
	public JMEditorTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "UnrealEd",
			"Json", "JsonUtilities", "StructUtils"
		});
	}
}
