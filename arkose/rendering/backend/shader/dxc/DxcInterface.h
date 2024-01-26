#pragma once

#include "core/Types.h"
#include "rendering/backend/shader/CompilationResult.h"
#include "rendering/backend/shader/ShaderFile.h"
#include <memory>
#include <string_view>

namespace DxcInterface {

    std::unique_ptr<CompilationResult<u8>> compileShader(ShaderFile const& shaderFile, std::string_view resolvedFilePath);

}
