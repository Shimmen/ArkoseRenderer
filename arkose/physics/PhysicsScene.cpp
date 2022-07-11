#include "PhysicsScene.h"

#include "physics/backend/base/PhysicsBackend.h"

#include "core/Assert.h"
#include "core/Logging.h"

PhysicsScene::PhysicsScene(Scene& scene, PhysicsBackend& backend)
    : m_scene(scene)
    , m_backend(backend)
{
}

PhysicsScene::~PhysicsScene()
{
}
