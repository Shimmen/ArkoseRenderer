#pragma once

#include "application/apps/AppBase.h"
#include "scene/camera/FpsCameraController.h"
#include "scene/camera/MapCameraController.h"

struct MapCity {
    std::string name {};
    u32 population { 0 };
    vec3 location { 0.0f, 0.0f, 0.0f };
};

struct MapRegion {
    std::string name;
    std::string ISO_3166_1_alpha_2; // https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2
    vec3 geometricCenter;
    std::unique_ptr<MeshAsset> mesh;
    std::vector<MapCity> cities;
};

class GeodataApp : public AppBase {
public:
    std::vector<Backend::Capability> requiredCapabilities() override;

    void setup(Backend& graphicsBackend, PhysicsBackend* physicsBackend) override;
    bool update(float elapsedTime, float deltaTime) override;
    void render(Backend&, float elapsedTime, float deltaTime) override;

    void loadHeightmap();
    float sampleHeightmap(vec2 latlong) const;

    void createMapRegions();
    void createCities();

    bool m_guiEnabled { true };

    CameraController* m_cameraController { nullptr };
    MapCameraController m_mapCameraController {};
    FpsCameraController m_debugCameraController {};

    float m_timeOfDay = 16.00f; // as a 24-hour clock in hours then decial hours (not actual 0-60 minutes..)
    void controlSunOrientation(Scene&, Input const&, float deltaTime);

    ImageAsset* m_worldHeightMap { nullptr };
    static constexpr float m_heightScale { 8.0f };
    std::unordered_map<std::string, std::unique_ptr<MapRegion>> m_mapRegions;
};
