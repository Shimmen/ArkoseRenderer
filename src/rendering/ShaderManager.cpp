#include "ShaderManager.h"

#include "utility/FileIO.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include "utility/util.h"
#include <chrono>
#include <cstddef>
#include <thread>

// TODO: Implement Windows support!
#include <sys/stat.h>

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
    m_fileWatcherThread = std::make_unique<std::thread>([this, msBetweenPolls, &fileChangeCallback]() {
        Profiling::setNameForActiveThread("Shader file watcher");
        while (m_fileWatchingActive) {
            {
                SCOPED_PROFILE_ZONE_NAMED("Shader file watching");
                std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

                std::vector<std::string> filesToRemove;
                int numChangedFiles = 0;

                for (auto& [_, data] : m_loadedShaders) {

                    if (!FileIO::isFileReadable(data.filePath)) {
                        LogWarning("ShaderManager: removing shader '%s' from managed set since it seems to have been removed.\n");
                        filesToRemove.push_back(data.filePath);
                        continue;
                    }

                    uint64_t lastEdit = getFileEditTimestamp(data.filePath);
                    if (lastEdit <= data.lastEditTimestamp)
                        continue;

                    //LogInfo("ShaderManager: updating file '%s'\n", data.path.c_str());
                    data.glslSource = FileIO::readEntireFile(data.filePath).value();
                    data.lastEditTimestamp = lastEdit;
                    if (compileGlslToSpirv(data)) {
                        data.currentBinaryVersion += 1;
                        numChangedFiles += 1;
                    } else {
                        LogError("Shader at path '%s' could not compile:\n\t%s\n", data.filePath.c_str(), data.lastCompileError.c_str());
                    }
                }

                for (const auto& path : filesToRemove)
                    m_loadedShaders.erase(path);

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

    auto result = m_loadedShaders.find(path);
    if (result == m_loadedShaders.end()) {

        if (!FileIO::isFileReadable(path))
            return "file '" + name + "' not found";

        ShaderData data { path };
        data.glslSource = FileIO::readEntireFile(path).value();
        data.lastEditTimestamp = getFileEditTimestamp(path);

        compileGlslToSpirv(data);
        m_loadedShaders[path] = std::move(data);
    }

    ShaderData& data = m_loadedShaders[path];
    if (data.lastEditSuccessfullyCompiled) {
        data.currentBinaryVersion = 1;
        return {};
    } else {
        return data.lastCompileError;
    }
}

const std::vector<uint32_t>& ShaderManager::spirv(const std::string& name) const
{
    auto path = resolvePath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_loadedShaders.find(path);

    // NOTE: This function should only be called from some backend, so if the
    //  file doesn't exist in the set of loaded shaders something is wrong,
    //  because the frontend makes sure to not run if shaders don't work.
    ASSERT(result != m_loadedShaders.end());

    const ShaderData& data = result->second;
    return data.spirvBinary;
}

uint64_t ShaderManager::getFileEditTimestamp(const std::string& path) const
{
    struct stat statResult = {};
    if (stat(path.c_str(), &statResult) == 0) {
        return statResult.st_mtime;
    }

    ASSERT_NOT_REACHED();
}

shaderc_shader_kind ShaderManager::shaderKindForPath(const std::string& path) const
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

    LogWarning("ShaderManager::shaderKindForPath(): unrecognized shader file type '%s'\n", path.c_str());
    return shaderc_glsl_infer_from_source;
}

bool ShaderManager::compileGlslToSpirv(ShaderData& data) const
{
    SCOPED_PROFILE_ZONE();
    ASSERT(!data.glslSource.empty());

    class Includer : public shaderc::CompileOptions::IncluderInterface {
    public:
        explicit Includer(const ShaderManager& shaderManager)
            : m_shaderManager(shaderManager)
        {
        }

        struct FileData {
            std::string path;
            std::string content;
        };

        shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type include_type, const char* requesting_source, size_t include_depth) override
        {
            SCOPED_PROFILE_ZONE();

            // FIXME: Support relative includes!
            ASSERT(include_type == shaderc_include_type_standard);

            std::string path = m_shaderManager.resolvePath(requested_source);
            auto maybeFileContent = FileIO::readEntireFile(path);
            if (!maybeFileContent.has_value()) {
                LogErrorAndExit("ShaderManager: could not find file '%s' included by '%s', exiting", requested_source, requesting_source);
                return nullptr;
            }

            auto* data = new shaderc_include_result();

            auto* fileData = new FileData();
            fileData->path = path;
            fileData->content = maybeFileContent.value();
            data->user_data = fileData;

            data->source_name = fileData->path.c_str();
            data->source_name_length = fileData->path.size();

            data->content = fileData->content.c_str();
            data->content_length = fileData->content.size();

            return data;
        }

        void ReleaseInclude(shaderc_include_result* data) override
        {
            delete (FileData*)data->user_data;
            delete data;
        }

    private:
        const ShaderManager& m_shaderManager;
    };

    shaderc::Compiler compiler;

    shaderc::CompileOptions options;
    options.SetIncluder(std::make_unique<Includer>(*this));
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    options.SetTargetSpirv(shaderc_spirv_version_1_0);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetForcedVersionProfile(460, shaderc_profile_none);

    shaderc_shader_kind kind = shaderKindForPath(data.filePath);
    shaderc::SpvCompilationResult result;
    {
        SCOPED_PROFILE_ZONE_NAMED("ShaderC work");
        result = compiler.CompileGlslToSpv(data.glslSource, kind, data.filePath.c_str(), options);
    }

    // Note that we only should overwrite the binary if it compiled correctly!
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        data.lastEditSuccessfullyCompiled = false;
        data.lastCompileError = result.GetErrorMessage();
    } else {
        data.lastEditSuccessfullyCompiled = true;
        data.lastCompileError.clear();
        data.spirvBinary = std::vector<uint32_t>(result.cbegin(), result.cend());
    }

    return data.lastEditSuccessfullyCompiled;
}
