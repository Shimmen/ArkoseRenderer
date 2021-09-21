#include "ShaderManager.h"

#include "utility/FileIO.h"
#include "utility/Logging.h"
#include "utility/Profiling.h"
#include "utility/util.h"
#include <algorithm>
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
    } else if (ext6 == ".rahit") {
        return shaderc_anyhit_shader;
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
            path = m_shaderManager.resolveGlslPath(requested_source);
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

void ShaderManager::startFileWatching(unsigned msBetweenPolls, FilesChangedCallback filesChangedCallback)
{
    if (m_fileWatcherThread != nullptr || m_fileWatchingActive)
        return;

    m_fileWatchingActive = true;
    m_fileWatcherThread = std::make_unique<std::thread>([this, msBetweenPolls, filesChangedCallback]() {
        Profiling::setNameForActiveThread("Shader file watcher");
        while (m_fileWatchingActive) {
            {
                SCOPED_PROFILE_ZONE_NAMED("Shader file watching");
                std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

                std::vector<std::string> recompiledFiles {};
                for (auto& [_, compiledShader] : m_compiledShaders) {

                    uint64_t latestTimestamp = compiledShader->findLatestEditTimestampInIncludeTree();
                    if (latestTimestamp <= compiledShader->compiledTimestamp)
                        continue;

                    LogInfo("Recompiling shader '%s'", compiledShader->filePath.c_str());

                    if (compiledShader->recompile()) {
                        LogInfo(" (success)\n");
                        recompiledFiles.push_back(compiledShader->shaderName);
                    } else {
                        // TODO: Pop an error window in the draw window instead.. that would be easier to keep track of
                        LogError(" (error):\n  %s", compiledShader->lastCompileError.c_str());
                    }
                }

                if (recompiledFiles.size() > 0 && filesChangedCallback)
                    filesChangedCallback(recompiledFiles);
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

std::string ShaderManager::resolveGlslPath(const std::string& name) const
{
    std::string resolvedPath = m_shaderBasePath + "/" + name;
    return resolvedPath;
}

std::string ShaderManager::resolveSpirvPath(const std::string& name) const
{
    std::string spirvName = name + ".spv";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + spirvName;
    return resolvedPath;
}

std::string ShaderManager::resolveSpirvAssemblyPath(const std::string& name) const
{
    std::string asmName = name + ".spv-asm";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + asmName;
    return resolvedPath;
}

std::optional<std::string> ShaderManager::loadAndCompileImmediately(const std::string& name)
{
    std::string path = resolveGlslPath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

    auto entry = m_compiledShaders.find(path);
    if (entry == m_compiledShaders.end()) {

        if (!FileIO::isFileReadable(path))
            return "file '" + name + "' not found";

        auto compiledShader = std::make_unique<CompiledShader>(*this, name, path);
        if (compiledShader->tryLoadingFromBinaryCache() == false) {
            compiledShader->recompile();
        }

        m_compiledShaders[path] = std::move(compiledShader);
    }

    CompiledShader& compiledShader = *m_compiledShaders[path];
    if (compiledShader.currentSpirvBinary.empty()) {
        return compiledShader.lastCompileError;
    }

    return {};
}

const std::vector<uint32_t>& ShaderManager::spirv(const std::string& name) const
{
    auto path = resolveGlslPath(name);

    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_compiledShaders.find(path);

    // NOTE: This function should only be called from some backend, so if the
    //  file doesn't exist in the set of loaded shaders something is wrong,
    //  because the frontend makes sure to not run if shaders don't work.
    ASSERT(result != m_compiledShaders.end());

    const ShaderManager::CompiledShader& data = *result->second;
    return data.currentSpirvBinary;
}


ShaderManager::CompiledShader::CompiledShader(ShaderManager& manager, std::string name, std::string path)
    : shaderManager(manager)
    , shaderName(std::move(name))
    , filePath(std::move(path))
{
}

bool ShaderManager::CompiledShader::tryLoadingFromBinaryCache()
{
    SCOPED_PROFILE_ZONE();

    std::string spirvPath = shaderManager.resolveSpirvPath(shaderName);

    struct stat statResult {};
    bool binaryCacheExists = stat(spirvPath.c_str(), &statResult) == 0;

    if (!binaryCacheExists)
        return false;

    uint64_t cachedTimestamp = statResult.st_mtime;
    if (cachedTimestamp < findLatestEditTimestampInIncludeTree(true))
        return false;

    currentSpirvBinary = FileIO::readBinaryDataFromFile<uint32_t>(spirvPath).value();
    compiledTimestamp = cachedTimestamp;
    lastCompileError.clear();

    return true;
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
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetTargetSpirv(shaderc_spirv_version_1_5);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetForcedVersionProfile(460, shaderc_profile_none);
    options.SetGenerateDebugInfo(); // always generate debug info

    shaderc_shader_kind shaderKind = glslShaderKindForPath(filePath);
    std::string glslSource = FileIO::readEntireFile(filePath).value();

    shaderc::Compiler compiler {};
    shaderc::SpvCompilationResult result;
    {
        SCOPED_PROFILE_ZONE_NAMED("ShaderC work")
        result = compiler.CompileGlslToSpv(glslSource, shaderKind, filePath.c_str(), options);
    }

    bool compilationSuccess = result.GetCompilationStatus() == shaderc_compilation_status_success;

    if (compilationSuccess) {

        currentSpirvBinary = std::vector<uint32_t>(result.cbegin(), result.cend());
        FileIO::writeBinaryDataToFile(shaderManager.resolveSpirvPath(shaderName), currentSpirvBinary);

        includedFilePaths = std::move(newIncludedFiles);
        lastCompileError.clear();

        {
            // NOTE: This causes a weird crash in ShaderC for some reason *for some shader*
            //SCOPED_PROFILE_ZONE_NAMED("ShaderC ASM work");
            //shaderc::AssemblyCompilationResult asmResult = compiler.CompileGlslToSpvAssembly(glslSource, shaderKind, filePath.c_str(), options);
            //FileIO::writeBinaryDataToFile(shaderManager.resolveSpirvAssemblyPath(shaderName), std::vector<char>(asmResult.cbegin(), asmResult.cend()));
        }

    } else {
        lastCompileError = result.GetErrorMessage();
    }

    if (lastEditTimestamp == 0)
        lastEditTimestamp = findLatestEditTimestampInIncludeTree();
    compiledTimestamp = lastEditTimestamp;

    return compilationSuccess;
}

uint64_t ShaderManager::CompiledShader::findLatestEditTimestampInIncludeTree(bool scanForNewIncludes)
{
    SCOPED_PROFILE_ZONE();

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

    if (scanForNewIncludes) {
        includedFilePaths = findAllIncludedFiles();
    }

    checkFile(filePath);
    for (auto& file : includedFilePaths) {
        checkFile(file);
    }

    // TODO: Implement better error reporting
    ASSERT(anyMissingFiles == false);

    lastEditTimestamp = latestTimestamp;
    return latestTimestamp;
}

std::vector<std::string> ShaderManager::CompiledShader::findAllIncludedFiles() const
{
    SCOPED_PROFILE_ZONE();

    std::vector<std::string> files {};

    std::vector<std::string> filesToTest { filePath };
    while (filesToTest.size() > 0) {

        std::string fileToTest = filesToTest.back();
        filesToTest.pop_back();

        FileIO::readFileLineByLine(fileToTest, [&files, &filesToTest, this](const std::string& line) {

            size_t includeIdx = line.find("#include");
            if (includeIdx == std::string::npos)
                return FileIO::NextAction::Continue;

            size_t fileStartIdx = line.find('<', includeIdx);
            size_t fileEndIdx = line.find('>', fileStartIdx);

            if (fileStartIdx == std::string::npos && fileEndIdx == std::string::npos)
                return FileIO::NextAction::Continue;

            std::string newFile = line.substr(fileStartIdx + 1, fileEndIdx - fileStartIdx - 1);
            std::string newFilePath = shaderManager.resolveGlslPath(newFile);

            if (std::find(files.begin(), files.end(), newFilePath) == files.end()) {
                files.push_back(newFilePath);
                filesToTest.push_back(newFilePath);
            }

            return FileIO::NextAction::Continue;
        });
    }



    return files;
}
