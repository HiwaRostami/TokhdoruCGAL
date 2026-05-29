// Copyright NazruGeo. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Tokhdoru.h"
#include "TokhdoruActor.generated.h"

UCLASS(hidecategories = (Rendering, Physics, Collision, Navigation, Lighting,
                          AssetUserData, Activation, Instances, Input, Mobile))
class TOKHDORU_API ATokhdoruActor : public AActor
{
	GENERATED_BODY()

public:
	ATokhdoruActor();

	// ====================================================================
	// Actions
	// ====================================================================
	UFUNCTION(CallInEditor, Category = "Tokhdoru", meta = (DisplayPriority = "0"))
	void Generate();

	UFUNCTION(CallInEditor, Category = "Tokhdoru", meta = (DisplayPriority = "0"))
	void Clear();

	// ====================================================================
	// Data Source
	// ====================================================================
	/** Path to GeoJSON file (used when bUseGeoJSON = true) */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
	FString GeoJSONFilePath;

	/** Path to OSM (.osm) file (used when bUseGeoJSON = false) */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
	FString OSMFilePath;

	/** True = load GeoJSON, False = load OSM */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
	bool bUseGeoJSON;

	/** Meters -> UE units conversion. Default 100 (1 m = 100 cm). */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru",
		meta = (ClampMin = "1.0", ClampMax = "1000.0", DisplayPriority = "1"))
	float ScaleFactor;

	/** Z offset applied to the entire ground mesh. */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
	float GroundOffset;

	/** Use vertex color material for rendering (default: true) */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
	bool bUseVertexColorMaterial;

	// ====================================================================
	// Material Override (optional - if null, OSM colors are used)
	// ====================================================================
	/** Optional override material for building walls.
	 *  If null, a dynamic material is created from OSM building:colour tag. */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru|Material Override", meta = (DisplayPriority = "2"))
	UMaterialInterface* WallMaterialOverride;

	/** Optional override material for roofs.
	 *  If null, a dynamic material is created from OSM roof:colour tag. */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru|Material Override", meta = (DisplayPriority = "2"))
	UMaterialInterface* RoofMaterialOverride;

	/** Optional override material for roads. */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru|Material Override", meta = (DisplayPriority = "2"))
	UMaterialInterface* RoadMaterialOverride;

	/** Optional override material for water. */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru|Material Override", meta = (DisplayPriority = "2"))
	UMaterialInterface* WaterMaterialOverride;

	/** Optional override material for ground/vegetation. */
	UPROPERTY(EditAnywhere, Category = "Tokhdoru|Material Override", meta = (DisplayPriority = "2"))
	UMaterialInterface* GroundMaterialOverride;

	// ====================================================================
	// Vegetation
	// ====================================================================
	UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "3"))
	UStaticMesh* TreeMesh;

	UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "3"))
	UStaticMesh* GrassMesh;

	UPROPERTY(EditAnywhere, Category = "Tokhdoru",
		meta = (ClampMin = "0.0", ClampMax = "10.0", DisplayPriority = "3"))
	float TreeDensityScale;

	UPROPERTY(EditAnywhere, Category = "Tokhdoru",
		meta = (ClampMin = "0.0", ClampMax = "10.0", DisplayPriority = "3"))
	float GrassDensityScale;

	UPROPERTY(EditAnywhere, Category = "Tokhdoru",
		meta = (ClampMin = "10", ClampMax = "10000", DisplayPriority = "3"))
	int32 MaxTreeInstances;

	UPROPERTY(EditAnywhere, Category = "Tokhdoru",
		meta = (ClampMin = "10", ClampMax = "50000", DisplayPriority = "3"))
	int32 MaxGrassInstances;

	// ====================================================================
	// Components (read-only)
	// ====================================================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	USceneComponent* DefaultRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	UProceduralMeshComponent* BuildingMeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	UProceduralMeshComponent* RoadMeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	UProceduralMeshComponent* SidewalkMeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	UProceduralMeshComponent* WaterMeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	UProceduralMeshComponent* GroundMeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	UProceduralMeshComponent* RoofMeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	UProceduralMeshComponent* VegetationMeshComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	UInstancedStaticMeshComponent* TreeInstancedComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
		meta = (DisplayPriority = "10"))
	UInstancedStaticMeshComponent* GrassInstancedComp;

protected:
	virtual void BeginPlay() override;

private:
	// ---- Internal material references (loaded in constructor or LoadDefaultAssets) ----
	UMaterialInterface* BuildingWallMaterial;
	UMaterialInterface* BuildingGlassMaterial;
	UMaterialInterface* BrickFacadeMaterial;
	UMaterialInterface* DarkStoneFacadeMaterial;
	UMaterialInterface* LightStoneFacadeMaterial;
	UMaterialInterface* RoofMaterial;
	UMaterialInterface* RoadMaterial;
	UMaterialInterface* SidewalkMaterial;
	UMaterialInterface* WaterMaterial;
	UMaterialInterface* GroundGrassMaterial;
	UMaterialInterface* VegetationMaterial;

	// ---- Material creation helpers ----

	/** Load all default material assets from project content */
	void LoadDefaultAssets();

	/** Create a solid-color parent material (M_SolidColor) */
	UMaterialInterface* CreateSolidColorMaterial();

	/** Create a vertex-color material that displays per-vertex colors. */
	UMaterialInterface* CreateVertexColorMaterial();

	/** Create a glass material */
	UMaterialInterface* CreateGlassMaterial();

	/** Create a dynamic material instance from a parent and color */
	UMaterialInstanceDynamic* CreateColoredMID(UMaterialInterface* ParentMat, FLinearColor Color);

	/** Apply materials to all mesh sections */
	void ApplyMaterials();

	// ---- Vegetation ----
	void SpawnVegetation(const TArray<FTransform>& TreeTransforms,
			      const TArray<FTransform>& GrassTransforms);
	void RemoveVegetation();
};
