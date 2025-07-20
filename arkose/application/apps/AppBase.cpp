#include "AppBase.h"

void AppBase::setup(Backend& graphicsBackend, PhysicsBackend* physicsBackend)
{
    m_mainScene = std::make_unique<Scene>(graphicsBackend, physicsBackend);
    m_mainRenderPipeline = std::make_unique<RenderPipeline>(&m_mainScene->gpuScene());
}

bool AppBase::update(float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    mainScene().camera().setTargetWindowSize(mainRenderPipeline().outputResolution());

    mainScene().update(elapsedTime, deltaTime);

    return true;
}

void AppBase::render(Backend& backend, float elapsedTime, float deltaTime)
{
    SCOPED_PROFILE_ZONE();

    mainScene().preRender();

    bool frameExecuted = false;
    while (!frameExecuted) {
        frameExecuted = backend.executeFrame(mainRenderPipeline(), elapsedTime, deltaTime);

        if (!frameExecuted) {
            ARKOSE_LOG(Error, "Failed to execute render pipeline for frame, retrying");
        }
    }

    mainScene().postRender();
}
