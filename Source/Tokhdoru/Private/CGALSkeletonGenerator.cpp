// Copyright NazruGeo. All Rights Reserved.

#include "CGALSkeletonGenerator.h"
#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START

#pragma push_macro("check")
#undef check

#pragma push_macro("verify")
#undef verify

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/create_straight_skeleton_2.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Triangulation_vertex_base_2.h>
#include <CGAL/Triangulation_face_base_2.h>
#include <CGAL/Constrained_triangulation_plus_2.h>
#include <CGAL/Polygon_with_holes_2.h>

#pragma pop_macro("verify")
#pragma pop_macro("check")

THIRD_PARTY_INCLUDES_END

typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef CGAL::Polygon_2<K> Polygon_2;
typedef K::Point_2 Point_2;

// Types for Constrained Delaunay Triangulation
typedef CGAL::Triangulation_vertex_base_2<K>             Vb2;
typedef CGAL::Constrained_triangulation_face_base_2<K>   Fb2;
typedef CGAL::Triangulation_data_structure_2<Vb2, Fb2>   Tds2;
typedef CGAL::Constrained_Delaunay_triangulation_2<K, Tds2> CDT2;
typedef CGAL::Constrained_triangulation_plus_2<CDT2>      CDTPlus;

// ============================================================================
// GenerateSkeleton
// ============================================================================
FCGALSkeletonResult FCGALSkeletonGenerator::GenerateSkeleton(
	const TArray<FVector2D>& InPolygon)
{
	FCGALSkeletonResult Result;

	if (InPolygon.Num() < 3)
	{
		return Result;
	}

	Polygon_2 Poly;
	for (const FVector2D& Point : InPolygon)
	{
		Poly.push_back(K::Point_2(Point.X, Point.Y));
	}

	if (!Poly.is_simple())
	{
		UE_LOG(LogTemp, Error, TEXT("CGAL Polygon is not simple"));
		return Result;
	}

	// Use auto to let the compiler deduce the correct smart pointer type
	// (std::shared_ptr in CGAL 5.x, boost::shared_ptr in older versions)
	auto Skeleton =
		CGAL::create_interior_straight_skeleton_2(
			Poly.vertices_begin(),
			Poly.vertices_end()
		);

	if (!Skeleton)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create CGAL Skeleton"));
		return Result;
	}

	// Build vertex map: CGAL vertex handle -> our index
	TMap<intptr_t, int32> VertexIndexMap;
	float MaxTime = 0.f;

	// First pass: collect all vertices
	for (auto HVit = Skeleton->halfedges_begin();
	     HVit != Skeleton->halfedges_end();
	     ++HVit)
	{
		// Only process each vertex once (via the halfedge that has the smaller id)
		auto VH = HVit->vertex();
		intptr_t VHKey = reinterpret_cast<intptr_t>(&*VH);

		if (!VertexIndexMap.Contains(VHKey))
		{
			int32 Idx = Result.Vertices.Num();
			VertexIndexMap.Add(VHKey, Idx);

			const auto& Pt = VH->point();
			FVector2D Pos(
				static_cast<float>(CGAL::to_double(Pt.x())),
				static_cast<float>(CGAL::to_double(Pt.y()))
			);

			float Time = static_cast<float>(VH->time());
			bool bIsBoundary = (Time < 0.001f);

			Result.Vertices.Add(FCGALSkeletonVertex(Pos, Time, bIsBoundary));

			if (Time > MaxTime)
			{
				MaxTime = Time;
			}
		}
	}

	Result.MaxTime = MaxTime;

	// Second pass: collect edges
	for (auto HVit = Skeleton->halfedges_begin();
	     HVit != Skeleton->halfedges_end();
	     ++HVit)
	{
		// Only process each edge once (skip opposite halfedges)
		if (HVit->id() > HVit->opposite()->id())
		{
			continue;
		}

		auto VHA = HVit->vertex();
		auto VHB = HVit->opposite()->vertex();

		intptr_t KeyA = reinterpret_cast<intptr_t>(&*VHA);
		intptr_t KeyB = reinterpret_cast<intptr_t>(&*VHB);

		int32* IdxA = VertexIndexMap.Find(KeyA);
		int32* IdxB = VertexIndexMap.Find(KeyB);

		if (IdxA && IdxB)
		{
			bool bIsBisector = HVit->is_bisector();
			Result.Edges.Add(FCGALSkeletonEdge(*IdxA, *IdxB, bIsBisector));
		}
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("Generated CGAL Skeleton: %d vertices, %d edges, maxTime=%.1f"),
		Result.Vertices.Num(), Result.Edges.Num(), Result.MaxTime);

	return Result;
}

// ============================================================================
// TriangulateWithCGAL
// Uses CGAL Constrained Delaunay Triangulation with Steiner point insertion
// for robust triangulation of arbitrary (including concave) polygons.
// ============================================================================
void FCGALSkeletonGenerator::TriangulateWithCGAL(
	const TArray<FVector2D>& Polygon,
	TArray<int32>& OutTriangles,
	TArray<FVector2D>& OutSteinerPoints)
{
	OutTriangles.Empty();
	OutSteinerPoints.Empty();

	if (Polygon.Num() < 3)
	{
		return;
	}

	CDTPlus cdt;

	// Insert polygon as constraints
	TArray<CDTPlus::Vertex_handle> VertHandles;
	VertHandles.Reserve(Polygon.Num());

	for (const FVector2D& Pt : Polygon)
	{
		auto VH = cdt.insert(Point_2(Pt.X, Pt.Y));
		VertHandles.Add(VH);
	}

	// Add constraints (polygon edges)
	for (int32 i = 0; i < Polygon.Num(); i++)
	{
		int32 Next = (i + 1) % Polygon.Num();
		cdt.insert_constraint(VertHandles[i], VertHandles[Next]);
	}

	// Check if the CDT has valid faces
	if (cdt.number_of_faces() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CGAL CDT produced no faces"));
		return;
	}

	// Build index map: CDT vertex handle -> output index
	TMap<intptr_t, int32> VertexIndexMap;

	// First, add the original polygon vertices
	for (int32 i = 0; i < Polygon.Num(); i++)
	{
		intptr_t Key = reinterpret_cast<intptr_t>(&*VertHandles[i]);
		VertexIndexMap.Add(Key, i);
	}

	// Then, add Steiner points
	int32 SteinerCount = 0;
	for (auto VIt = cdt.vertices_begin(); VIt != cdt.vertices_end(); ++VIt)
	{
		intptr_t Key = reinterpret_cast<intptr_t>(&*VIt);
		if (!VertexIndexMap.Contains(Key))
		{
			// This is a Steiner point
			FVector2D SteinerPt(
				static_cast<float>(CGAL::to_double(VIt->point().x())),
				static_cast<float>(CGAL::to_double(VIt->point().y()))
			);
			OutSteinerPoints.Add(SteinerPt);
			VertexIndexMap.Add(Key, Polygon.Num() + SteinerCount);
			SteinerCount++;
		}
	}

	// Extract triangles — only interior faces (not infinite faces)
	int32 TotalVertices = Polygon.Num() + OutSteinerPoints.Num();

	for (auto FIt = cdt.finite_faces_begin(); FIt != cdt.finite_faces_end(); ++FIt)
	{
		int32 Idx0 = -1, Idx1 = -1, Idx2 = -1;

		intptr_t Key0 = reinterpret_cast<intptr_t>(&*(FIt->vertex(0)));
		intptr_t Key1 = reinterpret_cast<intptr_t>(&*(FIt->vertex(1)));
		intptr_t Key2 = reinterpret_cast<intptr_t>(&*(FIt->vertex(2)));

		int32* V0 = VertexIndexMap.Find(Key0);
		int32* V1 = VertexIndexMap.Find(Key1);
		int32* V2 = VertexIndexMap.Find(Key2);

		if (V0 && V1 && V2)
		{
			// Validate indices
			if (*V0 >= 0 && *V0 < TotalVertices &&
				*V1 >= 0 && *V1 < TotalVertices &&
				*V2 >= 0 && *V2 < TotalVertices &&
				*V0 != *V1 && *V0 != *V2 && *V1 != *V2)
			{
				OutTriangles.Add(*V0);
				OutTriangles.Add(*V1);
				OutTriangles.Add(*V2);
			}
		}
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("CGAL CDT: %d input verts, %d Steiner points, %d triangles"),
		Polygon.Num(), SteinerCount, OutTriangles.Num() / 3);
}
