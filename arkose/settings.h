#pragma once

// Specify backend to use

#include "backend/base/Backend.h"

static constexpr auto SelectedBackendType = Backend::Type::Vulkan;

// Specify app to run: 'SelectedApp'

#include "apps/ShowcaseApp.h"
#include "apps/BootstrappingApp.h"

using SelectedApp = ShowcaseApp;
