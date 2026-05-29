#include "CGALSkeletonImpl.h"  // First include - satisfies UBT "first header" rule

// ========================================================================
// CRITICAL: Undefine NDEBUG before including CGAL headers
// UE5 defines NDEBUG in Development/Shipping, which triggers
// CGAL's test-suite #error check.
// ========================================================================
#ifdef NDEBUG
#undef NDEBUG
#endif

// ========================================================================
// CGAL Configuration - MUST be before any CGAL #include
// ========================================================================
#ifndef CGAL_DISABLE_ROUNDING_MATH_CHECK
#define CGAL_DISABLE_ROUNDING_MATH_CHECK 1
#endif
#ifndef CGAL_NO_DEPRECATED_CODE
#define CGAL_NO_DEPRECATED_CODE 1
#endif
#ifndef CGAL_NO_ASSERTIONS
#define CGAL_NO_ASSERTIONS 1
#endif

// Force-disable CGAL_TEST_SUITE
#ifdef CGAL_TEST_SUITE
#undef CGAL_TEST_SUITE
#endif
#define CGAL_TEST_SUITE 0

// Prevent CGAL from trying to auto-detect compiler features
// that trigger "not defined as preprocessor macro" warnings
#ifndef _M_IX86_FP
#define _M_IX86_FP 2  // MSVC x64 default: SSE2
#endif

// ========================================================================
// Suppress all MSVC warnings from CGAL headers
// C4668 = 'macro' is not defined as preprocessor macro
// ========================================================================
#pragma warning(push)
#pragma warning(disable: 4668 4996 4512 4267 4244 4702 4005 4456 4458 4457 4189 4701 4505 4250 4275 4661 4703 4459 4626 4625 4624 4464)

// ========================================================================
// STEP 1: Include CGAL's config.h FIRST
// This is auto-generated and may define CGAL_USE_LEDA=1 if it was
// configured that way. We'll override it right after.
// ========================================================================
#include <CGAL/config.h>

// ========================================================================
// STEP 2: Force-UNDEFINE LEDA and other features we don't have
// Using #undef instead of #define 0 because CGAL checks with
// both #if and #ifdef — #define 0 would make #ifdef TRUE!
// ========================================================================
#ifdef CGAL_USE_LEDA
#undef CGAL_USE_LEDA
#endif
#ifdef CGAL_USE_CORE
#undef CGAL_USE_CORE
#endif

// ========================================================================
// STEP 3: Now include the actual CGAL headers
// ========================================================================
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/Polygon_with_holes_2.h>
#include <CGAL/create_straight_skeleton_2.h>
#include <CGAL/Straight_skeleton_2.h>
#include <CGAL/Straight_skeleton_face_base_2.h>
#include <CGAL/Straight_skeleton_vertex_base_2.h>
#include <CGAL/Straight_skeleton_halfedge_base_2.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Constrained_triangulation_plus_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>
#include <CGAL/Polygon_2_algorithms.h>

#include <memory>
#include <vector>
#include <map>
#include <queue>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <iostream>

#pragma warning(pop)

// ========================================================================
// CGAL Type Definitions
// ========================================================================

typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
typedef K::Point_2    Point_2;
// NOTE: Do NOT typedef FT at global scope - it conflicts with UE5's FT.
// Use K::FT directly where needed inside function bodies.
// typedef K::FT FT;  // REMOVED: causes 'FT hides global declaration'

typedef CGAL::Polygon_2<K>       Polygon_2;

// Straight Skeleton types
typedef CGAL::Straight_skeleton_2<K>  Skeleton_2;
typedef std::shared_ptr<Skeleton_2>   SkeletonPtr;

// CDT types with face info (for inside/outside marking)
struct FaceInfo
{
    bool bIsInside;
    FaceInfo() : bIsInside(false) {}
};

typedef CGAL::Triangulation_vertex_base_2<K>                           VB;
typedef CGAL::Constrained_triangulation_face_base_2<K>                 CFB_Base;
typedef CGAL::Triangulation_face_base_with_info_2<FaceInfo, K>         FBWI;
typedef CGAL::Constrained_triangulation_face_base_2<K, FBWI>           CFB;
typedef CGAL::Triangulation_data_structure_2<VB, CFB>                  TDS;
typedef CGAL::Constrained_Delaunay_triangulation_2<K, TDS>             CDT;
typedef CDT::Face_handle                                               FaceHandle;

// ========================================================================
// Helper: Build CGAL polygon from flat coordinate array
// ========================================================================
static bool BuildPolygon(const std::vector<double>& Points, Polygon_2& OutPoly)
{
    OutPoly.clear();
    for (size_t i = 0; i + 1 < Points.size(); i += 2)
    {
        OutPoly.push_back(Point_2(Points[i], Points[i + 1]));
    }

    if (OutPoly.size() < 3)
        return false;

    // Remove duplicate closing vertex
    if (OutPoly.size() >= 2 &&
        CGAL::squared_distance(OutPoly.vertex(0), OutPoly.vertex(OutPoly.size() - 1)) < 0.01)
    {
        Polygon_2 Clean;
        for (auto it = OutPoly.vertices_begin(); it != OutPoly.vertices_end() - 1; ++it)
            Clean.push_back(*it);
        OutPoly = Clean;
    }

    if (OutPoly.size() < 3)
        return false;

    // Check polygon simplicity - CGAL requires simple polygons
    if (!OutPoly.is_simple())
    {
        // Non-simple polygons cannot be processed by CGAL skeleton
        return false;
    }

    // Ensure CCW orientation
    if (OutPoly.orientation() != CGAL::COUNTERCLOCKWISE)
    {
        OutPoly.reverse_orientation();
    }

    return true;
}

// ========================================================================
// GenerateSkeleton - Full skeleton with vertex time data
// ========================================================================
CGALSkeletonResult CGAL_GenerateSkeleton(const std::vector<double>& PolygonPoints)
{
    CGALSkeletonResult Result;
    Result.MaxTime = 0.0;

    Polygon_2 Poly;
    if (!BuildPolygon(PolygonPoints, Poly))
        return Result;

    // Create straight skeleton with try-catch
    SkeletonPtr Skeleton;
    try
    {
        Skeleton = CGAL::create_interior_straight_skeleton_2(
            Poly.vertices_begin(), Poly.vertices_end());
    }
    catch (const std::exception& e)
    {
        // CGAL threw an exception during skeleton creation
        // This can happen with degenerate or nearly-degenerate polygons
        return Result;
    }
    catch (...)
    {
        // Unknown exception
        return Result;
    }

    if (!Skeleton)
        return Result;

    // Build vertex map: CGAL vertex pointer -> our index
    std::map<const Skeleton_2::Vertex*, int> VertexToIndex;
    int NextIdx = 0;

    for (auto VIt = Skeleton->vertices_begin(); VIt != Skeleton->vertices_end(); ++VIt)
    {
        const Point_2& P = VIt->point();
        double Time = CGAL::to_double(VIt->time());

        CGALSkeletonVertex V;
        V.X = CGAL::to_double(P.x());
        V.Y = CGAL::to_double(P.y());
        V.Time = Time;

        int Idx = NextIdx++;
        Result.Vertices.push_back(V);
        VertexToIndex[&*VIt] = Idx;

        if (Time > Result.MaxTime)
            Result.MaxTime = Time;
    }

    // Extract edges (only once per halfedge pair)
    for (auto HIt = Skeleton->halfedges_begin(); HIt != Skeleton->halfedges_end(); ++HIt)
    {
        if (HIt->id() > HIt->opposite()->id())
            continue;

        auto ItA = VertexToIndex.find(&*(HIt->vertex()));
        auto ItB = VertexToIndex.find(&*(HIt->opposite()->vertex()));

        if (ItA == VertexToIndex.end() || ItB == VertexToIndex.end())
            continue;

        CGALSkeletonEdge E;
        E.VertexA = ItA->second;
        E.VertexB = ItB->second;
        E.IsBisector = HIt->is_bisector() ? 1 : 0;
        Result.Edges.push_back(E);
    }

    return Result;
}

// ========================================================================
// GenerateSkeletonEdges - Legacy: edge position pairs
// ========================================================================
std::vector<CGALSkeletonEdgePair> CGAL_GenerateSkeletonEdges(const std::vector<double>& PolygonPoints)
{
    std::vector<CGALSkeletonEdgePair> Result;

    CGALSkeletonResult Skel = CGAL_GenerateSkeleton(PolygonPoints);

    for (const CGALSkeletonEdge& E : Skel.Edges)
    {
        if (!E.IsBisector) continue;
        if (E.VertexA < 0 || E.VertexA >= (int)Skel.Vertices.size()) continue;
        if (E.VertexB < 0 || E.VertexB >= (int)Skel.Vertices.size()) continue;

        const CGALSkeletonVertex& VA = Skel.Vertices[E.VertexA];
        const CGALSkeletonVertex& VB = Skel.Vertices[E.VertexB];

        CGALSkeletonEdgePair Pair;
        Pair.AX = VA.X; Pair.AY = VA.Y;
        Pair.BX = VB.X; Pair.BY = VB.Y;
        Result.push_back(Pair);
    }

    return Result;
}

// ========================================================================
// TriangulateWithCGAL - Constrained Delaunay Triangulation
// ========================================================================
CGALTriangulationResult CGAL_Triangulate(const std::vector<double>& PolygonPoints)
{
    CGALTriangulationResult Result;
    Result.OriginalVertexCount = 0;

    if (PolygonPoints.size() < 6) // need at least 3 points (6 doubles)
        return Result;

    int NumInputVerts = static_cast<int>(PolygonPoints.size() / 2);
    Result.OriginalVertexCount = NumInputVerts;

    // Build points
    std::vector<Point_2> Points;
    Points.reserve(NumInputVerts);
    for (int i = 0; i + 1 < (int)PolygonPoints.size(); i += 2)
    {
        Points.push_back(Point_2(PolygonPoints[i], PolygonPoints[i + 1]));
    }

    // Check for duplicate/collinear points that would cause CDT to crash
    // Remove points that are too close together
    std::vector<Point_2> CleanPoints;
    CleanPoints.reserve(Points.size());
    for (const Point_2& P : Points)
    {
        bool bDuplicate = false;
        for (const Point_2& Existing : CleanPoints)
        {
            if (CGAL::squared_distance(P, Existing) < 0.01)
            {
                bDuplicate = true;
                break;
            }
        }
        if (!bDuplicate)
            CleanPoints.push_back(P);
    }

    if (CleanPoints.size() < 3)
        return Result;

    // Create CDT with polygon edges as constraints
    // Wrap in try-catch because CDT can throw on degenerate/intersecting input
    CDT cdt;
    try
    {
        for (size_t i = 0; i < CleanPoints.size(); i++)
        {
            size_t next = (i + 1) % CleanPoints.size();
            cdt.insert_constraint(CleanPoints[i], CleanPoints[next]);
        }
    }
    catch (const std::exception& e)
    {
        // CDT threw an exception - likely self-intersecting polygon
        // Return empty result so caller can fall back to ear-clipping
        return Result;
    }
    catch (...)
    {
        // Unknown exception during CDT construction
        return Result;
    }

    if (cdt.number_of_vertices() < 3)
        return Result;

    // Mark faces inside/outside using flood-fill from infinite faces.
    // We use bIsInside as a "visited by outside flood" marker during traversal,
    // then flip it at the end so that inside faces have bIsInside=true.
    std::queue<FaceHandle> FaceQueue;
    for (FaceHandle fh : cdt.all_face_handles())
    {
        fh->info().bIsInside = false;   // false = not yet visited by flood-fill
        if (cdt.is_infinite(fh))
        {
            fh->info().bIsInside = true; // Mark infinite faces as visited immediately
            FaceQueue.push(fh);
        }
    }

    while (!FaceQueue.empty())
    {
        FaceHandle fh = FaceQueue.front();
        FaceQueue.pop();

        for (int i = 0; i < 3; i++)
        {
            FaceHandle ni = fh->neighbor(i);
            // Create edge from face handle + index
            CDT::Edge edge(fh, i);
            // Only traverse across non-constrained edges to unvisited faces
            if (!cdt.is_constrained(edge) && !ni->info().bIsInside)
            {
                ni->info().bIsInside = true;  // Mark as visited BEFORE enqueuing
                FaceQueue.push(ni);
            }
        }
    }

    // Now bIsInside=true means "reached from outside" (i.e., OUTSIDE).
    // Flip: faces NOT reached by flood-fill are INSIDE.
    for (FaceHandle fh : cdt.finite_face_handles())
    {
        fh->info().bIsInside = !fh->info().bIsInside;
    }

    // Build vertex index map
    std::map<Point_2, int> PointToIndex;
    int NextIdx = 0;

    // First, add all original polygon vertices
    for (int i = 0; i < NumInputVerts; i++)
    {
        Point_2 P(PolygonPoints[i * 2], PolygonPoints[i * 2 + 1]);
        if (PointToIndex.find(P) == PointToIndex.end())
        {
            PointToIndex[P] = NextIdx++;
        }
    }

    // Collect inside triangles
    for (FaceHandle fh : cdt.finite_face_handles())
    {
        if (!fh->info().bIsInside)
            continue;

        int TriIndices[3];
        for (int i = 0; i < 3; i++)
        {
            Point_2 P = fh->vertex(i)->point();
            auto It = PointToIndex.find(P);
            if (It != PointToIndex.end())
            {
                TriIndices[i] = It->second;
            }
            else
            {
                // Steiner point
                TriIndices[i] = NextIdx;
                PointToIndex[P] = NextIdx;
                Result.SteinerPoints.push_back(CGAL::to_double(P.x()));
                Result.SteinerPoints.push_back(CGAL::to_double(P.y()));
                NextIdx++;
            }
        }

        Result.Triangles.push_back(TriIndices[0]);
        Result.Triangles.push_back(TriIndices[1]);
        Result.Triangles.push_back(TriIndices[2]);
    }

    return Result;
}
