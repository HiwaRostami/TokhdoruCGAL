using UnrealBuildTool;

public class TokhdoruEditor : ModuleRules
{
	public TokhdoruEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Disable Unity Build for CGAL compatibility (same as runtime module)
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"EditorStyle",
			"Tokhdoru",
			"ProceduralMeshComponent",
		});
	}
}
