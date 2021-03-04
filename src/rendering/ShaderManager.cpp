#include "ShaderManager.h"

#include "utility/FileIO.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include "utility/util.h"
#include <chrono>
#include <cstddef>
#include <thread>
#include <sys/stat.h>

shaderc_shader_kind glslShaderKindForPath(const std::string& path)
{
    // Actually, since we have the ShaderFile with ShaderFileType we already know what the author intends the shader to be!
    // Also, this code is real shit.. but it works!

    if (path.length() < 5) {
        return shaderc_glsl_infer_from_source;
    }
    std::string ext5 = path.substr(path.length() - 5);

    if (ext5 == ".vert") {
        return shaderc_vertex_shader;
    } else if (ext5 == ".frag") {
        return shaderc_fragment_shader;
    } else if (ext5 == ".rgen") {
        return shaderc_raygen_shader;
    } else if (ext5 == ".comp") {
        return shaderc_compute_shader;
    } else if (ext5 == ".rint") {
        return shaderc_intersection_shader;
    }

    if (path.length() < 6) {
        return shaderc_glsl_infer_from_source;
    }
    std::string ext6 = path.substr(path.length() - 6);

    if (ext6 == ".rmiss") {
        return shaderc_miss_shader;
    } else if (ext6 == ".rchit") {
        return shaderc_closesthit_shader;
    }

    LogWarning("ShaderManager: unrecognized shader file type '%s'\n", path.c_str());
    return shaderc_glsl_infer_from_source;
}

class GlslIncluder : public shaderc::CompileOptions::IncluderInterface {
public:

    using OnIncludeFileCallback = std::function<void(const std::string& filePath)>;

    explicit GlslIncluder(const ShaderManager& shaderManager, OnIncludeFileCallback callback)
        : m_shaderManager(shaderManager)
        , m_onIncludeFileCallback(move(callback))
    {
    }

    struct FileData {
        std::string path;
        std::string content;
    };

    shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type include_type, const char* requesting_source, size_t include_depth) override
    {
        SCOPED_PROFILE_ZONE();

        std::string path;
        switch (include_type) {
        case shaderc_include_type_standard:
            path = m_shaderManager.resolvePath(requested_source);
            break;
        case shaderc_include_type_relative:
            // FIXME: Support relative includes!
            NOT_YET_IMPLEMENTED();
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
            LogError("ShaderManager: could not find file '%s' included by '%s', exiting", requested_source, requesting_source);
        }

        return data;
    }

    void ReleaseInclude(shaderc_include_result* data) override
    {
        delete (FileData*)data->user_data;
        delete data;
    }

private:
    const ShaderManager& m_shaderManager;
    OnIncludeFileCallback m_onIncludeFileCallback;
};


ShaderManager& ShaderManager::instance()
{
    static ShaderManager instance { "shaders" };
    return instance;
}

ShaderManager::ShaderManager(std::string basePath)
    : m_shaderBasePath(std::move(basePath))
{
}

void ShaderManager::startFileWatching(unsigned msBetweenPolls, std::function<void()> fileChangeCallback)
{
    if (m_fileWatcherThread != nullptr || m_fileWatchingActive)
        return;

    m_fileWatchingActive = true;
    m_fileWatcherThread = std::make_unique<std::thread>([this, msBetweenPolls, fileChangeCallback]() {
        Profiling::setNameForActiveThread("Shader file watcher");
        while (m_fileWatchingActive) {
            {
                SCOPED_PROFILE_ZONE_NAMED("Shader file watching");
                std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

                int numChangedFiles = 0;
                for (auto& [_, compiledShader] : m_compiledShaders) {

                    uint64_t latestTimestamp = compiledShader->findLatestEditTimestampInIncludeTree();
                    if (latestTimestamp <= compiledShader->compiledTimestamp)
                        continue;

                    LogInfo("Recompiling shader '%s'", compiledShader->filePath.c_str());

                    if (compiledShader->recompile()) {
                        LogInfo(" (success)\n");
                        numChangedFiles += 1;
                    } else {
                        // TODO: Pop an error window in the draw window instead.. that would be easier to keep track of
                        LogError(" (error):\n  %s", compiledShader->lastCompileError.c_str());
                    }
                }

                if (numChangedFiles > 0 && fileChangeCallback)
                    fileChangeCallback();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(msBetweenPolls));
        }
    });
}

void ShaderManager::stopFileWatching()
{
    if (!m_fileWatchingActive)
        return;
    m_fileWatchingActive = false;
    m_fileWatcherThread->join();
}

std::string ShaderManager::resolvePath(const std::string& name) const
{
    std::string resolvedPath = m_shaderBasePath + "/" + name;
    return resolvedPath;
}

std::optional<std::string> ShaderManager::loadAndCompileImmediately(const std::string& name)
{
    auto path = resolvePath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

    auto entry = m_compiledShaders.find(path);
    if (entry == m_compiledShaders.end()) {

        if (!FileIO::isFileReadable(path))
            return "file '" + name + "' not found";

        auto data = std::make_unique<CompiledShader>(path);
        data->recompile();

        m_compiledShaders[path] = std::move(data);
    }

    CompiledShader& compiledShader = *m_compiledShaders[path];
    if (compiledShader.currentSpirvBinary.empty()) {
        return compiledShader.lastCompileError;
    }

    return {};
}

const std::vector<uint32_t>& ShaderManager::spirv(const std::string& name) const
{
    auto path = resolvePath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_compiledShaders.find(path);

    // NOTE: This function should only be called from some backend, so if the
    //  file doesn't exist in the set of loaded shaders something is wrong,
    //  because the frontend makes sure to not run if shaders don't work.
    ASSERT(result != m_compiledShaders.end());

    const ShaderManager::CompiledShader& data = *result->second;
    return data.currentSpirvBinary;
}


ShaderManager::CompiledShader::CompiledShader(std::string path)
    : filePath(std::move(path))
{
}

bool ShaderManager::CompiledShader::recompile()
{
    SCOPED_PROFILE_ZONE();

    std::vector<std::string> newIncludedFiles {};
    auto includer = std::make_unique<GlslIncluder>(ShaderManager::instance(), [&newIncludedFiles](std::string includedFile) {
        newIncludedFiles.push_back(std::move(includedFile));
    });

    shaderc::CompileOptions options;
    options.SetIncluder(std::move(includer));
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetTargetSpirv(shaderc_spirv_version_1_0);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetForcedVersionProfile(460, shaderc_profile_none);

    shaderc_shader_kind shaderKind = glslShaderKindForPath(filePath);
    std::string glslSource = FileIO::readEntireFile(filePath).value();

    shaderc::SpvCompilationResult result;
    {
        SCOPED_PROFILE_ZONE_NAMED("ShaderC work");

        shaderc::Compiler compiler;
        result = compiler.CompileGlslToSpv(glslSource, shaderKind, filePath.c_str(), options);
    }

    bool compilationSuccess = result.GetCompilationStatus() == shaderc_compilation_status_success;

    if (compilationSuccess) {
        currentSpirvBinary = std::vector<uint32_t>(result.cbegin(), result.cend());
        includedFilePaths = std::move(newIncludedFiles);
        lastCompileError.clear();
    } else {
        lastCompileError = result.GetErrorMessage();
    }

    if (lastEditTimestamp == 0)
        lastEditTimestamp = findLatestEditTimestampInIncludeTree();
    compiledTimestamp = lastEditTimestamp;

    return compilationSuccess;
}

uint64_t ShaderManager::CompiledShader::findLatestEditTimestampInIncludeTree()
{
    bool anyMissingFiles = false;    
    uint64_t latestTimestamp = 0;

    auto checkFile = [&](const std::string& file) {
        struct stat statResult {};
        if (stat(file.c_str(), &statResult) == 0) {
            uint64_t timestamp = statResult.st_mtime;
            latestTimestamp = std::max(timestamp, latestTimestamp);
        } else {
            anyMissingFiles = true;
        }
    };

    checkFile(filePath);
    for (auto& file : includedFilePaths) {
        checkFile(file);
    }

    // TODO: Implement better error reporting
    ASSERT(anyMissingFiles == false);

    lastEditTimestamp = latestTimestamp;
    return latestTimestamp;
}
