#include "AppBase.h"

void AppBase::setup(Backend& graphicsBackend, PhysicsBackend* physicsBackend)
{
    m_scene = std::make_unique<Scene>(graphicsBackend, physicsBackend);
    m_renderPipeline = std::make_unique<RenderPipeline>(&m_scene->gpuScene());
}

bool AppBase::update(float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    scene().camera().setTargetWindowSize(m_renderPipeline->outputResolution());

    scene().update(elapsedTime, deltaTime);

    return true;
}

void AppBase::render(Backend& backend, float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    m_scene->preRender();

    bool frameExecuted = false;
    while (!frameExecuted) {
        frameExecuted = backend.executeFrame(*m_renderPipeline, elapsedTime, deltaTime);

        if (!frameExecuted) {
            ARKOSE_LOG(Error, "Failed to execute render pipeline for frame, retrying");
        }
    }

    m_scene->postRender();
}
