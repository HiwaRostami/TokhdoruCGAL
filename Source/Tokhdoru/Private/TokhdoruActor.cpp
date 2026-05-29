// Copyright NazruGeo. All Rights Reserved.

#include "TokhdoruActor.h"
#include "GeoMeshGenerator.h"
#include "OSMLoader.h"
#include "GeoJSONLoader.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

// ============================================================================
// Constructor
// ============================================================================
ATokhdoruActor::ATokhdoruActor()
{
        PrimaryActorTick.bCanEverTick = false;

        ScaleFactor        = 100.0f;
        GroundOffset       = 0.0f;
        bUseGeoJSON        = true;
        bUseVertexColorMaterial = true;
        TreeDensityScale   = 1.0f;
        GrassDensityScale  = 1.0f;
        MaxTreeInstances   = 2000;
        MaxGrassInstances  = 5000;

        // ---------- Root ----------
        DefaultRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultRoot"));
        RootComponent = DefaultRoot;

        // ---------- Procedural Mesh Components ----------
        auto MakePMC = [&](const TCHAR* Name) -> UProceduralMeshComponent*
        {
                UProceduralMeshComponent* C = CreateDefaultSubobject<UProceduralMeshComponent>(Name);
                C->SetupAttachment(RootComponent);
                return C;
        };

        BuildingMeshComp   = MakePMC(TEXT("BuildingMesh"));
        RoadMeshComp       = MakePMC(TEXT("RoadMesh"));
        SidewalkMeshComp   = MakePMC(TEXT("SidewalkMesh"));
        WaterMeshComp      = MakePMC(TEXT("WaterMesh"));
        GroundMeshComp     = MakePMC(TEXT("GroundMesh"));
        RoofMeshComp       = MakePMC(TEXT("RoofMesh"));
        VegetationMeshComp = MakePMC(TEXT("VegetationMesh"));

        // ---------- Instanced Static Mesh Components ----------
        TreeInstancedComp = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeInstanced"));
        TreeInstancedComp->SetupAttachment(RootComponent);
        TreeInstancedComp->SetMobility(EComponentMobility::Movable);

        GrassInstancedComp = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GrassInstanced"));
        GrassInstancedComp->SetupAttachment(RootComponent);
        GrassInstancedComp->SetMobility(EComponentMobility::Movable);

        // ---------- Default Material Assets ----------
        static ConstructorHelpers::FObjectFinder<UMaterialInterface> WallMat(
                TEXT("Material'/Tokhdoru/Materials/Wall/Wall_1.Wall_1'"));
        if (WallMat.Succeeded()) BuildingWallMaterial = WallMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlassMat(
                TEXT("Material'/Tokhdoru/Materials/Glass/Glass.Glass'"));
        if (GlassMat.Succeeded()) BuildingGlassMaterial = GlassMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoofMat(
                TEXT("Material'/Tokhdoru/Materials/Roof/Roof.Roof'"));
        if (RoofMat.Succeeded()) RoofMaterial = RoofMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoadMat(
                TEXT("Material'/Tokhdoru/Materials/Asphalt/Asphalt.Asphalt'"));
        if (RoadMat.Succeeded()) RoadMaterial = RoadMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> SWMat(
                TEXT("Material'/Tokhdoru/Materials/Sidewalk/Sidewalk.Sidewalk'"));
        if (SWMat.Succeeded()) SidewalkMaterial = SWMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterMat(
                TEXT("Material'/Tokhdoru/Materials/Water/Water.Water'"));
        if (WaterMat.Succeeded()) WaterMaterial = WaterMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> GroundMat(
                TEXT("Material'/Tokhdoru/Materials/BaseMaterial/Base.Base'"));
        if (GroundMat.Succeeded()) GroundGrassMaterial = GroundMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> VegMat(
                TEXT("Material'/Tokhdoru/Materials/Grass/Meadow.Meadow'"));
        if (VegMat.Succeeded()) VegetationMaterial = VegMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> BrickMat(
                TEXT("MaterialInstanceConstant'/Tokhdoru/Materials/Brick/MI_Brick_Facade_ulmmccpo_2K.MI_Brick_Facade_ulmmccpo_2K'"));
        if (BrickMat.Succeeded()) BrickFacadeMaterial = BrickMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> DarkStoneMat(
                TEXT("MaterialInstanceConstant'/Tokhdoru/Materials/Stone/MI_Via_Lattea_Granite_weoldhcv_4K.MI_Via_Lattea_Granite_weoldhcv_4K'"));
        if (DarkStoneMat.Succeeded()) DarkStoneFacadeMaterial = DarkStoneMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> LightStoneMat(
                TEXT("MaterialInstanceConstant'/Tokhdoru/Materials/Stone/MI_Off_White_Tiles_tfihaepg_4K.MI_Off_White_Tiles_tfihaepg_4K'"));
        if (LightStoneMat.Succeeded()) LightStoneFacadeMaterial = LightStoneMat.Object;

        static ConstructorHelpers::FObjectFinder<UStaticMesh> TreeMeshAsset(
                TEXT("StaticMesh'/Tokhdoru/StaticMeshes/Trees/PCG_Tree_02.PCG_Tree_02'"));
        if (TreeMeshAsset.Succeeded()) { TreeMesh = TreeMeshAsset.Object; TreeInstancedComp->SetStaticMesh(TreeMesh); }

        static ConstructorHelpers::FObjectFinder<UStaticMesh> GrassMeshAsset(
                TEXT("StaticMesh'/Tokhdoru/StaticMeshes/Grass/GRASS.GRASS'"));
        if (GrassMeshAsset.Succeeded()) { GrassMesh = GrassMeshAsset.Object; GrassInstancedComp->SetStaticMesh(GrassMesh); }
}

// ============================================================================
// BeginPlay
// ============================================================================
void ATokhdoruActor::BeginPlay()
{
        Super::BeginPlay();
}

// ============================================================================
// LoadDefaultAssets
// ============================================================================
void ATokhdoruActor::LoadDefaultAssets()
{
        auto TryLoad = [](UMaterialInterface*& Mat, const TCHAR* Path)
        {
                if (!Mat) Mat = LoadObject<UMaterialInterface>(nullptr, Path);
        };

        TryLoad(BuildingWallMaterial,    TEXT("Material'/Tokhdoru/Materials/Wall/Wall_1.Wall_1'"));
        TryLoad(BuildingGlassMaterial,   TEXT("Material'/Tokhdoru/Materials/Glass/Glass.Glass'"));
        TryLoad(RoofMaterial,            TEXT("Material'/Tokhdoru/Materials/Roof/Roof.Roof'"));
        TryLoad(RoadMaterial,            TEXT("Material'/Tokhdoru/Materials/Asphalt/Asphalt.Asphalt'"));
        TryLoad(SidewalkMaterial,        TEXT("Material'/Tokhdoru/Materials/Sidewalk/Sidewalk.Sidewalk'"));
        TryLoad(WaterMaterial,           TEXT("Material'/Tokhdoru/Materials/Water/Water.Water'"));
        TryLoad(GroundGrassMaterial,     TEXT("Material'/Tokhdoru/Materials/BaseMaterial/Base.Base'"));
        TryLoad(VegetationMaterial,      TEXT("Material'/Tokhdoru/Materials/Grass/Meadow.Meadow'"));
        TryLoad(BrickFacadeMaterial,     TEXT("MaterialInstanceConstant'/Tokhdoru/Materials/Brick/MI_Brick_Facade_ulmmccpo_2K.MI_Brick_Facade_ulmmccpo_2K'"));
        TryLoad(DarkStoneFacadeMaterial, TEXT("MaterialInstanceConstant'/Tokhdoru/Materials/Stone/MI_Via_Lattea_Granite_weoldhcv_4K.MI_Via_Lattea_Granite_weoldhcv_4K'"));
        TryLoad(LightStoneFacadeMaterial,TEXT("MaterialInstanceConstant'/Tokhdoru/Materials/Stone/MI_Off_White_Tiles_tfihaepg_4K.MI_Off_White_Tiles_tfihaepg_4K'"));

        if (!TreeMesh)
        {
                TreeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("StaticMesh'/Tokhdoru/StaticMeshes/Trees/PCG_Tree_02.PCG_Tree_02'"));
                if (TreeMesh && TreeInstancedComp) TreeInstancedComp->SetStaticMesh(TreeMesh);
        }
        if (!GrassMesh)
        {
                GrassMesh = LoadObject<UStaticMesh>(nullptr, TEXT("StaticMesh'/Tokhdoru/StaticMeshes/Grass/GRASS.GRASS'"));
                if (GrassMesh && GrassInstancedComp) GrassInstancedComp->SetStaticMesh(GrassMesh);
        }
}

// ============================================================================
// Material creation helpers
// ============================================================================
UMaterialInterface* ATokhdoruActor::CreateSolidColorMaterial()
{
        UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr,
                TEXT("Material'/Tokhdoru/Materials/M_SolidColor.M_SolidColor'"));
        if (!M)
                UE_LOG(LogTemp, Warning, TEXT("Tokhdoru: M_SolidColor not found. "
                        "Create: VectorParameter 'Color'→BaseColor, ScalarParameter 'Roughness'=0.7, "
                        "Opaque, Default Lit, Two Sided."));
        return M;
}

UMaterialInterface* ATokhdoruActor::CreateVertexColorMaterial()
{
        UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr,
                TEXT("Material'/Tokhdoru/Materials/M_VertexColor.M_VertexColor'"));
        if (!M)
                M = LoadObject<UMaterialInterface>(nullptr,
                        TEXT("Material'/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial'"));
        return M;
}

UMaterialInterface* ATokhdoruActor::CreateGlassMaterial()
{
        return LoadObject<UMaterialInterface>(nullptr,
                TEXT("Material'/Tokhdoru/Materials/Glass/Glass.Glass'"));
}

UMaterialInstanceDynamic* ATokhdoruActor::CreateColoredMID(UMaterialInterface* ParentMat, FLinearColor Color)
{
        if (!ParentMat) return nullptr;
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ParentMat, this);
        if (MID)
        {
                MID->SetVectorParameterValue(FName("Color"),     Color);
                MID->SetVectorParameterValue(FName("BaseColor"), Color);
        }
        return MID;
}

// ============================================================================
// ApplyMaterials
// ============================================================================
void ATokhdoruActor::ApplyMaterials()
{
        static const FLinearColor WallColor       = FLinearColor(FColor(180,170,160));
        static const FLinearColor RoadColor       = FLinearColor(FColor(60, 60, 60));
        static const FLinearColor SidewalkColor   = FLinearColor(FColor(180,175,170));
        static const FLinearColor WaterColor      = FLinearColor(FColor(30, 100,200));
        static const FLinearColor RoofColor       = FLinearColor(FColor(140, 80, 55));
        static const FLinearColor VegetationColor = FLinearColor(FColor(50, 140, 40));
        static const FLinearColor BrickColor      = FLinearColor(FColor(180, 90, 50));
        static const FLinearColor DarkStoneColor  = FLinearColor(FColor(120,120,115));
        static const FLinearColor LightStoneColor = FLinearColor(FColor(220,210,195));
        static const FLinearColor GlassColor      = FLinearColor(FColor(70, 130,200));

        UMaterialInterface* SolidMat  = CreateSolidColorMaterial();
        UMaterialInterface* VCMat     = CreateVertexColorMaterial();
        UMaterialInterface* GlassMat  = BuildingGlassMaterial ? BuildingGlassMaterial : CreateGlassMaterial();

        // Helper: apply to a section; prefers SolidMat MID, then VCMat fallback
        auto ApplySolid = [&](UProceduralMeshComponent* Comp, int32 Sec,
                               UMaterialInterface* Preferred, const FLinearColor& FallbackColor)
        {
                if (!Comp) return;
                if (Preferred)          { Comp->SetMaterial(Sec, Preferred); return; }
                if (SolidMat)           { Comp->SetMaterial(Sec, CreateColoredMID(SolidMat, FallbackColor)); return; }
                if (VCMat)              { Comp->SetMaterial(Sec, VCMat); return; }
        };

        if (bUseVertexColorMaterial)
        {
                // Building sections
                // 0=Wall  1=Glass  2=Brick  3=DarkStone  4=LightStone
                ApplySolid(BuildingMeshComp, 0, BuildingWallMaterial, WallColor);
                if (BuildingMeshComp && BuildingMeshComp->GetNumSections() > 1)
                        ApplySolid(BuildingMeshComp, 1, GlassMat, GlassColor);
                if (BuildingMeshComp && BuildingMeshComp->GetNumSections() > 2)
                        ApplySolid(BuildingMeshComp, 2, BrickFacadeMaterial, BrickColor);
                if (BuildingMeshComp && BuildingMeshComp->GetNumSections() > 3)
                        ApplySolid(BuildingMeshComp, 3, DarkStoneFacadeMaterial, DarkStoneColor);
                if (BuildingMeshComp && BuildingMeshComp->GetNumSections() > 4)
                        ApplySolid(BuildingMeshComp, 4, LightStoneFacadeMaterial, LightStoneColor);

                ApplySolid(RoadMeshComp,       0, RoadMaterial,        RoadColor);
                ApplySolid(SidewalkMeshComp,   0, SidewalkMaterial,    SidewalkColor);
                ApplySolid(WaterMeshComp,      0, WaterMaterial,       WaterColor);
                ApplySolid(GroundMeshComp,     0, GroundGrassMaterial, FLinearColor(FColor(80,140,60)));
                ApplySolid(VegetationMeshComp, 0, VegetationMaterial,  VegetationColor);

                // ---- Roof: use per-section MIDs with per-building roof colour ----
                // RoofMeshComp sections map 1:1 to Buildings array (one section per building).
                // We apply RoofMaterial globally (it is a texture atlas) OR fall back to
                // M_SolidColor with the roof colour baked as vertex colour average.
                // For simplicity (and correct colour-per-building) we apply a single
                // RoofMaterial to section 0; the vertex colours in the mesh carry per-building
                // colour that the material can sample.
                if (RoofMeshComp)
                {
                        if (RoofMaterial)
                                RoofMeshComp->SetMaterial(0, RoofMaterial);
                        else if (SolidMat)
                                RoofMeshComp->SetMaterial(0, CreateColoredMID(SolidMat, RoofColor));
                        else if (VCMat)
                                RoofMeshComp->SetMaterial(0, VCMat);
                }
        }
        else
        {
                // Custom materials mode
                auto TryApply = [&](UProceduralMeshComponent* Comp, int32 Sec, UMaterialInterface* Mat)
                {
                        if (!Comp) return;
                        if (Mat)      { Comp->SetMaterial(Sec, Mat); return; }
                        if (SolidMat) { Comp->SetMaterial(Sec, SolidMat); return; }
                        if (VCMat)    { Comp->SetMaterial(Sec, VCMat); }
                };

                TryApply(BuildingMeshComp,   0, BuildingWallMaterial);
                if (BuildingMeshComp && BuildingMeshComp->GetNumSections() > 1)
                        TryApply(BuildingMeshComp, 1, GlassMat);
                if (BuildingMeshComp && BuildingMeshComp->GetNumSections() > 2)
                        TryApply(BuildingMeshComp, 2, BrickFacadeMaterial);
                if (BuildingMeshComp && BuildingMeshComp->GetNumSections() > 3)
                        TryApply(BuildingMeshComp, 3, DarkStoneFacadeMaterial);
                if (BuildingMeshComp && BuildingMeshComp->GetNumSections() > 4)
                        TryApply(BuildingMeshComp, 4, LightStoneFacadeMaterial);

                TryApply(RoadMeshComp,       0, RoadMaterial);
                TryApply(SidewalkMeshComp,   0, SidewalkMaterial);
                TryApply(WaterMeshComp,      0, WaterMaterial);
                TryApply(GroundMeshComp,     0, GroundGrassMaterial);
                TryApply(RoofMeshComp,       0, RoofMaterial);
                TryApply(VegetationMeshComp, 0, VegetationMaterial);
        }
}

// ============================================================================
// SpawnVegetation / RemoveVegetation
// ============================================================================
void ATokhdoruActor::SpawnVegetation(
        const TArray<FTransform>& TreeTransforms,
        const TArray<FTransform>& GrassTransforms)
{
        RemoveVegetation();

        if (TreeMesh && TreeInstancedComp)
        {
                TreeInstancedComp->SetStaticMesh(TreeMesh);
                int32 Cap = FMath::Min(TreeTransforms.Num(), MaxTreeInstances);
                for (int32 i=0;i<Cap;i++) TreeInstancedComp->AddInstanceWorldSpace(TreeTransforms[i]);
                UE_LOG(LogTemp, Log, TEXT("Tokhdoru: Trees %d/%d"), Cap, TreeTransforms.Num());
        }
        if (GrassMesh && GrassInstancedComp)
        {
                GrassInstancedComp->SetStaticMesh(GrassMesh);
                int32 Cap = FMath::Min(GrassTransforms.Num(), MaxGrassInstances);
                for (int32 i=0;i<Cap;i++) GrassInstancedComp->AddInstanceWorldSpace(GrassTransforms[i]);
                UE_LOG(LogTemp, Log, TEXT("Tokhdoru: Grass %d/%d"), Cap, GrassTransforms.Num());
        }
}

void ATokhdoruActor::RemoveVegetation()
{
        if (TreeInstancedComp)  TreeInstancedComp->ClearInstances();
        if (GrassInstancedComp) GrassInstancedComp->ClearInstances();
}

// ============================================================================
// Generate
// ============================================================================
void ATokhdoruActor::Generate()
{
        LoadDefaultAssets();
        Clear();

        // ---------- Load geodata ----------
        TArray<FGeoBuilding>   Buildings;
        TArray<FGeoRoad>       Roads;
        TArray<FGeoWater>      WaterBodies;
        TArray<FGeoPOI>        POIs;
        TArray<FGeoVegetation> Vegetations;

        if (bUseGeoJSON && !GeoJSONFilePath.IsEmpty())
        {
                FGeoJSONLoader Loader;
                if (!Loader.LoadGeoJSON(GeoJSONFilePath))
                {
                        UE_LOG(LogTemp, Error, TEXT("Tokhdoru: Failed to load GeoJSON: %s"), *GeoJSONFilePath);
                        return;
                }
                Buildings  = Loader.GetBuildings();
                Roads      = Loader.GetRoads();
                WaterBodies= Loader.GetWaters();
                POIs       = Loader.GetPOIs();
                Vegetations= Loader.GetVegetations();
                // GeoJSON trees are loaded; vegetation transforms will be generated
                // by GenerateVegetationTransforms alongside OSM-style vegetation areas.
        }
        else if (!OSMFilePath.IsEmpty())
        {
                FOSMLoader Loader;
                if (!Loader.LoadOSMFile(OSMFilePath))
                {
                        UE_LOG(LogTemp, Error, TEXT("Tokhdoru: Failed to load OSM: %s"), *OSMFilePath);
                        return;
                }
                Buildings  = Loader.GetBuildings();
                Roads      = Loader.GetRoads();
                WaterBodies= Loader.GetWaters();
                POIs       = Loader.GetPOIs();
                Vegetations= Loader.GetVegetations();
        }
        else
        {
                UE_LOG(LogTemp, Warning, TEXT("Tokhdoru: No data file specified"));
                return;
        }

        UE_LOG(LogTemp, Log, TEXT("Tokhdoru: Loaded B=%d R=%d W=%d P=%d V=%d"),
                Buildings.Num(), Roads.Num(), WaterBodies.Num(), POIs.Num(), Vegetations.Num());

        // ---------- Centre-offset (double precision) ----------
        double DMinX=DBL_MAX,DMaxX=-DBL_MAX,DMinY=DBL_MAX,DMaxY=-DBL_MAX;
        auto ExpandD = [&](double X, double Y)
        {
                if(X<DMinX)DMinX=X; if(X>DMaxX)DMaxX=X;
                if(Y<DMinY)DMinY=Y; if(Y>DMaxY)DMaxY=Y;
        };
        for (const FGeoBuilding& B : Buildings)
                for (const FVector2D& N : B.Nodes) ExpandD(N.X, N.Y);
        for (const FGeoRoad& R : Roads)
                for (const FVector2D& P : R.Points) ExpandD(P.X, P.Y);
        for (const FGeoWater& W : WaterBodies)
                for (const FVector2D& P : W.Points) ExpandD(P.X, P.Y);
        for (const FGeoVegetation& V : Vegetations)
                for (const FVector2D& P : V.Points) ExpandD(P.X, P.Y);

        float OX=0.f, OY=0.f;
        if (DMinX<DMaxX && DMinY<DMaxY)
        {
                OX = (float)((DMinX+DMaxX)*0.5);
                OY = (float)((DMinY+DMaxY)*0.5);
        }

        // Shift all coordinates so centre = (0,0)
        auto ShiftNodes = [&](TArray<FVector2D>& Arr)
        { for (FVector2D& P : Arr) { P.X -= OX; P.Y -= OY; } };

        for (FGeoBuilding& B : Buildings)
        {
                ShiftNodes(B.Nodes);
                if (B.Nodes.Num() > 0)
                {
                        FVector2D S(0,0);
                        for (const FVector2D& N : B.Nodes) S += N;
                        B.Center = S / (float)B.Nodes.Num();
                }
        }
        for (FGeoRoad& R : Roads)       ShiftNodes(R.Points);
        for (FGeoWater& W : WaterBodies) ShiftNodes(W.Points);
        for (FGeoVegetation& V : Vegetations) ShiftNodes(V.Points);

        // ---------- Generate meshes ----------
        FGeoMeshGenerator MeshGen;
        MeshGen.ScaleFactor = ScaleFactor;

        // Buildings
        FGeoMeshGenerator::FMeshData WallData, GlassData, BrickData, DarkData, LightData;
        MeshGen.GenerateBuildingMeshes(Buildings, WallData, GlassData, BrickData, DarkData, LightData);

        auto CreateSection = [&](UProceduralMeshComponent* Comp, int32 Sec,
                                  FGeoMeshGenerator::FMeshData& D, bool bCollision)
        {
                if (D.Vertices.Num() > 0)
                        Comp->CreateMeshSection(Sec,
                                D.Vertices, D.Triangles, D.Normals,
                                D.UV0, D.VertexColors, D.Tangents, bCollision);
        };

        CreateSection(BuildingMeshComp, 0, WallData,  true);
        CreateSection(BuildingMeshComp, 1, GlassData, false);
        CreateSection(BuildingMeshComp, 2, BrickData, false);
        CreateSection(BuildingMeshComp, 3, DarkData,  false);
        CreateSection(BuildingMeshComp, 4, LightData, false);

        // Roads
        FGeoMeshGenerator::FMeshData RoadData, SWData;
        MeshGen.GenerateRoadMeshes(Roads, RoadData, SWData);
        CreateSection(RoadMeshComp,     0, RoadData, true);
        CreateSection(SidewalkMeshComp, 0, SWData,   true);

        // Water
        FGeoMeshGenerator::FMeshData WaterData;
        MeshGen.GenerateWaterMeshes(WaterBodies, WaterData);
        CreateSection(WaterMeshComp, 0, WaterData, false);

        // Ground
        FGeoMeshGenerator::FMeshData GroundData;
        MeshGen.GenerateGroundMesh(Buildings, Roads, WaterBodies, Vegetations, GroundData, GroundOffset);
        CreateSection(GroundMeshComp, 0, GroundData, true);

        // Roofs  ← now with shaped geometry + per-building colour
        FGeoMeshGenerator::FMeshData RoofData;
        MeshGen.GenerateRoofMeshes(Buildings, RoofData);
        CreateSection(RoofMeshComp, 0, RoofData, false);

        // Vegetation
        FGeoMeshGenerator::FMeshData VegData;
        MeshGen.GenerateVegetationMeshes(Vegetations, VegData);
        CreateSection(VegetationMeshComp, 0, VegData, false);

        // ---------- Materials ----------
        ApplyMaterials();

        // ---------- Update bounds ----------
        auto UpdateB = [](UProceduralMeshComponent* C) { if (C) C->UpdateBounds(); };
        UpdateB(BuildingMeshComp); UpdateB(RoadMeshComp); UpdateB(SidewalkMeshComp);
        UpdateB(WaterMeshComp);    UpdateB(GroundMeshComp); UpdateB(RoofMeshComp);
        UpdateB(VegetationMeshComp);

        // ---------- Vegetation instances ----------
        TArray<FTransform> TreeXf, GrassXf;
        MeshGen.GenerateVegetationTransforms(Vegetations, Buildings, Roads,
                TreeXf, GrassXf, TreeDensityScale, GrassDensityScale);
        SpawnVegetation(TreeXf, GrassXf);

        // ---------- Log summary ----------
        UE_LOG(LogTemp, Log, TEXT("Tokhdoru: Done. ScaleFactor=%.0f  Centre(%.1f, %.1f)m"), ScaleFactor, OX, OY);
        UE_LOG(LogTemp, Log, TEXT("  Verts: Wall=%d Glass=%d Road=%d SW=%d Water=%d Ground=%d Roof=%d Veg=%d"),
                WallData.Vertices.Num(), GlassData.Vertices.Num(),
                RoadData.Vertices.Num(), SWData.Vertices.Num(),
                WaterData.Vertices.Num(), GroundData.Vertices.Num(),
                RoofData.Vertices.Num(), VegData.Vertices.Num());
        UE_LOG(LogTemp, Log, TEXT("  Trees=%d  Grass=%d"), TreeXf.Num(), GrassXf.Num());

        // Log building:part count for debugging
        int32 NumParts = 0;
        for (const FGeoBuilding& B : Buildings) if (B.bIsBuildingPart) NumParts++;
        UE_LOG(LogTemp, Log, TEXT("  Buildings=%d  (building:parts=%d)"), Buildings.Num(), NumParts);

        // Log roof shape distribution
        TMap<ERoofShape,int32> RoofCount;
        for (const FGeoBuilding& B : Buildings) RoofCount.FindOrAdd(B.RoofShape)++;
        UE_LOG(LogTemp, Log, TEXT("  Roof shapes: Flat=%d Gabled=%d Hipped=%d Pyramidal=%d Other=%d"),
                RoofCount.FindRef(ERoofShape::Flat),
                RoofCount.FindRef(ERoofShape::Gabled),
                RoofCount.FindRef(ERoofShape::Hipped),
                RoofCount.FindRef(ERoofShape::Pyramidal),
                Buildings.Num()
                  - RoofCount.FindRef(ERoofShape::Flat)
                  - RoofCount.FindRef(ERoofShape::Gabled)
                  - RoofCount.FindRef(ERoofShape::Hipped)
                  - RoofCount.FindRef(ERoofShape::Pyramidal));
}

// ============================================================================
// Clear
// ============================================================================
void ATokhdoruActor::Clear()
{
        if (BuildingMeshComp)   BuildingMeshComp->ClearAllMeshSections();
        if (RoadMeshComp)       RoadMeshComp->ClearAllMeshSections();
        if (SidewalkMeshComp)   SidewalkMeshComp->ClearAllMeshSections();
        if (WaterMeshComp)      WaterMeshComp->ClearAllMeshSections();
        if (GroundMeshComp)     GroundMeshComp->ClearAllMeshSections();
        if (RoofMeshComp)       RoofMeshComp->ClearAllMeshSections();
        if (VegetationMeshComp) VegetationMeshComp->ClearAllMeshSections();
        RemoveVegetation();
}