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
#include "Engine/Texture.h"


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
        b3DWindow          = false;
        bUseTerrainElevation = true;
        TerrainGridSubdivisions = 48;
        bShowRailways      = true;
        bShowPOIMarkers    = true;
        bUsePBRMaterials   = true;
        POIMarkerRadius    = 80.f;
        POIMarkerHeight    = 250.f;
        TreeDensityScale   = 1.0f;
        GrassDensityScale  = 1.0f;
        MaxTreeInstances   = 2000;
        MaxGrassInstances  = 5000;
        ModelScale         = 1.0f;
        ModelBaseRotation  = FRotator(0.f, 0.f, 90.f); // stand up Y-up GLB models

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
        RailwayMeshComp    = MakePMC(TEXT("RailwayMesh"));
        POIMeshComp        = MakePMC(TEXT("POIMesh"));

        // ---------- Instanced Static Mesh Components ----------
        TreeInstancedComp = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeInstanced"));
        TreeInstancedComp->SetupAttachment(RootComponent);
        TreeInstancedComp->SetMobility(EComponentMobility::Movable);

        GrassInstancedComp = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GrassInstanced"));
        GrassInstancedComp->SetupAttachment(RootComponent);
        GrassInstancedComp->SetMobility(EComponentMobility::Movable);

        // ---------- Default Material Assets (imported from streets-gl) ----------
        // These live in the plugin Content folder under /Tokhdoru/Materials/.
        BuildingWallMaterial    = nullptr;
        BuildingGlassMaterial   = nullptr;
        BrickFacadeMaterial     = nullptr;
        DarkStoneFacadeMaterial = nullptr;
        LightStoneFacadeMaterial= nullptr;
        RoofMaterial            = nullptr;
        RoadMaterial            = nullptr;
        SidewalkMaterial        = nullptr;
        WaterMaterial           = nullptr;
        GroundGrassMaterial     = nullptr;
        VegetationMaterial      = nullptr;
        RailwayBallastMaterial  = nullptr;
        RailwayRailMaterial     = nullptr;
        POIMaterial             = nullptr;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> BuildingMat(
                TEXT("/Tokhdoru/Materials/M_Building.M_Building"));
        if (BuildingMat.Succeeded()) BuildingWallMaterial = BuildingMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoofMat(
                TEXT("/Tokhdoru/Materials/M_Roof.M_Roof"));
        if (RoofMat.Succeeded()) RoofMaterial = RoofMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoadMat(
                TEXT("/Tokhdoru/Materials/M_Road.M_Road"));
        if (RoadMat.Succeeded()) RoadMaterial = RoadMat.Object;

        // M_Area covers projected ground areas: sidewalks/pavement and vegetation.
        static ConstructorHelpers::FObjectFinder<UMaterialInterface> AreaMat(
                TEXT("/Tokhdoru/Materials/M_Area.M_Area"));
        if (AreaMat.Succeeded()) { SidewalkMaterial = AreaMat.Object; VegetationMaterial = AreaMat.Object; }

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterMat(
                TEXT("/Tokhdoru/Materials/M_Water.M_Water"));
        if (WaterMat.Succeeded()) WaterMaterial = WaterMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> TerrainMat(
                TEXT("/Tokhdoru/Materials/M_Terrain.M_Terrain"));
        if (TerrainMat.Succeeded()) GroundGrassMaterial = TerrainMat.Object;

        static ConstructorHelpers::FObjectFinder<UMaterialInterface> RailwayMat(
                TEXT("/Tokhdoru/Materials/M_Railway.M_Railway"));
        if (RailwayMat.Succeeded()) { RailwayBallastMaterial = RailwayMat.Object; RailwayRailMaterial = RailwayMat.Object; }

        static ConstructorHelpers::FObjectFinder<UStaticMesh> TreeMeshAsset(
                TEXT("StaticMesh'/Tokhdoru/Models/Trees/PCG_Tree_02.PCG_Tree_02'"));
        if (TreeMeshAsset.Succeeded()) { TreeMesh = TreeMeshAsset.Object; TreeInstancedComp->SetStaticMesh(TreeMesh); }

        static ConstructorHelpers::FObjectFinder<UStaticMesh> GrassMeshAsset(
                TEXT("StaticMesh'/Tokhdoru/Models/Grass/GRASS.GRASS'"));
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
// PostActorCreated - force newly placed actors to the world origin
// ============================================================================
void ATokhdoruActor::PostActorCreated()
{
        Super::PostActorCreated();

        // Only when first created in the editor (drag-drop / placement). Actors
        // streamed in from a saved map go through PostLoad instead, so their saved
        // transform is preserved. Runtime game spawns are left untouched.
        if (UWorld* W = GetWorld())
        {
                if (!W->IsGameWorld())
                        SetActorTransform(FTransform::Identity);
        }
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

        // Imported streets-gl materials live under /Tokhdoru/Materials/.
        TryLoad(BuildingWallMaterial,    TEXT("/Tokhdoru/Materials/M_Building.M_Building"));
        TryLoad(RoofMaterial,            TEXT("/Tokhdoru/Materials/M_Roof.M_Roof"));
        TryLoad(RoadMaterial,            TEXT("/Tokhdoru/Materials/M_Road.M_Road"));
        TryLoad(SidewalkMaterial,        TEXT("/Tokhdoru/Materials/M_Area.M_Area"));
        TryLoad(WaterMaterial,           TEXT("/Tokhdoru/Materials/M_Water.M_Water"));
        TryLoad(GroundGrassMaterial,     TEXT("/Tokhdoru/Materials/M_Terrain.M_Terrain"));
        TryLoad(VegetationMaterial,      TEXT("/Tokhdoru/Materials/M_Area.M_Area"));
        TryLoad(BuildingGlassMaterial,   TEXT("/Tokhdoru/Materials/M_Glass.M_Glass"));
        TryLoad(RailwayBallastMaterial,  TEXT("/Tokhdoru/Materials/M_Railway.M_Railway"));
        TryLoad(RailwayRailMaterial,     TEXT("/Tokhdoru/Materials/M_Railway.M_Railway"));

        // Per-building atlas diagnostics.
        UE_LOG(LogTemp, Warning,
                TEXT("Tokhdoru materials | Building=%s Roof=%s Road=%s Area=%s Water=%s Terrain=%s"),
                BuildingWallMaterial?TEXT("OK"):TEXT("MISSING"),
                RoofMaterial?TEXT("OK"):TEXT("MISSING"),
                RoadMaterial?TEXT("OK"):TEXT("MISSING"),
                SidewalkMaterial?TEXT("OK"):TEXT("MISSING"),
                WaterMaterial?TEXT("OK"):TEXT("MISSING"),
                GroundGrassMaterial?TEXT("OK"):TEXT("MISSING"));

        if (!TreeMesh)
        {
                TreeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("StaticMesh'/Tokhdoru/Models/Trees/PCG_Tree_02.PCG_Tree_02'"));
                if (TreeMesh && TreeInstancedComp) TreeInstancedComp->SetStaticMesh(TreeMesh);
        }
        if (!GrassMesh)
        {
                GrassMesh = LoadObject<UStaticMesh>(nullptr, TEXT("StaticMesh'/Tokhdoru/Models/Grass/GRASS.GRASS'"));
                if (GrassMesh && GrassInstancedComp) GrassInstancedComp->SetStaticMesh(GrassMesh);
        }
}

// ============================================================================
// Material creation helpers
// ============================================================================
UMaterialInterface* ATokhdoruActor::CreateSolidColorMaterial()
{
        UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr,
                TEXT("Material'/Tokhdoru/Material/M_SolidColor.M_SolidColor'"));
        if (!M)
                UE_LOG(LogTemp, Warning, TEXT("Tokhdoru: M_SolidColor not found. "
                        "Create: VectorParameter 'Color'→BaseColor, ScalarParameter 'Roughness'=0.7, "
                        "Opaque, Default Lit, Two Sided."));
        return M;
}

UMaterialInterface* ATokhdoruActor::CreateVertexColorMaterial()
{
        // Plain vertex-colour material that lives in the plugin Content folder.
        UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr,
                TEXT("/Tokhdoru/Materials/M_VertexColor.M_VertexColor"));
        if (!M)
                M = LoadObject<UMaterialInterface>(nullptr,
                        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        return M;
}

UMaterialInterface* ATokhdoruActor::CreateGlassMaterial()
{
        return LoadObject<UMaterialInterface>(nullptr,
                TEXT("Material'/Tokhdoru/Material/Glass/Glass.Glass'"));
}

UMaterialInstanceDynamic* ATokhdoruActor::CreateColoredMID(UMaterialInterface* ParentMat, FLinearColor Color)
{
        if (!ParentMat) return nullptr;
        UMaterialInterface* ResolvedParent = ResolvePBRMaterial(ParentMat);
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ResolvedParent, this);
        if (MID)
        {
                MID->SetVectorParameterValue(FName("Color"),     Color);
                MID->SetVectorParameterValue(FName("BaseColor"), Color);
        }
        return MID;
}

// ============================================================================
// ResolvePBRMaterial — bind streets-gl normal/ORM maps when present
// ============================================================================
UMaterialInterface* ATokhdoruActor::ResolvePBRMaterial(UMaterialInterface* BaseMat)
{
        if (!bUsePBRMaterials || !BaseMat) return BaseMat;

        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMat, this);
        if (!MID) return BaseMat;

        auto TryTex = [&](const TCHAR* Param, const TCHAR* Path)
        {
                if (UTexture* Tex = LoadObject<UTexture>(nullptr, Path))
                        MID->SetTextureParameterValue(FName(Param), Tex);
        };

        // streets-gl building facade PBR (parameter names match imported M_Building).
        TryTex(TEXT("NormalTexture"),   TEXT("/Tokhdoru/Textures/T_BuildingNormal.T_BuildingNormal"));
        TryTex(TEXT("NormalMap"),        TEXT("/Tokhdoru/Textures/T_BuildingNormal.T_BuildingNormal"));
        TryTex(TEXT("RoughnessMap"),     TEXT("/Tokhdoru/Textures/T_BuildingRoughness.T_BuildingRoughness"));
        TryTex(TEXT("ORMTexture"),       TEXT("/Tokhdoru/Textures/T_BuildingORM.T_BuildingORM"));
        TryTex(TEXT("MetallicMap"),      TEXT("/Tokhdoru/Textures/T_BuildingORM.T_BuildingORM"));

        return MID;
}

// ============================================================================
// ApplyMaterials
// ============================================================================
void ATokhdoruActor::ApplyMaterials()
{
    LoadDefaultAssets();
    
    UMaterialInterface* VCMat = CreateVertexColorMaterial();

    auto Resolve = [&](UMaterialInterface* Override, UMaterialInterface* Textured) -> UMaterialInterface*
    {
        if (Override) return Override;
        if (Textured) return Textured;
        return VCMat;
    };

    auto ApplyAll = [&](UProceduralMeshComponent* Comp, UMaterialInterface* Mat)
    {
        if (!Comp || !Mat) return;
        const int32 NumSec = Comp->GetNumSections();
        for (int32 s = 0; s < FMath::Max(1, NumSec); s++)
            Comp->SetMaterial(s, Mat);
    };

    UMaterialInterface* BuildingMat = ResolvePBRMaterial(Resolve(WallMaterialOverride, BuildingWallMaterial));
    UMaterialInterface* RoofMat     = ResolvePBRMaterial(Resolve(RoofMaterialOverride, RoofMaterial));
    UMaterialInterface* RoadMat     = ResolvePBRMaterial(Resolve(RoadMaterialOverride, RoadMaterial));
    UMaterialInterface* WaterMat    = Resolve(WaterMaterialOverride, WaterMaterial);
    UMaterialInterface* GroundMat   = ResolvePBRMaterial(Resolve(GroundMaterialOverride, GroundGrassMaterial));
    UMaterialInterface* SidewalkMat = ResolvePBRMaterial(Resolve(nullptr, SidewalkMaterial));
    UMaterialInterface* VegMat      = Resolve(nullptr, VegetationMaterial);
    UMaterialInterface* RailwayMat    = Resolve(RailwayMaterialOverride, RailwayBallastMaterial);
    UMaterialInterface* POIMat      = bUseVertexColorMaterial ? VCMat : Resolve(POIMaterialOverride, VCMat);

    // Buildings: Apply base material to all sections first
    ApplyAll(BuildingMeshComp, BuildingMat);

    // ========================================================================
    // 3DWindow toggle — manage ALL window-related sections:
    //   Section 1 = GlassData (3D glass for tall commercial buildings)
    //   Section 2 = BrickData (facade panels)
    //   Section 3 = DarkData (facade panels)
    //   Section 4 = LightData (facade panels)
    //   Section 5 = ColorWallData (towers/churches - OSM color only)
    //   Section 6 = WindowData (flat window texture panels)    [b3DWindow=false]
    //   Section 7 = WindowFrameData (3D window frame bars)     [b3DWindow=true]
    //   Section 8 = WindowGlassData (3D window glass panes)    [b3DWindow=true]
    // ========================================================================
    if (BuildingMeshComp && BuildingMeshComp->GetNumSections() > 1)
    {
        // Load M_Glass material for 3D window mode
        UMaterialInterface* GlassMat = BuildingGlassMaterial
            ? BuildingGlassMaterial
            : LoadObject<UMaterialInterface>(nullptr, TEXT("/Tokhdoru/Materials/M_Glass.M_Glass"));
        UMaterialInterface* FinalGlassMat = GlassMat ? GlassMat : BuildingMat;

        // Section 0: WallData — always building material
        BuildingMeshComp->SetMaterial(0, BuildingMat);

        // Section 1: Glass curtain-walls (tall commercial buildings)
        if (BuildingMeshComp->GetNumSections() > 1)
        {
            BuildingMeshComp->SetMaterial(1, FinalGlassMat);
            BuildingMeshComp->SetMeshSectionVisible(1, true);
        }

        // Section 2,3,4: Facade panels — always building material
        for (int32 Sec : {2, 3, 4})
        {
            if (BuildingMeshComp->GetNumSections() > Sec)
            {
                BuildingMeshComp->SetMaterial(Sec, BuildingMat);
                BuildingMeshComp->SetMeshSectionVisible(Sec, true);
            }
        }

        // Section 5: Color-only buildings (towers/churches)
        if (BuildingMeshComp->GetNumSections() > 5)
        {
            BuildingMeshComp->SetMaterial(5, BuildingMat);
            BuildingMeshComp->SetMeshSectionVisible(5, true);
        }

        if (b3DWindow)
        {
            // === 3D WINDOW MODE ===
            // Section 6: Flat window panels — HIDE
            if (BuildingMeshComp->GetNumSections() > 6)
            {
                BuildingMeshComp->SetMeshSectionVisible(6, false);
            }
            // Section 7: 3D window frame bars — SHOW with building material (facade texture)
            if (BuildingMeshComp->GetNumSections() > 7)
            {
                BuildingMeshComp->SetMaterial(7, BuildingMat);
                BuildingMeshComp->SetMeshSectionVisible(7, true);
            }
            // Section 8: 3D window glass panes — SHOW with M_Glass
            if (BuildingMeshComp->GetNumSections() > 8)
            {
                BuildingMeshComp->SetMaterial(8, FinalGlassMat);
                BuildingMeshComp->SetMeshSectionVisible(8, true);
            }
        }
        else
        {
            // === FLAT WINDOW MODE ===
            // Section 6: Flat window panels — SHOW with building material
            // (reads WindowSlice from UV1 → TA_BuildingDiffuse atlas window texture)
            if (BuildingMeshComp->GetNumSections() > 6)
            {
                BuildingMeshComp->SetMaterial(6, BuildingMat);
                BuildingMeshComp->SetMeshSectionVisible(6, true);
            }
            // Section 7: 3D window frame bars — HIDE
            if (BuildingMeshComp->GetNumSections() > 7)
            {
                BuildingMeshComp->SetMeshSectionVisible(7, false);
            }
            // Section 8: 3D window glass panes — HIDE
            if (BuildingMeshComp->GetNumSections() > 8)
            {
                BuildingMeshComp->SetMeshSectionVisible(8, false);
            }
        }
    }

    ApplyAll(RoadMeshComp,     RoadMat);
    ApplyAll(SidewalkMeshComp, SidewalkMat);
    ApplyAll(WaterMeshComp,    WaterMat);
    ApplyAll(GroundMeshComp,   GroundMat);
    ApplyAll(RoofMeshComp,     RoofMat);
    ApplyAll(VegetationMeshComp, VegMat);

    if (RailwayMeshComp)
    {
        if (RailwayMeshComp->GetNumSections() > 0)
                RailwayMeshComp->SetMaterial(0, RailwayMat ? RailwayMat : RoadMat);
        if (RailwayMeshComp->GetNumSections() > 1)
                RailwayMeshComp->SetMaterial(1, RailwayRailMaterial ? RailwayRailMaterial : RoadMat);
    }
    ApplyAll(POIMeshComp, POIMat);
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
// SpawnModels / RemoveModels - place OSM 3D models (street furniture, power,
// landmarks) from /Tokhdoru/Models/, one instanced component per model type.
// ============================================================================
namespace
{
        // Folder name == model type; the StaticMesh asset inside is named after the
        // folder, except the two statues.
        FString TokhdoruModelMeshPath(const FString& Type)
        {
                FString Mesh = Type;
                if (Type == TEXT("statue_0"))      Mesh = TEXT("the_thinker");
                else if (Type == TEXT("statue_1")) Mesh = TEXT("kentaur");
                return FString::Printf(TEXT("/Tokhdoru/Models/%s/%s.%s"), *Type, *Mesh, *Mesh);
        }
}

void ATokhdoruActor::RemoveModels()
{
        for (UInstancedStaticMeshComponent* C : ModelInstancedComps)
                if (C) C->DestroyComponent();
        ModelInstancedComps.Empty();
}

void ATokhdoruActor::SpawnModels(const TArray<FGeoModelInstance>& Models, float OX, float OY)
{
        RemoveModels();
        if (Models.Num() == 0) return;

        // One instanced component per model type (cached while iterating).
        TMap<FString, UInstancedStaticMeshComponent*> ByType;

        int32 Placed = 0;
        for (const FGeoModelInstance& M : Models)
        {
                if (!ByType.Contains(M.ModelType))
                {
                        UInstancedStaticMeshComponent* NewISM = nullptr;
                        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *TokhdoruModelMeshPath(M.ModelType));
                        if (Mesh)
                        {
                                NewISM = NewObject<UInstancedStaticMeshComponent>(this);
                                NewISM->SetupAttachment(RootComponent);
                                NewISM->SetMobility(EComponentMobility::Movable);
                                NewISM->RegisterComponent();
                                NewISM->SetStaticMesh(Mesh);
                                ModelInstancedComps.Add(NewISM);
                        }
                        else
                        {
                                UE_LOG(LogTemp, Warning, TEXT("Tokhdoru: model mesh missing: %s"),
                                        *TokhdoruModelMeshPath(M.ModelType));
                        }
                        ByType.Add(M.ModelType, NewISM);
                }

                UInstancedStaticMeshComponent* ISM = ByType.FindRef(M.ModelType);
                if (!ISM) continue;

                // Same centre offset + scale as the generated meshes (Y negated).
                const float WX = (M.Location.X - OX) * ScaleFactor;
                const float WY = -(M.Location.Y - OY) * ScaleFactor;

                float Yaw;
                if (M.RotationDeg >= 0.f)
                {
                        Yaw = -M.RotationDeg; // OSM bearing -> UE yaw (approx)
                }
                else
                {
                        // Deterministic pseudo-random yaw from position.
                        const int32 H = FMath::Abs((int32)(M.Location.X * 13.13f + M.Location.Y * 7.77f));
                        Yaw = (float)(H % 360);
                }

                // Most models import already upright (no base correction). A few
                // lie on their side and need a -90° roll to stand up. The heading
                // yaw about world Z is then applied on top.
                const bool bNeedsUpright =
                        (M.ModelType == TEXT("hydrant") || M.ModelType == TEXT("ad_column"));
                const FQuat BaseQ = bNeedsUpright
                        ? FRotator(0.f, 0.f, -90.f).Quaternion()
                        : FQuat::Identity;
                const FQuat YawQ(FVector::UpVector, FMath::DegreesToRadians(Yaw));
                const FTransform Xf(
                        YawQ * BaseQ,
                        FVector(WX, WY, GroundOffset),
                        FVector(ModelScale));
                ISM->AddInstance(Xf);
                ++Placed;
        }

        UE_LOG(LogTemp, Log, TEXT("Tokhdoru: placed %d OSM models across %d type(s)"),
                Placed, ModelInstancedComps.Num());
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
        TArray<FGeoModelInstance> ModelInstances;
        TArray<FGeoRailway> Railways;
        TArray<FGeoTree> Trees;
        TArray<FElevationSample> ElevationSamples;

        if (bUseGeoJSON && !GeoJSONFilePath.IsEmpty())
        {
                FGeoJSONLoader Loader;
                if (!Loader.LoadGeoJSON(GeoJSONFilePath))
                {
                        UE_LOG(LogTemp, Error, TEXT("Tokhdoru: Failed to load GeoJSON: %s"), *GeoJSONFilePath);
                        return;
                }
                Buildings       = Loader.GetBuildings();
                Roads           = Loader.GetRoads();
                WaterBodies     = Loader.GetWaters();
                POIs            = Loader.GetPOIs();
                Vegetations     = Loader.GetVegetations();
                ModelInstances  = Loader.GetModelInstances();
                Railways        = Loader.GetRailways();
                Trees           = Loader.GetTrees();
                ElevationSamples= Loader.GetElevationSamples();
        }
        else if (!OSMFilePath.IsEmpty())
        {
                FOSMLoader Loader;
                if (!Loader.LoadOSMFile(OSMFilePath))
                {
                        UE_LOG(LogTemp, Error, TEXT("Tokhdoru: Failed to load OSM: %s"), *OSMFilePath);
                        return;
                }
                Buildings       = Loader.GetBuildings();
                Roads           = Loader.GetRoads();
                WaterBodies     = Loader.GetWaters();
                POIs            = Loader.GetPOIs();
                Vegetations     = Loader.GetVegetations();
                ModelInstances  = Loader.GetModelInstances();
                Railways        = Loader.GetRailways();
                Trees           = Loader.GetTrees();
                ElevationSamples= Loader.GetElevationSamples();
        }
        else
        {
                UE_LOG(LogTemp, Warning, TEXT("Tokhdoru: No data file specified"));
                return;
        }

        UE_LOG(LogTemp, Log, TEXT("Tokhdoru: Loaded B=%d R=%d W=%d P=%d V=%d Rail=%d Trees=%d Ele=%d"),
                Buildings.Num(), Roads.Num(), WaterBodies.Num(), POIs.Num(), Vegetations.Num(),
                Railways.Num(), Trees.Num(), ElevationSamples.Num());

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
        for (const FGeoRailway& Ry : Railways)
                for (const FVector2D& P : Ry.Points) ExpandD(P.X, P.Y);
        for (const FGeoPOI& P : POIs) ExpandD(P.Location.X, P.Location.Y);
        for (const FGeoTree& T : Trees) ExpandD(T.Location.X, T.Location.Y);
        for (const FElevationSample& E : ElevationSamples) ExpandD(E.Location.X, E.Location.Y);

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
                for (TArray<FVector2D>& Hole : B.Holes) ShiftNodes(Hole);
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
        for (FGeoRailway& Ry : Railways) ShiftNodes(Ry.Points);
        for (FGeoPOI& P : POIs) { P.Location.X -= OX; P.Location.Y -= OY; }
        for (FGeoTree& T : Trees) { T.Location.X -= OX; T.Location.Y -= OY; }
        for (FElevationSample& E : ElevationSamples) { E.Location.X -= OX; E.Location.Y -= OY; }

        // ---------- Generate meshes ----------
        FGeoMeshGenerator MeshGen;
        MeshGen.ScaleFactor = ScaleFactor;

        // Buildings
        FGeoMeshGenerator::FMeshData WallData, GlassData, BrickData, DarkData, LightData, ColorWallData, WindowData;
        FGeoMeshGenerator::FMeshData WindowFrameData, WindowGlassData;
        MeshGen.GenerateBuildingMeshes(Buildings, WallData, GlassData, BrickData, DarkData, LightData, ColorWallData, WindowData,
                WindowFrameData, WindowGlassData);

        static const TArray<FVector2D> EmptyUV;
        static const TArray<FVector> EmptyV3;
        static const TArray<FColor> EmptyCol;
        static const TArray<FProcMeshTangent> EmptyTang;

        auto CreateSection = [&](UProceduralMeshComponent* Comp, int32 Sec,
                                  FGeoMeshGenerator::FMeshData& D, bool bCollision)
        {
                // ALWAYS create the section — even with zero vertices — so that
                // section indices remain contiguous (PMC requires 0..N-1 with no
                // gaps).  Empty sections are invisible and cost nothing.
                if (D.Vertices.Num() > 0)
                {
                        Comp->CreateMeshSection(Sec,
                                D.Vertices, D.Triangles, D.Normals,
                                D.UV0, D.UV1, EmptyUV, EmptyUV,
                                D.VertexColors, D.Tangents, bCollision);
                }
                else
                {
                        Comp->CreateMeshSection(Sec,
                                EmptyV3, TArray<int32>(), EmptyV3,
                                EmptyUV, EmptyUV, EmptyUV, EmptyUV,
                                EmptyCol, EmptyTang, bCollision);
                }
        };

        CreateSection(BuildingMeshComp, 0, WallData,      true);
        CreateSection(BuildingMeshComp, 1, GlassData,     false);
        CreateSection(BuildingMeshComp, 2, BrickData,     false);
        CreateSection(BuildingMeshComp, 3, DarkData,      false);
        CreateSection(BuildingMeshComp, 4, LightData,     false);
        CreateSection(BuildingMeshComp, 5, ColorWallData, true);  // tower/church walls (OSM colour)
        CreateSection(BuildingMeshComp, 6, WindowData,    false); // wall-inset windows (*_Window atlas slice)
        CreateSection(BuildingMeshComp, 7, WindowFrameData, false); // 3D window frame bars
        CreateSection(BuildingMeshComp, 8, WindowGlassData, false); // 3D window glass panes

        // Roads
        FGeoMeshGenerator::FMeshData RoadData, SWData;
        MeshGen.GenerateRoadMeshes(Roads, RoadData, SWData);
        CreateSection(RoadMeshComp,     0, RoadData, true);
        CreateSection(SidewalkMeshComp, 0, SWData,   true);

        // Water
        FGeoMeshGenerator::FMeshData WaterData;
        MeshGen.GenerateWaterMeshes(WaterBodies, WaterData);
        CreateSection(WaterMeshComp, 0, WaterData, false);

        // Ground (flat or terrain-deformed from ele / GeoJSON Z)
        FGeoMeshGenerator::FMeshData GroundData;
        MeshGen.GenerateGroundMesh(Buildings, Roads, WaterBodies, Vegetations,
                ElevationSamples, GroundData, GroundOffset, bUseTerrainElevation, TerrainGridSubdivisions);
        CreateSection(GroundMeshComp, 0, GroundData, true);

        // Railways
        if (bShowRailways)
        {
                FGeoMeshGenerator::FMeshData BallastData, RailStripData;
                MeshGen.GenerateRailwayMeshes(Railways, BallastData, RailStripData);
                CreateSection(RailwayMeshComp, 0, BallastData, false);
                CreateSection(RailwayMeshComp, 1, RailStripData, false);
        }

        // POI markers
        FGeoMeshGenerator::FMeshData POIData;
        if (bShowPOIMarkers)
        {
                MeshGen.GeneratePOIMarkers(POIs, POIData, POIMarkerRadius, POIMarkerHeight);
                CreateSection(POIMeshComp, 0, POIData, false);
        }

        // Roofs  ← flat roofs (atlas) in section 0, shaped/landmark roofs (OSM colour) in section 1
        FGeoMeshGenerator::FMeshData RoofData, RoofColorData;
        MeshGen.GenerateRoofMeshes(Buildings, RoofData, RoofColorData);
        CreateSection(RoofMeshComp, 0, RoofData,      false);
        CreateSection(RoofMeshComp, 1, RoofColorData, false);

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
        UpdateB(VegetationMeshComp); UpdateB(RailwayMeshComp); UpdateB(POIMeshComp);

        // ---------- Vegetation instances ----------
        TArray<FTransform> TreeXf, GrassXf;
        MeshGen.GenerateVegetationTransforms(Vegetations, Buildings, Roads,
                TreeXf, GrassXf, TreeDensityScale, GrassDensityScale);
        FGeoMeshGenerator::AppendOSMTreeTransforms(Trees, TreeXf, ScaleFactor, GroundOffset);
        SpawnVegetation(TreeXf, GrassXf);

        // ---------- OSM 3D models (hydrants, benches, towers, statues, …) ----------
        SpawnModels(ModelInstances, OX, OY);
        UE_LOG(LogTemp, Log, TEXT("  Models=%d"), ModelInstances.Num());

        // ---------- Log summary ----------
        UE_LOG(LogTemp, Log, TEXT("Tokhdoru: Done. ScaleFactor=%.0f  Centre(%.1f, %.1f)m"), ScaleFactor, OX, OY);
        UE_LOG(LogTemp, Log, TEXT("  Verts: Wall=%d Glass=%d Road=%d SW=%d Water=%d Ground=%d Roof=%d Veg=%d POI=%d"),
                WallData.Vertices.Num(), GlassData.Vertices.Num(),
                RoadData.Vertices.Num(), SWData.Vertices.Num(),
                WaterData.Vertices.Num(), GroundData.Vertices.Num(),
                RoofData.Vertices.Num(), VegData.Vertices.Num(), POIData.Vertices.Num());
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
        if (RailwayMeshComp)    RailwayMeshComp->ClearAllMeshSections();
        if (POIMeshComp)        POIMeshComp->ClearAllMeshSections();
        RemoveVegetation();
        RemoveModels();
}