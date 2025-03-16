#include "ShadercInterface.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <shaderc/shaderc.hpp>
#include <functional>

static shaderc_shader_kind glslShaderKindForShaderFile(ShaderFile const& shaderFile)
{
    switch (shaderFile.shaderStage()) {
    case ShaderStage::Vertex:
        return shaderc_vertex_shader;
    case ShaderStage::Fragment:
        return shaderc_fragment_shader;
    case ShaderStage::Compute:
        return shaderc_compute_shader;
    case ShaderStage::RTRayGen:
        return shaderc_raygen_shader;
    case ShaderStage::RTClosestHit:
        return shaderc_closesthit_shader;
    case ShaderStage::RTAnyHit:
        return shaderc_anyhit_shader;
    case ShaderStage::RTIntersection:
        return shaderc_intersection_shader;
    case ShaderStage::RTMiss:
        return shaderc_miss_shader;
    case ShaderStage::Task:
        return shaderc_task_shader;
    case ShaderStage::Mesh:
        return shaderc_mesh_shader;
    case ShaderStage::Unknown:
        ARKOSE_LOG(Warning, "Can't find glsl shader kind for shader file of unknown type ('{}')", shaderFile.path());
        return shaderc_glsl_infer_from_source;
    default:
        ASSERT_NOT_REACHED();
    }
}

class ShadercIncluder : public shaderc::CompileOptions::IncluderInterface {
public:
    using OnIncludeFileCallback = std::function<void(std::string const& filePath)>;

    explicit ShadercIncluder(ShaderManager const& shaderManager, OnIncludeFileCallback callback)
        : m_shaderManager(shaderManager)
        , m_onIncludeFileCallback(move(callback))
    {
    }

    struct FileData {
        std::string path;
        std::string content;
    };

    shaderc_include_result* GetInclude(char const* requestedSource, shaderc_include_type includeType, char const* requestingSource, size_t includeDepth) override
    {
        SCOPED_PROFILE_ZONE();

        std::string path;
        switch (includeType) {
        case shaderc_include_type_standard:
            path = m_shaderManager.resolveSourceFilePath(requestedSource);
            break;
        case shaderc_include_type_relative:
            std::string_view dirOfRequesting = FileIO::extractDirectoryFromPath(requestingSource);
            path = std::string(dirOfRequesting) + requestedSource;
            break;
        }

        auto* data = new shaderc_include_result();
        auto* dataOwner = new FileData();
        data->user_data = dataOwner;

        dataOwner->path = path;
        data->source_name = dataOwner->path.c_str();
        data->source_name_length = dataOwner->path.size();

        auto maybeFileContent = FileIO::readEntireFile(path);
        if (maybeFileContent.has_value()) {

            m_onIncludeFileCallback(path);

            dataOwner->content = std::move(maybeFileContent.value());
            data->content = dataOwner->content.c_str();
            data->content_length = dataOwner->content.size();

        } else {
            // TODO: Handle include errors slightly more gracefully
            ARKOSE_LOG(Error, "ShadercIncluder: could not find file '{}' included by '{}', exiting", requestedSource, requestingSource);
        }

        return data;
    }

    void ReleaseInclude(shaderc_include_result* data) override
    {
        delete (FileData*)data->user_data;
        delete data;
    }

private:
    ShaderManager const& m_shaderManager;
    OnIncludeFileCallback m_onIncludeFileCallback;
};

class ShadercResult final : public CompilationResult<u32> {
public:
    explicit ShadercResult(shaderc::SpvCompilationResult&& svpCompilationResult, std::vector<std::string>&& includedFiles)
        : m_svpCompilationResult(std::move(svpCompilationResult))
        , m_includedFiles(std::move(includedFiles))
    {
    }

    virtual bool success() const override
    {
        return m_svpCompilationResult.GetCompilationStatus() == shaderc_compilation_status_success;
    }

    virtual std::string errorMessage() const override
    {
        return m_svpCompilationResult.GetErrorMessage();
    }

    virtual std::vector<std::string> const& includedFiles() const override
    {
        return m_includedFiles;
    }

    virtual const_iterator begin() const override
    {
        return m_svpCompilationResult.begin();
    }

    virtual const_iterator end() const override
    {
        return m_svpCompilationResult.end();
    }

private:
    shaderc::SpvCompilationResult m_svpCompilationResult;
    std::vector<std::string> m_includedFiles;
};

std::unique_ptr<CompilationResult<u32>> ShadercInterface::compileShader(ShaderFile const& shaderFile, std::string_view resolvedFilePath)
{
    shaderc::CompileOptions options;

    // Setup default settings (works for now when we only target Vulkan for GLSL files)
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
    options.SetTargetSpirv(shaderc_spirv_version_1_6);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetForcedVersionProfile(460, shaderc_profile_none);

    if (ShaderManager::instance().usingDebugShaders()) {
        options.SetOptimizationLevel(shaderc_optimization_level_zero);
        options.SetGenerateDebugInfo();
    } else {
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
        options.SetGenerateDebugInfo(); // needed for named constant lookup! :/
    }

    // Setup a file includer
    std::vector<std::string> includedFiles;
    auto includer = std::make_unique<ShadercIncluder>(ShaderManager::instance(), [&includedFiles](std::string includedFile) {
        includedFiles.push_back(std::move(includedFile));
    });
    options.SetIncluder(std::move(includer));

    // Add macro definitions
    for (const ShaderDefine& define : shaderFile.defines()) {
        if (define.value.has_value()) {
            options.AddMacroDefinition(define.symbol, define.value.value());
        } else {
            options.AddMacroDefinition(define.symbol);
        }
    }

    shaderc_shader_kind shaderKind = glslShaderKindForShaderFile(shaderFile);
    std::string glslSource = FileIO::readEntireFile(std::string(resolvedFilePath)).value();

    shaderc::Compiler compiler {};
    shaderc::SpvCompilationResult result;
    {
        SCOPED_PROFILE_ZONE_NAMED("Shaderc - CompileGlslToSpv");
        result = compiler.CompileGlslToSpv(glslSource, shaderKind, resolvedFilePath.data(), options);
    };

    return std::make_unique<ShadercResult>(std::move(result), std::move(includedFiles));
}
