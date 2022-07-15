#pragma once

// Specify backends to use

#include "rendering/backend/base/Backend.h"
#include "physics/backend/base/PhysicsBackend.h"

static constexpr auto SelectedBackendType = Backend::Type::Vulkan;
static constexpr auto SelectedPhysicsBackendType = PhysicsBackend::Type::None;

// Specify app to run: 'SelectedApp'

#include "apps/ShowcaseApp.h"
#include "apps/BootstrappingApp.h"

using SelectedApp = ShowcaseApp;
