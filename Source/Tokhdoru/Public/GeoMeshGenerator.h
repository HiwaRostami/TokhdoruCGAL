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
 * Roof generation uses ONLY CGAL Straight Skeleton for accurate
 * roof geometry on arbitrary (non-rectangular) building footprints.
 * Legacy AABB-based roof generators have been removed.
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
		TArray<FColor> VertexColors;
		TArray<FProcMeshTangent> Tangents;

		void Reset()
		{
			Vertices.Empty();
			Triangles.Empty();
			Normals.Empty();
			UV0.Empty();
			VertexColors.Empty();
			Tangents.Empty();
		}

		void Append(const FMeshData& Other)
		{
			int32 VertexOffset = Vertices.Num();
			Vertices.Append(Other.Vertices);
			Normals.Append(Other.Normals);
			UV0.Append(Other.UV0);
			VertexColors.Append(Other.VertexColors);
			Tangents.Append(Other.Tangents);
			for (int32 Idx : Other.Triangles)
				Triangles.Add(Idx + VertexOffset);
		}
	};

	// ---------- Settings ----------
	float ScaleFactor; // meters -> UE units (default 100.0 = m->cm)

	/** Whether to use CGAL Straight Skeleton for roof generation (default: true).
	 *  When false, falls back to AABB-based approximations. */
	static bool bUseCGALSkeleton;

	// ---------- Constructor ----------
	FGeoMeshGenerator() : ScaleFactor(100.0f) {}

	// ---------- Mesh Generation Methods ----------
	void GenerateBuildingMeshes(
		const TArray<FGeoBuilding>& Buildings,
		FMeshData& OutWallData,
		FMeshData& OutGlassData,
		FMeshData& OutBrickData,
		FMeshData& OutDarkStoneData,
		FMeshData& OutLightStoneData);

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
		FMeshData& OutGroundData,
		float GroundOffset = 0.0f);

	void GenerateRoofMeshes(
		const TArray<FGeoBuilding>& Buildings,
		FMeshData& OutRoofData);

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

	// ---------- Roof geometry router ----------
	/** Dispatch to the appropriate roof generator based on ERoofShape */
	static void AddRoofGeometry(
		FMeshData& OutRoofData,
		ERoofShape RoofShape,
		const TArray<FVector>& LocalFootprint,
		float WallTopZ,
		float RidgeHeight,
		const FColor& RoofFColor);

	// ---------- CGAL Skeleton-based roof generators ----------

	/** Generate a hipped roof using CGAL Straight Skeleton.
	 *  Each skeleton vertex's time value determines roof height at that point.
	 *  This produces accurate results for any polygon shape, not just rectangles. */
	static void AddSkeletonHippedRoof(
		FMeshData& OutRoofData,
		const TArray<FVector>& Footprint,
		float BaseZ,
		float RidgeHeight,
		const FColor& Color);

	/** Generate a gabled roof using CGAL Straight Skeleton.
	 *  Uses skeleton to find the ridge line and compute accurate face slopes. */
	static void AddSkeletonGabledRoof(
		FMeshData& OutRoofData,
		const TArray<FVector>& Footprint,
		float BaseZ,
		float RidgeHeight,
		const FColor& Color);

	/** Generate a mansard/gambrel roof using CGAL Skeleton for the inner offset curve. */
	static void AddSkeletonMansardRoof(
		FMeshData& OutRoofData,
		const TArray<FVector>& Footprint,
		float BaseZ,
		float RidgeHeight,
		const FColor& Color);

	// ---------- Non-AABB roof generators (CGAL fallback) ----------

	/** Gabled roof using AABB-based ridge detection and SplitPolygon2D */
	static void AddGabledRoof(
		FMeshData& OutRoofData,
		const TArray<FVector>& Footprint,
		float BaseZ,
		float RidgeHeight,
		const FColor& Color);

	/** Hipped roof using AABB-based ridge detection */
	static void AddHippedRoof(
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
		const FColor& Color);

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
