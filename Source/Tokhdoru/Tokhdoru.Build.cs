using UnrealBuildTool;

public class Tokhdoru : ModuleRules
{
    public Tokhdoru(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "ProceduralMeshComponent",
            "Json",
            "JsonUtilities",
            "XmlParser",
            "TokhdoruCGAL"   // CRITICAL: Bridge module for CGAL operations
        });
    }
}
