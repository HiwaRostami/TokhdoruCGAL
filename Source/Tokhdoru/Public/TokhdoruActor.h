// Copyright NazruGeo. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Tokhdoru.h"
#include "TokhdoruActor.generated.h"

struct FGeoModelInstance;

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

        /** When true: windows rendered as real 3D glass geometry using M_Glass (Section 1).
         *  When false (default): windows are a flat facade atlas texture slice (Section 6)
         *  — cheaper and better for city-scale views. Toggle without re-generating. */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
        bool b3DWindow;

        /** Deform ground mesh from OSM ele tags / GeoJSON Z coordinates (streets.gl terrain). */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
        bool bUseTerrainElevation;

        /** Grid resolution for terrain mesh when elevation data is available. */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru",
                meta = (ClampMin = "8", ClampMax = "256", DisplayPriority = "1"))
        int32 TerrainGridSubdivisions;

        /** Generate railway ballast + rails from OSM railway ways. */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
        bool bShowRailways;

        /** Generate coloured POI pin markers for amenity/shop/tourism nodes. */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
        bool bShowPOIMarkers;

        /** Bind streets-gl normal/roughness textures when the PBR material supports them. */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "1"))
        bool bUsePBRMaterials;

        /** POI marker base radius in UE units (cm). */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru",
                meta = (ClampMin = "20.0", ClampMax = "500.0", DisplayPriority = "1", EditCondition = "bShowPOIMarkers"))
        float POIMarkerRadius;

        /** POI marker height in UE units (cm). */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru",
                meta = (ClampMin = "50.0", ClampMax = "2000.0", DisplayPriority = "1", EditCondition = "bShowPOIMarkers"))
        float POIMarkerHeight;

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

        /** Optional override for railway ballast bed. */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru|Material Override", meta = (DisplayPriority = "2"))
        UMaterialInterface* RailwayMaterialOverride;

        /** Optional override for POI markers (falls back to vertex colour). */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru|Material Override", meta = (DisplayPriority = "2"))
        UMaterialInterface* POIMaterialOverride;

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

        /** Uniform scale applied to placed OSM models (hydrants, benches, towers, …).
         *  Increase if the imported models appear too small for the world scale. */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru",
                meta = (ClampMin = "0.01", ClampMax = "1000.0", DisplayPriority = "3"))
        float ModelScale;

        /** Base rotation applied to every placed model BEFORE its heading yaw.
         *  Fixes models whose up-axis differs from UE (Y-up GLB imports lie flat —
         *  Roll = 90 stands them upright). Tweak if models face the wrong way. */
        UPROPERTY(EditAnywhere, Category = "Tokhdoru", meta = (DisplayPriority = "3"))
        FRotator ModelBaseRotation;

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
        UProceduralMeshComponent* RailwayMeshComp;

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
                meta = (DisplayPriority = "10"))
        UProceduralMeshComponent* POIMeshComp;

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
                meta = (DisplayPriority = "10"))
        UInstancedStaticMeshComponent* TreeInstancedComp;

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tokhdoru|Components",
                meta = (DisplayPriority = "10"))
        UInstancedStaticMeshComponent* GrassInstancedComp;

protected:
        virtual void BeginPlay() override;

        /** Snap to world origin (0,0,0) the moment the actor is dropped into a
         *  viewport, so the generated city — which is centred on (0,0,0) — lines up
         *  without any manual transform tweaking. Saved/loaded actors keep their
         *  stored transform, so it can still be moved later if desired. */
        virtual void PostActorCreated() override;

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
        UMaterialInterface* RailwayBallastMaterial;
        UMaterialInterface* RailwayRailMaterial;
        UMaterialInterface* POIMaterial;

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

        /** Attach streets-gl PBR normal/roughness maps when available. */
        UMaterialInterface* ResolvePBRMaterial(UMaterialInterface* BaseMat);

        // ---- Vegetation ----
        void SpawnVegetation(const TArray<FTransform>& TreeTransforms,
                              const TArray<FTransform>& GrassTransforms);
        void RemoveVegetation();

        // ---- OSM 3D models (street furniture, power, landmarks) ----
        /** Spawn one instanced-static-mesh component per model type and place an
         *  instance at every OSM node, using the same centre offset (OX,OY) and
         *  ScaleFactor as the generated meshes. */
        void SpawnModels(const TArray<FGeoModelInstance>& Models, float OX, float OY);
        void RemoveModels();

        /** Dynamically created instanced components for placed models (one per type). */
        UPROPERTY(Transient)
        TArray<UInstancedStaticMeshComponent*> ModelInstancedComps;
};
