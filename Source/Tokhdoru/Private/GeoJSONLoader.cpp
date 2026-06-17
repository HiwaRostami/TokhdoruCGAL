#include "GeoJSONLoader.h"
#include "JsonUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FGeoJSONLoader::FGeoJSONLoader()
	: CenterLatitude(0.0), CenterLongitude(0.0), bIsLoaded(false)
	, MinLat(DBL_MAX), MaxLat(-DBL_MAX), MinLon(DBL_MAX), MaxLon(-DBL_MAX) {}

FGeoJSONLoader::~FGeoJSONLoader() {}

bool FGeoJSONLoader::LoadGeoJSON(const FString& FilePath)
{
	FString FullPath = FPaths::IsRelative(FilePath) ? FPaths::ProjectDir() + FilePath : FilePath;
	if (!FPaths::FileExists(*FullPath)) return false;

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FullPath)) return false;

	return LoadGeoJSONFromString(FileContent);
}

bool FGeoJSONLoader::LoadGeoJSONFromString(const FString& JsonString)
{
	Buildings.Empty(); Roads.Empty(); Waters.Empty(); Vegetations.Empty(); POIs.Empty();
	Trees.Empty(); Railways.Empty(); ElevationSamples.Empty(); ModelInstances.Empty();
	MinLat = DBL_MAX; MaxLat = -DBL_MAX; MinLon = DBL_MAX; MaxLon = -DBL_MAX;

	// Parse JSON on game thread (UE JSON system is not thread-safe)
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid()) return false;

	FString Type = RootObject->GetStringField(TEXT("type"));
	if (Type != TEXT("FeatureCollection") && Type != TEXT("Feature")) return false;

	// First pass: collect all coordinates as double to preserve precision
	ParseGeoJSON(RootObject);
	CalculateCenterPoint();
	ConvertAllCoordsToLocal();

	bIsLoaded = true;
	return bIsLoaded;
}

void FGeoJSONLoader::ParseGeoJSON(const TSharedPtr<FJsonObject>& RootObject)
{
	if (!RootObject.IsValid()) return;
	FString Type = RootObject->GetStringField(TEXT("type"));

	if (Type == TEXT("FeatureCollection"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Features = nullptr;
		if (RootObject->TryGetArrayField(TEXT("features"), Features))
		{
			for (const TSharedPtr<FJsonValue>& FeatureValue : *Features)
			{
				if (FeatureValue.IsValid() && FeatureValue->AsObject().IsValid()) ParseFeature(FeatureValue->AsObject());
			}
		}
	}
	else if (Type == TEXT("Feature")) ParseFeature(RootObject);
}

void FGeoJSONLoader::ParseFeature(const TSharedPtr<FJsonObject>& Feature)
{
	if (!Feature.IsValid()) return;
	const TSharedPtr<FJsonObject>* Geometry = nullptr;
	const TSharedPtr<FJsonObject>* Properties = nullptr;

	TSharedPtr<FJsonObject> GeometryObj = Feature->TryGetObjectField(TEXT("geometry"), Geometry) ? *Geometry : nullptr;
	TSharedPtr<FJsonObject> PropertiesObj = Feature->TryGetObjectField(TEXT("properties"), Properties) ? *Properties : nullptr;

	if (GeometryObj.IsValid()) ParseGeometry(GeometryObj, PropertiesObj);
}

void FGeoJSONLoader::ParseGeometry(const TSharedPtr<FJsonObject>& Geometry, const TSharedPtr<FJsonObject>& Properties)
{
	FString GeomType;
	const TArray<TSharedPtr<FJsonValue>>* Coordinates = nullptr;
	if (!Geometry->TryGetStringField(TEXT("type"), GeomType) || !Geometry->TryGetArrayField(TEXT("coordinates"), Coordinates)) return;

	if (GeomType == TEXT("Point")) ParsePoint(*Coordinates, Properties);
	else if (GeomType == TEXT("LineString")) ParseLineString(*Coordinates, Properties);
	else if (GeomType == TEXT("Polygon")) ParsePolygon(*Coordinates, Properties);
	else if (GeomType == TEXT("MultiPolygon")) ParseMultiPolygon(*Coordinates, Properties);
	else if (GeomType == TEXT("MultiLineString"))
	{
		for (const TSharedPtr<FJsonValue>& LineValue : *Coordinates)
		{
			if (LineValue.IsValid() && LineValue->Type == EJson::Array) ParseLineString(LineValue->AsArray(), Properties);
		}
	}
}

void FGeoJSONLoader::ParsePoint(const TArray<TSharedPtr<FJsonValue>>& Coordinates, const TSharedPtr<FJsonObject>& Properties)
{
	if (Coordinates.Num() < 2) return;
	double Lon = Coordinates[0]->AsNumber();
	double Lat = Coordinates[1]->AsNumber();

	MinLat = FMath::Min(MinLat, Lat); MaxLat = FMath::Max(MaxLat, Lat);
	MinLon = FMath::Min(MinLon, Lon); MaxLon = FMath::Max(MaxLon, Lon);

	double Ele = 0.0;
	bool bHasEle = false;
	if (Coordinates.Num() >= 3)
	{
		Ele = Coordinates[2]->AsNumber();
		bHasEle = true;
	}
	if (Properties.IsValid() && Properties->HasField(TEXT("ele")))
	{
		Ele = FCString::Atod(*Properties->GetStringField(TEXT("ele")));
		bHasEle = true;
	}
	if (bHasEle)
	{
		FElevationSample Sample;
		Sample.RawLonLat = FDoublePoint2D(Lon, Lat);
		Sample.Elevation = Ele;
		ElevationSamples.Add(Sample);
	}

	// Check if this point is a tree (natural=tree)
	if (Properties.IsValid() && Properties->HasField(TEXT("natural")))
	{
		FString Natural = Properties->GetStringField(TEXT("natural"));
		if (Natural == TEXT("tree"))
		{
			FGeoTree Tree;
			Tree.Location = FVector2D(static_cast<float>(Lon), static_cast<float>(Lat));
			Tree.RawLonLat = FDoublePoint2D(Lon, Lat);
			if (Properties->HasField(TEXT("species"))) Tree.Species = Properties->GetStringField(TEXT("species"));
			if (Properties->HasField(TEXT("genus"))) Tree.Genus = Properties->GetStringField(TEXT("genus"));
			if (Properties->HasField(TEXT("height")))
			{
				Tree.Height = FCString::Atof(*Properties->GetStringField(TEXT("height")));
				if (Tree.Height <= 0.0f) Tree.Height = 8.0f;
			}
			if (Properties->HasField(TEXT("crown_radius")))
			{
				Tree.CrownRadius = FCString::Atof(*Properties->GetStringField(TEXT("crown_radius")));
				if (Tree.CrownRadius <= 0.0f) Tree.CrownRadius = 3.0f;
			}
			Trees.Add(Tree);
			return;
		}
	}

	// Store placeholder FVector2D (will be overwritten by ConvertAllCoordsToLocal)
	// and raw double-precision coordinate for accurate conversion
	ProcessPOIFeature(FVector2D(static_cast<float>(Lon), static_cast<float>(Lat)),
		FDoublePoint2D(Lon, Lat), Properties);
}

void FGeoJSONLoader::ParseLineString(const TArray<TSharedPtr<FJsonValue>>& Coordinates, const TSharedPtr<FJsonObject>& Properties)
{
	if (Coordinates.Num() < 2) return;
	TArray<FVector2D> Points;
	TArray<FDoublePoint2D> RawPoints;

	for (const TSharedPtr<FJsonValue>& CoordValue : Coordinates)
	{
		if (CoordValue.IsValid() && CoordValue->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Coord = CoordValue->AsArray();
			if (Coord.Num() >= 2)
			{
				double Lon = Coord[0]->AsNumber();
				double Lat = Coord[1]->AsNumber();
				MinLat = FMath::Min(MinLat, Lat); MaxLat = FMath::Max(MaxLat, Lat);
				MinLon = FMath::Min(MinLon, Lon); MaxLon = FMath::Max(MaxLon, Lon);
				Points.Add(FVector2D(static_cast<float>(Lon), static_cast<float>(Lat)));
				RawPoints.Add(FDoublePoint2D(Lon, Lat));

				if (Coord.Num() >= 3)
				{
					FElevationSample Sample;
					Sample.RawLonLat = FDoublePoint2D(Lon, Lat);
					Sample.Elevation = Coord[2]->AsNumber();
					ElevationSamples.Add(Sample);
				}
			}
		}
	}

	// Check if this is a railway
	if (Properties.IsValid() && Properties->HasField(TEXT("railway")))
	{
		ProcessRailwayFeature(Points, RawPoints, Properties);
	}
	else
	{
		ProcessRoadFeature(Points, RawPoints, Properties);
	}
}

void FGeoJSONLoader::ParsePolygon(const TArray<TSharedPtr<FJsonValue>>& Coordinates, const TSharedPtr<FJsonObject>& Properties)
{
	if (Coordinates.Num() < 1 || !Coordinates[0].IsValid() || Coordinates[0]->Type != EJson::Array) return;
	const TArray<TSharedPtr<FJsonValue>>& ExteriorRing = Coordinates[0]->AsArray();
	if (ExteriorRing.Num() < 3) return;

	TArray<FVector2D> Nodes;
	TArray<FDoublePoint2D> RawNodes;
	for (const TSharedPtr<FJsonValue>& CoordValue : ExteriorRing)
	{
		if (CoordValue.IsValid() && CoordValue->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Coord = CoordValue->AsArray();
			if (Coord.Num() >= 2)
			{
				double Lon = Coord[0]->AsNumber();
				double Lat = Coord[1]->AsNumber();
				MinLat = FMath::Min(MinLat, Lat); MaxLat = FMath::Max(MaxLat, Lat);
				MinLon = FMath::Min(MinLon, Lon); MaxLon = FMath::Max(MaxLon, Lon);
				Nodes.Add(FVector2D(static_cast<float>(Lon), static_cast<float>(Lat)));
				RawNodes.Add(FDoublePoint2D(Lon, Lat));
			}
		}
	}

	if (Nodes.Num() >= 3)
	{
		// Check for vegetation first (park, forest, wood, grass, orchard)
		bool bIsVegetation = false;
		if (Properties.IsValid())
		{
			if (Properties->HasField(TEXT("landuse")))
			{
				FString Landuse = Properties->GetStringField(TEXT("landuse"));
				if (Landuse == TEXT("grass") || Landuse == TEXT("forest") || Landuse == TEXT("orchard") || Landuse == TEXT("vineyard") || Landuse == TEXT("meadow"))
				{
					FGeoVegetation Veg;
					Veg.Points = Nodes;
					Veg.RawLonLat = RawNodes;
					if (Properties->HasField(TEXT("name"))) Veg.Name = Properties->GetStringField(TEXT("name"));
					Veg.VegType = Landuse;
					if (Landuse == TEXT("forest")) Veg.Color = FLinearColor(0.1f, 0.4f, 0.1f, 1.0f);
					else if (Landuse == TEXT("grass") || Landuse == TEXT("meadow")) Veg.Color = FLinearColor(0.25f, 0.6f, 0.2f, 1.0f);
					else Veg.Color = FLinearColor(0.2f, 0.55f, 0.15f, 1.0f);
					Vegetations.Add(Veg);
					bIsVegetation = true;
				}
			}
			else if (Properties->HasField(TEXT("leisure")))
			{
				FString Leisure = Properties->GetStringField(TEXT("leisure"));
				if (Leisure == TEXT("park") || Leisure == TEXT("garden"))
				{
					FGeoVegetation Veg;
					Veg.Points = Nodes;
					Veg.RawLonLat = RawNodes;
					if (Properties->HasField(TEXT("name"))) Veg.Name = Properties->GetStringField(TEXT("name"));
					Veg.VegType = TEXT("park");
					Veg.Color = FLinearColor(0.2f, 0.55f, 0.15f, 1.0f);
					Vegetations.Add(Veg);
					bIsVegetation = true;
				}
			}
			else if (Properties->HasField(TEXT("natural")))
			{
				FString Natural = Properties->GetStringField(TEXT("natural"));
				if (Natural == TEXT("wood") || Natural == TEXT("scrub") || Natural == TEXT("heath"))
				{
					FGeoVegetation Veg;
					Veg.Points = Nodes;
					Veg.RawLonLat = RawNodes;
					if (Properties->HasField(TEXT("name"))) Veg.Name = Properties->GetStringField(TEXT("name"));
					Veg.VegType = (Natural == TEXT("wood")) ? TEXT("wood") : TEXT("forest");
					Veg.Color = FLinearColor(0.1f, 0.4f, 0.1f, 1.0f);
					Vegetations.Add(Veg);
					bIsVegetation = true;
				}
			}
		}

		if (!bIsVegetation)
		{
			// Check for water
			if (Properties.IsValid() && (Properties->HasField(TEXT("water")) || Properties->HasField(TEXT("waterway"))))
			{
				ProcessWaterFeature(Nodes, RawNodes, Properties);
			}
			else
			{
				ProcessBuildingFeature(Nodes, RawNodes, Properties);
			}
		}
	}
}

void FGeoJSONLoader::ParseMultiPolygon(const TArray<TSharedPtr<FJsonValue>>& Coordinates, const TSharedPtr<FJsonObject>& Properties)
{
	for (const TSharedPtr<FJsonValue>& PolygonValue : Coordinates)
	{
		if (PolygonValue.IsValid() && PolygonValue->Type == EJson::Array) ParsePolygon(PolygonValue->AsArray(), Properties);
	}
}

void FGeoJSONLoader::ApplyBuildingProperties(FGeoBuilding& Building, const TSharedPtr<FJsonObject>& Properties)
{
	if (!Properties.IsValid()) return;

	// Building colour
	if (Properties->HasField(TEXT("building:colour")))
	{
		FString ColourStr = Properties->GetStringField(TEXT("building:colour"));
		Building.BuildingColour = ParseHexColour(ColourStr, Building.Color);
		Building.Color = Building.BuildingColour;
		Building.bHasExplicitBuildingColour = true;
	}

	// Building material (affects facade type)
	if (Properties->HasField(TEXT("building:material")))
	{
		Building.FacadeMaterialType = FOSMBuildingMaterialParser::Parse(Properties->GetStringField(TEXT("building:material")));
	}

	// Roof shape
	if (Properties->HasField(TEXT("roof:shape")))
		Building.RoofShape = ParseRoofShape(Properties->GetStringField(TEXT("roof:shape")));
	else if (Properties->HasField(TEXT("building:roof:shape")))
		Building.RoofShape = ParseRoofShape(Properties->GetStringField(TEXT("building:roof:shape")));

	// Roof colour
	if (Properties->HasField(TEXT("roof:colour")))
	{
		FString RoofColourStr = Properties->GetStringField(TEXT("roof:colour"));
		Building.RoofColor = ParseHexColour(RoofColourStr, Building.RoofColor);
		Building.bHasExplicitRoofColor = true;
	}
	else if (Properties->HasField(TEXT("building:roof:colour")))
	{
		FString RoofColourStr = Properties->GetStringField(TEXT("building:roof:colour"));
		Building.RoofColor = ParseHexColour(RoofColourStr, Building.RoofColor);
		Building.bHasExplicitRoofColor = true;
	}

	// Roof height
	if (Properties->HasField(TEXT("roof:height")))
	{
		Building.RoofHeight = FCString::Atod(*Properties->GetStringField(TEXT("roof:height")));
	}

	// Roof levels
	if (Properties->HasField(TEXT("roof:levels")))
	{
		Building.RoofLevels = FCString::Atoi(*Properties->GetStringField(TEXT("roof:levels")));
	}

	// Roof angle (used to estimate roof height if roof:height not set)
	if (Properties->HasField(TEXT("roof:angle")))
	{
		Building.RoofAngle = FCString::Atod(*Properties->GetStringField(TEXT("roof:angle")));
	}

	// Roof direction (degrees: 0=North, 90=East, etc.)
	if (Properties->HasField(TEXT("roof:direction")))
	{
		Building.RoofDirection = FCString::Atod(*Properties->GetStringField(TEXT("roof:direction")));
	}

	// Roof orientation
	if (Properties->HasField(TEXT("roof:orientation")))
		Building.RoofOrientation = Properties->GetStringField(TEXT("roof:orientation"));

	// Roof material
	if (Properties->HasField(TEXT("roof:material")))
		Building.RoofMaterial = Properties->GetStringField(TEXT("roof:material"));

	// Min height
	if (Properties->HasField(TEXT("min_height")))
		Building.MinHeight = FCString::Atod(*Properties->GetStringField(TEXT("min_height")));

	// Estimate roof height from angle if not explicitly set
	if (Building.RoofHeight <= 0.0 && Building.RoofAngle > 0.0 && Building.Nodes.Num() > 0)
	{
		// Compute building half-width for roof height estimation
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

	// Default roof height for non-flat roofs if still not set
	if (Building.RoofHeight <= 0.0 && Building.RoofShape != ERoofShape::Flat)
	{
		Building.RoofHeight = 3.0; // Default 3m roof for non-flat shapes
	}
}

void FGeoJSONLoader::ProcessBuildingFeature(const TArray<FVector2D>& Nodes, const TArray<FDoublePoint2D>& RawCoords, const TSharedPtr<FJsonObject>& Properties)
{
	FGeoBuilding Building; Building.Nodes = Nodes; Building.RawLonLat = RawCoords;
	float CenterX = 0.f, CenterY = 0.f;
	for (const FVector2D& N : Nodes) { CenterX += N.X; CenterY += N.Y; }
	if (Nodes.Num() > 0) Building.Center = FVector2D(CenterX / Nodes.Num(), CenterY / Nodes.Num());

	if (Properties.IsValid())
	{
		if (Properties->HasField(TEXT("name"))) Building.Name = Properties->GetStringField(TEXT("name"));
		if (Properties->HasField(TEXT("building:levels"))) Building.Levels = FCString::Atoi(*Properties->GetStringField(TEXT("building:levels")));
		Building.Height = Properties->HasField(TEXT("height")) ? FCString::Atod(*Properties->GetStringField(TEXT("height"))) : Building.Levels * 3.5;
		if (Properties->HasField(TEXT("building")))
		{
			Building.BuildingTypeStr = Properties->GetStringField(TEXT("building"));
			// Map common building types to numeric codes
			FString BLower = Building.BuildingTypeStr.ToLower();
			if (BLower == TEXT("residential") || BLower == TEXT("house") || BLower == TEXT("apartments"))
				Building.BuildingType = 0;
			else if (BLower == TEXT("commercial") || BLower == TEXT("office") || BLower == TEXT("retail"))
				Building.BuildingType = 1;
			else if (BLower == TEXT("industrial") || BLower == TEXT("warehouse"))
				Building.BuildingType = 2;
			else
				Building.BuildingType = 3; // other
		}

		// Apply roof and building colour properties
		ApplyBuildingProperties(Building, Properties);
	}
	Building.Color = GetColorFromType(Building.BuildingTypeStr, TEXT("building"));

	// If explicit building colour was set, override type-based color
	if (Building.bHasExplicitBuildingColour)
		Building.Color = Building.BuildingColour;

	Buildings.Add(Building);
}

void FGeoJSONLoader::ProcessRoadFeature(const TArray<FVector2D>& Points, const TArray<FDoublePoint2D>& RawCoords, const TSharedPtr<FJsonObject>& Properties)
{
	FGeoRoad Road; Road.Points = Points; Road.RawLonLat = RawCoords;
	if (Properties.IsValid() && Properties->HasField(TEXT("highway")))
	{
		Road.RoadTypeStr = Properties->GetStringField(TEXT("highway"));
		// Map road types to numeric codes
		FString RLower = Road.RoadTypeStr.ToLower();
		if (RLower == TEXT("residential") || RLower == TEXT("living_street")) Road.RoadType = 0;
		else if (RLower == TEXT("primary") || RLower == TEXT("motorway") || RLower == TEXT("trunk")) Road.RoadType = 1;
		else if (RLower == TEXT("secondary")) Road.RoadType = 2;
		else if (RLower == TEXT("tertiary")) Road.RoadType = 3;
		else if (RLower == TEXT("service") || RLower == TEXT("unclassified")) Road.RoadType = 4;
		else Road.RoadType = 5;

		// Road widths in meters
		if (Road.RoadTypeStr == TEXT("motorway"))            Road.Width = 24.0f;
		else if (Road.RoadTypeStr == TEXT("motorway_link"))   Road.Width = 12.0f;
		else if (Road.RoadTypeStr == TEXT("trunk"))           Road.Width = 20.0f;
		else if (Road.RoadTypeStr == TEXT("trunk_link"))      Road.Width = 12.0f;
		else if (Road.RoadTypeStr == TEXT("primary"))         Road.Width = 14.0f;
		else if (Road.RoadTypeStr == TEXT("primary_link"))    Road.Width = 10.0f;
		else if (Road.RoadTypeStr == TEXT("secondary"))       Road.Width = 12.0f;
		else if (Road.RoadTypeStr == TEXT("secondary_link"))  Road.Width = 10.0f;
		else if (Road.RoadTypeStr == TEXT("tertiary"))        Road.Width = 10.0f;
		else if (Road.RoadTypeStr == TEXT("residential"))     Road.Width = 8.0f;
		else if (Road.RoadTypeStr == TEXT("service"))         Road.Width = 5.0f;
		else                                                Road.Width = 8.0f;

		Road.bIsHighway = (Road.RoadTypeStr == TEXT("motorway") || Road.RoadTypeStr == TEXT("trunk") ||
				   Road.RoadTypeStr == TEXT("primary") || Road.RoadTypeStr == TEXT("secondary") ||
				   Road.RoadTypeStr == TEXT("tertiary") ||
				   Road.RoadTypeStr == TEXT("motorway_link") || Road.RoadTypeStr == TEXT("trunk_link") ||
				   Road.RoadTypeStr == TEXT("primary_link") || Road.RoadTypeStr == TEXT("secondary_link"));
	}
	Road.Color = GetColorFromType(Road.RoadTypeStr, TEXT("road"));
	Roads.Add(Road);
}

void FGeoJSONLoader::ProcessWaterFeature(const TArray<FVector2D>& Points, const TArray<FDoublePoint2D>& RawCoords, const TSharedPtr<FJsonObject>& Properties)
{
	FGeoWater Water; Water.Points = Points; Water.RawLonLat = RawCoords;
	if (Properties.IsValid() && Properties->HasField(TEXT("name"))) Water.Name = Properties->GetStringField(TEXT("name"));
	Water.Color = FLinearColor::Blue;
	Waters.Add(Water);
}

void FGeoJSONLoader::ProcessPOIFeature(const FVector2D& Location, const FDoublePoint2D& RawCoord, const TSharedPtr<FJsonObject>& Properties)
{
	FGeoPOI POI; POI.Location = Location; POI.RawLonLat = RawCoord;
	if (Properties.IsValid() && Properties->HasField(TEXT("name"))) POI.Name = Properties->GetStringField(TEXT("name"));
	POIs.Add(POI);
}

void FGeoJSONLoader::ProcessRailwayFeature(const TArray<FVector2D>& Points, const TArray<FDoublePoint2D>& RawCoords, const TSharedPtr<FJsonObject>& Properties)
{
	FGeoRailway Railway; Railway.Points = Points; Railway.RawLonLat = RawCoords;
	if (Properties.IsValid())
	{
		if (Properties->HasField(TEXT("name"))) Railway.Name = Properties->GetStringField(TEXT("name"));
		if (Properties->HasField(TEXT("railway"))) Railway.RailwayType = Properties->GetStringField(TEXT("railway"));
		if (Properties->HasField(TEXT("gauge"))) Railway.Gauge = FCString::Atof(*Properties->GetStringField(TEXT("gauge")));
		if (Properties->HasField(TEXT("tracks"))) Railway.Tracks = FCString::Atoi(*Properties->GetStringField(TEXT("tracks")));
	}
	Railways.Add(Railway);
}

void FGeoJSONLoader::CalculateCenterPoint()
{
	CenterLatitude = (MinLat + MaxLat) / 2.0; CenterLongitude = (MinLon + MaxLon) / 2.0;
}

void FGeoJSONLoader::ConvertAllCoordsToLocal()
{
	// Use double-precision RawLonLat when available (avoids float precision loss),
	// otherwise fall back to float FVector2D (shouldn't happen for GeoJSON data).
	for (FGeoBuilding& B : Buildings)
	{
		if (B.RawLonLat.Num() > 0)
		{
			B.Nodes.Empty();
			for (const FDoublePoint2D& RL : B.RawLonLat)
				B.Nodes.Add(LatLonToLocalCoords(RL.Y, RL.X, CenterLatitude, CenterLongitude));
		}
		else
		{
			for (FVector2D& N : B.Nodes) N = LatLonToLocalCoords(N.Y, N.X, CenterLatitude, CenterLongitude);
		}
		// Recalculate Center from the now-converted local nodes
		if (B.Nodes.Num() > 0)
		{
			float CX = 0.0f, CY = 0.0f;
			for (const FVector2D& N : B.Nodes) { CX += N.X; CY += N.Y; }
			B.Center = FVector2D(CX / B.Nodes.Num(), CY / B.Nodes.Num());
		}
	}
	for (FGeoRoad& R : Roads)
	{
		if (R.RawLonLat.Num() > 0)
		{
			R.Points.Empty();
			for (const FDoublePoint2D& RL : R.RawLonLat)
				R.Points.Add(LatLonToLocalCoords(RL.Y, RL.X, CenterLatitude, CenterLongitude));
		}
		else
		{
			for (FVector2D& P : R.Points) P = LatLonToLocalCoords(P.Y, P.X, CenterLatitude, CenterLongitude);
		}
	}
	for (FGeoWater& W : Waters)
	{
		if (W.RawLonLat.Num() > 0)
		{
			W.Points.Empty();
			for (const FDoublePoint2D& RL : W.RawLonLat)
				W.Points.Add(LatLonToLocalCoords(RL.Y, RL.X, CenterLatitude, CenterLongitude));
		}
		else
		{
			for (FVector2D& P : W.Points) P = LatLonToLocalCoords(P.Y, P.X, CenterLatitude, CenterLongitude);
		}
	}
	for (FGeoVegetation& V : Vegetations)
	{
		if (V.RawLonLat.Num() > 0)
		{
			V.Points.Empty();
			for (const FDoublePoint2D& RL : V.RawLonLat)
				V.Points.Add(LatLonToLocalCoords(RL.Y, RL.X, CenterLatitude, CenterLongitude));
		}
		else
		{
			for (FVector2D& P : V.Points) P = LatLonToLocalCoords(P.Y, P.X, CenterLatitude, CenterLongitude);
		}
	}
	for (FGeoPOI& P : POIs)
	{
		P.Location = LatLonToLocalCoords(P.RawLonLat.Y, P.RawLonLat.X, CenterLatitude, CenterLongitude);
	}
	for (FGeoTree& T : Trees)
	{
		T.Location = LatLonToLocalCoords(T.RawLonLat.Y, T.RawLonLat.X, CenterLatitude, CenterLongitude);
	}
	for (FGeoRailway& R : Railways)
	{
		if (R.RawLonLat.Num() > 0)
		{
			R.Points.Empty();
			for (const FDoublePoint2D& RL : R.RawLonLat)
				R.Points.Add(LatLonToLocalCoords(RL.Y, RL.X, CenterLatitude, CenterLongitude));
		}
		else
		{
			for (FVector2D& P : R.Points) P = LatLonToLocalCoords(P.Y, P.X, CenterLatitude, CenterLongitude);
		}
	}
	for (FElevationSample& E : ElevationSamples)
	{
		E.Location = LatLonToLocalCoords(E.RawLonLat.Y, E.RawLonLat.X, CenterLatitude, CenterLongitude);
	}
}

FVector2D FGeoJSONLoader::LatLonToLocalCoords(double Lat, double Lon, double CenterLat, double CenterLon)
{
	const double EarthRadiusM = 6371000.0;
	double X = EarthRadiusM * ((Lon - CenterLon) * UE_PI / 180.0) * FMath::Cos(CenterLat * UE_PI / 180.0);
	double Y = EarthRadiusM * ((Lat - CenterLat) * UE_PI / 180.0);
	return FVector2D(static_cast<float>(X), static_cast<float>(Y));
}

FLinearColor FGeoJSONLoader::GetColorFromType(const FString& Type, const FString& Category)
{
	if (Category == TEXT("building"))
	{
		if (Type == TEXT("residential") || Type == TEXT("apartments"))
			return FLinearColor(0.85f, 0.82f, 0.78f, 1.0f);
		else if (Type == TEXT("commercial") || Type == TEXT("office"))
			return FLinearColor(0.65f, 0.68f, 0.75f, 1.0f);
		else if (Type == TEXT("industrial"))
			return FLinearColor(0.6f, 0.6f, 0.55f, 1.0f);
		else if (Type == TEXT("retail") || Type == TEXT("shop"))
			return FLinearColor(0.8f, 0.75f, 0.7f, 1.0f);
		else
			return FLinearColor(0.75f, 0.75f, 0.75f, 1.0f);
	}
	else if (Category == TEXT("road"))
	{
		if (Type == TEXT("motorway") || Type == TEXT("trunk"))
			return FLinearColor(0.3f, 0.3f, 0.35f, 1.0f);
		else if (Type == TEXT("primary") || Type == TEXT("secondary"))
			return FLinearColor(0.35f, 0.35f, 0.35f, 1.0f);
		else
			return FLinearColor(0.4f, 0.4f, 0.4f, 1.0f);
	}

	return FLinearColor::Gray;
}
