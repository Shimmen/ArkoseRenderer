#pragma once

// Specify backend to use

#include "backend/base/Backend.h"

static constexpr auto SelectedBackendType = Backend::Type::D3D12;

// Specify app to run: 'SelectedApp'

#include "apps/ShowcaseApp.h"
#include "apps/BootstrappingApp.h"

using SelectedApp = BootstrappingApp;
