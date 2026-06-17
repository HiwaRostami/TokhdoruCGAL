// Copyright NazruGeo. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "GeoJSONLoader.h"
#include "CGALSkeletonGenerator.h"

/**
 * Utility class for generating procedural meshes from geographic data.
 * Data from GeoJSON/OSM loaders is already converted to local meters.
 * ScaleFactor converts meters to UE centimeters (default: 100.0).
 *
 * Roof generation uses CGAL Straight Skeleton for accurate results on any
 * polygon shape (rectangle, L, U, cross, with courtyards). AABB-based
 * ridge detection has been removed.
 */
class FGeoMeshGenerator
{
public:
        // ---------- Mesh Data Structure (PUBLIC for external access) ----------
        struct FMeshData
        {
                TArray<FVector> Vertices;
                TArray<int32> Triangles;
                TArray<FVector> Normals;
                TArray<FVector2D> UV0;
                // UV1.X carries the texture-atlas slice index (streets-gl style): the
                // facade/roof Texture2DArray material reads the slice from TexCoord[1].R.
                // Left empty for meshes that don't use the atlas (roads, water, ...).
                TArray<FVector2D> UV1;
                TArray<FColor> VertexColors;
                TArray<FProcMeshTangent> Tangents;

                void Reset()
                {
                        Vertices.Empty();
                        Triangles.Empty();
                        Normals.Empty();
                        UV0.Empty();
                        UV1.Empty();
                        VertexColors.Empty();
                        Tangents.Empty();
                }

                void Append(const FMeshData& Other)
                {
                        int32 VertexOffset = Vertices.Num();
                        Vertices.Append(Other.Vertices);
                        Normals.Append(Other.Normals);
                        UV0.Append(Other.UV0);
                        UV1.Append(Other.UV1);
                        VertexColors.Append(Other.VertexColors);
                        Tangents.Append(Other.Tangents);
                        for (int32 Idx : Other.Triangles)
                                Triangles.Add(Idx + VertexOffset);
                }
        };

        // ---------- Settings ----------
        float ScaleFactor; // meters -> UE units (default 100.0 = m->cm)

        // ---------- Constructor ----------
        FGeoMeshGenerator() : ScaleFactor(100.0f) {}

        // ---------- Mesh Generation Methods ----------
        void GenerateBuildingMeshes(
                const TArray<FGeoBuilding>& Buildings,
                FMeshData& OutWallData,
                FMeshData& OutGlassData,
                FMeshData& OutBrickData,
                FMeshData& OutDarkStoneData,
                FMeshData& OutLightStoneData,
                // Walls of towers / churches: kept as plain OSM colour (no facade atlas).
                FMeshData& OutColorWallData,
                // Wall-inset window panels stamped with the material-matched *_Window
                // atlas slice (FacadeBrickWindow, FacadePlasterWindow, etc.).
                // Separate from OutGlassData so the UE5 material can sample the correct
                // window cut-out texture instead of the plain glass curtain-wall slice.
                FMeshData& OutWindowData,
                // 3D window frames: protruding frame bars with building wall texture.
                // Visible only when b3DWindow=true.
                FMeshData& OutWindowFrameData,
                // 3D window glass: flat glass pane inside the frame with M_Glass material.
                // Visible only when b3DWindow=true.
                FMeshData& OutWindowGlassData);

        void GenerateRoadMeshes(
                const TArray<FGeoRoad>& Roads,
                FMeshData& OutRoadData,
                FMeshData& OutSidewalkData);

        void GenerateWaterMeshes(
                const TArray<FGeoWater>& WaterBodies,
                FMeshData& OutWaterData);

        void GenerateGroundMesh(
                const TArray<FGeoBuilding>& Buildings,
                const TArray<FGeoRoad>& Roads,
                const TArray<FGeoWater>& WaterBodies,
                const TArray<FGeoVegetation>& Vegetations,
                const TArray<FElevationSample>& ElevationSamples,
                FMeshData& OutGroundData,
                float GroundOffset = 0.0f,
                bool bUseTerrainElevation = true,
                int32 TerrainGridSubdivisions = 48);

        void GenerateRailwayMeshes(
                const TArray<FGeoRailway>& Railways,
                FMeshData& OutBallastData,
                FMeshData& OutRailData);

        void GeneratePOIMarkers(
                const TArray<FGeoPOI>& POIs,
                FMeshData& OutPOIData,
                float MarkerRadius = 80.f,
                float MarkerHeight = 250.f);

        void GenerateRoofMeshes(
                const TArray<FGeoBuilding>& Buildings,
                FMeshData& OutRoofData,
                // Non-flat roofs + tower/church roofs: kept as plain OSM colour.
                FMeshData& OutRoofColorData);

        void GenerateVegetationMeshes(
                const TArray<FGeoVegetation>& Vegetations,
                FMeshData& OutVegetationData);

        void GenerateVegetationTransforms(
                const TArray<FGeoVegetation>& Vegetations,
                const TArray<FGeoBuilding>& Buildings,
                const TArray<FGeoRoad>& Roads,
                TArray<FTransform>& OutTreeTransforms,
                TArray<FTransform>& OutGrassTransforms,
                float TreeDensityScale = 1.0f,
                float GrassDensityScale = 1.0f);

        /** Append transforms for individually tagged OSM trees (natural=tree). */
        static void AppendOSMTreeTransforms(
                const TArray<FGeoTree>& Trees,
                TArray<FTransform>& OutTreeTransforms,
                float ScaleFactor,
                float GroundOffset = 0.f);

private:
        // ---------- Primitive helpers ----------
        static void AddQuad(
                FMeshData& MeshData,
                const FVector& V0, const FVector& V1,
                const FVector& V2, const FVector& V3,
                const FColor& Color);

        static void AddDoubleSidedQuad(
                FMeshData& MeshData,
                const FVector& V0, const FVector& V1,
                const FVector& V2, const FVector& V3,
                const FColor& Color);

        static void AddTriangle(
                FMeshData& MeshData,
                const FVector& V0, const FVector& V1, const FVector& V2,
                const FColor& Color);

        static void AddPolygonTriangulated(
                FMeshData& MeshData,
                const TArray<FVector>& PolygonPoints,
                float Height,
                const FColor& Color,
                bool bUseVertexZ = false);

        static void AddExtrudedPolygon(
                FMeshData& MeshData,
                const TArray<FVector>& PolygonPoints,
                float BaseHeight,
                float TopHeight,
                const FColor& WallColor,
                bool bAddBottom = false,
                const FColor& BottomColor = FColor::Black);

        /** Like AddExtrudedPolygon but the top of each wall vertex follows the
         *  skillion roof slope, so the wall hugs the underside of the roof exactly.
         *  RoofDirection: OSM bearing (0=N,90=E,...) of the LOW eave. Pass -1 for flat.
         *  RoofH: vertical rise from low eave to high eave. */
        static void AddSkillionExtrudedPolygon(
                FMeshData& MeshData,
                const TArray<FVector>& PolygonPoints,
                float BaseHeight,
                float WallTopFlat,
                float RoofH,
                float RoofDirection,
                const FColor& WallColor);

        // ---------- Roof geometry router ----------
        /** Dispatch to the appropriate roof generator based on ERoofShape */
        static void AddRoofGeometry(
                FMeshData& OutRoofData,
                ERoofShape RoofShape,
                const TArray<FVector>& LocalFootprint,
                float WallTopZ,
                float RidgeHeight,
                const FColor& RoofFColor,
                const TArray<TArray<FVector2D>>& Holes = TArray<TArray<FVector2D>>(),
                float RoofDirection = -1.f,
                const FString& RoofOrientation = TEXT(""),
                const FColor& WallColor = FColor(180, 170, 160));

        // ---------- CGAL Skeleton-based roof generators ----------

        /** Generate a hipped roof using CGAL Straight Skeleton.
         *  Each skeleton vertex's time value determines roof height at that point.
         *  This produces accurate results for any polygon shape, not just rectangles.
         *  Holes (inner courtyards) are excluded from the roof surface. */
        static void AddSkeletonHippedRoof(
                FMeshData& OutRoofData,
                const TArray<FVector>& Footprint,
                float BaseZ,
                float RidgeHeight,
                const FColor& Color,
                const TArray<TArray<FVector2D>>& Holes = TArray<TArray<FVector2D>>());

        /** Build a sloped roof directly from the straight-skeleton FACES: one panel
         *  per footprint edge, all rising to meet at the ridge. Watertight and
         *  gap-free for ANY polygon (rect, L, U, cross, with courtyards). This is
         *  the robust replacement for the Delaunay-over-points hipped roof.
         *  Returns false if the skeleton has no usable faces (caller falls back). */
        static bool AddStraightSkeletonRoof(
                FMeshData& OutRoofData,
                const TArray<FVector>& Footprint,
                float BaseZ,
                float RidgeHeight,
                const FColor& Color,
                const TArray<TArray<FVector2D>>& Holes = TArray<TArray<FVector2D>>());

        /** Generate a gabled roof using CGAL Straight Skeleton.
         *  Uses skeleton to find the ridge line and compute accurate face slopes.
         *  RoofDirection (0-360, OSM compass bearing) orients the ridge when set >= 0.
         *  RoofOrientation ("along"/"across") flips auto-detected orientation. */
        static void AddSkeletonGabledRoof(
                FMeshData& OutRoofData,
                const TArray<FVector>& Footprint,
                float BaseZ,
                float RidgeHeight,
                const FColor& Color,
                const TArray<TArray<FVector2D>>& Holes = TArray<TArray<FVector2D>>(),
                float RoofDirection = -1.f,
                const FString& RoofOrientation = TEXT(""));

        /** Generate a mansard/gambrel roof using CGAL Skeleton for the inner offset curve. */
        static void AddSkeletonMansardRoof(
                FMeshData& OutRoofData,
                const TArray<FVector>& Footprint,
                float BaseZ,
                float RidgeHeight,
                const FColor& Color);

        /** Mansard roof using inset polygon + lower extrusion + upper pyramidal */
        static void AddMansardRoof(
                FMeshData& OutRoofData,
                const TArray<FVector>& Footprint,
                float BaseZ,
                float RidgeHeight,
                const FColor& Color);
        
        static void AddGambrelRoof(
                FMeshData & OutRoofData,
                const TArray <FVector > & Footprint,
                float BaseZ,
                float RidgeHeight,
                const FColor & Color,
                float RoofDirection = -1.f);
                
        static void AddPyramidalRoof(
                FMeshData& OutRoofData,
                const TArray<FVector>& Footprint,
                float BaseZ,
                float ApexHeight,
                const FColor& Color);

        static void AddSkillionRoof(
                FMeshData& OutRoofData,
                const TArray<FVector>& Footprint,
                float BaseZ,
                float RidgeHeight,
                const FColor& Color,
                float RoofDirection = -1.f);

        /** Barrel-vault / round roof: semi-cylindrical arch along the long axis.
         *  RoofDirection orients the barrel axis (same convention as gabled). */
        static void AddRoundRoof(
                FMeshData& OutRoofData,
                const TArray<FVector>& Footprint,
                float BaseZ,
                float RidgeHeight,
                const FColor& Color,
                float RoofDirection = -1.f,
                const FString& RoofOrientation = TEXT(""),
                const FColor& WallColor = FColor(180, 170, 160));

        static void AddDomeRoof(
                FMeshData& OutRoofData,
                const TArray<FVector>& Footprint,
                float BaseZ,
                float DomeHeight,
                const FColor& Color,
                bool bOnion = false);

        // ---------- Geometry helpers ----------
        static void ComputeFootprintBounds(
                const TArray<FVector>& Footprint,
                float& OutMinX, float& OutMaxX,
                float& OutMinY, float& OutMaxY);

        static FVector ComputePolygonCentroid(const TArray<FVector>& Footprint);

        static TArray<FVector> InsetPolygon(
                const TArray<FVector>& Footprint,
                float InsetDist);

        static bool IsLongerAlongX(const TArray<FVector>& Footprint);

        static void ComputeDataBounds(
                const TArray<FGeoBuilding>& Buildings,
                const TArray<FGeoRoad>& Roads,
                const TArray<FGeoWater>& WaterBodies,
                const TArray<FGeoVegetation>& Vegetations,
                float& OutMinX, float& OutMaxX, float& OutMinY, float& OutMaxY);

        static bool IsPointInTriangle2D(
                const FVector2D& P,
                const FVector2D& A, const FVector2D& B, const FVector2D& C);

        static float ComputeSignedArea2D(const TArray<FVector>& PolygonPoints);

        /** Signed distance from point to infinite line (A->B). Positive=left, Negative=right */
        static float SignedDistanceToLine(
                const FVector2D& Point,
                const FVector2D& LineA,
                const FVector2D& LineB);

        /** Split polygon by ray into sub-polygons (used by skeleton gabled roof) */
        static bool SplitPolygon2D(
                const TArray<FVector2D>& Polygon,
                const FVector2D& RayOrig,
                const FVector2D& RayDir,
                TArray<TArray<FVector2D>>& OutPolygons);

        /** Shrink polygon by offsetting edges inward (used by skeleton mansard and flat roof) */
        static TArray<FVector> InsetPolygonByEdgeOffset(
                const TArray<FVector>& Footprint,
                float InsetDist);

        /** Triangulate a 2D polygon using ear-clipping (used as CDT fallback) */
        static void TriangulatePolygon2D(
                const TArray<FVector2D>& Polygon,
                TArray<int32>& OutTriangles);

        /** Clean a footprint: remove duplicate vertices, ensure CCW winding order.
         *  Returns empty array if fewer than 3 unique vertices remain. */
        static TArray<FVector> CleanFootprint(
                const TArray<FVector>& Footprint,
                float BaseZ,
                float DuplicateTolerance = 0.5f);

        /** Validate CDT triangle indices - returns true if all indices are within bounds.
         *  This prevents the 0xFFFFFFFF x 4 = 17GB OOM crash from invalid CGAL CDT output. */
        static bool ValidateCDTIndices(
                const TArray<int32>& Triangles,
                int32 VertexCount);
};