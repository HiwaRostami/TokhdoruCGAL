// Copyright NazruGeo. All Rights Reserved.

#include "GeoMeshGenerator.h"
#include "ProceduralMeshComponent.h"
#include "Algo/Reverse.h"

// ============================================================================
// Static member definition (must be defined exactly once in a .cpp file)
// ============================================================================
bool FGeoMeshGenerator::bUseCGALSkeleton = true;

// ============================================================================
// Local helper: Convert FLinearColor to FColor
// ============================================================================
static FColor LinearToFColor(const FLinearColor& LC)
{
        return FColor(
                (uint8)FMath::Clamp(FMath::RoundToInt(LC.R * 255.f), 0, 255),
                (uint8)FMath::Clamp(FMath::RoundToInt(LC.G * 255.f), 0, 255),
                (uint8)FMath::Clamp(FMath::RoundToInt(LC.B * 255.f), 0, 255),
                255);
}

// ============================================================================
// Local helper: RecomputeNormals from triangle faces
// ============================================================================
static void RecomputeNormals(FGeoMeshGenerator::FMeshData& MeshData)
{
        MeshData.Normals.Init(FVector::ZeroVector, MeshData.Vertices.Num());

        for (int32 ti = 0; ti + 2 < MeshData.Triangles.Num(); ti += 3)
        {
                int32 I0 = MeshData.Triangles[ti];
                int32 I1 = MeshData.Triangles[ti + 1];
                int32 I2 = MeshData.Triangles[ti + 2];

                if (I0 >= MeshData.Vertices.Num() ||
                        I1 >= MeshData.Vertices.Num() ||
                        I2 >= MeshData.Vertices.Num())
                        continue;

                const FVector& V0 = MeshData.Vertices[I0];
                const FVector& V1 = MeshData.Vertices[I1];
                const FVector& V2 = MeshData.Vertices[I2];

                FVector FaceN = FVector::CrossProduct(V1 - V0, V2 - V0);
                if (!FaceN.IsNearlyZero())
                {
                        FaceN.Normalize();
                        FaceN = -FaceN; // Reverse normal direction for roofs
                        MeshData.Normals[I0] += FaceN;
                        MeshData.Normals[I1] += FaceN;
                        MeshData.Normals[I2] += FaceN;
                }
        }

        for (int32 i = 0; i < MeshData.Normals.Num(); i++)
                if (!MeshData.Normals[i].IsNearlyZero())
                        MeshData.Normals[i].Normalize();
}

// ============================================================================
// Local helper: RecomputeTangents
// ============================================================================
static void RecomputeTangents(FGeoMeshGenerator::FMeshData& MeshData)
{
        MeshData.Tangents.SetNum(MeshData.Vertices.Num());
        for (int32 i = 0; i < MeshData.Vertices.Num(); i++)
        {
                const FVector& N = MeshData.Normals[i];
                FVector Tangent(1, 0, 0);
                if (FMath::Abs(FVector::DotProduct(Tangent, N)) > 0.9f)
                        Tangent = FVector(0, 1, 0);
                Tangent -= FVector::DotProduct(Tangent, N) * N;
                if (!Tangent.IsNearlyZero()) Tangent.Normalize();
                else Tangent = FVector(1, 0, 0);
                MeshData.Tangents[i] = FProcMeshTangent(Tangent, false);
        }
}

// ============================================================================
// Local helper: FlipNormals (used for roads/ground that need downward-facing)
// ============================================================================
static void FlipNormals(FGeoMeshGenerator::FMeshData& MeshData)
{
        for (FVector& N : MeshData.Normals)
                N = -N;
}

// ============================================================================
// ============================================================================
// PRIMITIVE HELPERS
// ============================================================================
// ============================================================================

// ============================================================================
// AddQuad
// ============================================================================
void FGeoMeshGenerator::AddQuad(
        FMeshData& MeshData,
        const FVector& V0, const FVector& V1,
        const FVector& V2, const FVector& V3,
        const FColor& Color)
{
        int32 BI = MeshData.Vertices.Num();
        MeshData.Vertices.Add(V0); MeshData.Vertices.Add(V1);
        MeshData.Vertices.Add(V2); MeshData.Vertices.Add(V3);

        MeshData.Triangles.Add(BI + 0); MeshData.Triangles.Add(BI + 1); MeshData.Triangles.Add(BI + 2);
        MeshData.Triangles.Add(BI + 0); MeshData.Triangles.Add(BI + 2); MeshData.Triangles.Add(BI + 3);

        FVector E1 = V1 - V0, E2 = V3 - V0;
        FVector Normal = FVector::CrossProduct(E1, E2);
        if (Normal.IsNearlyZero()) Normal = FVector::CrossProduct(V2 - V0, V3 - V1);
        if (Normal.IsNearlyZero()) Normal = FVector(0, 0, 1);
        Normal.Normalize();

        FVector Tangent = E1;
        Tangent -= FVector::DotProduct(Tangent, Normal) * Normal;
        if (!Tangent.IsNearlyZero()) Tangent.Normalize(); else Tangent = FVector(1, 0, 0);

        float UScale = 100.0f, VScale = 100.0f;
        for (int32 i = 0; i < 4; i++)
        {
                MeshData.Normals.Add(Normal);
                MeshData.VertexColors.Add(Color);
                MeshData.Tangents.Add(FProcMeshTangent(Tangent, false));
        }
        MeshData.UV0.Add(FVector2D(0, V3.Z / VScale));
        MeshData.UV0.Add(FVector2D(FVector2D::Distance(FVector2D(V0.X, V0.Y), FVector2D(V1.X, V1.Y)) / UScale, V3.Z / VScale));
        MeshData.UV0.Add(FVector2D(FVector2D::Distance(FVector2D(V0.X, V0.Y), FVector2D(V1.X, V1.Y)) / UScale, V2.Z / VScale));
        MeshData.UV0.Add(FVector2D(0, V2.Z / VScale));
}

// ============================================================================
// AddTriangle
// ============================================================================
void FGeoMeshGenerator::AddTriangle(
        FMeshData& MeshData,
        const FVector& V0, const FVector& V1, const FVector& V2,
        const FColor& Color)
{
        int32 BI = MeshData.Vertices.Num();
        MeshData.Vertices.Add(V0); MeshData.Vertices.Add(V1); MeshData.Vertices.Add(V2);
        MeshData.Triangles.Add(BI); MeshData.Triangles.Add(BI + 1); MeshData.Triangles.Add(BI + 2);

        FVector Normal = FVector::CrossProduct(V1 - V0, V2 - V0);
        if (Normal.IsNearlyZero()) Normal = FVector(0, 0, 1);
        Normal.Normalize();

        FVector Tangent = V1 - V0;
        Tangent -= FVector::DotProduct(Tangent, Normal) * Normal;
        if (!Tangent.IsNearlyZero()) Tangent.Normalize(); else Tangent = FVector(1, 0, 0);

        for (int32 i = 0; i < 3; i++)
        {
                MeshData.Normals.Add(Normal);
                MeshData.VertexColors.Add(Color);
                MeshData.Tangents.Add(FProcMeshTangent(Tangent, false));
        }
        MeshData.UV0.Add(FVector2D(0, 0)); MeshData.UV0.Add(FVector2D(1, 0)); MeshData.UV0.Add(FVector2D(0.5f, 1));
}

// ============================================================================
// AddDoubleSidedQuad
// ============================================================================
void FGeoMeshGenerator::AddDoubleSidedQuad(
        FMeshData& MeshData, const FVector& V0, const FVector& V1,
        const FVector& V2, const FVector& V3, const FColor& Color)
{
        AddQuad(MeshData, V0, V1, V2, V3, Color);
        AddQuad(MeshData, V3, V2, V1, V0, Color);
}

// ============================================================================
// AddPolygonTriangulated
// ============================================================================
void FGeoMeshGenerator::AddPolygonTriangulated(
        FMeshData& MeshData, const TArray<FVector>& Points,
        float Height, const FColor& Color, bool bUseVertexZ)
{
        TArray<FVector> WorkingPoints;
        for (const FVector& P : Points)
        {
                FVector TargetPos = FVector(P.X, P.Y, bUseVertexZ ? P.Z : Height);
                if (WorkingPoints.Num() > 0 && TargetPos.Equals(WorkingPoints[0], 1.0f)) continue;
                if (WorkingPoints.Num() == 0 || !TargetPos.Equals(WorkingPoints.Last(), 1.0f))
                {
                        WorkingPoints.Add(TargetPos);
                }
        }

        int32 N = WorkingPoints.Num();
        if (N < 3) return;

        int32 BI = MeshData.Vertices.Num();
        FVector GlobalTangent = FVector(1, 0, 0);
        if (N > 1)
        {
                GlobalTangent = (WorkingPoints[1] - WorkingPoints[0]);
                GlobalTangent.Z = 0.f;
                GlobalTangent.Normalize();
        }

        for (const FVector& P : WorkingPoints)
        {
                MeshData.Vertices.Add(P);
                MeshData.Normals.Add(FVector(0.f, 0.f, 1.f));
                MeshData.VertexColors.Add(Color);
                MeshData.Tangents.Add(FProcMeshTangent(GlobalTangent, false));
                MeshData.UV0.Add(FVector2D(P.X / 100.f, P.Y / 100.f));
        }

        TArray<int32> Idx;
        for (int32 i = 0; i < N; i++) Idx.Add(i);

        int32 Fail = 0;
        while (Idx.Num() > 3)
        {
                bool bEar = false;
                int32 NI = Idx.Num();
                for (int32 i = 0; i < NI; i++)
                {
                        int32 ip = (i == 0) ? NI - 1 : i - 1;
                        int32 in = (i + 1) % NI;
                        int32 Ip = Idx[ip], Ic = Idx[i], In = Idx[in];

                        FVector2D vp(WorkingPoints[Ip].X, WorkingPoints[Ip].Y);
                        FVector2D vc(WorkingPoints[Ic].X, WorkingPoints[Ic].Y);
                        FVector2D vn(WorkingPoints[In].X, WorkingPoints[In].Y);

                        float Cross = (vc.X - vp.X) * (vn.Y - vp.Y) - (vc.Y - vp.Y) * (vn.X - vp.X);
                        if (FMath::Abs(Cross) <= 0.01f) continue;

                        bool bIsEar = true;
                        for (int32 j = 0; j < NI; j++)
                        {
                                if (j == ip || j == i || j == in) continue;
                                FVector2D tp(WorkingPoints[Idx[j]].X, WorkingPoints[Idx[j]].Y);
                                if (IsPointInTriangle2D(tp, vp, vc, vn)) { bIsEar = false; break; }
                        }
                        if (bIsEar)
                        {
                                MeshData.Triangles.Add(BI + Ip);
                                MeshData.Triangles.Add(BI + Ic);
                                MeshData.Triangles.Add(BI + In);

                                Idx.RemoveAt(i);
                                bEar = true;
                                Fail = 0;
                                break;
                        }
                }
                if (!bEar)
                {
                        Fail++;
                        if (Fail > NI * 3)
                        {
                                for (int32 i = 1; i < Idx.Num() - 1; i++)
                                {
                                        MeshData.Triangles.Add(BI + Idx[0]);
                                        MeshData.Triangles.Add(BI + Idx[i]);
                                        MeshData.Triangles.Add(BI + Idx[i + 1]);
                                }
                                return;
                        }
                }
        }
        if (Idx.Num() == 3)
        {
                MeshData.Triangles.Add(BI + Idx[0]);
                MeshData.Triangles.Add(BI + Idx[1]);
                MeshData.Triangles.Add(BI + Idx[2]);
        }
}

// ============================================================================
// AddExtrudedPolygon
// ============================================================================
void FGeoMeshGenerator::AddExtrudedPolygon(
        FMeshData& MeshData, const TArray<FVector>& Points,
        float BaseH, float TopH, const FColor& WallColor, bool bBottom, const FColor& BotColor)
{
        int32 N = Points.Num(); if (N < 3) return;

        TArray<FVector> WorkPoints = Points;
        if (ComputeSignedArea2D(WorkPoints) < 0.f)
                Algo::Reverse(WorkPoints);

        for (int32 i = 0; i < N; i++)
        {
                int32 Next = (i + 1) % N;
                FVector B0(WorkPoints[i].X, WorkPoints[i].Y, BaseH), B1(WorkPoints[Next].X, WorkPoints[Next].Y, BaseH);
                FVector T1(WorkPoints[Next].X, WorkPoints[Next].Y, TopH), T0(WorkPoints[i].X, WorkPoints[i].Y, TopH);
                FVector EdgeDir = B1 - B0;
                FVector OutN = FVector(EdgeDir.Y, -EdgeDir.X, 0);

                if (!OutN.IsNearlyZero()) OutN.Normalize(); else OutN = FVector(0, 0, 1);
                FVector Tang = EdgeDir; if (!Tang.IsNearlyZero()) Tang.Normalize(); else Tang = FVector(1, 0, 0);
                int32 BI = MeshData.Vertices.Num();

                MeshData.Vertices.Add(B0); MeshData.Vertices.Add(B1); MeshData.Vertices.Add(T1); MeshData.Vertices.Add(T0);
                MeshData.Triangles.Add(BI + 0); MeshData.Triangles.Add(BI + 2); MeshData.Triangles.Add(BI + 1);
                MeshData.Triangles.Add(BI + 0); MeshData.Triangles.Add(BI + 3); MeshData.Triangles.Add(BI + 2);

                float WW = FVector2D::Distance(FVector2D(B0.X, B0.Y), FVector2D(B1.X, B1.Y)) / 100.f;
                MeshData.UV0.Add(FVector2D(0, BaseH / 100.f)); MeshData.UV0.Add(FVector2D(WW, BaseH / 100.f));
                MeshData.UV0.Add(FVector2D(WW, TopH / 100.f)); MeshData.UV0.Add(FVector2D(0, TopH / 100.f));
                for (int32 v = 0; v < 4; v++) { MeshData.Normals.Add(OutN); MeshData.VertexColors.Add(WallColor); MeshData.Tangents.Add(FProcMeshTangent(Tang, false)); }
        }
        if (bBottom) AddPolygonTriangulated(MeshData, WorkPoints, BaseH, BotColor);
}

// ============================================================================
// ============================================================================
// GEOMETRY HELPERS
// ============================================================================
// ============================================================================

// ============================================================================
// IsPointInTriangle2D
// ============================================================================
bool FGeoMeshGenerator::IsPointInTriangle2D(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
        float d1 = (P.X - C.X) * (A.Y - C.Y) - (A.X - C.X) * (P.Y - C.Y);
        float d2 = (P.X - A.X) * (B.Y - A.Y) - (B.X - A.X) * (P.Y - A.Y);
        float d3 = (P.X - B.X) * (C.Y - B.Y) - (C.X - B.X) * (P.Y - B.Y);
        return !(((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0)));
}

// ============================================================================
// ComputeSignedArea2D
// ============================================================================
float FGeoMeshGenerator::ComputeSignedArea2D(const TArray<FVector>& P)
{
        float A = 0.f; int32 N = P.Num();
        for (int32 i = 0; i < N; i++) { int32 j = (i + 1) % N; A += P[i].X * P[j].Y - P[j].X * P[i].Y; }
        return A * 0.5f;
}

// ============================================================================
// SignedDistanceToLine
// Signed distance from point to infinite line A->B
// Formula: (DX*(Point.Y - LineA.Y) - DY*(Point.X - LineA.X)) / Len
// Positive = left side, Negative = right side
// ============================================================================
float FGeoMeshGenerator::SignedDistanceToLine(
        const FVector2D& Point, const FVector2D& LineA, const FVector2D& LineB)
{
        float DX = LineB.X - LineA.X;
        float DY = LineB.Y - LineA.Y;
        float Len = FMath::Sqrt(DX * DX + DY * DY);
        if (Len < 0.0001f) return 0.f;
        return (DX * (Point.Y - LineA.Y) - DY * (Point.X - LineA.X)) / Len;
}

// ============================================================================
// SplitPolygon2D
// Split polygon by ray into sub-polygons (streets.gl algorithm)
// ============================================================================
bool FGeoMeshGenerator::SplitPolygon2D(
        const TArray<FVector2D>& Polygon,
        const FVector2D& RayOrig,
        const FVector2D& RayDir,
        TArray<TArray<FVector2D>>& OutPolygons)
{
        OutPolygons.Empty();
        if (Polygon.Num() < 3) return false;

        struct FIntersection
        {
                int32 EdgeStart;
                int32 EdgeEnd;
                FVector2D Point;
                float T;
                TArray<FVector2D>* Crossback;
        };

        TArray<FIntersection> Intersections;
        int32 N = Polygon.Num();

        FVector2D Start = Polygon[N - 1];
        for (int32 iVert = 0; iVert < N; iVert++)
        {
                FVector2D End = Polygon[iVert];
                float EdgeDX = End.X - Start.X;
                float EdgeDY = End.Y - Start.Y;
                float Den = RayDir.X * EdgeDY - RayDir.Y * EdgeDX;

                if (FMath::Abs(Den) > 0.0001f)
                {
                        float NumS = RayDir.X * (RayOrig.Y - Start.Y) - RayDir.Y * (RayOrig.X - Start.X);
                        float S = NumS / Den;

                        if (S >= -0.001f && S <= 1.001f)
                        {
                                FVector2D P(Start.X + S * EdgeDX, Start.Y + S * EdgeDY);
                                float NumT = EdgeDX * (RayOrig.Y - Start.Y) - EdgeDY * (RayOrig.X - Start.X);
                                float T = NumT / Den;

                                FIntersection Inter;
                                Inter.EdgeStart = (iVert + N - 1) % N;
                                Inter.EdgeEnd = iVert;
                                Inter.Point = P;
                                Inter.T = T;
                                Inter.Crossback = nullptr;
                                Intersections.Add(Inter);
                        }
                }
                Start = End;
        }

        Intersections.Sort([](const FIntersection& A, const FIntersection& B) {
                return A.T < B.T;
        });

        if (Intersections.Num() < 2 || Intersections.Num() % 2 != 0)
                return false;

        OutPolygons.Add(TArray<FVector2D>());
        TArray<FVector2D>* CurPoly = &OutPolygons[0];

        for (int32 iVert = 0; iVert < N; iVert++)
        {
                CurPoly->Add(Polygon[iVert]);

                int32 InterIdx = -1;
                for (int32 j = 0; j < Intersections.Num(); j++)
                {
                        if (Intersections[j].EdgeEnd == iVert)
                        {
                                InterIdx = j;
                                break;
                        }
                }

                if (InterIdx >= 0)
                {
                        CurPoly->Add(Intersections[InterIdx].Point);

                        if (InterIdx % 2 == 0)
                                Intersections[InterIdx + 1].Crossback = CurPoly;
                        else
                                Intersections[InterIdx - 1].Crossback = CurPoly;

                        if (Intersections[InterIdx].Crossback != nullptr)
                                CurPoly = Intersections[InterIdx].Crossback;
                        else
                        {
                                OutPolygons.Add(TArray<FVector2D>());
                                CurPoly = &OutPolygons[OutPolygons.Num() - 1];
                        }

                        CurPoly->Add(Intersections[InterIdx].Point);
                }
        }

        return OutPolygons.Num() >= 2;
}

// ============================================================================
// InsetPolygonByEdgeOffset
// Shrink polygon by offsetting edges inward (robust for concave polygons)
// ============================================================================
TArray<FVector> FGeoMeshGenerator::InsetPolygonByEdgeOffset(
        const TArray<FVector>& Footprint, float InsetDist)
{
        int32 N = Footprint.Num();
        if (N < 3) return Footprint;

        TArray<FVector> WorkFP = Footprint;
        float SignedArea = 0.f;
        for (int32 i = 0; i < N; i++)
        {
                int32 j = (i + 1) % N;
                SignedArea += WorkFP[i].X * WorkFP[j].Y - WorkFP[j].X * WorkFP[i].Y;
        }
        if (SignedArea < 0.f) Algo::Reverse(WorkFP);

        TArray<FVector> Result;
        Result.Reserve(N);

        for (int32 i = 0; i < N; i++)
        {
                int32 Prev = (i == 0) ? N - 1 : i - 1;
                int32 Next = (i + 1) % N;

                FVector2D PPrev(WorkFP[Prev].X, WorkFP[Prev].Y);
                FVector2D PCurr(WorkFP[i].X, WorkFP[i].Y);
                FVector2D PNext(WorkFP[Next].X, WorkFP[Next].Y);

                FVector2D Edge1 = PCurr - PPrev;
                float Edge1Len = Edge1.Size();
                if (Edge1Len < 0.001f) { Result.Add(WorkFP[i]); continue; }
                Edge1 /= Edge1Len;
                FVector2D Norm1(Edge1.Y, -Edge1.X);

                FVector2D Edge2 = PNext - PCurr;
                float Edge2Len = Edge2.Size();
                if (Edge2Len < 0.001f) { Result.Add(WorkFP[i]); continue; }
                Edge2 /= Edge2Len;
                FVector2D Norm2(Edge2.Y, -Edge2.X);

                FVector2D L1A = PPrev + Norm1 * InsetDist;
                FVector2D L1B = PCurr + Norm1 * InsetDist;
                FVector2D L2A = PCurr + Norm2 * InsetDist;
                FVector2D L2B = PNext + Norm2 * InsetDist;

                float Den = (L1B.X - L1A.X) * (L2B.Y - L2A.Y) - (L1B.Y - L1A.Y) * (L2B.X - L2A.X);

                if (FMath::Abs(Den) < 0.001f)
                {
                        FVector2D Mid = (L1B + L2A) * 0.5f;
                        Result.Add(FVector(Mid.X, Mid.Y, WorkFP[i].Z));
                }
                else
                {
                        float T = ((L2A.X - L1A.X) * (L2B.Y - L2A.Y) - (L2A.Y - L1A.Y) * (L2B.X - L2A.X)) / Den;
                        T = FMath::Clamp(T, -2.f, 2.f);
                        FVector2D Intersection(L1A.X + T * (L1B.X - L1A.X), L1A.Y + T * (L1B.Y - L1A.Y));
                        Result.Add(FVector(Intersection.X, Intersection.Y, WorkFP[i].Z));
                }
        }

        return Result;
}

// ============================================================================
// TriangulatePolygon2D
// Ear-clipping triangulation of 2D polygon
// ============================================================================
void FGeoMeshGenerator::TriangulatePolygon2D(
        const TArray<FVector2D>& Polygon,
        TArray<int32>& OutTriangles)
{
        OutTriangles.Empty();
        int32 N = Polygon.Num();
        if (N < 3) return;

        TArray<int32> Idx;
        for (int32 i = 0; i < N; i++) Idx.Add(i);

        int32 Fail = 0;
        while (Idx.Num() > 3)
        {
                bool bEar = false;
                int32 NI = Idx.Num();

                for (int32 i = 0; i < NI; i++)
                {
                        int32 ip = (i == 0) ? NI - 1 : i - 1;
                        int32 in2 = (i + 1) % NI;
                        int32 Ip = Idx[ip], Ic = Idx[i], In = Idx[in2];

                        const FVector2D& vp = Polygon[Ip];
                        const FVector2D& vc = Polygon[Ic];
                        const FVector2D& vn = Polygon[In];

                        float Cross = (vc.X - vp.X) * (vn.Y - vp.Y) - (vc.Y - vp.Y) * (vn.X - vp.X);
                        if (FMath::Abs(Cross) <= 0.01f) continue;

                        bool bIsEar = true;
                        for (int32 j = 0; j < NI; j++)
                        {
                                if (j == ip || j == i || j == in2) continue;
                                if (IsPointInTriangle2D(Polygon[Idx[j]], vp, vc, vn))
                                {
                                        bIsEar = false;
                                        break;
                                }
                        }

                        if (bIsEar)
                        {
                                OutTriangles.Add(Ip);
                                OutTriangles.Add(Ic);
                                OutTriangles.Add(In);
                                Idx.RemoveAt(i);
                                bEar = true;
                                Fail = 0;
                                break;
                        }
                }

                if (!bEar)
                {
                        Fail++;
                        if (Fail > NI * 3)
                        {
                                for (int32 i = 1; i < Idx.Num() - 1; i++)
                                {
                                        OutTriangles.Add(Idx[0]);
                                        OutTriangles.Add(Idx[i]);
                                        OutTriangles.Add(Idx[i + 1]);
                                }
                                return;
                        }
                }
        }

        if (Idx.Num() == 3)
        {
                OutTriangles.Add(Idx[0]);
                OutTriangles.Add(Idx[1]);
                OutTriangles.Add(Idx[2]);
        }
}

// ============================================================================
// ComputeFootprintBounds
// ============================================================================
void FGeoMeshGenerator::ComputeFootprintBounds(
        const TArray<FVector>& FP,
        float& MinX, float& MaxX, float& MinY, float& MaxY)
{
        MinX = FLT_MAX; MaxX = -FLT_MAX; MinY = FLT_MAX; MaxY = -FLT_MAX;
        for (const FVector& V : FP)
        {
                if (V.X < MinX) MinX = V.X; if (V.X > MaxX) MaxX = V.X;
                if (V.Y < MinY) MinY = V.Y; if (V.Y > MaxY) MaxY = V.Y;
        }
}

// ============================================================================
// ComputePolygonCentroid
// ============================================================================
FVector FGeoMeshGenerator::ComputePolygonCentroid(const TArray<FVector>& FP)
{
        FVector C(0, 0, 0);
        for (const FVector& V : FP) C += FVector(V.X, V.Y, 0);
        return C / (float)FP.Num();
}

// ============================================================================
// IsLongerAlongX
// ============================================================================
bool FGeoMeshGenerator::IsLongerAlongX(const TArray<FVector>& FP)
{
        float MinX, MaxX, MinY, MaxY;
        ComputeFootprintBounds(FP, MinX, MaxX, MinY, MaxY);
        return (MaxX - MinX) >= (MaxY - MinY);
}

// ============================================================================
// InsetPolygon (centroid-based, for flat roofs)
// ============================================================================
TArray<FVector> FGeoMeshGenerator::InsetPolygon(const TArray<FVector>& FP, float InsetDist)
{
        int32 N = FP.Num();
        if (N < 3) return FP;

        FVector Cen = ComputePolygonCentroid(FP);
        TArray<FVector> Out;
        for (const FVector& V : FP)
        {
                FVector2D Dir2D(Cen.X - V.X, Cen.Y - V.Y);
                float Len = Dir2D.Size();
                if (Len < 0.01f) { Out.Add(V); continue; }
                Dir2D /= Len;
                float MoveAmt = FMath::Min(InsetDist, Len * 0.45f);
                Out.Add(FVector(V.X + Dir2D.X * MoveAmt, V.Y + Dir2D.Y * MoveAmt, V.Z));
        }
        return Out;
}

// ============================================================================
// ComputeDataBounds
// ============================================================================
void FGeoMeshGenerator::ComputeDataBounds(
        const TArray<FGeoBuilding>& Buildings,
        const TArray<FGeoRoad>& Roads,
        const TArray<FGeoWater>& WaterBodies,
        const TArray<FGeoVegetation>& Vegetations,
        float& OutMinX, float& OutMaxX, float& OutMinY, float& OutMaxY)
{
        OutMinX = FLT_MAX; OutMaxX = -FLT_MAX; OutMinY = FLT_MAX; OutMaxY = -FLT_MAX;
        auto Expand = [&](float X, float Y)
        {
                if (X < OutMinX) OutMinX = X; if (X > OutMaxX) OutMaxX = X;
                if (Y < OutMinY) OutMinY = Y; if (Y > OutMaxY) OutMaxY = Y;
        };
        for (const FGeoBuilding& B : Buildings) for (const FVector2D& N : B.Nodes) Expand(N.X, N.Y);
        for (const FGeoRoad& R : Roads) for (const FVector2D& P : R.Points) Expand(P.X, P.Y);
        for (const FGeoWater& W : WaterBodies) for (const FVector2D& P : W.Points) Expand(P.X, P.Y);
        for (const FGeoVegetation& V : Vegetations) for (const FVector2D& P : V.Points) Expand(P.X, P.Y);
}

// ============================================================================
// ============================================================================
// ROOF SHAPE GENERATORS - streets.gl approach
// ============================================================================
// ============================================================================

// ============================================================================
// AddRoofGeometry - router that dispatches based on ERoofShape
// ============================================================================
void FGeoMeshGenerator::AddRoofGeometry(
        FMeshData& OutRoofData,
        ERoofShape RoofShape,
        const TArray<FVector>& LocalFootprint,
        float WallTopZ,
        float RoofH,
        const FColor& RoofFColor)
{
        switch (RoofShape)
        {
        case ERoofShape::Flat:
        default:
        {
                const float ParapetHeight = 70.0f;
                const float ParapetWidth = 30.0f;
                float ParapetTop = WallTopZ + ParapetHeight;
                float RoofZ = WallTopZ;

                TArray<FVector> InnerFP = InsetPolygonByEdgeOffset(LocalFootprint, ParapetWidth);

                AddPolygonTriangulated(OutRoofData, InnerFP, RoofZ, RoofFColor);
                AddExtrudedPolygon(OutRoofData, LocalFootprint, WallTopZ, ParapetTop, RoofFColor, false);
                AddExtrudedPolygon(OutRoofData, InnerFP, RoofZ, ParapetTop, RoofFColor, false);

                int32 NP = LocalFootprint.Num();
                for (int32 i = 0; i < NP; i++)
                {
                        int32 Next = (i + 1) % NP;
                        FVector O0(LocalFootprint[i].X, LocalFootprint[i].Y, ParapetTop);
                        FVector O1(LocalFootprint[Next].X, LocalFootprint[Next].Y, ParapetTop);
                        FVector I1(InnerFP[Next].X, InnerFP[Next].Y, ParapetTop);
                        FVector I0(InnerFP[i].X, InnerFP[i].Y, ParapetTop);
                        AddQuad(OutRoofData, O0, O1, I1, I0, RoofFColor);
                }
        }
        break;

        case ERoofShape::Gabled:
        case ERoofShape::Saltbox:
        case ERoofShape::QuadrupleSaltbox:
                if (bUseCGALSkeleton)
                        AddSkeletonGabledRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor);
                else
                        AddGabledRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor);
                break;

        case ERoofShape::Hipped:
                if (bUseCGALSkeleton)
                        AddSkeletonHippedRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor);
                else
                        AddHippedRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor);
                break;

        case ERoofShape::Pyramidal:
                if (bUseCGALSkeleton)
                        AddSkeletonHippedRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor);
                else
                        AddPyramidalRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor);
                break;

        case ERoofShape::Skillion:
                AddSkillionRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor);
                break;

        case ERoofShape::Mansard:
        case ERoofShape::Gambrel:
                if (bUseCGALSkeleton)
                        AddSkeletonMansardRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor);
                else
                        AddMansardRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor);
                break;

        case ERoofShape::Dome:
                AddDomeRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor, false);
                break;

        case ERoofShape::Onion:
                AddDomeRoof(OutRoofData, LocalFootprint, WallTopZ, RoofH, RoofFColor, true);
                break;
        }
}

// ============================================================================
// AddGabledRoof - streets.gl approach
// Algorithm:
// 1. Clean footprint (remove dupes, ensure CCW)
// 2. Compute AABB bounds, determine if longer along X or Y
// 3. Define ridge ray: from AABB center, perpendicular to longer axis
// 4. Call SplitPolygon2D to split into two halves
// 5. If split fails, fall back to simple quad-based approach
// 6. For each sub-polygon: triangulate with TriangulatePolygon2D,
//    then assign vertex heights based on SignedDistanceToLine
// ============================================================================
void FGeoMeshGenerator::AddGabledRoof(
        FMeshData& OutData, const TArray<FVector>& FP,
        float BaseZ, float RidgeH, const FColor& Color)
{
        if (FP.Num() < 3) return;

        // Clean and ensure CCW
        TArray<FVector> CleanFP;
        for (const FVector& V : FP)
        {
                FVector P(V.X, V.Y, BaseZ);
                if (CleanFP.Num() > 0 && P.Equals(CleanFP.Last(), 0.5f)) continue;
                if (CleanFP.Num() > 0 && P.Equals(CleanFP[0], 0.5f)) continue;
                CleanFP.Add(P);
        }
        if (CleanFP.Num() < 3) return;
        if (ComputeSignedArea2D(CleanFP) < 0.f) Algo::Reverse(CleanFP);

        float MinX, MaxX, MinY, MaxY;
        ComputeFootprintBounds(CleanFP, MinX, MaxX, MinY, MaxY);
        float CX = (MinX + MaxX) * 0.5f, CY = (MinY + MaxY) * 0.5f;
        bool bLongX = IsLongerAlongX(CleanFP);

        // 2D polygon for split
        TArray<FVector2D> Poly2D;
        for (const FVector& V : CleanFP)
                Poly2D.Add(FVector2D(V.X, V.Y));

        // Ridge ray: from AABB center, perpendicular to longer axis
        FVector2D RayOrig, RayDir;
        if (bLongX) { RayOrig = FVector2D(CX, CY); RayDir = FVector2D(0.f, 1.f); }
        else        { RayOrig = FVector2D(CX, CY); RayDir = FVector2D(1.f, 0.f); }

        // Split polygon along ridge line
        TArray<TArray<FVector2D>> SubPolygons;
        bool bSplitOK = SplitPolygon2D(Poly2D, RayOrig, RayDir, SubPolygons);

        if (!bSplitOK || SubPolygons.Num() < 2)
        {
                // Fallback: simple quad-based approach
                int32 NC = CleanFP.Num();
                float RidgeZ = BaseZ + RidgeH;
                for (int32 i = 0; i < NC; i++)
                {
                        int32 Next = (i + 1) % NC;
                        const FVector& A = CleanFP[i];
                        const FVector& B = CleanFP[Next];

                        float AX, AY, BX, BY;
                        if (bLongX) { AX = A.X; AY = CY; BX = B.X; BY = CY; }
                        else        { AX = CX; AY = A.Y; BX = CX; BY = B.Y; }

                        FVector RA(AX, AY, RidgeZ);
                        FVector RB(BX, BY, RidgeZ);

                        if (RA.Equals(RB, 1.f))
                        {
                                if (!A.Equals(RA, 1.f) && !B.Equals(RB, 1.f))
                                        AddTriangle(OutData, A, B, RA, Color);
                        }
                        else
                        {
                                if (A.Equals(RA, 1.f)) AddTriangle(OutData, B, RB, A, Color);
                                else if (B.Equals(RB, 1.f)) AddTriangle(OutData, A, B, RA, Color);
                                else AddQuad(OutData, A, B, RB, RA, Color);
                        }
                }
                return;
        }

        // Ridge line as distance reference
        FVector2D RidgeA, RidgeB;
        if (bLongX) { RidgeA = FVector2D(MinX, CY); RidgeB = FVector2D(MaxX, CY); }
        else        { RidgeA = FVector2D(CX, MinY); RidgeB = FVector2D(CX, MaxY); }

        // Max distance from ridge line
        float MaxDist = 0.f;
        for (const FVector& V : CleanFP)
        {
                float D = FMath::Abs(SignedDistanceToLine(FVector2D(V.X, V.Y), RidgeA, RidgeB));
                if (D > MaxDist) MaxDist = D;
        }
        if (MaxDist < 1.f) MaxDist = 1.f;

        // For each sub-polygon: triangulate + assign heights
        for (const TArray<FVector2D>& SubPoly : SubPolygons)
        {
                if (SubPoly.Num() < 3) continue;

                TArray<int32> Triangles;
                TriangulatePolygon2D(SubPoly, Triangles);

                int32 BaseIdx = OutData.Vertices.Num();

                for (const FVector2D& V2D : SubPoly)
                {
                        float Dist = SignedDistanceToLine(V2D, RidgeA, RidgeB);
                        float NormalizedDist = FMath::Abs(Dist) / MaxDist;
                        float VertexZ = BaseZ + RidgeH * (1.f - NormalizedDist);

                        OutData.Vertices.Add(FVector(V2D.X, V2D.Y, VertexZ));
                        OutData.Normals.Add(FVector(0.f, 0.f, 1.f));
                        OutData.VertexColors.Add(Color);
                        OutData.Tangents.Add(FProcMeshTangent(FVector(1, 0, 0), false));
                        OutData.UV0.Add(FVector2D(V2D.X / 100.f, V2D.Y / 100.f));
                }

                for (int32 TriIdx : Triangles)
                        OutData.Triangles.Add(BaseIdx + TriIdx);
        }
}

// ============================================================================
// AddHippedRoof - streets.gl approach
// Algorithm:
// 1. Clean footprint, compute AABB
// 2. Triangulate entire footprint with TriangulatePolygon2D
// 3. Assign vertex heights based on signed distance from ridge line
// 4. Height = BaseZ + RidgeH * (1.0 - abs(Dist) / MaxDist)
// ============================================================================
void FGeoMeshGenerator::AddHippedRoof(
        FMeshData& OutData, const TArray<FVector>& FP,
        float BaseZ, float RidgeH, const FColor& Color)
{
        if (FP.Num() < 3) return;

        TArray<FVector> CleanFP;
        for (const FVector& V : FP)
        {
                if (CleanFP.Num() > 0 && FVector(V.X, V.Y, BaseZ).Equals(CleanFP[0], 0.5f)) continue;
                CleanFP.Add(FVector(V.X, V.Y, BaseZ));
        }
        if (CleanFP.Num() < 3) return;
        if (ComputeSignedArea2D(CleanFP) < 0.f) Algo::Reverse(CleanFP);

        float MinX, MaxX, MinY, MaxY;
        ComputeFootprintBounds(CleanFP, MinX, MaxX, MinY, MaxY);
        float CX = (MinX + MaxX) * 0.5f, CY = (MinY + MaxY) * 0.5f;
        float DX = (MaxX - MinX), DY = (MaxY - MinY);
        bool bLongX = (DX >= DY);

        FVector2D RidgeA, RidgeB;
        if (bLongX) { RidgeA = FVector2D(MinX, CY); RidgeB = FVector2D(MaxX, CY); }
        else        { RidgeA = FVector2D(CX, MinY); RidgeB = FVector2D(CX, MaxY); }

        float MaxDist = 0.f;
        for (const FVector& V : CleanFP)
        {
                float D = FMath::Abs(SignedDistanceToLine(FVector2D(V.X, V.Y), RidgeA, RidgeB));
                if (D > MaxDist) MaxDist = D;
        }
        if (MaxDist < 1.f) MaxDist = 1.f;

        TArray<FVector2D> Poly2D;
        for (const FVector& V : CleanFP)
                Poly2D.Add(FVector2D(V.X, V.Y));

        TArray<int32> Triangles;
        TriangulatePolygon2D(Poly2D, Triangles);

        int32 BaseIdx = OutData.Vertices.Num();

        for (const FVector2D& V2D : Poly2D)
        {
                float Dist = FMath::Abs(SignedDistanceToLine(V2D, RidgeA, RidgeB));
                float NormalizedDist = Dist / MaxDist;
                float VertexZ = BaseZ + RidgeH * (1.f - NormalizedDist);

                OutData.Vertices.Add(FVector(V2D.X, V2D.Y, VertexZ));
                OutData.Normals.Add(FVector(0.f, 0.f, 1.f));
                OutData.VertexColors.Add(Color);
                OutData.Tangents.Add(FProcMeshTangent(FVector(1, 0, 0), false));
                OutData.UV0.Add(FVector2D(V2D.X / 100.f, V2D.Y / 100.f));
        }

        for (int32 TriIdx : Triangles)
                OutData.Triangles.Add(BaseIdx + TriIdx);
}

// ============================================================================
// AddPyramidalRoof - simple apex at centroid
// ============================================================================
void FGeoMeshGenerator::AddPyramidalRoof(
        FMeshData& OutData, const TArray<FVector>& FP,
        float BaseZ, float ApexH, const FColor& Color)
{
        if (FP.Num() < 3) return;

        TArray<FVector> CleanFP;
        for (const FVector& V : FP)
        {
                FVector P(V.X, V.Y, BaseZ);
                if (CleanFP.Num() > 0 && P.Equals(CleanFP.Last(), 0.5f)) continue;
                if (CleanFP.Num() > 0 && P.Equals(CleanFP[0], 0.5f)) continue;
                CleanFP.Add(P);
        }
        if (CleanFP.Num() < 3) return;
        if (ComputeSignedArea2D(CleanFP) < 0.f) Algo::Reverse(CleanFP);

        FVector Apex = ComputePolygonCentroid(CleanFP);
        Apex.Z = BaseZ + ApexH;

        for (int32 i = 0; i < CleanFP.Num(); i++)
        {
                int32 Next = (i + 1) % CleanFP.Num();
                const FVector& A = CleanFP[i];
                const FVector& B = CleanFP[Next];
                if (FVector2D::Distance(FVector2D(A.X, A.Y), FVector2D(B.X, B.Y)) < 1.f) continue;
                if (A.Equals(Apex, 0.5f) || B.Equals(Apex, 0.5f)) continue;
                AddTriangle(OutData, A, B, Apex, Color);
        }
}

// ============================================================================
// AddSkillionRoof - single-direction slope
// ============================================================================
void FGeoMeshGenerator::AddSkillionRoof(
        FMeshData& OutData, const TArray<FVector>& FP,
        float BaseZ, float RidgeH, const FColor& Color)
{
        if (FP.Num() < 3) return;
        float MinX, MaxX, MinY, MaxY;
        ComputeFootprintBounds(FP, MinX, MaxX, MinY, MaxY);

        bool bLongX = (MaxX - MinX) >= (MaxY - MinY);

        TArray<FVector> SlopedFP;
        for (const FVector& V : FP)
        {
                float T;
                if (bLongX)
                        T = (MaxY > MinY) ? (V.Y - MinY) / (MaxY - MinY) : 0.f;
                else
                        T = (MaxX > MinX) ? (V.X - MinX) / (MaxX - MinX) : 0.f;
                float Z = BaseZ + FMath::Lerp(RidgeH, 0.f, T);
                SlopedFP.Add(FVector(V.X, V.Y, Z));
        }
        AddPolygonTriangulated(OutData, SlopedFP, 0.f, Color, true);
}

// ============================================================================
// AddMansardRoof - inset polygon + lower extrusion + upper pyramidal
// Algorithm:
// 1. LowerH = RidgeH * 0.6, UpperH = RidgeH * 0.4
// 2. MidZ = BaseZ + LowerH
// 3. InsetFP = InsetPolygonByEdgeOffset(FP, LowerH * 0.8)
// 4. AddExtrudedPolygon for the inset (BaseZ to MidZ) - steep lower part
// 5. AddPyramidalRoof on inset at MidZ with UpperH - upper part
// ============================================================================
void FGeoMeshGenerator::AddMansardRoof(
        FMeshData& OutData, const TArray<FVector>& FP,
        float BaseZ, float RidgeH, const FColor& Color)
{
        if (FP.Num() < 3) return;
        float LowerH = RidgeH * 0.6f;
        float UpperH = RidgeH * 0.4f;
        float MidZ = BaseZ + LowerH;

        TArray<FVector> InsetFP = InsetPolygonByEdgeOffset(FP, LowerH * 0.8f);
        for (FVector& V : InsetFP) V.Z = 0.f;

        AddExtrudedPolygon(OutData, InsetFP, BaseZ, MidZ, Color, false);
        AddPyramidalRoof(OutData, InsetFP, MidZ, UpperH, Color);
}

// ============================================================================
// AddDomeRoof - parametric dome with rings and segments
// ============================================================================
void FGeoMeshGenerator::AddDomeRoof(
        FMeshData& OutData, const TArray<FVector>& FP,
        float BaseZ, float DomeH, const FColor& Color,
        bool bOnion)
{
        if (FP.Num() < 3) return;
        FVector Cen = ComputePolygonCentroid(FP);
        float MinX, MaxX, MinY, MaxY;
        ComputeFootprintBounds(FP, MinX, MaxX, MinY, MaxY);
        float RadX = (MaxX - MinX) * 0.5f, RadY = (MaxY - MinY) * 0.5f;

        const int32 Rings = 8;
        const int32 Segments = FMath::Max(16, FP.Num() * 2);

        TArray<TArray<FVector>> RingVerts;
        for (int32 r = 0; r <= Rings; r++)
        {
                float T = (float)r / (float)Rings;
                float Phi = T * PI * 0.5f;
                float sinPhi = FMath::Sin(Phi);
                float cosPhi = FMath::Cos(Phi);

                float ScaleR = bOnion ? (cosPhi * (1.f + 0.3f * sinPhi)) : cosPhi;

                TArray<FVector> Ring;
                for (int32 s = 0; s < Segments; s++)
                {
                        float Theta = 2.f * PI * (float)s / (float)Segments;
                        float X = Cen.X + RadX * ScaleR * FMath::Cos(Theta);
                        float Y = Cen.Y + RadY * ScaleR * FMath::Sin(Theta);
                        float Z = BaseZ + DomeH * sinPhi;
                        Ring.Add(FVector(X, Y, Z));
                }
                RingVerts.Add(Ring);
        }

        for (int32 r = 0; r < Rings; r++)
        {
                const TArray<FVector>& Bottom = RingVerts[r];
                const TArray<FVector>& Top = RingVerts[r + 1];
                for (int32 s = 0; s < Segments; s++)
                {
                        int32 sn = (s + 1) % Segments;
                        AddQuad(OutData, Bottom[s], Bottom[sn], Top[sn], Top[s], Color);
                }
        }

        FVector Apex(Cen.X, Cen.Y, BaseZ + DomeH);
        const TArray<FVector>& LastRing = RingVerts[Rings];
        for (int32 s = 0; s < Segments; s++)
        {
                int32 sn = (s + 1) % Segments;
                AddTriangle(OutData, LastRing[s], LastRing[sn], Apex, Color);
        }
}

// ============================================================================
// ============================================================================
// CGAL SKELETON-BASED ROOF GENERATORS
// These use CGAL Straight Skeleton for accurate roof geometry on arbitrary
// (non-rectangular) building footprints, following the streets.gl approach.
// ============================================================================
// ============================================================================

// ============================================================================
// Local helper: Compute roof height at a 2D point using skeleton vertex data
// Uses Inverse Distance Weighting (IDW) from skeleton vertices to interpolate
// heights. Boundary vertices have time=0 (height=BaseZ), interior vertices
// have time>0 (raised proportionally to RidgeHeight).
// ============================================================================
static float ComputeSkeletonHeight(
        const FVector2D& Point,
        const FCGALSkeletonResult& Skeleton,
        float BaseZ, float RidgeHeight)
{
        if (Skeleton.MaxTime < 0.01f || Skeleton.Vertices.Num() == 0)
                return BaseZ;

        // Check if point is very close to a skeleton vertex
        for (const FCGALSkeletonVertex& SV : Skeleton.Vertices)
        {
                if (FVector2D::Distance(Point, SV.Position) < 1.f)
                {
                        float NormalizedTime = SV.Time / Skeleton.MaxTime;
                        return BaseZ + RidgeHeight * NormalizedTime;
                }
        }

        // IDW interpolation from skeleton vertices
        float WeightSum = 0.f;
        float HeightWeightSum = 0.f;

        for (const FCGALSkeletonVertex& SV : Skeleton.Vertices)
        {
                float Dist = FVector2D::Distance(Point, SV.Position);
                float W = 1.f / FMath::Max(Dist * Dist, 1.f);
                float NormalizedTime = SV.Time / Skeleton.MaxTime;
                WeightSum += W;
                HeightWeightSum += W * (BaseZ + RidgeHeight * NormalizedTime);
        }

        return (WeightSum > 0.f) ? (HeightWeightSum / WeightSum) : BaseZ;
}

// ============================================================================
// Local helper: Check if a 2D point is on the polygon boundary
// ============================================================================
static bool IsOnBoundary(const FVector2D& Point, const TArray<FVector2D>& Polygon, float Tolerance = 5.f)
{
        for (const FVector2D& P : Polygon)
        {
                if (FVector2D::Distance(Point, P) < Tolerance)
                        return true;
        }
        return false;
}

// ============================================================================
// AddSkeletonHippedRoof
// Uses CGAL Straight Skeleton for accurate hipped roof generation.
//
// Algorithm:
// 1. Generate straight skeleton of the footprint
// 2. Triangulate the polygon using CGAL Constrained Delaunay Triangulation
// 3. For each vertex in the triangulation, compute roof height from skeleton:
//    - Boundary vertices: height = BaseZ (skeleton time = 0)
//    - Interior vertices: height = BaseZ + RidgeHeight * (time / MaxTime)
//    - Steiner points: IDW interpolation from nearby skeleton vertices
// 4. Build the mesh from triangulated vertices with computed heights
//
// This produces correct hipped roof geometry for ANY polygon shape,
// not just rectangles — concave, L-shaped, etc.
// ============================================================================
void FGeoMeshGenerator::AddSkeletonHippedRoof(
        FMeshData& OutRoofData,
        const TArray<FVector>& Footprint,
        float BaseZ,
        float RidgeHeight,
        const FColor& Color)
{
        if (Footprint.Num() < 3) return;

        try
        {
        // Clean footprint
        TArray<FVector> CleanFP;
        for (const FVector& V : Footprint)
        {
                FVector P(V.X, V.Y, BaseZ);
                if (CleanFP.Num() > 0 && P.Equals(CleanFP.Last(), 0.5f)) continue;
                if (CleanFP.Num() > 0 && P.Equals(CleanFP[0], 0.5f)) continue;
                CleanFP.Add(P);
        }
        if (CleanFP.Num() < 3) return;
        if (ComputeSignedArea2D(CleanFP) < 0.f) Algo::Reverse(CleanFP);

        // Convert to 2D
        TArray<FVector2D> Poly2D;
        for (const FVector& V : CleanFP)
                Poly2D.Add(FVector2D(V.X, V.Y));

        // Generate straight skeleton
        FCGALSkeletonResult Skeleton = FCGALSkeletonGenerator::GenerateSkeleton(Poly2D);
        if (!Skeleton.IsValid() || Skeleton.MaxTime < 0.01f)
        {
                UE_LOG(LogTemp, Warning, TEXT("CGAL Skeleton failed for hipped roof, falling back to AABB"));
                AddHippedRoof(OutRoofData, Footprint, BaseZ, RidgeHeight, Color);
                return;
        }

        // Triangulate using CGAL CDT (more robust than ear-clipping)
        TArray<int32> Triangles;
        TArray<FVector2D> SteinerPoints;
        FCGALSkeletonGenerator::TriangulateWithCGAL(Poly2D, Triangles, SteinerPoints);

        if (Triangles.Num() < 3)
        {
                UE_LOG(LogTemp, Warning, TEXT("CGAL CDT failed for hipped roof, falling back to AABB"));
                AddHippedRoof(OutRoofData, Footprint, BaseZ, RidgeHeight, Color);
                return;
        }

        // Combine original + Steiner points
        TArray<FVector2D> AllPoints;
        AllPoints.Append(Poly2D);
        AllPoints.Append(SteinerPoints);

        // Build mesh vertices with skeleton-computed heights
        int32 BaseIdx = OutRoofData.Vertices.Num();

        for (int32 i = 0; i < AllPoints.Num(); i++)
        {
                const FVector2D& Pt = AllPoints[i];

                // Boundary vertices get BaseZ, interior get skeleton height
                float VertexZ;
                if (i < Poly2D.Num())
                {
                        // This is an original boundary vertex
                        VertexZ = BaseZ;
                }
                else
                {
                        // Steiner point — compute height from skeleton
                        VertexZ = ComputeSkeletonHeight(Pt, Skeleton, BaseZ, RidgeHeight);
                }

                OutRoofData.Vertices.Add(FVector(Pt.X, Pt.Y, VertexZ));
                OutRoofData.Normals.Add(FVector(0.f, 0.f, 1.f));
                OutRoofData.VertexColors.Add(Color);
                OutRoofData.Tangents.Add(FProcMeshTangent(FVector(1, 0, 0), false));
                OutRoofData.UV0.Add(FVector2D(Pt.X / 100.f, Pt.Y / 100.f));
        }

        // Add triangles
        for (int32 TriIdx : Triangles)
                OutRoofData.Triangles.Add(BaseIdx + TriIdx);

        UE_LOG(LogTemp, Verbose,
                TEXT("CGAL Skeleton Hipped Roof: %d verts, %d tris, maxTime=%.1f"),
                AllPoints.Num(), Triangles.Num() / 3, Skeleton.MaxTime);
        }
        catch (...)
        {
                UE_LOG(LogTemp, Warning, TEXT("CGAL Skeleton Hipped Roof: Exception caught, falling back to AABB"));
                AddHippedRoof(OutRoofData, Footprint, BaseZ, RidgeHeight, Color);
        }
}

// ============================================================================
// AddSkeletonGabledRoof
// Uses CGAL Straight Skeleton for ridge detection + CGAL CDT for
// robust triangulation.
//
// Algorithm:
// 1. Generate straight skeleton to find the ridge line
// 2. The ridge is the longest bisector edge in the skeleton
// 3. Split polygon along the ridge line
// 4. For each sub-polygon, triangulate with CGAL CDT
// 5. Assign heights: boundary vertices near ridge get raised to ridge height,
//    others stay at BaseZ. This creates the gable walls.
// 6. If skeleton/split fails, fall back to legacy AABB approach
// ============================================================================
void FGeoMeshGenerator::AddSkeletonGabledRoof(
        FMeshData& OutRoofData,
        const TArray<FVector>& Footprint,
        float BaseZ,
        float RidgeHeight,
        const FColor& Color)
{
        if (Footprint.Num() < 3) return;

        try
        {
        // Clean footprint
        TArray<FVector> CleanFP;
        for (const FVector& V : Footprint)
        {
                FVector P(V.X, V.Y, BaseZ);
                if (CleanFP.Num() > 0 && P.Equals(CleanFP.Last(), 0.5f)) continue;
                if (CleanFP.Num() > 0 && P.Equals(CleanFP[0], 0.5f)) continue;
                CleanFP.Add(P);
        }
        if (CleanFP.Num() < 3) return;
        if (ComputeSignedArea2D(CleanFP) < 0.f) Algo::Reverse(CleanFP);

        // Convert to 2D
        TArray<FVector2D> Poly2D;
        for (const FVector& V : CleanFP)
                Poly2D.Add(FVector2D(V.X, V.Y));

        // Try to find ridge line from skeleton
        FCGALSkeletonResult Skeleton = FCGALSkeletonGenerator::GenerateSkeleton(Poly2D);

        // Find the longest bisector edge — that's the ridge line
        FVector2D RidgeA(0.f, 0.f), RidgeB(0.f, 0.f);
        float RidgeLen = 0.f;
        bool bFoundRidge = false;

        if (Skeleton.IsValid())
        {
                for (const FCGALSkeletonEdge& Edge : Skeleton.Edges)
                {
                        if (!Edge.bIsBisector) continue;
                        if (Edge.VertexA < 0 || Edge.VertexA >= Skeleton.Vertices.Num()) continue;
                        if (Edge.VertexB < 0 || Edge.VertexB >= Skeleton.Vertices.Num()) continue;

                        const FCGALSkeletonVertex& VA = Skeleton.Vertices[Edge.VertexA];
                        const FCGALSkeletonVertex& VB = Skeleton.Vertices[Edge.VertexB];

                        // Only consider edges between two interior vertices (both have time > 0)
                        // This is the ridge line
                        if (VA.Time > 0.01f && VB.Time > 0.01f)
                        {
                                float Len = FVector2D::Distance(VA.Position, VB.Position);
                                if (Len > RidgeLen)
                                {
                                        RidgeLen = Len;
                                        RidgeA = VA.Position;
                                        RidgeB = VB.Position;
                                        bFoundRidge = true;
                                }
                        }
                }
        }

        // If we couldn't find a ridge from the skeleton, use AABB fallback
        if (!bFoundRidge)
        {
                float MinX, MaxX, MinY, MaxY;
                ComputeFootprintBounds(CleanFP, MinX, MaxX, MinY, MaxY);
                float CX = (MinX + MaxX) * 0.5f, CY = (MinY + MaxY) * 0.5f;

                if (IsLongerAlongX(CleanFP))
                {
                        RidgeA = FVector2D(MinX, CY);
                        RidgeB = FVector2D(MaxX, CY);
                }
                else
                {
                        RidgeA = FVector2D(CX, MinY);
                        RidgeB = FVector2D(CX, MaxY);
                }
        }

        // Compute max distance from ridge line
        float MaxDist = 0.f;
        for (const FVector& V : CleanFP)
        {
                float D = FMath::Abs(SignedDistanceToLine(FVector2D(V.X, V.Y), RidgeA, RidgeB));
                if (D > MaxDist) MaxDist = D;
        }
        if (MaxDist < 1.f) MaxDist = 1.f;

        // Split polygon along ridge line
        FVector2D RidgeDir = RidgeB - RidgeA;
        if (!RidgeDir.IsNearlyZero()) RidgeDir.Normalize();
        FVector2D RidgeMid = (RidgeA + RidgeB) * 0.5f;

        TArray<TArray<FVector2D>> SubPolygons;
        bool bSplitOK = SplitPolygon2D(Poly2D, RidgeMid, RidgeDir, SubPolygons);

        if (!bSplitOK || SubPolygons.Num() < 2)
        {
                // Fallback: use CGAL CDT on the whole polygon with ridge-based heights
                TArray<int32> Triangles;
                TArray<FVector2D> SteinerPoints;
                FCGALSkeletonGenerator::TriangulateWithCGAL(Poly2D, Triangles, SteinerPoints);

                if (Triangles.Num() < 3)
                {
                        AddGabledRoof(OutRoofData, Footprint, BaseZ, RidgeHeight, Color);
                        return;
                }

                TArray<FVector2D> AllPoints;
                AllPoints.Append(Poly2D);
                AllPoints.Append(SteinerPoints);

                int32 BaseIdx = OutRoofData.Vertices.Num();

                for (int32 i = 0; i < AllPoints.Num(); i++)
                {
                        const FVector2D& Pt = AllPoints[i];
                        float Dist = FMath::Abs(SignedDistanceToLine(Pt, RidgeA, RidgeB));
                        float NormalizedDist = Dist / MaxDist;
                        float VertexZ = BaseZ + RidgeHeight * (1.f - NormalizedDist);

                        OutRoofData.Vertices.Add(FVector(Pt.X, Pt.Y, VertexZ));
                        OutRoofData.Normals.Add(FVector(0.f, 0.f, 1.f));
                        OutRoofData.VertexColors.Add(Color);
                        OutRoofData.Tangents.Add(FProcMeshTangent(FVector(1, 0, 0), false));
                        OutRoofData.UV0.Add(FVector2D(Pt.X / 100.f, Pt.Y / 100.f));
                }

                for (int32 TriIdx : Triangles)
                        OutRoofData.Triangles.Add(BaseIdx + TriIdx);

                // Add gable walls: vertical triangles at the ridge ends
                for (int32 i = 0; i < CleanFP.Num(); i++)
                {
                        int32 Next = (i + 1) % CleanFP.Num();
                        const FVector& A = CleanFP[i];
                        const FVector& B = CleanFP[Next];

                        float DistA = SignedDistanceToLine(FVector2D(A.X, A.Y), RidgeA, RidgeB);
                        float DistB = SignedDistanceToLine(FVector2D(B.X, B.Y), RidgeA, RidgeB);

                        // If one vertex is on one side and the other is very close to the ridge,
                        // or both are close to the ridge but on opposite sides,
                        // we need a gable wall
                        if (FMath::Abs(DistA) < MaxDist * 0.05f || FMath::Abs(DistB) < MaxDist * 0.05f)
                        {
                                if (FMath::Abs(DistA) < MaxDist * 0.05f && FMath::Abs(DistB) < MaxDist * 0.05f)
                                {
                                        // Both on ridge — add gable wall
                                        FVector ATop(A.X, A.Y, BaseZ + RidgeHeight);
                                        FVector BTop(B.X, B.Y, BaseZ + RidgeHeight);
                                        AddQuad(OutRoofData, A, B, BTop, ATop, Color);
                                }
                        }
                }

                return;
        }

        // Split succeeded — build two roof halves with gable walls
        for (const TArray<FVector2D>& SubPoly : SubPolygons)
        {
                if (SubPoly.Num() < 3) continue;

                TArray<int32> Triangles;
                TArray<FVector2D> SteinerPoints;
                FCGALSkeletonGenerator::TriangulateWithCGAL(SubPoly, Triangles, SteinerPoints);

                if (Triangles.Num() < 3)
                {
                        // Fall back to ear-clipping
                        TriangulatePolygon2D(SubPoly, Triangles);
                }

                TArray<FVector2D> AllPoints;
                AllPoints.Append(SubPoly);
                AllPoints.Append(SteinerPoints);

                int32 BaseIdx = OutRoofData.Vertices.Num();

                for (const FVector2D& V2D : AllPoints)
                {
                        float Dist = FMath::Abs(SignedDistanceToLine(V2D, RidgeA, RidgeB));
                        float NormalizedDist = Dist / MaxDist;
                        float VertexZ = BaseZ + RidgeHeight * (1.f - NormalizedDist);

                        OutRoofData.Vertices.Add(FVector(V2D.X, V2D.Y, VertexZ));
                        OutRoofData.Normals.Add(FVector(0.f, 0.f, 1.f));
                        OutRoofData.VertexColors.Add(Color);
                        OutRoofData.Tangents.Add(FProcMeshTangent(FVector(1, 0, 0), false));
                        OutRoofData.UV0.Add(FVector2D(V2D.X / 100.f, V2D.Y / 100.f));
                }

                for (int32 TriIdx : Triangles)
                        OutRoofData.Triangles.Add(BaseIdx + TriIdx);
        }

        // Add gable walls at the ridge line ends
        // Find boundary edges that cross the ridge line
        int32 N = CleanFP.Num();
        for (int32 i = 0; i < N; i++)
        {
                int32 Next = (i + 1) % N;
                const FVector& A = CleanFP[i];
                const FVector& B = CleanFP[Next];

                float DistA = SignedDistanceToLine(FVector2D(A.X, A.Y), RidgeA, RidgeB);
                float DistB = SignedDistanceToLine(FVector2D(B.X, B.Y), RidgeA, RidgeB);

                // Edge crosses the ridge line
                if (DistA * DistB < 0.f)
                {
                        // Find intersection point
                        FVector2D IntersectPt;
                        float EdgeDX = B.X - A.X;
                        float EdgeDY = B.Y - A.Y;
                        float Den = RidgeDir.X * EdgeDY - RidgeDir.Y * EdgeDX;
                        if (FMath::Abs(Den) > 0.0001f)
                        {
                                float NumS = RidgeDir.X * ((A.Y) - RidgeA.Y) - RidgeDir.Y * ((A.X) - RidgeA.X);
                                float S = NumS / Den;
                                IntersectPt = FVector2D(A.X + S * EdgeDX, A.Y + S * EdgeDY);

                                // Gable wall: vertical triangle from intersection up to ridge
                                FVector BotPt(IntersectPt.X, IntersectPt.Y, BaseZ);
                                FVector TopPt(IntersectPt.X, IntersectPt.Y, BaseZ + RidgeHeight);
                                AddQuad(OutRoofData,
                                        FVector(A.X, A.Y, BaseZ),
                                        FVector(A.X, A.Y, BaseZ + RidgeHeight),
                                        TopPt,
                                        BotPt,
                                        Color);
                        }
                }
        }
        }
        catch (...)
        {
                UE_LOG(LogTemp, Warning, TEXT("CGAL Skeleton Gabled Roof: Exception caught, falling back to AABB"));
                AddGabledRoof(OutRoofData, Footprint, BaseZ, RidgeHeight, Color);
        }
}

// ============================================================================
// AddSkeletonMansardRoof
// Uses CGAL Straight Skeleton to compute the inner offset curve for the
// mansard break line, instead of the approximate InsetPolygonByEdgeOffset.
//
// Algorithm:
// 1. Generate straight skeleton of the footprint
// 2. Find the "break" vertices: skeleton vertices at time ~60% of MaxTime
// 3. These form the inner offset polygon (mansard break line)
// 4. Lower part: steep slope from boundary to break line
// 5. Upper part: shallow pyramidal roof from break line to apex
// ============================================================================
void FGeoMeshGenerator::AddSkeletonMansardRoof(
        FMeshData& OutRoofData,
        const TArray<FVector>& Footprint,
        float BaseZ,
        float RidgeHeight,
        const FColor& Color)
{
        if (Footprint.Num() < 3) return;

        try
        {

        // Clean footprint
        TArray<FVector> CleanFP;
        for (const FVector& V : Footprint)
        {
                FVector P(V.X, V.Y, BaseZ);
                if (CleanFP.Num() > 0 && P.Equals(CleanFP.Last(), 0.5f)) continue;
                if (CleanFP.Num() > 0 && P.Equals(CleanFP[0], 0.5f)) continue;
                CleanFP.Add(P);
        }
        if (CleanFP.Num() < 3) return;
        if (ComputeSignedArea2D(CleanFP) < 0.f) Algo::Reverse(CleanFP);

        // Convert to 2D
        TArray<FVector2D> Poly2D;
        for (const FVector& V : CleanFP)
                Poly2D.Add(FVector2D(V.X, V.Y));

        // Generate straight skeleton
        FCGALSkeletonResult Skeleton = FCGALSkeletonGenerator::GenerateSkeleton(Poly2D);

        if (!Skeleton.IsValid() || Skeleton.MaxTime < 0.01f)
        {
                UE_LOG(LogTemp, Warning, TEXT("CGAL Skeleton failed for mansard roof, falling back"));
                AddMansardRoof(OutRoofData, Footprint, BaseZ, RidgeHeight, Color);
                return;
        }

        // Find the inner offset polygon at 60% of MaxTime
        // This is the mansard break line where the steep lower slope meets
        // the shallow upper slope
        const float BreakTimeRatio = 0.6f;
        float BreakTime = Skeleton.MaxTime * BreakTimeRatio;
        float LowerH = RidgeHeight * BreakTimeRatio;
        float UpperH = RidgeHeight * (1.f - BreakTimeRatio);
        float MidZ = BaseZ + LowerH;

        // Collect skeleton vertices near the break time to form the inner polygon
        TArray<FVector2D> InnerPoly2D;
        float TimeTolerance = Skeleton.MaxTime * 0.15f; // 15% tolerance

        for (const FCGALSkeletonVertex& SV : Skeleton.Vertices)
        {
                if (SV.Time > 0.01f && FMath::Abs(SV.Time - BreakTime) < TimeTolerance)
                {
                        InnerPoly2D.Add(SV.Position);
                }
        }

        // If we couldn't find enough break vertices, fall back to edge-offset
        if (InnerPoly2D.Num() < 3)
        {
                TArray<FVector> InsetFP = InsetPolygonByEdgeOffset(Footprint, LowerH * 0.8f);
                for (FVector& V : InsetFP) V.Z = 0.f;
                AddExtrudedPolygon(OutRoofData, InsetFP, BaseZ, MidZ, Color, false);
                AddPyramidalRoof(OutRoofData, InsetFP, MidZ, UpperH, Color);
                return;
        }

        // Sort the inner polygon vertices in CCW order
        // Use angle from centroid
        FVector2D Centroid(0.f, 0.f);
        for (const FVector2D& P : InnerPoly2D)
                Centroid += P;
        Centroid /= (float)InnerPoly2D.Num();

        InnerPoly2D.Sort([&Centroid](const FVector2D& A, const FVector2D& B)
        {
                float AngleA = FMath::Atan2(A.Y - Centroid.Y, A.X - Centroid.X);
                float AngleB = FMath::Atan2(B.Y - Centroid.Y, B.X - Centroid.X);
                return AngleA < AngleB;
        });

        // Convert inner polygon to 3D
        TArray<FVector> InnerFP;
        for (const FVector2D& P : InnerPoly2D)
                InnerFP.Add(FVector(P.X, P.Y, 0.f));

        // Lower part: steep slope from boundary (BaseZ) to break line (MidZ)
        AddExtrudedPolygon(OutRoofData, CleanFP, BaseZ, MidZ, Color, false);
        AddExtrudedPolygon(OutRoofData, InnerFP, BaseZ, MidZ, Color, false);

        // Fill the sloped faces between outer and inner polygons
        int32 NumOuter = CleanFP.Num();
        int32 NumInner = InnerFP.Num();

        // For each outer edge, find the closest inner edge and create sloped quad
        for (int32 i = 0; i < NumOuter; i++)
        {
                int32 NextO = (i + 1) % NumOuter;
                FVector O0(CleanFP[i].X, CleanFP[i].Y, BaseZ);
                FVector O1(CleanFP[NextO].X, CleanFP[NextO].Y, BaseZ);

                // Find closest inner edge midpoint
                FVector2D EdgeMid = (FVector2D(CleanFP[i].X, CleanFP[i].Y) +
                        FVector2D(CleanFP[NextO].X, CleanFP[NextO].Y)) * 0.5f;

                int32 BestInner = 0;
                float BestDist = FLT_MAX;
                for (int32 j = 0; j < NumInner; j++)
                {
                        float D = FVector2D::Distance(EdgeMid, InnerPoly2D[j]);
                        if (D < BestDist) { BestDist = D; BestInner = j; }
                }

                int32 NextI = (BestInner + 1) % NumInner;
                FVector I0(InnerFP[BestInner].X, InnerFP[BestInner].Y, MidZ);
                FVector I1(InnerFP[NextI].X, InnerFP[NextI].Y, MidZ);

                AddQuad(OutRoofData, O0, O1, I1, I0, Color);
        }

        // Upper part: pyramidal/hipped roof from break line to apex
        AddPyramidalRoof(OutRoofData, InnerFP, MidZ, UpperH, Color);

        UE_LOG(LogTemp, Verbose,
                TEXT("CGAL Skeleton Mansard Roof: %d inner verts, breakTime=%.1f"),
                InnerPoly2D.Num(), BreakTime);
        }
        catch (...)
        {
                UE_LOG(LogTemp, Warning, TEXT("CGAL Skeleton Mansard Roof: Exception caught, falling back"));
                AddMansardRoof(OutRoofData, Footprint, BaseZ, RidgeHeight, Color);
        }
}

// ============================================================================
// ValidateCDTIndices - Check that all triangle indices are within bounds
// Prevents the 0xFFFFFFFF x 4 = 17GB OOM crash from invalid CGAL CDT output
// ============================================================================
bool FGeoMeshGenerator::ValidateCDTIndices(
        const TArray<int32>& Triangles,
        int32 VertexCount)
{
        for (int32 Idx : Triangles)
        {
                if (Idx < 0 || Idx >= VertexCount)
                {
                        UE_LOG(LogTemp, Error,
                                TEXT("Invalid CDT index: %d (vertex count: %d)"), Idx, VertexCount);
                        return false;
                }
        }
        return true;
}

// ============================================================================
// CleanFootprint - Remove duplicate vertices, ensure CCW winding order
// ============================================================================
TArray<FVector> FGeoMeshGenerator::CleanFootprint(
        const TArray<FVector>& Footprint,
        float BaseZ,
        float DuplicateTolerance)
{
        TArray<FVector> Result;
        for (const FVector& V : Footprint)
        {
                FVector P(V.X, V.Y, BaseZ);
                if (Result.Num() > 0 && P.Equals(Result.Last(), DuplicateTolerance)) continue;
                if (Result.Num() > 0 && P.Equals(Result[0], DuplicateTolerance)) continue;
                Result.Add(P);
        }
        if (Result.Num() >= 3 && ComputeSignedArea2D(Result) < 0.f)
                Algo::Reverse(Result);
        return Result;
}

// ============================================================================
// ============================================================================
// MAIN GENERATION FUNCTIONS
// ============================================================================
// ============================================================================

// ============================================================================
// GenerateRoofMeshes
// ============================================================================
void FGeoMeshGenerator::GenerateRoofMeshes(
        const TArray<FGeoBuilding>& Buildings,
        FMeshData& OutRoofData)
{
        OutRoofData.Reset();

        const float FoundationHeight = 50.0f;
        const float DefaultRoofPitch = 0.4f;

        for (const FGeoBuilding& Building : Buildings)
        {
                if (Building.Nodes.Num() < 3) continue;

                TArray<FVector> LocalFootprint;
                for (const FVector2D& N : Building.Nodes)
                        LocalFootprint.Add(FVector(N.X * ScaleFactor, N.Y * ScaleFactor, 0.f));

                float BuildingH = (float)(Building.Height * (double)ScaleFactor);
                if (BuildingH < 100.f) BuildingH = 300.f;
                float MinH = (float)(Building.MinHeight * (double)ScaleFactor);
                float EffBase = FMath::Max(MinH, FoundationHeight);
                float WallTopZ = EffBase + BuildingH;

                FColor RoofFColor = LinearToFColor(Building.RoofColor);

                float RoofH = 0.f;
                if (Building.RoofHeight > 0.0)
                {
                        RoofH = (float)(Building.RoofHeight * (double)ScaleFactor);
                }
                else if (Building.RoofLevels > 0)
                {
                        RoofH = Building.RoofLevels * 300.f;
                }
                else
                {
                        float MinX, MaxX, MinY, MaxY;
                        ComputeFootprintBounds(LocalFootprint, MinX, MaxX, MinY, MaxY);
                        float HalfShort = FMath::Min(MaxX - MinX, MaxY - MinY) * 0.5f;
                        RoofH = FMath::Max(50.f, HalfShort * DefaultRoofPitch);
                }

                AddRoofGeometry(OutRoofData, Building.RoofShape, LocalFootprint, WallTopZ, RoofH, RoofFColor);
        }

        RecomputeNormals(OutRoofData);
        RecomputeTangents(OutRoofData);
}

// ============================================================================
// GenerateBuildingMeshes (with windows, facade materials, etc.)
// ============================================================================
void FGeoMeshGenerator::GenerateBuildingMeshes(
        const TArray<FGeoBuilding>& Buildings,
        FMeshData& OutWallData, FMeshData& OutGlassData,
        FMeshData& OutBrickData, FMeshData& OutDarkStoneData, FMeshData& OutLightStoneData)
{
        OutWallData.Reset(); OutGlassData.Reset();
        OutBrickData.Reset(); OutDarkStoneData.Reset(); OutLightStoneData.Reset();

        FColor WallColor(180, 170, 160), GlassColor(100, 150, 220, 200), FoundationColor(120, 115, 110);

        const float FoundationHeight = 50.f;
        const float MinEdgeForWindows = 300.f;
        const float WindowSpacingCm = 200.f;
        const float WindowOutset = 5.f;
        const float WindowMarginRatio = 0.1f;

        for (const FGeoBuilding& Building : Buildings)
        {
                if (Building.Nodes.Num() < 3) continue;

                TArray<FVector> LocalFootprint;
                for (const FVector2D& N : Building.Nodes)
                        LocalFootprint.Add(FVector(N.X * ScaleFactor, N.Y * ScaleFactor, 0.f));

                float BuildingH = (float)(Building.Height * (double)ScaleFactor);
                if (BuildingH < 100.f) BuildingH = 300.f;

                float MinH = (float)(Building.MinHeight * (double)ScaleFactor);
                float WallBase = MinH;

                int32 NumLevels = Building.Levels > 0 ? Building.Levels : FMath::Max(1, FMath::RoundToInt(BuildingH / 350.f));
                float LevelH = BuildingH / (float)NumLevels;

                if (MinH < 10.f)
                        AddExtrudedPolygon(OutWallData, LocalFootprint, 0.f, FoundationHeight, FoundationColor, false);

                float EffBase = FMath::Max(WallBase, FoundationHeight);
                float WallTop = EffBase + BuildingH;

                // Determine facade material from FacadeMaterialType
                FMeshData& FacadeData = (Building.FacadeMaterialType == 1) ? OutBrickData
                        : (Building.FacadeMaterialType == 2) ? OutDarkStoneData
                        : (Building.FacadeMaterialType == 3) ? OutLightStoneData
                        : OutWallData;
                FColor FacadeColor(255, 255, 255);

                // For tall buildings (>=21 levels): use glass material for upper portion
                bool bTall = (NumLevels >= 21);

                if (bTall)
                {
                        for (int32 Lv = 0; Lv < NumLevels; Lv++)
                        {
                                float LB = EffBase + Lv * LevelH;
                                if (Lv == NumLevels - 1)
                                {
                                        AddExtrudedPolygon(FacadeData, LocalFootprint, LB, LB + LevelH, FacadeColor, false);
                                }
                                else
                                {
                                        AddExtrudedPolygon(FacadeData, LocalFootprint, LB, LB + LevelH * 0.6f, FacadeColor, false);
                                        AddExtrudedPolygon(OutGlassData, LocalFootprint, LB + LevelH * 0.6f, LB + LevelH * 0.95f, GlassColor, false);
                                        AddExtrudedPolygon(OutWallData, LocalFootprint, LB + LevelH * 0.95f, LB + LevelH, WallColor, false);
                                }
                        }
                }
                else
                {
                        AddExtrudedPolygon(FacadeData, LocalFootprint, EffBase, WallTop, FacadeColor, false);

                        float FPMinX = FLT_MAX, FPMaxX = -FLT_MAX, FPMinY = FLT_MAX, FPMaxY = -FLT_MAX;
                        ComputeFootprintBounds(LocalFootprint, FPMinX, FPMaxX, FPMinY, FPMaxY);
                        float FootMin = FMath::Min(FPMaxX - FPMinX, FPMaxY - FPMinY);
                        bool bNoWin = (FootMin < 500.f && BuildingH > 900.f)
                                || (FootMin > 0.f && BuildingH / FootMin > 3.f);

                        if (!bNoWin)
                        {
                                float WinH = LevelH * 0.5f;
                                float WinVOff = LevelH * 0.25f;

                                TArray<FVector> WinFootprint = LocalFootprint;
                                if (ComputeSignedArea2D(WinFootprint) < 0.f)
                                        Algo::Reverse(WinFootprint);

                                for (int32 i = 0; i < WinFootprint.Num(); i++)
                                {
                                        int32 Next = (i + 1) % WinFootprint.Num();
                                        FVector P0(WinFootprint[i].X, WinFootprint[i].Y, 0.f);
                                        FVector P1(WinFootprint[Next].X, WinFootprint[Next].Y, 0.f);
                                        FVector ED = P1 - P0; float EL = ED.Size2D(); if (EL < 1.f) continue; ED.Normalize();
                                        FVector OutN(ED.Y, -ED.X, 0.f);
                                        if (EL < MinEdgeForWindows) continue;
                                        float Margin = EL * WindowMarginRatio;
                                        float Avail = EL - 2.f * Margin;
                                        int32 WPF = FMath::Max(2, FMath::FloorToInt(Avail / WindowSpacingCm));
                                        for (int32 Lv = 0; Lv < NumLevels; Lv++)
                                        {
                                                if (Lv == NumLevels - 1) continue;
                                                int32 NW = (Lv == 0 && NumLevels > 2) ? FMath::Max(1, WPF - 1) : WPF;
                                                float SlotW = Avail / (float)NW;
                                                float WW = FMath::Clamp(SlotW * 0.55f, 60.f, 150.f);
                                                float FB = EffBase + Lv * LevelH;
                                                float WB = FB + WinVOff, WT = WB + WinH;
                                                for (int32 w = 0; w < NW; w++)
                                                {
                                                        float CT = Margin + SlotW * ((float)w + 0.5f);
                                                        FVector WC = P0 + ED * CT + OutN * WindowOutset;
                                                        float HW = WW * 0.5f;
                                                        FVector WBL = WC - ED * HW; WBL.Z = WB;
                                                        FVector WBR = WC + ED * HW; WBR.Z = WB;
                                                        FVector WTL = WBL; WTL.Z = WT;
                                                        FVector WTR = WBR; WTR.Z = WT;
                                                        AddQuad(OutGlassData, WBL, WBR, WTR, WTL, GlassColor);
                                                }
                                        }
                                }
                        }
                }
        }
}

// ============================================================================
// GenerateRoadMeshes
// ============================================================================
void FGeoMeshGenerator::GenerateRoadMeshes(
        const TArray<FGeoRoad>& Roads,
        FMeshData& OutRoadData, FMeshData& OutSidewalkData)
{
        OutRoadData.Reset(); OutSidewalkData.Reset();
        FColor RoadColor(60, 60, 60), SidewalkColor(180, 175, 170);
        const float SidewalkWidth = 150.f, CurbHeight = 15.f, SidewalkZOffset = 10.f;

        for (const FGeoRoad& Road : Roads)
        {
                if (Road.Points.Num() < 2) continue;
                float RW = Road.Width > 0.f ? Road.Width * ScaleFactor : 600.f;
                float HW = RW * 0.5f, OH = HW + SidewalkWidth;

                TArray<FVector> RL, RR, SL, SR;
                for (int32 i = 0; i < Road.Points.Num(); i++)
                {
                        float X = Road.Points[i].X * ScaleFactor, Y = Road.Points[i].Y * ScaleFactor;
                        FVector Dir(0, 0, 0);
                        if (i < Road.Points.Num() - 1) { FVector N(Road.Points[i + 1].X * ScaleFactor, Road.Points[i + 1].Y * ScaleFactor, 0); Dir = (N - FVector(X, Y, 0)).GetSafeNormal(); }
                        if (i > 0 && Dir.IsNearlyZero()) { FVector P(Road.Points[i - 1].X * ScaleFactor, Road.Points[i - 1].Y * ScaleFactor, 0); Dir = (FVector(X, Y, 0) - P).GetSafeNormal(); }
                        if (Dir.IsNearlyZero()) Dir = FVector(1, 0, 0);
                        FVector Perp(Dir.Y, -Dir.X, 0);
                        RL.Add(FVector(X, Y, 0) + Perp * HW); RR.Add(FVector(X, Y, 0) - Perp * HW);
                        SL.Add(FVector(X, Y, 0) + Perp * OH); SR.Add(FVector(X, Y, 0) - Perp * OH);
                }
                for (int32 i = 0; i < Road.Points.Num() - 1; i++)
                {
                        AddQuad(OutRoadData, RL[i], RR[i], RR[i + 1], RL[i + 1], RoadColor);
                        AddQuad(OutSidewalkData, SL[i], RL[i], RL[i + 1], SL[i + 1], SidewalkColor);
                        AddQuad(OutSidewalkData, FVector(RL[i].X, RL[i].Y, 0), FVector(RL[i + 1].X, RL[i + 1].Y, 0), FVector(RL[i + 1].X, RL[i + 1].Y, CurbHeight), FVector(RL[i].X, RL[i].Y, CurbHeight), SidewalkColor);
                        AddQuad(OutSidewalkData, FVector(SL[i].X, SL[i].Y, SidewalkZOffset), FVector(RL[i].X, RL[i].Y, SidewalkZOffset), FVector(RL[i + 1].X, RL[i + 1].Y, SidewalkZOffset), FVector(SL[i + 1].X, SL[i + 1].Y, SidewalkZOffset), SidewalkColor);
                        AddQuad(OutSidewalkData, RR[i], SR[i], SR[i + 1], RR[i + 1], SidewalkColor);
                        AddQuad(OutSidewalkData, FVector(RR[i].X, RR[i].Y, 0), FVector(RR[i + 1].X, RR[i + 1].Y, 0), FVector(RR[i + 1].X, RR[i + 1].Y, CurbHeight), FVector(RR[i].X, RR[i].Y, CurbHeight), SidewalkColor);
                        AddQuad(OutSidewalkData, FVector(RR[i].X, RR[i].Y, SidewalkZOffset), FVector(SR[i].X, SR[i].Y, SidewalkZOffset), FVector(SR[i + 1].X, SR[i + 1].Y, SidewalkZOffset), FVector(RR[i + 1].X, RR[i + 1].Y, SidewalkZOffset), SidewalkColor);
                }
        }
        FlipNormals(OutRoadData);
        FlipNormals(OutSidewalkData);
}

// ============================================================================
// GenerateWaterMeshes
// ============================================================================
void FGeoMeshGenerator::GenerateWaterMeshes(const TArray<FGeoWater>& WaterBodies, FMeshData& OutWaterData)
{
        OutWaterData.Reset();
        FColor WC(30, 100, 200);
        for (const FGeoWater& Water : WaterBodies)
        {
                if (Water.Points.Num() < 3) continue;
                TArray<FVector> LP;
                for (const FVector2D& P : Water.Points)
                        LP.Add(FVector(P.X * ScaleFactor, P.Y * ScaleFactor, 0.f));
                AddPolygonTriangulated(OutWaterData, LP, 0.f, WC);
        }
}

// ============================================================================
// GenerateGroundMesh
// ============================================================================
void FGeoMeshGenerator::GenerateGroundMesh(
        const TArray<FGeoBuilding>& Buildings, const TArray<FGeoRoad>& Roads,
        const TArray<FGeoWater>& WaterBodies, const TArray<FGeoVegetation>& Vegetations,
        FMeshData& OutGroundData, float GroundOffset)
{
        OutGroundData.Reset();
        float MinX, MaxX, MinY, MaxY;
        ComputeDataBounds(Buildings, Roads, WaterBodies, Vegetations, MinX, MaxX, MinY, MaxY);
        if (MinX >= MaxX || MinY >= MaxY) { MinX = -5000.f; MaxX = 5000.f; MinY = -5000.f; MaxY = 5000.f; }
        float Pad = 5000.f;
        float UMinX = MinX * ScaleFactor - Pad, UMaxX = MaxX * ScaleFactor + Pad;
        float UMinY = MinY * ScaleFactor - Pad, UMaxY = MaxY * ScaleFactor + Pad;
        float Z = GroundOffset - 5.f;

        AddQuad(OutGroundData,
                FVector(UMinX, UMinY, Z),
                FVector(UMaxX, UMinY, Z),
                FVector(UMaxX, UMaxY, Z),
                FVector(UMinX, UMaxY, Z),
                FColor(80, 140, 60));

        int32 US = OutGroundData.UV0.Num() - 4;
        float TX = (UMaxX - UMinX) / 1000.f, TY = (UMaxY - UMinY) / 1000.f;
        OutGroundData.UV0[US + 0] = FVector2D(0, 0);
        OutGroundData.UV0[US + 1] = FVector2D(TX, 0);
        OutGroundData.UV0[US + 2] = FVector2D(TX, TY);
        OutGroundData.UV0[US + 3] = FVector2D(0, TY);
        FlipNormals(OutGroundData);
}

// ============================================================================
// GenerateVegetationMeshes
// ============================================================================
void FGeoMeshGenerator::GenerateVegetationMeshes(const TArray<FGeoVegetation>& Vegetations, FMeshData& OutVegetationData)
{
        OutVegetationData.Reset();
        for (const FGeoVegetation& Veg : Vegetations)
        {
                if (Veg.Points.Num() < 3) continue;
                TArray<FVector> LP;
                for (const FVector2D& P : Veg.Points)
                        LP.Add(FVector(P.X * ScaleFactor, P.Y * ScaleFactor, 0.f));
                FColor VC(50, 140, 40);
                AddPolygonTriangulated(OutVegetationData, LP, 5.f, VC);
        }
}

// ============================================================================
// GenerateVegetationTransforms
// ============================================================================
void FGeoMeshGenerator::GenerateVegetationTransforms(
        const TArray<FGeoVegetation>& Vegetations,
        const TArray<FGeoBuilding>& Buildings,
        const TArray<FGeoRoad>& Roads,
        TArray<FTransform>& OutTreeTransforms,
        TArray<FTransform>& OutGrassTransforms,
        float TreeDensityScale, float GrassDensityScale)
{
        OutTreeTransforms.Empty(); OutGrassTransforms.Empty();
        TreeDensityScale = FMath::Max(TreeDensityScale, 0.01f);
        GrassDensityScale = FMath::Max(GrassDensityScale, 0.01f);

        const float VegetationBaseZ = 50.f;

        // Build building polygon list for collision check
        TArray<TArray<FVector2D>> BldgPolys;
        for (const FGeoBuilding& B : Buildings)
        {
                TArray<FVector2D> Poly;
                for (const FVector2D& N : B.Nodes) Poly.Add(FVector2D(N.X * ScaleFactor, N.Y * ScaleFactor));
                BldgPolys.Add(Poly);
        }

        // Build road segment list for collision check
        struct FRS { FVector2D A, B; float HW; };
        TArray<FRS> RoadSegs;
        for (const FGeoRoad& R : Roads)
        {
                if (R.Points.Num() < 2) continue;
                float HW = (R.Width > 0.f ? R.Width * ScaleFactor : 600.f) * 0.5f;
                for (int32 i = 0; i < R.Points.Num() - 1; i++)
                        RoadSegs.Add({ FVector2D(R.Points[i].X * ScaleFactor, R.Points[i].Y * ScaleFactor),
                                FVector2D(R.Points[i + 1].X * ScaleFactor, R.Points[i + 1].Y * ScaleFactor), HW });
        }

        // Point-in-polygon test
        auto InPoly = [](const FVector2D& P, const TArray<FVector2D>& Poly) -> bool
        {
                int32 N = Poly.Num(); bool Inside = false;
                for (int32 i = 0, j = N - 1; i < N; j = i++)
                        if (((Poly[i].Y > P.Y) != (Poly[j].Y > P.Y)) && (P.X < (Poly[j].X - Poly[i].X) * (P.Y - Poly[i].Y) / (Poly[j].Y - Poly[i].Y) + Poly[i].X))
                                Inside = !Inside;
                return Inside;
        };

        auto InAnyBldg = [&](const FVector2D& P) -> bool { for (const auto& Poly : BldgPolys) if (InPoly(P, Poly)) return true; return false; };

        auto OnRoad = [&](const FVector2D& P) -> bool
        {
                for (const FRS& S : RoadSegs)
                {
                        FVector2D AB = S.B - S.A, AP = P - S.A;
                        float L = AB.Size(); if (L < 1.f) continue;
                        float T = FMath::Clamp(FVector2D::DotProduct(AP, AB) / (L * L), 0.f, 1.f);
                        if (FVector2D::Distance(P, S.A + AB * T) < S.HW) return true;
                }
                return false;
        };

        for (const FGeoVegetation& Veg : Vegetations)
        {
                if (Veg.Points.Num() < 3) continue;
                float MnX = FLT_MAX, MxX = -FLT_MAX, MnY = FLT_MAX, MxY = -FLT_MAX;
                TArray<FVector2D> VP;
                for (const FVector2D& P : Veg.Points)
                {
                        float UX = P.X * ScaleFactor, UY = P.Y * ScaleFactor;
                        VP.Add(FVector2D(UX, UY));
                        if (UX < MnX) MnX = UX; if (UX > MxX) MxX = UX;
                        if (UY < MnY) MnY = UY; if (UY > MxY) MxY = UY;
                }
                float AX = MxX - MnX, AY = MxY - MnY;
                if (AX <= 0.f || AY <= 0.f) continue;

                // Scatter tree transforms
                float TS = 800.f / TreeDensityScale;
                int32 NTX = FMath::Max(1, FMath::FloorToInt(AX / TS)), NTY = FMath::Max(1, FMath::FloorToInt(AY / TS));
                for (int32 tx = 0; tx < NTX; tx++) for (int32 ty = 0; ty < NTY; ty++)
                {
                        float PX = MnX + TS * ((float)tx + 0.5f) + FMath::FRandRange(-TS * 0.2f, TS * 0.2f);
                        float PY = MnY + TS * ((float)ty + 0.5f) + FMath::FRandRange(-TS * 0.2f, TS * 0.2f);
                        FVector2D TP(PX, PY);
                        if (!InPoly(TP, VP) || InAnyBldg(TP) || OnRoad(TP)) continue;
                        float S = FMath::FRandRange(0.7f, 1.3f);
                        FTransform X(FVector(PX, PY, VegetationBaseZ)); X.SetScale3D(FVector(S)); OutTreeTransforms.Add(X);
                }

                // Scatter grass transforms
                float GS = 200.f / GrassDensityScale;
                int32 NGX = FMath::Max(1, FMath::FloorToInt(AX / GS)), NGY = FMath::Max(1, FMath::FloorToInt(AY / GS));
                for (int32 gx = 0; gx < NGX; gx++) for (int32 gy = 0; gy < NGY; gy++)
                {
                        float PX = MnX + GS * ((float)gx + 0.5f) + FMath::FRandRange(-GS * 0.3f, GS * 0.3f);
                        float PY = MnY + GS * ((float)gy + 0.5f) + FMath::FRandRange(-GS * 0.3f, GS * 0.3f);
                        FVector2D TP(PX, PY);
                        if (!InPoly(TP, VP) || InAnyBldg(TP) || OnRoad(TP)) continue;
                        float S = FMath::FRandRange(0.5f, 1.5f), R = FMath::FRandRange(0.f, 360.f);
                        OutGrassTransforms.Add(FTransform(FRotator(0, R, 0), FVector(PX, PY, VegetationBaseZ), FVector(S)));
                }
        }
}
