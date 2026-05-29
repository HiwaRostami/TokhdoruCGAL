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
        NodeCoords.Empty(); NodeRawCoords.Empty(); NodeTags.Empty(); WayMap.Empty();
        MinLat = DBL_MAX; MaxLat = -DBL_MAX; MinLon = DBL_MAX; MaxLon = -DBL_MAX;

        // FXmlFile construction: try without explicit EXmlFileStyle for
        // compatibility across UE5 versions. The constructor's second parameter
        // defaults to ContentOnly. If that fails, our OSMLoader.h provides
        // a fallback EXmlFileStyle definition.
        FXmlFile XmlDocument(OSMContent, EConstructMethod::ConstructOnly);
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

                // Determine what kind of feature this way represents
                bool bHasBuildingTag = Tags.Contains(TEXT("building"));
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
                                || Landuse == TEXT("shrubs"))
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

                // Check if this is a building relation
                bool bIsBuildingRelation = Tags.Contains(TEXT("building")) ||
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

        // Name
        if (Tags.Contains(TEXT("name"))) Building.Name = Tags[TEXT("name")];

        // Height
        if (Tags.Contains(TEXT("height")))
                Building.Height = FCString::Atod(*Tags[TEXT("height")]);

        // Levels
        if (Tags.Contains(TEXT("building:levels")))
                Building.Levels = FCString::Atoi(*Tags[TEXT("building:levels")]);

        // Default height from levels
        if (Building.Height <= 0.0 && Building.Levels > 0)
                Building.Height = Building.Levels * 3.5;

        // Min height
        if (Tags.Contains(TEXT("min_height")))
                Building.MinHeight = FCString::Atod(*Tags[TEXT("min_height")]);

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

        // Default roof height for non-flat roofs
        if (Building.RoofHeight <= 0.0 && Building.RoofShape != ERoofShape::Flat)
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
                else if (HW == TEXT("pedestrian"))      Road.Width = 10.0f;
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
}

// ============================================================================
// AssembleBuildingParts - Group building:part entries with their parent buildings
// ============================================================================
void FOSMLoader::AssembleBuildingParts()
{
        // Simple approach: for now, building:parts are already added as individual
        // FGeoBuilding entries with bIsBuildingPart=true. The mesh generator
        // handles them as separate meshes stacked on the same footprint area.
        // Future improvement: merge overlapping parts and resolve Z-ordering.
        //
        // This is where you would implement proper building:part assembly:
        // 1. Group parts by their parent relation
        // 2. For each group, determine the outline
        // 3. Stack parts vertically based on min_height/max_height
        // 4. Generate merged building with multiple vertical sections

        int32 PartCount = 0;
        for (const FGeoBuilding& B : Buildings)
                if (B.bIsBuildingPart) PartCount++;

        if (PartCount > 0)
                UE_LOG(LogTemp, Log, TEXT("OSMLoader: %d building:parts loaded (rendered as separate meshes)"), PartCount);
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
