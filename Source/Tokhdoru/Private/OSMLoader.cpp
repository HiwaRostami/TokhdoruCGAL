// Copyright NazruGeo. All Rights Reserved.

#include "OSMLoader.h"
#include "XmlParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FOSMLoader::FOSMLoader()
        : bIsLoaded(false)
        , CenterLatitude(0.0), CenterLongitude(0.0)
        , MinLat(DBL_MAX), MaxLat(-DBL_MAX), MinLon(DBL_MAX), MaxLon(-DBL_MAX)
{}

FOSMLoader::~FOSMLoader() {}

// ============================================================================
// LoadOSMFile
// ============================================================================
bool FOSMLoader::LoadOSMFile(const FString& FilePath)
{
        FString FullPath = FPaths::IsRelative(FilePath) ? FPaths::ProjectDir() + FilePath : FilePath;
        if (!FPaths::FileExists(*FullPath)) return false;

        FString FileContent;
        if (!FFileHelper::LoadFileToString(FileContent, *FullPath)) return false;

        return LoadOSMFromString(FileContent);
}

// ============================================================================
// LoadOSMFromString
// ============================================================================
bool FOSMLoader::LoadOSMFromString(const FString& OSMContent)
{
        Buildings.Empty(); Roads.Empty(); Waters.Empty();
        Vegetations.Empty(); POIs.Empty(); Trees.Empty(); Railways.Empty();
        ModelInstances.Empty(); ElevationSamples.Empty();
        ModelInstances.Empty();
        NodeCoords.Empty(); NodeRawCoords.Empty(); NodeTags.Empty(); WayMap.Empty();
        MinLat = DBL_MAX; MaxLat = -DBL_MAX; MinLon = DBL_MAX; MaxLon = -DBL_MAX;

        // FXmlFile construction from an in-memory string buffer.
        // EConstructMethod has only two members in UE5: ConstructFromFile and
        // ConstructFromBuffer. Since OSMContent is the XML text (not a path),
        // we must use ConstructFromBuffer.
        FXmlFile XmlDocument(OSMContent, EConstructMethod::ConstructFromBuffer);
        if (!XmlDocument.IsValid())
        {
                UE_LOG(LogTemp, Error, TEXT("OSMLoader: Failed to parse OSM XML"));
                return false;
        }

        FXmlNode* RootNode = XmlDocument.GetRootNode();
        if (!RootNode) return false;

        // Phase 1: Parse all <node> elements (coordinates + tags)
        ParseNodes(RootNode);

        // Phase 2: Parse all <way> elements (roads, buildings, etc.)
        ParseWays(RootNode);

        // Phase 3: Parse all <relation> elements (multipolygon buildings, etc.)
        ParseRelations(RootNode);

        // Phase 4: Assemble building:parts into composite buildings
        AssembleBuildingParts();

        // Phase 5: Calculate center and convert coordinates
        CalculateCenterPoint();
        ConvertAllCoordsToLocal();

        bIsLoaded = true;
        UE_LOG(LogTemp, Log, TEXT("OSMLoader: Loaded B=%d R=%d W=%d V=%d Trees=%d Rail=%d"),
                Buildings.Num(), Roads.Num(), Waters.Num(), Vegetations.Num(),
                Trees.Num(), Railways.Num());

        return true;
}

// ============================================================================
// ParseNodes - Extract all <node> elements
// ============================================================================
void FOSMLoader::ParseNodes(const FXmlNode* RootNode)
{
        if (!RootNode) return;

        for (const FXmlNode* Child : RootNode->GetChildrenNodes())
        {
                if (Child->GetTag() != TEXT("node")) continue;

                // Extract lat/lon attributes
                int64 NodeId = FCString::Atoi64(*Child->GetAttribute(TEXT("id")));
                double Lat = FCString::Atod(*Child->GetAttribute(TEXT("lat")));
                double Lon = FCString::Atod(*Child->GetAttribute(TEXT("lon")));

                // Update bounds
                if (Lat < MinLat) MinLat = Lat;
                if (Lat > MaxLat) MaxLat = Lat;
                if (Lon < MinLon) MinLon = Lon;
                if (Lon > MaxLon) MaxLon = Lon;

                // Store raw coordinates for later conversion
                NodeRawCoords.Add(NodeId, FDoublePoint2D(Lon, Lat));

                // Extract tags from this node (for POIs, trees, etc.)
                TMap<FString, FString> Tags = ExtractTags(Child);
                if (Tags.Num() > 0)
                {
                        NodeTags.Add(NodeId, Tags);

                        // OSM ele tag — used for terrain deformation (streets.gl terrain parity).
                        const FString* EleStr = Tags.Find(TEXT("ele"));
                        if (EleStr && EleStr->IsNumeric())
                        {
                                FElevationSample Sample;
                                Sample.RawLonLat = FDoublePoint2D(Lon, Lat);
                                Sample.Elevation = FCString::Atod(**EleStr);
                                ElevationSamples.Add(Sample);
                        }
                }
        }
}

// ============================================================================
// ParseWays - Extract all <way> elements
// ============================================================================
void FOSMLoader::ParseWays(const FXmlNode* RootNode)
{
        if (!RootNode) return;

        for (const FXmlNode* Child : RootNode->GetChildrenNodes())
        {
                if (Child->GetTag() != TEXT("way")) continue;

                int64 WayId = FCString::Atoi64(*Child->GetAttribute(TEXT("id")));
                TMap<FString, FString> Tags = ExtractTags(Child);

                // Collect node references
                TArray<int64> NodeRefs;
                for (const FXmlNode* NdChild : Child->GetChildrenNodes())
                {
                        if (NdChild->GetTag() == TEXT("nd"))
                        {
                                int64 Ref = FCString::Atoi64(*NdChild->GetAttribute(TEXT("ref")));
                                NodeRefs.Add(Ref);
                        }
                }

                // Store in WayMap for relation lookup
                FOSMWay& Way = WayMap.Add(WayId);
                Way.NodeRefs = NodeRefs;
                Way.Tags = Tags;

                if (NodeRefs.Num() < 2) continue;

                // Determine what kind of feature this way represents.
                // building=no/construction/proposed are NOT real buildings: they must
                // not be extruded (e.g. an "Alte Akademie" construction site that
                // streets.gl keeps flat). Such ways fall through to nothing.
                bool bHasBuildingTag = false;
                if (Tags.Contains(TEXT("building")))
                {
                        const FString BV = Tags[TEXT("building")].ToLower();
                        bHasBuildingTag = (BV != TEXT("no") && BV != TEXT("construction") &&
                                BV != TEXT("proposed") && BV != TEXT("demolished"));
                }
                bool bHasHighwayTag = Tags.Contains(TEXT("highway"));
                bool bHasRailwayTag = Tags.Contains(TEXT("railway"));
                bool bHasWaterTag = Tags.Contains(TEXT("water")) || Tags.Contains(TEXT("waterway"));
                bool bHasBuildingPartTag = Tags.Contains(TEXT("building:part"));
                bool bIsArea = Tags.Contains(TEXT("area")) && Tags[TEXT("area")] == TEXT("yes");

                // Check for natural/landuse/leisure (vegetation)
                bool bIsVegetation = false;
                if (Tags.Contains(TEXT("landuse")))
                {
                        FString Landuse = Tags[TEXT("landuse")];
                        if (Landuse == TEXT("grass") || Landuse == TEXT("forest") || Landuse == TEXT("orchard")
                                || Landuse == TEXT("vineyard") || Landuse == TEXT("meadow") || Landuse == TEXT("flowerbed")
                                || Landuse == TEXT("shrubs")
                                // Bare ground surfaces — rendered as a flat AREA (brown, no
                                // grass/trees scattered) so a construction site reads as a
                                // dirt lot instead of a green field (e.g. Baufeld Marienhof).
                                || Landuse == TEXT("construction") || Landuse == TEXT("brownfield")
                                || Landuse == TEXT("landfill"))
                                bIsVegetation = true;
                }
                if (!bIsVegetation && Tags.Contains(TEXT("leisure")))
                {
                        FString Leisure = Tags[TEXT("leisure")];
                        if (Leisure == TEXT("park") || Leisure == TEXT("garden"))
                                bIsVegetation = true;
                }
                if (!bIsVegetation && Tags.Contains(TEXT("natural")))
                {
                        FString Natural = Tags[TEXT("natural")];
                        if (Natural == TEXT("wood") || Natural == TEXT("scrub") || Natural == TEXT("heath")
                                || Natural == TEXT("tree_row"))
                                bIsVegetation = true;
                }

                // Route to appropriate processor
                if (bHasBuildingTag || bHasBuildingPartTag)
                {
                        ProcessBuildingWay(Child);
                }
                else if (bIsVegetation && NodeRefs.Num() >= 3)
                {
                        ProcessVegetationWay(Child);
                }
                else if (bHasWaterTag)
                {
                        ProcessWaterWay(Child);
                }
                else if (bHasHighwayTag)
                {
                        ProcessRoadWay(Child);
                }
                else if (bHasRailwayTag)
                {
                        ProcessRailwayWay(Child);
                }
        }

        // Process POIs and trees from nodes
        for (const auto& Pair : NodeTags)
        {
                int64 NodeId = Pair.Key;
                const TMap<FString, FString>& Tags = Pair.Value;

                // Check for tree
                if (Tags.Contains(TEXT("natural")) && Tags[TEXT("natural")] == TEXT("tree"))
                {
                        const FDoublePoint2D* RawCoord = NodeRawCoords.Find(NodeId);
                        if (RawCoord)
                        {
                                FGeoTree Tree;
                                Tree.RawLonLat = *RawCoord;
                                Tree.Location = FVector2D(static_cast<float>(RawCoord->X), static_cast<float>(RawCoord->Y));
                                if (Tags.Contains(TEXT("species"))) Tree.Species = Tags[TEXT("species")];
                                if (Tags.Contains(TEXT("genus"))) Tree.Genus = Tags[TEXT("genus")];
                                if (Tags.Contains(TEXT("height")))
                                {
                                        Tree.Height = FCString::Atof(*Tags[TEXT("height")]);
                                        if (Tree.Height <= 0.f) Tree.Height = 8.f;
                                }
                                if (Tags.Contains(TEXT("crown_radius")))
                                {
                                        Tree.CrownRadius = FCString::Atof(*Tags[TEXT("crown_radius")]);
                                        if (Tree.CrownRadius <= 0.f) Tree.CrownRadius = 3.f;
                                }
                                Trees.Add(Tree);
                        }
                        continue;
                }

                // 3D model instances (street furniture, power, landmarks).
                // Mirrors streets-gl's OSMNodeQualifierFactory node->model mapping.
                {
                        auto Is = [&](const TCHAR* K, const TCHAR* V) -> bool
                        { const FString* P = Tags.Find(K); return P && (*P == V); };
                        auto ParseDir = [](const FString& S) -> float
                        {
                                if (S.IsNumeric()) return FCString::Atof(*S);
                                const FString U = S.ToUpper();
                                if (U == TEXT("N"))  return 0.f;   if (U == TEXT("NE")) return 45.f;
                                if (U == TEXT("E"))  return 90.f;  if (U == TEXT("SE")) return 135.f;
                                if (U == TEXT("S"))  return 180.f; if (U == TEXT("SW")) return 225.f;
                                if (U == TEXT("W"))  return 270.f; if (U == TEXT("NW")) return 315.f;
                                return -1.f;
                        };

                        FString ModelType;
                        float RotDeg = -1.f, Hgt = 0.f;

                        if (Is(TEXT("emergency"), TEXT("fire_hydrant")))
                        {
                                const FString* HT = Tags.Find(TEXT("fire_hydrant:type"));
                                if (!HT || *HT == TEXT("pillar")) ModelType = TEXT("hydrant");
                        }
                        else if (Is(TEXT("advertising"), TEXT("column"))) ModelType = TEXT("ad_column");
                        // Construction tower cranes (man_made=crane, usually crane:type=tower_crane).
                        else if (Is(TEXT("man_made"), TEXT("crane")))     ModelType = TEXT("tower_crane");
                        else if (Is(TEXT("power"), TEXT("tower")))        ModelType = TEXT("transmission_tower");
                        else if (Is(TEXT("power"), TEXT("pole")))         ModelType = TEXT("utility_pole");
                        else if (Is(TEXT("amenity"), TEXT("bench")))
                        {
                                ModelType = TEXT("bench");
                                const FString* Dir = Tags.Find(TEXT("direction"));
                                if (Dir) RotDeg = ParseDir(*Dir);
                        }
                        else if (Is(TEXT("leisure"), TEXT("picnic_table"))) ModelType = TEXT("picnic_table");
                        else if (Is(TEXT("highway"), TEXT("bus_stop")))     ModelType = TEXT("bus_stop");
                        else if (Is(TEXT("power"), TEXT("generator")) && Is(TEXT("generator:source"), TEXT("wind")))
                        {
                                ModelType = TEXT("wind_turbine");
                                const FString* H = Tags.Find(TEXT("height"));
                                if (H) Hgt = FCString::Atof(**H);
                        }
                        else if (Is(TEXT("historic"), TEXT("memorial")))
                        {
                                const FString* M = Tags.Find(TEXT("memorial"));
                                const FString MV = M ? M->ToLower() : FString();
                                if (MV.Contains(TEXT("statue")))         ModelType = TEXT("statue_0");
                                else if (MV.Contains(TEXT("sculpture"))) ModelType = TEXT("sculpture");
                                else                                     ModelType = TEXT("memorial");
                        }
                        else if (Is(TEXT("tourism"), TEXT("artwork")))
                        {
                                const FString* A = Tags.Find(TEXT("artwork_type"));
                                const FString AV = A ? A->ToLower() : FString();
                                if (AV.Contains(TEXT("statue")))         ModelType = TEXT("statue_1");
                                else if (AV.Contains(TEXT("sculpture"))) ModelType = TEXT("sculpture");
                        }

                        if (!ModelType.IsEmpty())
                        {
                                const FDoublePoint2D* RawCoord = NodeRawCoords.Find(NodeId);
                                if (RawCoord)
                                {
                                        FGeoModelInstance MI;
                                        MI.RawLonLat = *RawCoord;
                                        MI.Location = FVector2D((float)RawCoord->X, (float)RawCoord->Y);
                                        MI.ModelType = ModelType;
                                        MI.RotationDeg = RotDeg;
                                        MI.Height = Hgt;
                                        ModelInstances.Add(MI);
                                }
                                continue; // handled as a model; don't also add as a generic POI
                        }
                }

                // Other POIs
                if (Tags.Contains(TEXT("amenity")) || Tags.Contains(TEXT("shop")) ||
                        Tags.Contains(TEXT("tourism")) || Tags.Contains(TEXT("office")) ||
                        Tags.Contains(TEXT("public_transport")))
                {
                        const FDoublePoint2D* RawCoord = NodeRawCoords.Find(NodeId);
                        if (RawCoord)
                        {
                                FGeoPOI POI;
                                POI.RawLonLat = *RawCoord;
                                POI.Location = FVector2D(static_cast<float>(RawCoord->X), static_cast<float>(RawCoord->Y));
                                if (Tags.Contains(TEXT("name"))) POI.Name = Tags[TEXT("name")];
                                if (Tags.Contains(TEXT("amenity"))) { POI.POIType = TEXT("amenity"); POI.POISubType = Tags[TEXT("amenity")]; }
                                else if (Tags.Contains(TEXT("shop"))) { POI.POIType = TEXT("shop"); POI.POISubType = Tags[TEXT("shop")]; }
                                else if (Tags.Contains(TEXT("tourism"))) { POI.POIType = TEXT("tourism"); POI.POISubType = Tags[TEXT("tourism")]; }
                                POIs.Add(POI);
                        }
                }
        }
}

// ============================================================================
// ParseRelations - Extract <relation> elements (multipolygon buildings, etc.)
// ============================================================================
void FOSMLoader::ParseRelations(const FXmlNode* RootNode)
{
        if (!RootNode) return;

        for (const FXmlNode* Child : RootNode->GetChildrenNodes())
        {
                if (Child->GetTag() != TEXT("relation")) continue;

                TMap<FString, FString> Tags = ExtractTags(Child);
                if (Tags.Num() == 0) continue;

                // Check if this is a building relation (excluding non-building
                // building=no/construction/proposed, same as the way path).
                bool bBuildingVal = false;
                if (Tags.Contains(TEXT("building")))
                {
                        const FString BV = Tags[TEXT("building")].ToLower();
                        bBuildingVal = (BV != TEXT("no") && BV != TEXT("construction") &&
                                BV != TEXT("proposed") && BV != TEXT("demolished"));
                }
                bool bIsBuildingRelation = bBuildingVal ||
                        (Tags.Contains(TEXT("type")) && Tags[TEXT("type")] == TEXT("building"));

                if (!bIsBuildingRelation) continue;

                // Collect outer and inner member ways
                TArray<int64> OuterWayIds, InnerWayIds;
                for (const FXmlNode* MemberNode : Child->GetChildrenNodes())
                {
                        if (MemberNode->GetTag() != TEXT("member")) continue;
                        FString Role = MemberNode->GetAttribute(TEXT("role"));
                        int64 Ref = FCString::Atoi64(*MemberNode->GetAttribute(TEXT("ref")));
                        if (Role == TEXT("outer") || Role == TEXT("outline"))
                                OuterWayIds.Add(Ref);
                        else if (Role == TEXT("inner"))
                                InnerWayIds.Add(Ref);
                }

                // Resolve outer rings into a single building
                if (OuterWayIds.Num() > 0)
                {
                        TArray<FVector2D> AllNodes;
                        TArray<FDoublePoint2D> AllRawNodes;

                        for (int64 WayId : OuterWayIds)
                        {
                                FOSMWay* Way = WayMap.Find(WayId);
                                if (!Way) continue;

                                TArray<FVector2D> WayPoints;
                                TArray<FDoublePoint2D> WayRawPoints;
                                if (ResolveNodeRefs(Way->NodeRefs, WayPoints, WayRawPoints))
                                {
                                        // Skip last point if it equals the first (closed way)
                                        if (WayPoints.Num() > 1 && WayPoints[0].Equals(WayPoints.Last(), 0.01f))
                                        {
                                                WayPoints.Pop();
                                                WayRawPoints.Pop();
                                        }
                                        AllNodes.Append(WayPoints);
                                        AllRawNodes.Append(WayRawPoints);
                                }
                        }

                        if (AllNodes.Num() >= 3)
                        {
                                FGeoBuilding Building;
                                Building.Nodes = AllNodes;
                                Building.RawLonLat = AllRawNodes;

                                // Resolve inner rings (courtyards / holes).
                                for (int64 InnerId : InnerWayIds)
                                {
                                        FOSMWay* IW = WayMap.Find(InnerId);
                                        if (!IW) continue;
                                        TArray<FVector2D> HP;
                                        TArray<FDoublePoint2D> HRP;
                                        if (ResolveNodeRefs(IW->NodeRefs, HP, HRP))
                                        {
                                                if (HRP.Num() > 1 && HRP[0].X == HRP.Last().X && HRP[0].Y == HRP.Last().Y)
                                                        HRP.Pop();
                                                if (HRP.Num() >= 3)
                                                        Building.HolesRaw.Add(HRP);
                                        }
                                }

                                // Calculate center
                                float CX = 0.f, CY = 0.f;
                                for (const FVector2D& N : AllNodes) { CX += N.X; CY += N.Y; }
                                Building.Center = FVector2D(CX / AllNodes.Num(), CY / AllNodes.Num());

                                ApplyBuildingTags(Building, Tags);
                                Buildings.Add(Building);
                        }
                }
        }
}

// ============================================================================
// ProcessBuildingWay
// ============================================================================
void FOSMLoader::ProcessBuildingWay(const FXmlNode* WayNode)
{
        if (!WayNode) return;

        TMap<FString, FString> Tags = ExtractTags(WayNode);

        // Collect node references
        TArray<int64> NodeRefs;
        for (const FXmlNode* NdChild : WayNode->GetChildrenNodes())
        {
                if (NdChild->GetTag() == TEXT("nd"))
                        NodeRefs.Add(FCString::Atoi64(*NdChild->GetAttribute(TEXT("ref"))));
        }

        TArray<FVector2D> Points;
        TArray<FDoublePoint2D> RawPoints;
        if (!ResolveNodeRefs(NodeRefs, Points, RawPoints)) return;

        // Remove closing point if it duplicates the first
        if (Points.Num() > 1 && Points[0].Equals(Points.Last(), 0.01f))
        {
                Points.Pop();
                RawPoints.Pop();
        }

        if (Points.Num() < 3) return;

        FGeoBuilding Building;
        Building.Nodes = Points;
        Building.RawLonLat = RawPoints;

        // Check if this is a building:part
        if (Tags.Contains(TEXT("building:part")))
        {
                Building.bIsBuildingPart = true;
                Building.BuildingPartId = WayNode->GetAttribute(TEXT("id"));
                // building:part=tower is a free-standing tower (e.g. Alter Peter), NOT a
                // small roof turret. Towers must never have their column dropped to a
                // neighbour's eave nor their skillion cap stretched toward an adjacent dome.
                if (Tags[TEXT("building:part")].ToLower() == TEXT("tower"))
                        Building.bIsTowerPart = true;
        }

        // Calculate center
        float CX = 0.f, CY = 0.f;
        for (const FVector2D& N : Points) { CX += N.X; CY += N.Y; }
        Building.Center = FVector2D(CX / Points.Num(), CY / Points.Num());

        ApplyBuildingTags(Building, Tags);
        Buildings.Add(Building);
}

// ============================================================================
// ProcessRoadWay
// ============================================================================
void FOSMLoader::ProcessRoadWay(const FXmlNode* WayNode)
{
        if (!WayNode) return;

        TMap<FString, FString> Tags = ExtractTags(WayNode);

        TArray<int64> NodeRefs;
        for (const FXmlNode* NdChild : WayNode->GetChildrenNodes())
        {
                if (NdChild->GetTag() == TEXT("nd"))
                        NodeRefs.Add(FCString::Atoi64(*NdChild->GetAttribute(TEXT("ref"))));
        }

        TArray<FVector2D> Points;
        TArray<FDoublePoint2D> RawPoints;
        if (!ResolveNodeRefs(NodeRefs, Points, RawPoints)) return;
        if (Points.Num() < 2) return;

        FGeoRoad Road;
        Road.Points = Points;
        Road.RawLonLat = RawPoints;
        ApplyRoadTags(Road, Tags);
        Roads.Add(Road);
}

// ============================================================================
// ProcessRailwayWay
// ============================================================================
void FOSMLoader::ProcessRailwayWay(const FXmlNode* WayNode)
{
        if (!WayNode) return;

        TMap<FString, FString> Tags = ExtractTags(WayNode);

        TArray<int64> NodeRefs;
        for (const FXmlNode* NdChild : WayNode->GetChildrenNodes())
        {
                if (NdChild->GetTag() == TEXT("nd"))
                        NodeRefs.Add(FCString::Atoi64(*NdChild->GetAttribute(TEXT("ref"))));
        }

        TArray<FVector2D> Points;
        TArray<FDoublePoint2D> RawPoints;
        if (!ResolveNodeRefs(NodeRefs, Points, RawPoints)) return;
        if (Points.Num() < 2) return;

        FGeoRailway Railway;
        Railway.Points = Points;
        Railway.RawLonLat = RawPoints;
        if (Tags.Contains(TEXT("name"))) Railway.Name = Tags[TEXT("name")];
        if (Tags.Contains(TEXT("railway"))) Railway.RailwayType = Tags[TEXT("railway")];
        if (Tags.Contains(TEXT("gauge"))) Railway.Gauge = FCString::Atof(*Tags[TEXT("gauge")]);
        if (Tags.Contains(TEXT("tracks"))) Railway.Tracks = FCString::Atoi(*Tags[TEXT("tracks")]);
        Railways.Add(Railway);
}

// ============================================================================
// ProcessWaterWay
// ============================================================================
void FOSMLoader::ProcessWaterWay(const FXmlNode* WayNode)
{
        if (!WayNode) return;

        TMap<FString, FString> Tags = ExtractTags(WayNode);

        TArray<int64> NodeRefs;
        for (const FXmlNode* NdChild : WayNode->GetChildrenNodes())
        {
                if (NdChild->GetTag() == TEXT("nd"))
                        NodeRefs.Add(FCString::Atoi64(*NdChild->GetAttribute(TEXT("ref"))));
        }

        TArray<FVector2D> Points;
        TArray<FDoublePoint2D> RawPoints;
        if (!ResolveNodeRefs(NodeRefs, Points, RawPoints)) return;

        // Remove closing point
        if (Points.Num() > 1 && Points[0].Equals(Points.Last(), 0.01f))
        {
                Points.Pop();
                RawPoints.Pop();
        }

        if (Points.Num() < 3) return;

        FGeoWater Water;
        Water.Points = Points;
        Water.RawLonLat = RawPoints;
        if (Tags.Contains(TEXT("name"))) Water.Name = Tags[TEXT("name")];
        Waters.Add(Water);
}

// ============================================================================
// ProcessVegetationWay
// ============================================================================
void FOSMLoader::ProcessVegetationWay(const FXmlNode* WayNode)
{
        if (!WayNode) return;

        TMap<FString, FString> Tags = ExtractTags(WayNode);

        TArray<int64> NodeRefs;
        for (const FXmlNode* NdChild : WayNode->GetChildrenNodes())
        {
                if (NdChild->GetTag() == TEXT("nd"))
                        NodeRefs.Add(FCString::Atoi64(*NdChild->GetAttribute(TEXT("ref"))));
        }

        TArray<FVector2D> Points;
        TArray<FDoublePoint2D> RawPoints;
        if (!ResolveNodeRefs(NodeRefs, Points, RawPoints)) return;

        // Remove closing point
        if (Points.Num() > 1 && Points[0].Equals(Points.Last(), 0.01f))
        {
                Points.Pop();
                RawPoints.Pop();
        }

        if (Points.Num() < 3) return;

        FGeoVegetation Veg;
        Veg.Points = Points;
        Veg.RawLonLat = RawPoints;

        if (Tags.Contains(TEXT("name"))) Veg.Name = Tags[TEXT("name")];

        // Determine vegetation type
        if (Tags.Contains(TEXT("landuse")))
        {
                FString Landuse = Tags[TEXT("landuse")];
                Veg.VegType = Landuse;
                if (Landuse == TEXT("forest")) Veg.Color = FLinearColor(0.1f, 0.4f, 0.1f, 1.f);
                else if (Landuse == TEXT("grass") || Landuse == TEXT("meadow")) Veg.Color = FLinearColor(0.25f, 0.6f, 0.2f, 1.f);
                else if (Landuse == TEXT("flowerbed")) Veg.Color = FLinearColor(0.6f, 0.4f, 0.3f, 1.f);
                // Bare construction/brownfield/landfill ground: dirt brown, no greenery.
                else if (Landuse == TEXT("construction") || Landuse == TEXT("brownfield") || Landuse == TEXT("landfill"))
                        Veg.Color = FLinearColor(0.42f, 0.33f, 0.24f, 1.f);
                else Veg.Color = FLinearColor(0.2f, 0.55f, 0.15f, 1.f);
        }
        else if (Tags.Contains(TEXT("leisure")))
        {
                Veg.VegType = TEXT("park");
                Veg.Color = FLinearColor(0.2f, 0.55f, 0.15f, 1.f);
        }
        else if (Tags.Contains(TEXT("natural")))
        {
                FString Natural = Tags[TEXT("natural")];
                Veg.VegType = (Natural == TEXT("wood")) ? TEXT("wood") : TEXT("forest");
                Veg.Color = FLinearColor(0.1f, 0.4f, 0.1f, 1.f);
        }

        Vegetations.Add(Veg);
}

// ============================================================================
// ApplyBuildingTags - Map OSM tags to FGeoBuilding fields
// ============================================================================
void FOSMLoader::ApplyBuildingTags(FGeoBuilding& Building, const TMap<FString, FString>& Tags)
{
        // Building type string
        if (Tags.Contains(TEXT("building")))
        {
                Building.BuildingTypeStr = Tags[TEXT("building")];
                // Legacy int mapping for compatibility
                FString BType = Building.BuildingTypeStr.ToLower();
                if (BType == TEXT("residential") || BType == TEXT("apartments") || BType == TEXT("house")
                        || BType == TEXT("detached") || BType == TEXT("semidetached_house") || BType == TEXT("terrace"))
                        Building.BuildingType = 0;
                else if (BType == TEXT("commercial") || BType == TEXT("office") || BType == TEXT("retail"))
                        Building.BuildingType = 1;
                else if (BType == TEXT("industrial") || BType == TEXT("warehouse"))
                        Building.BuildingType = 2;
                else
                        Building.BuildingType = 3;
        }

        // Places of worship are frequently tagged building=yes + amenity=place_of_worship
        // (e.g. "Sankt Michael"), so the building= value alone misses them. Fold the
        // amenity/religion hint into the type string so IsColorOnlyBuilding treats them
        // as colour-only landmarks (and they never pull the generic 00-03 roof slices).
        if (Tags.Contains(TEXT("amenity")) && Tags[TEXT("amenity")].ToLower() == TEXT("place_of_worship"))
                Building.BuildingTypeStr += TEXT(" place_of_worship");
        if (Tags.Contains(TEXT("religion")))
                Building.BuildingTypeStr += TEXT(" religious");

        // Name
        if (Tags.Contains(TEXT("name"))) Building.Name = Tags[TEXT("name")];

        // Height
        if (Tags.Contains(TEXT("height")))
                Building.Height = FCString::Atod(*Tags[TEXT("height")]);

        // Levels
        if (Tags.Contains(TEXT("building:levels")))
                Building.Levels = FCString::Atoi(*Tags[TEXT("building:levels")]);

        // Min height — must be parsed before the level-based height estimate so that
        // building:parts with only building:levels get the correct absolute top height.
        if (Tags.Contains(TEXT("min_height")))
                Building.MinHeight = FCString::Atod(*Tags[TEXT("min_height")]);

        // Default height from levels.
        // For building:parts the OSM 'height' tag is the ABSOLUTE top from ground level.
        // When only building:levels is present, estimate the absolute top as
        // min_height + levels * floor_height so elevated parts (towers, etc.) render
        // at the correct altitude instead of starting from ground.
        if (Building.Height <= 0.0 && Building.Levels > 0)
        {
                if (Building.bIsBuildingPart && Building.MinHeight > 0.0)
                        Building.Height = Building.MinHeight + Building.Levels * 3.5;
                else
                        Building.Height = Building.Levels * 3.5;
        }

        // Min level
        if (Tags.Contains(TEXT("building:min_level")))
                Building.MinLevel = FCString::Atoi(*Tags[TEXT("building:min_level")]);

        // Roof shape
        if (Tags.Contains(TEXT("roof:shape")))
                Building.RoofShape = FOSMRoofShapeParser::Parse(Tags[TEXT("roof:shape")]);
        else if (Tags.Contains(TEXT("building:roof:shape")))
                Building.RoofShape = FOSMRoofShapeParser::Parse(Tags[TEXT("building:roof:shape")]);

        // Roof height
        if (Tags.Contains(TEXT("roof:height")))
                Building.RoofHeight = FCString::Atod(*Tags[TEXT("roof:height")]);

        // Roof levels
        if (Tags.Contains(TEXT("roof:levels")))
                Building.RoofLevels = FCString::Atoi(*Tags[TEXT("roof:levels")]);

        // Roof angle
        if (Tags.Contains(TEXT("roof:angle")))
                Building.RoofAngle = FCString::Atod(*Tags[TEXT("roof:angle")]);

        // Roof direction (degrees: 0=North, 90=East, 180=South, 270=West)
        if (Tags.Contains(TEXT("roof:direction")))
                Building.RoofDirection = FCString::Atod(*Tags[TEXT("roof:direction")]);

        // Roof orientation (across / along)
        if (Tags.Contains(TEXT("roof:orientation")))
                Building.RoofOrientation = Tags[TEXT("roof:orientation")];

        // Roof material
        if (Tags.Contains(TEXT("roof:material")))
                Building.RoofMaterial = Tags[TEXT("roof:material")];

        // Roof colour
        if (Tags.Contains(TEXT("roof:colour")))
        {
                Building.RoofColor = FOSMColorParser::Parse(Tags[TEXT("roof:colour")], Building.RoofColor);
                Building.bHasExplicitRoofColor = true;
        }

        // Building colour
        if (Tags.Contains(TEXT("building:colour")))
        {
                Building.WallColor = FOSMColorParser::Parse(Tags[TEXT("building:colour")], Building.WallColor);
                Building.Color = Building.WallColor;
                Building.bHasExplicitBuildingColour = true;
        }

        // Building material
        if (Tags.Contains(TEXT("building:material")))
                Building.FacadeMaterialType = FOSMBuildingMaterialParser::Parse(Tags[TEXT("building:material")]);

        // Estimate roof height from angle if not set
        if (Building.RoofHeight <= 0.0 && Building.RoofAngle > 0.0 && Building.Nodes.Num() > 0)
        {
                float MinX = FLT_MAX, MaxX = -FLT_MAX, MinY = FLT_MAX, MaxY = -FLT_MAX;
                for (const FVector2D& N : Building.Nodes)
                {
                        if (N.X < MinX) MinX = N.X;
                        if (N.X > MaxX) MaxX = N.X;
                        if (N.Y < MinY) MinY = N.Y;
                        if (N.Y > MaxY) MaxY = N.Y;
                }
                float HalfWidth = FMath::Min(MaxX - MinX, MaxY - MinY) * 0.5f;
                Building.RoofHeight = HalfWidth * FMath::Tan(FMath::DegreesToRadians((float)Building.RoofAngle));
        }

        // Default roof height for non-flat roofs.
        // Onion, Dome and Round are intentionally LEFT at 0 here. Their rise is
        // derived from the footprint width inside GeoMeshGenerator (where the
        // geometry is already in local units — at this parse stage Building.Nodes
        // are still raw lat/lon degrees, so a width-based calc here would be wrong).
        // A flat 3 m default also marks roof:height as explicit downstream, which
        // would block that width-proportional sizing.
        // Gambrel and Mansard are also LEFT at 0: they carry roof:levels which
        // GeoMeshGenerator converts to height (levels*300 UU). Forcing 3 m here
        // sets bRoofHeightExplicit=true and causes the roof:levels branch to be
        // skipped, producing a squashed gambrel on top of lowered walls.
        if (Building.RoofHeight <= 0.0 && Building.RoofShape != ERoofShape::Flat
                && Building.RoofShape != ERoofShape::Onion
                && Building.RoofShape != ERoofShape::Dome
                && Building.RoofShape != ERoofShape::Round
                && Building.RoofShape != ERoofShape::Gambrel
                && Building.RoofShape != ERoofShape::Mansard)
                Building.RoofHeight = 3.0;
}

// ============================================================================
// ApplyRoadTags
// ============================================================================
void FOSMLoader::ApplyRoadTags(FGeoRoad& Road, const TMap<FString, FString>& Tags)
{
        if (Tags.Contains(TEXT("highway")))
        {
                Road.RoadTypeStr = Tags[TEXT("highway")];
                FString HW = Road.RoadTypeStr.ToLower();

                if (HW == TEXT("motorway"))            Road.Width = 24.0f;
                else if (HW == TEXT("motorway_link"))   Road.Width = 12.0f;
                else if (HW == TEXT("trunk"))           Road.Width = 20.0f;
                else if (HW == TEXT("trunk_link"))      Road.Width = 12.0f;
                else if (HW == TEXT("primary"))         Road.Width = 14.0f;
                else if (HW == TEXT("primary_link"))    Road.Width = 10.0f;
                else if (HW == TEXT("secondary"))       Road.Width = 12.0f;
                else if (HW == TEXT("secondary_link"))  Road.Width = 10.0f;
                else if (HW == TEXT("tertiary"))        Road.Width = 10.0f;
                else if (HW == TEXT("residential"))     Road.Width = 8.0f;
                else if (HW == TEXT("service"))         Road.Width = 5.0f;
                else if (HW == TEXT("living_street"))   Road.Width = 6.0f;
                else if (HW == TEXT("pedestrian"))      Road.Width = 6.0f;
                else if (HW == TEXT("footway"))         Road.Width = 2.0f;
                else if (HW == TEXT("path"))            Road.Width = 1.5f;
                else if (HW == TEXT("cycleway"))        Road.Width = 2.0f;
                else if (HW == TEXT("steps"))           Road.Width = 2.0f;
                else if (HW == TEXT("track"))           Road.Width = 3.0f;
                else                                    Road.Width = 8.0f;

                Road.bIsHighway = (HW == TEXT("motorway") || HW == TEXT("trunk") ||
                        HW == TEXT("primary") || HW == TEXT("secondary") || HW == TEXT("tertiary") ||
                        HW == TEXT("motorway_link") || HW == TEXT("trunk_link") ||
                        HW == TEXT("primary_link") || HW == TEXT("secondary_link"));
        }

        if (Tags.Contains(TEXT("name"))) Road.Name = Tags[TEXT("name")];
        if (Tags.Contains(TEXT("width")))
        {
                float W = FCString::Atof(*Tags[TEXT("width")]);
                if (W > 0.f) Road.Width = W;
        }
        if (Tags.Contains(TEXT("surface:colour")))
                Road.RoadColor = FOSMColorParser::Parse(Tags[TEXT("surface:colour")], Road.RoadColor);
}

// ============================================================================
// LatLonToLocal
// ============================================================================
FVector2D FOSMLoader::LatLonToLocal(double Lat, double Lon) const
{
        const double EarthRadiusM = 6371000.0;
        double X = EarthRadiusM * ((Lon - CenterLongitude) * UE_PI / 180.0) * FMath::Cos(CenterLatitude * UE_PI / 180.0);
        double Y = EarthRadiusM * ((Lat - CenterLatitude) * UE_PI / 180.0);
        return FVector2D(static_cast<float>(X), static_cast<float>(Y));
}

// ============================================================================
// CalculateCenterPoint
// ============================================================================
void FOSMLoader::CalculateCenterPoint()
{
        CenterLatitude = (MinLat + MaxLat) / 2.0;
        CenterLongitude = (MinLon + MaxLon) / 2.0;
}

// ============================================================================
// ConvertAllCoordsToLocal
// ============================================================================
void FOSMLoader::ConvertAllCoordsToLocal()
{
        // Convert nodes (double-precision)
        NodeCoords.Empty();
        for (auto& Pair : NodeRawCoords)
        {
                NodeCoords.Add(Pair.Key, LatLonToLocal(Pair.Value.Y, Pair.Value.X));
        }

        // Buildings
        for (FGeoBuilding& B : Buildings)
        {
                if (B.RawLonLat.Num() > 0)
                {
                        B.Nodes.Empty();
                        for (const FDoublePoint2D& RL : B.RawLonLat)
                                B.Nodes.Add(LatLonToLocal(RL.Y, RL.X));
                }
                if (B.Nodes.Num() > 0)
                {
                        float CX = 0.f, CY = 0.f;
                        for (const FVector2D& N : B.Nodes) { CX += N.X; CY += N.Y; }
                        B.Center = FVector2D(CX / B.Nodes.Num(), CY / B.Nodes.Num());
                }

                // Convert inner rings (courtyards / holes) to local meters.
                B.Holes.Empty();
                for (const TArray<FDoublePoint2D>& HRaw : B.HolesRaw)
                {
                        TArray<FVector2D> H;
                        H.Reserve(HRaw.Num());
                        for (const FDoublePoint2D& RL : HRaw)
                                H.Add(LatLonToLocal(RL.Y, RL.X));
                        if (H.Num() >= 3)
                                B.Holes.Add(H);
                }
        }

        // Roads
        for (FGeoRoad& R : Roads)
        {
                if (R.RawLonLat.Num() > 0)
                {
                        R.Points.Empty();
                        for (const FDoublePoint2D& RL : R.RawLonLat)
                                R.Points.Add(LatLonToLocal(RL.Y, RL.X));
                }
        }

        // Water
        for (FGeoWater& W : Waters)
        {
                if (W.RawLonLat.Num() > 0)
                {
                        W.Points.Empty();
                        for (const FDoublePoint2D& RL : W.RawLonLat)
                                W.Points.Add(LatLonToLocal(RL.Y, RL.X));
                }
        }

        // Vegetation
        for (FGeoVegetation& V : Vegetations)
        {
                if (V.RawLonLat.Num() > 0)
                {
                        V.Points.Empty();
                        for (const FDoublePoint2D& RL : V.RawLonLat)
                                V.Points.Add(LatLonToLocal(RL.Y, RL.X));
                }
        }

        // POIs
        for (FGeoPOI& P : POIs)
        {
                P.Location = LatLonToLocal(P.RawLonLat.Y, P.RawLonLat.X);
        }

        // Trees
        for (FGeoTree& T : Trees)
        {
                T.Location = LatLonToLocal(T.RawLonLat.Y, T.RawLonLat.X);
        }

        // Model instances (street furniture, power, landmarks)
        for (FGeoModelInstance& M : ModelInstances)
        {
                M.Location = LatLonToLocal(M.RawLonLat.Y, M.RawLonLat.X);
        }

        // Railways
        for (FGeoRailway& R : Railways)
        {
                if (R.RawLonLat.Num() > 0)
                {
                        R.Points.Empty();
                        for (const FDoublePoint2D& RL : R.RawLonLat)
                                R.Points.Add(LatLonToLocal(RL.Y, RL.X));
                }
        }

        // Elevation samples (OSM ele nodes)
        for (FElevationSample& E : ElevationSamples)
        {
                E.Location = LatLonToLocal(E.RawLonLat.Y, E.RawLonLat.X);
        }
}

// ============================================================================
// AssembleBuildingParts - Group building:part entries with their parent buildings
// ============================================================================
void FOSMLoader::AssembleBuildingParts()
{
        // Ray-casting point-in-polygon. Works in any consistent 2D coord system,
        // so it is safe to call here (before WGS84->local conversion) because the
        // outline polygon and the part centroid are in the SAME system.
        auto PointInPoly = [](const FVector2D& P, const TArray<FVector2D>& Poly) -> bool
        {
                bool bInside = false;
                const int32 N = Poly.Num();
                for (int32 i = 0, j = N - 1; i < N; j = i++)
                {
                        const FVector2D& A = Poly[i];
                        const FVector2D& B = Poly[j];
                        if (((A.Y > P.Y) != (B.Y > P.Y)) &&
                                (P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X))
                                bInside = !bInside;
                }
                return bInside;
        };

        int32 PartCount = 0, SuppressCount = 0;
        for (const FGeoBuilding& B : Buildings)
                if (B.bIsBuildingPart) PartCount++;

        // Simple 3D Buildings rule (what streets.gl does): when a building outline
        // is covered by building:part polygons, the OUTLINE itself must NOT be
        // rendered in 3D — only the parts define the geometry. Otherwise the
        // outline's walls+roof (often a flat roof at a different height) sit on top
        // of the detailed parts and ruin the result (e.g. the flat slab that was
        // covering the Frauenkirche nave + towers).
        //
        // Detect such outlines by testing whether any part's centroid lies inside
        // them. (Parts are usually smaller than, and contained by, their outline.)
        if (PartCount > 0)
        {
                for (FGeoBuilding& Outline : Buildings)
                {
                        if (Outline.bIsBuildingPart) continue;
                        if (Outline.Nodes.Num() < 3) continue;

                        for (const FGeoBuilding& Part : Buildings)
                        {
                                if (!Part.bIsBuildingPart) continue;
                                if (&Part == &Outline) continue;
                                if (PointInPoly(Part.Center, Outline.Nodes))
                                {
                                        Outline.bSuppressOutline = true;
                                        SuppressCount++;
                                        break;
                                }
                        }
                }
        }

        // Suppress flat roofs of building:parts that have another part sitting directly
        // on top (its min_height ≈ this part's height AND its centroid is inside this
        // part's footprint). This eliminates the "phantom plane" visible between towers
        // and the nave of a church where the nave's flat cap bleeds through.
        int32 RoofSuppressCount = 0;
        for (FGeoBuilding& Base : Buildings)
        {
                if (!Base.bIsBuildingPart) continue;
                if (Base.bSuppressOutline) continue;
                if (Base.Height <= 0.0) continue;
                // Only FLAT caps create the "phantom plane" this pass removes. A part
                // carrying a real roof shape (onion, dome, gabled, ...) is a genuine
                // feature — e.g. an onion dome sitting under a finial/cross part — and
                // must never be suppressed, or it vanishes from the scene.
                if (Base.RoofShape != ERoofShape::Flat) continue;

                for (const FGeoBuilding& Upper : Buildings)
                {
                        if (!Upper.bIsBuildingPart) continue;
                        if (&Upper == &Base) continue;
                        // Upper part starts approximately where Base ends (within 0.5 m).
                        if (FMath::Abs((float)(Upper.MinHeight - Base.Height)) > 0.5f) continue;
                        if (PointInPoly(Upper.Center, Base.Nodes))
                        {
                                Base.bSuppressRoof = true;
                                RoofSuppressCount++;
                                break;
                        }
                }
        }

        UE_LOG(LogTemp, Log,
                TEXT("OSMLoader: %d building:parts; %d outline(s) suppressed; %d part roof(s) suppressed (covered by upper parts)"),
                PartCount, SuppressCount, RoofSuppressCount);
}

// ============================================================================
// Helper: ExtractTags
// ============================================================================
TMap<FString, FString> FOSMLoader::ExtractTags(const FXmlNode* Element)
{
        TMap<FString, FString> Tags;
        if (!Element) return Tags;

        for (const FXmlNode* Child : Element->GetChildrenNodes())
        {
                if (Child->GetTag() == TEXT("tag"))
                {
                        FString K = Child->GetAttribute(TEXT("k"));
                        FString V = Child->GetAttribute(TEXT("v"));
                        if (!K.IsEmpty())
                                Tags.Add(K, V);
                }
        }
        return Tags;
}

// ============================================================================
// Helper: GetTag
// ============================================================================
FString FOSMLoader::GetTag(const TMap<FString, FString>& Tags, const FString& Key, const FString& Default)
{
        const FString* Val = Tags.Find(Key);
        return Val ? *Val : Default;
}

// ============================================================================
// Helper: GetTagFloat
// ============================================================================
float FOSMLoader::GetTagFloat(const TMap<FString, FString>& Tags, const FString& Key, float Default)
{
        const FString* Val = Tags.Find(Key);
        return Val ? FCString::Atof(**Val) : Default;
}

// ============================================================================
// Helper: GetTagInt
// ============================================================================
int32 FOSMLoader::GetTagInt(const TMap<FString, FString>& Tags, const FString& Key, int32 Default)
{
        const FString* Val = Tags.Find(Key);
        return Val ? FCString::Atoi(**Val) : Default;
}

// ============================================================================
// Helper: ResolveNodeRefs
// ============================================================================
bool FOSMLoader::ResolveNodeRefs(const TArray<int64>& NodeRefs, TArray<FVector2D>& OutPoints, TArray<FDoublePoint2D>& OutRawPoints) const
{
        OutPoints.Empty();
        OutRawPoints.Empty();

        for (int64 Ref : NodeRefs)
        {
                const FDoublePoint2D* Raw = NodeRawCoords.Find(Ref);
                if (!Raw)
                {
                        // Node not found - skip or use placeholder
                        continue;
                }
                OutRawPoints.Add(*Raw);
                // Temporary float coords (will be overwritten by ConvertAllCoordsToLocal)
                OutPoints.Add(FVector2D(static_cast<float>(Raw->X), static_cast<float>(Raw->Y)));
        }

        return OutPoints.Num() >= 2;
}