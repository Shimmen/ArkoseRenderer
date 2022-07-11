#include "PhysicsScene.h"

#include "physics/backend/base/PhysicsBackend.h"

#include "core/Assert.h"
#include "core/Logging.h"

PhysicsScene::PhysicsScene(PhysicsBackend& backend)
    : m_backend(backend)
{
}

PhysicsScene::~PhysicsScene()
{
}
