#include "GeodataApp.h"

#include "system/Input.h"
#include "rendering/forward/ForwardRenderNode.h"
#include "rendering/forward/PrepassNode.h"
#include "rendering/lighting/LightingComposeNode.h"
#include "rendering/meshlet/MeshletVisibilityBufferRenderNode.h"
#include "rendering/meshlet/VisibilityBufferDebugNode.h"
#include "rendering/nodes/BloomNode.h"
#include "rendering/nodes/DebugDrawNode.h"
#include "rendering/nodes/DirectionalLightShadowNode.h"
#include "rendering/nodes/FinalNode.h"
#include "rendering/nodes/LocalLightShadowNode.h"
#include "rendering/nodes/PickingNode.h"
#include "rendering/nodes/RTReflectionsNode.h"
#include "rendering/nodes/SSAONode.h"
#include "rendering/nodes/SkyViewNode.h"
#include "rendering/nodes/TAANode.h"
#include "rendering/nodes/TonemapNode.h"
#include "rendering/nodes/VisibilityBufferShadingNode.h"
#include "rendering/postprocess/CASNode.h"
#include "scene/Scene.h"
#include "scene/camera/Camera.h"
#include "scene/lights/DirectionalLight.h"
#include "utility/Profiling.h"
#include <ark/random.h>
#include <cmath>
#include <imgui.h>

// Geodata related
#include <nlohmann/json.hpp>

#pragma warning(push)
#pragma warning(disable: 4127)
#include <Eigen/Dense>
#pragma warning(pop)

#pragma warning(push)
#pragma warning(disable: 4244)
#include <igl/triangle/triangulate.h>
#pragma warning(pop)

std::vector<Backend::Capability> GeodataApp::requiredCapabilities()
{
    return { Backend::Capability::MeshShading };
}

void GeodataApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    SCOPED_PROFILE_ZONE();

    // Bootstrap: load geodata here - later, make it a proper asset type that is generated beforehand
    loadHeightmap();
    createMapRegions();
    createCities();

    DirectionalLight& sun = scene.addLight(std::make_unique<DirectionalLight>());
    sun.shadowMapWorldExtent = 360.0f + 10.0f; // map is 360 units wide, i.e. longitude degrees [-180, +180], then add some margins
    sun.customConstantBias = 3.5f;
    sun.customSlopeBias = 0.5f;
    sun.setIlluminance(90'000.0f);

    scene.setupFromDescription({ .path = "",
                                 .withRayTracing = false,
                                 .withMeshShading = true });
    scene.setAmbientIlluminance(1'000.0f);

    MeshAsset* boxMesh = MeshAsset::load("assets/sample/models/Box/Box.arkmsh");
    for ( auto const& [name, mapRegionPtr] : m_mapRegions) {
        MapRegion const& mapRegion = *mapRegionPtr;

        StaticMeshInstance& instance = scene.addMesh(mapRegion.mesh.get());
        instance.transform().setTranslation(mapRegion.geometricCenter);

        for (MapCity const& mapCity : mapRegion.cities) {
            StaticMeshInstance& cityInstance = scene.addMesh(boxMesh);
            cityInstance.transform().setPositionInWorld(mapCity.location);
            cityInstance.transform().setScale(std::max(0.06f, mapCity.population / 10e6f));
        }
    }

    m_mapCameraController.setMaxSpeed(140.0f);
    m_mapCameraController.setMapDistance(130.0f);
    m_mapCameraController.takeControlOfCamera(scene.camera());
    m_cameraController = &m_mapCameraController;
    m_debugCameraController.setMaxSpeed(140.0f);

    //

    pipeline.addNode<PickingNode>();

    pipeline.addNode<MeshletVisibilityBufferRenderNode>();
    //pipeline.addNode<PrepassNode>();

    pipeline.addNode<DirectionalLightShadowNode>();
    pipeline.addNode<LocalLightShadowNode>();

    pipeline.addNode<VisibilityBufferShadingNode>();
    //pipeline.addNode<ForwardRenderNode>(ForwardRenderNode::Mode::Opaque,
    //                                    ForwardMeshFilter::AllMeshes,
    //                                    ForwardClearMode::ClearBeforeFirstDraw);

    //auto& rtReflectionsNode = pipeline.addNode<RTReflectionsNode>();
    //rtReflectionsNode.setNoTracingRoughnessThreshold(1.0f);

    //pipeline.addNode<SSSSNode>();
    pipeline.addNode<SSAONode>();
    pipeline.addNode<LightingComposeNode>();

    pipeline.addNode<SkyViewNode>();
    scene.setEnvironmentMap({ .assetPath = "", .brightnessFactor = 500.0f });

    pipeline.addNode<BloomNode>();

    std::string sceneTexture = "SceneColor";
    const std::string finalTextureToScreen = "SceneColorLDR";

    //pipeline.addNode<VisibilityBufferDebugNode>(); sceneTexture = "VisibilityBufferDebugVis";

    pipeline.addNode<TonemapNode>(sceneTexture);
    pipeline.addNode<TAANode>(scene.camera());

    pipeline.addNode<DebugDrawNode>();

    pipeline.addNode<CASNode>(finalTextureToScreen);

    FinalNode& finalNode = pipeline.addNode<FinalNode>(finalTextureToScreen);
    finalNode.setRenderFilmGrain(false);

    m_renderPipeline = &pipeline;
}

bool GeodataApp::update(Scene& scene, float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    Input const& input = Input::instance();

    if (input.wasKeyReleased(Key::F1)) {
        m_guiEnabled = !m_guiEnabled;
    }

    if (m_guiEnabled) {
        if (ImGui::Begin("Render Pipeline")) {
            m_renderPipeline->drawGui();
        }
        ImGui::End();
    }

    if (input.wasKeyReleased(Key::F2)) {
        ARKOSE_ASSERT(m_cameraController != nullptr);
        if (m_cameraController == &m_mapCameraController) {
            ARKOSE_ASSERT(m_mapCameraController.isCurrentlyControllingCamera());
            m_debugCameraController.takeControlOfCamera(scene.camera());
            m_debugCameraController.setMaxSpeed(m_mapCameraController.maxSpeed());
            m_cameraController = &m_debugCameraController;
        } else {
            ARKOSE_ASSERT(m_debugCameraController.isCurrentlyControllingCamera());
            m_mapCameraController.takeControlOfCamera(scene.camera());
            m_mapCameraController.setMaxSpeed(m_debugCameraController.maxSpeed());
            m_cameraController = &m_mapCameraController;
        }
    }
    m_cameraController->update(input, deltaTime);

    controlSunOrientation(scene, input, deltaTime);

    return true;
}

void GeodataApp::loadHeightmap()
{
    m_worldHeightMap = ImageAsset::loadOrCreate("assets/geodata/world_elevation_map.png");
    ARKOSE_ASSERT(m_worldHeightMap != nullptr);
}

float GeodataApp::sampleHeightmap(vec2 latlong) const
{
    ARKOSE_ASSERT(latlong.x >= -90.0f && latlong.x <= +90.0f);
    ARKOSE_ASSERT(latlong.y >= -180.0f && latlong.y <= +180.0f);

    vec2 normalizedLatlong = ark::clamp((latlong + vec2(90.0f, 180.0f)) / vec2(180.0f, 360.0f), vec2(0.0f), vec2(1.0f));
    vec2 textureSpaceLongLat = vec2(normalizedLatlong.y, normalizedLatlong.x) * (m_worldHeightMap->extentAtMip(0).asExtent2D().asFloatVector() - vec2(0.01f, 0.01f));
    ivec2 pixelCoord = ivec2(static_cast<i32>(textureSpaceLongLat.x), static_cast<i32>(m_worldHeightMap->extentAtMip(0).height() - 1 - textureSpaceLongLat.y));

    // TODO: Do bilinear filtering!
    ImageAsset::rgba8 heightmapValue = m_worldHeightMap->getPixelAsRGBA8(pixelCoord.x, pixelCoord.y, 0, 0);

    return static_cast<float>(heightmapValue.x - 127) / 255.0f;
}

void GeodataApp::createMapRegions()
{
    using json = nlohmann::json;

    std::ifstream fileStream("assets/geodata/world-administrative-boundaries.geojson", std::ios::binary);
    json geoFeatureCollection = json::parse(fileStream);

    // GeoJSON Format: https://datatracker.ietf.org/doc/html/rfc7946

    // "FeatureCollection"
    ARKOSE_ASSERT(geoFeatureCollection.is_object());
    ARKOSE_ASSERT(geoFeatureCollection.contains("type"));
    ARKOSE_ASSERT(geoFeatureCollection["type"] == "FeatureCollection");
    auto const& geoFeatures = geoFeatureCollection["features"];

    ark::Random rng;
    //int geoFeatureCount = 0;

    ARKOSE_ASSERT(geoFeatures.is_array());
    for (auto const& geoFeature : geoFeatures) {

        //int geoFeatureIdx = geoFeatureCount++;

        // "Feature"
        ARKOSE_ASSERT(geoFeature.is_object());
        ARKOSE_ASSERT(geoFeature["type"] == "Feature");
        ARKOSE_ASSERT(geoFeature.contains("properties"));
        ARKOSE_ASSERT(geoFeature.contains("geometry"));

        // "Properties"
        auto const& geoFeatureProperties = geoFeature["properties"];
        ARKOSE_ASSERT(geoFeatureProperties.is_object());

        std::string geoName = geoFeatureProperties["name"];
        
        std::string geoISOCountryCode = "??";
        if (geoFeatureProperties.contains("iso_3166_1_alpha_2_codes") && geoFeatureProperties["iso_3166_1_alpha_2_codes"].is_string()) {
            geoISOCountryCode = geoFeatureProperties["iso_3166_1_alpha_2_codes"];
        }

        ARKOSE_LOG(Info, " {} ({})", geoName, geoISOCountryCode);

        // "Geometry"
        auto const& geoFeatureGeometry = geoFeature["geometry"];
        ARKOSE_ASSERT(geoFeatureGeometry.is_object());

        auto meshAsset = std::make_unique<MeshAsset>();
        MeshLODAsset& lod0 = meshAsset->LODs.emplace_back();

        auto& materialAsset = m_mapRegionMaterials.emplace_back(new MaterialAsset());
        materialAsset->colorTint.x = rng.randomFloatInRange(0.1f, 1.0f);
        materialAsset->colorTint.y = rng.randomFloatInRange(0.1f, 1.0f);
        materialAsset->colorTint.z = rng.randomFloatInRange(0.1f, 1.0f);

        auto processPolygonCoordinates = [&](auto const& geoPolygonCoordinates) {
            auto const& geoActualCoordinates = geoPolygonCoordinates[0]; // why?

            // skip last vertex as it's the same as the first one, as it's a closed polygon loop
            size_t polygonVertexCount = geoActualCoordinates.size() - 1;
            size_t polygonEdgeCount = polygonVertexCount;

            ARKOSE_LOG(Info, "   polygon with {} coordinates", polygonVertexCount);

            Eigen::MatrixXd V;
            Eigen::MatrixXi E;
            Eigen::MatrixXd H;

            V.resize(polygonVertexCount, 2);
            E.resize(polygonEdgeCount, 2);
            H.resize(0, 2); // empty! are there any holes though?

            for (size_t vertexIdx = 0; vertexIdx < polygonVertexCount; ++vertexIdx) {
                V(vertexIdx, 0) = geoActualCoordinates[vertexIdx][0];
                V(vertexIdx, 1) = geoActualCoordinates[vertexIdx][1];
            }

            for (size_t edgeIdx = 0; edgeIdx < polygonEdgeCount; ++edgeIdx) {
                E(edgeIdx, 0) = narrow_cast<int>(edgeIdx);
                E(edgeIdx, 1) = narrow_cast<int>((edgeIdx + 1) % polygonVertexCount);
            }

            // see https://www.cs.cmu.edu/~quake/triangle.switch.html
            std::string const argumentsHighPoly = "a0.004qQ";
            std::string const argumentsLowPoly = "qQ"; // (borders will be accurate but interiors will be as low-poly as possible)

            Eigen::MatrixXd V2;
            Eigen::MatrixXi F2;
            igl::triangle::triangulate(V, E, H, argumentsHighPoly, V2, F2);
            ARKOSE_LOG(Info, "    after triangulation, {} faces with {} vertices", F2.rows(), V2.rows());

            MeshSegmentAsset& segment = lod0.meshSegments.emplace_back();
            segment.material = materialAsset;

            size_t vertexCount = V2.rows();
            for (size_t vertexIdx = 0; vertexIdx < vertexCount; ++vertexIdx) {

                vec2 latlong = vec2(static_cast<float>(V2(vertexIdx, 1 /* latitude */)),
                                    static_cast<float>(V2(vertexIdx, 0 /* longitude */)));
                float height = m_heightScale * sampleHeightmap(latlong);

                segment.positions.emplace_back(latlong.y, latlong.x, -height);
                segment.texcoord0s.emplace_back(0.0f, 0.0f); // no tex-coords, for now
                segment.normals.emplace_back(0.0f, 0.0f, 1.0f);
            }

            size_t triangleCount = F2.rows();
            for (size_t triangleIdx = 0; triangleIdx < triangleCount; ++triangleIdx) {
                segment.indices.emplace_back(F2(triangleIdx, 0));
                segment.indices.emplace_back(F2(triangleIdx, 1));
                segment.indices.emplace_back(F2(triangleIdx, 2));
            }

            segment.generateMeshlets();
        };

        std::string geoGeometryType = geoFeatureGeometry["type"];
        auto const& geoGeometryCoordinates = geoFeatureGeometry["coordinates"];

        if (geoGeometryType == "Polygon") {
            processPolygonCoordinates(geoGeometryCoordinates);
        } else if (geoGeometryType == "MultiPolygon") {
            ARKOSE_LOG(Info, "  multi-polygon containing {} polygons", geoGeometryCoordinates.size());
            for (auto const& geoGeometryPolygonCoordinates : geoGeometryCoordinates) {
                processPolygonCoordinates(geoGeometryPolygonCoordinates);
            }
        } else {
            ARKOSE_LOG(Error, "  unable to handle geometry type '{}', for now, ignoring", geoGeometryType);
        }

        for (MeshLODAsset const& lod : meshAsset->LODs) {
            for (MeshSegmentAsset const& segment : lod.meshSegments) {
                for (vec3 const& position : segment.positions) {
                    meshAsset->boundingBox.expandWithPoint(position);
                }
            }
        }
        vec3 aabbCenter = (meshAsset->boundingBox.min + meshAsset->boundingBox.max) / 2.0f;
        float aabbRadiusIsh = length(meshAsset->boundingBox.extents() / 2.0f);
        meshAsset->boundingSphere = geometry::Sphere(aabbCenter, aabbRadiusIsh);

#if 1
        // Recenter all map regions to their own local space, place them in the world with the world matrix instead
        vec3 regionCenter = meshAsset->boundingSphere.center();
        for (MeshLODAsset& lod : meshAsset->LODs) {
            for (MeshSegmentAsset& segment : lod.meshSegments) {
                for (vec3& position : segment.positions) {
                    position -= regionCenter;
                }
                segment.generateMeshlets(); // regenerate now that positions have changed
            }
        }
#else
        vec3 regionCenter = vec3(0.0f);
#endif

        auto mapRegion = std::make_unique<MapRegion>();
        mapRegion->name = geoName;
        mapRegion->ISO_3166_1_alpha_2 = geoISOCountryCode;
        mapRegion->mesh = std::move(meshAsset);
        mapRegion->geometricCenter = regionCenter;

        m_mapRegions[geoISOCountryCode] = std::move(mapRegion);
    }
}

void GeodataApp::createCities()
{
    using json = nlohmann::json;

    std::ifstream fileStream("assets/geodata/geonames-all-cities-with-a-population-1000.geojson", std::ios::binary);
    json geoFeatureCollection = json::parse(fileStream);

    int cityCount = 0;

    // GeoJSON Format: https://datatracker.ietf.org/doc/html/rfc7946

    // "FeatureCollection"
    ARKOSE_ASSERT(geoFeatureCollection.is_object());
    ARKOSE_ASSERT(geoFeatureCollection.contains("type"));
    ARKOSE_ASSERT(geoFeatureCollection["type"] == "FeatureCollection");
    auto const& geoFeatures = geoFeatureCollection["features"];

    ARKOSE_ASSERT(geoFeatures.is_array());
    for (auto const& geoFeature : geoFeatures) {

        // "Feature"
        ARKOSE_ASSERT(geoFeature.is_object());
        ARKOSE_ASSERT(geoFeature["type"] == "Feature");
        ARKOSE_ASSERT(geoFeature.contains("properties"));
        ARKOSE_ASSERT(geoFeature.contains("geometry"));

        // "Properties"
        auto const& geoFeatureProperties = geoFeature["properties"];
        ARKOSE_ASSERT(geoFeatureProperties.is_object());

        std::string cityName = geoFeatureProperties["name"];
        int cityPopulation = geoFeatureProperties["population"];

        // Skip very small cities
        if (cityPopulation < 20'000) {
            continue;
        }

        std::string countryCodeMaybeISO = geoFeatureProperties["country_code"];

        std::string countryName = "<unknown>";
        if (geoFeatureProperties.contains("cou_name_en") && geoFeatureProperties["cou_name_en"].is_string()) {
            countryName = geoFeatureProperties["cou_name_en"];
        }

        ARKOSE_LOG(Info, " city {} (pop {}) in {}", cityName, cityPopulation, countryName);

        // "Geometry"
        auto const& geoFeatureGeometry = geoFeature["geometry"];
        ARKOSE_ASSERT(geoFeatureGeometry.is_object());

        std::string geoGeometryType = geoFeatureGeometry["type"];
        ARKOSE_ASSERT(geoGeometryType == "Point");

        auto const& geoGeometryCoordinates = geoFeatureGeometry["coordinates"];
        vec2 latlong = vec2(geoGeometryCoordinates[1], geoGeometryCoordinates[0]);
        ARKOSE_LOG(Info, "  latlong: {},{}", latlong.x, latlong.y);

        float elevation = m_heightScale * sampleHeightmap(latlong);

        // Put the city into the correct map region
        auto entry = m_mapRegions.find(countryCodeMaybeISO);
        if (entry != m_mapRegions.end()) {
            ARKOSE_LOG(Info, "   putting city in matching country!");

            MapRegion& mapRegion = *entry->second;
            MapCity& mapCity = mapRegion.cities.emplace_back();

            mapCity.name = cityName;
            mapCity.population = cityPopulation;
            mapCity.location = vec3(latlong.y, latlong.x, -elevation);

            cityCount += 1;
        }
    }

    ARKOSE_LOG(Info, "Added a total of {} cities", cityCount);
}

void GeodataApp::controlSunOrientation(Scene& scene, Input const& input, float deltaTime)
{
    const float hoursPerSecond = 1.0f;

    float adjustedInput = hoursPerSecond * deltaTime;
    m_timeOfDay += input.isKeyDown(Key::Comma) ? adjustedInput : 0.0f;
    m_timeOfDay -= input.isKeyDown(Key::Period) ? adjustedInput : 0.0f;
    m_timeOfDay = std::fmod(m_timeOfDay, 24.00f);

    if (DirectionalLight* sun = scene.firstDirectionalLight()) {
        float sunRotationAngle = (m_timeOfDay - 12.00f) / 24.00f * ark::TWO_PI;
        sun->transform().setOrientation(ark::axisAngle(ark::globalUp, sunRotationAngle));
    }
}