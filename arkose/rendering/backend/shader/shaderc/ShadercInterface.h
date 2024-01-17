#pragma once

#include "core/Types.h"
#include "rendering/backend/shader/ShaderFile.h"
#include "rendering/backend/shader/CompilationResult.h"
#include <memory>
#include <string_view>

namespace ShadercInterface {

    std::unique_ptr<CompilationResult<u32>> compileShader(ShaderFile const& shaderFile, std::string_view resolvedFilePath);

}
