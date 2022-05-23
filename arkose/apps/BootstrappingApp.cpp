#include "BootstrappingApp.h"

void BootstrappingApp::setup(Scene& scene, RenderPipeline& pipeline)
{
    scene.setupFromDescription({ .path = "assets/sample/sponza.json",
                                 .maintainRayTracingScene = false });
}

bool BootstrappingApp::update(Scene&, float elapsedTime, float deltaTime)
{
    return true;
}
