#include "ShaderManager.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <thread>
#include <sys/stat.h>
#include <shaderc/shaderc.hpp>
#include <spirv_hlsl.hpp>

static shaderc_shader_kind glslShaderKindForShaderFile(const ShaderFile& shaderFile)
{
    switch (shaderFile.type()) {
    case ShaderFileType::Vertex:
        return shaderc_vertex_shader;
    case ShaderFileType::Fragment:
        return shaderc_fragment_shader;
    case ShaderFileType::Compute:
        return shaderc_compute_shader;
    case ShaderFileType::RTRaygen:
        return shaderc_raygen_shader;
    case ShaderFileType::RTClosestHit:
        return shaderc_closesthit_shader;
    case ShaderFileType::RTAnyHit:
        return shaderc_anyhit_shader;
    case ShaderFileType::RTIntersection:
        return shaderc_intersection_shader;
    case ShaderFileType::RTMiss:
        return shaderc_miss_shader;
    case ShaderFileType::Unknown:
        ARKOSE_LOG(Warning, "Can't find glsl shader kind for shader file of unknown type ('{}')", shaderFile.path());
        return shaderc_glsl_infer_from_source;
    default:
        ASSERT_NOT_REACHED();
        return shaderc_glsl_infer_from_source;
    }
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
            std::string_view dirOfRequesting = FileIO::extractDirectoryFromPath(requesting_source);
            path = std::string(dirOfRequesting) + requested_source;
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
            ARKOSE_LOG(Error, "ShaderManager: could not find file '{}' included by '{}', exiting", requested_source, requesting_source);
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

                    ARKOSE_LOG(Info, "Recompiling shader '{}'", compiledShader->resolvedFilePath);

                    if (compiledShader->recompile()) {
                        ARKOSE_LOG(Info, " (success)");
                        recompiledFiles.push_back(compiledShader->shaderFile.path());
                    } else {
                        // TODO: Pop an error window in the draw window instead.. that would be easier to keep track of
                        ARKOSE_LOG(Error, " (error):\n  {}", compiledShader->lastCompileError);
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

std::string ShaderManager::createShaderIdentifier(const ShaderFile& shaderFile) const
{
    if (shaderFile.defines().size() > 0) {
        // TODO: Should we maybe hash the define identifier here to cut down on its length?
        std::string defineIdentifier = shaderFile.definesIdentifier();
        return shaderFile.path() + "_" + defineIdentifier;
    } else {
        return shaderFile.path();
    }
}

std::string ShaderManager::resolveSpirvPath(const ShaderFile& shaderFile) const
{
    std::string spirvName = createShaderIdentifier(shaderFile) + ".spv";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + spirvName;
    return resolvedPath;
}

std::string ShaderManager::resolveSpirvAssemblyPath(const ShaderFile& shaderFile) const
{
    std::string asmName = createShaderIdentifier(shaderFile) + ".spv-asm";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + asmName;
    return resolvedPath;
}

std::string ShaderManager::resolveHlslPath(const ShaderFile& shaderFile) const
{
    std::string hlslName = createShaderIdentifier(shaderFile) + ".hlsl";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + hlslName;
    return resolvedPath;
}

std::optional<std::string> ShaderManager::loadAndCompileImmediately(const ShaderFile& shaderFile)
{
    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

    std::string identifer = createShaderIdentifier(shaderFile);

    auto entry = m_compiledShaders.find(identifer);
    if (entry == m_compiledShaders.end() || entry->second->lastCompileError.length() > 0) {

        const std::string& shaderName = shaderFile.path();
        std::string resolvedPath = resolveGlslPath(shaderName);

        if (!FileIO::isFileReadable(resolvedPath))
            return "file '" + shaderName + "' not found";

        auto compiledShader = std::make_unique<CompiledShader>(*this, shaderFile, resolvedPath);
        if (compiledShader->tryLoadingFromBinaryCache() == false) {
            compiledShader->recompile();
        }

        m_compiledShaders[identifer] = std::move(compiledShader);
    }

    CompiledShader& compiledShader = *m_compiledShaders[identifer];
    if (compiledShader.currentSpirvBinary.empty()) {
        return compiledShader.lastCompileError;
    }

    return {};
}

const ShaderManager::SpirvData& ShaderManager::spirv(const ShaderFile& shaderFile) const
{
    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_compiledShaders.find(createShaderIdentifier(shaderFile));

    // NOTE: This function should only be called from some backend, so if the
    //  file doesn't exist in the set of loaded shaders something is wrong,
    //  because the frontend makes sure to not run if shaders don't work.
    ARKOSE_ASSERT(result != m_compiledShaders.end());

    const ShaderManager::CompiledShader& data = *result->second;
    return data.currentSpirvBinary;
}


ShaderManager::CompiledShader::CompiledShader(ShaderManager& manager, const ShaderFile& shaderFile, std::string resolvedPath)
    : shaderManager(manager)
    , shaderFile(shaderFile)
    , resolvedFilePath(std::move(resolvedPath))
{
}

bool ShaderManager::CompiledShader::tryLoadingFromBinaryCache()
{
    SCOPED_PROFILE_ZONE();

    std::string spirvPath = shaderManager.resolveSpirvPath(shaderFile);

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
    for (const ShaderDefine& define : shaderFile.defines()) {
        if (define.value.has_value())
            options.AddMacroDefinition(define.symbol, define.value.value());
        else
            options.AddMacroDefinition(define.symbol);
    }

    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetTargetSpirv(shaderc_spirv_version_1_5);
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetForcedVersionProfile(460, shaderc_profile_none);
    options.SetGenerateDebugInfo(); // always generate debug info

    shaderc_shader_kind shaderKind = glslShaderKindForShaderFile(shaderFile);
    std::string glslSource = FileIO::readEntireFile(resolvedFilePath).value();

    shaderc::Compiler compiler {};
    shaderc::SpvCompilationResult result;
    {
        SCOPED_PROFILE_ZONE_NAMED("ShaderC work");
        result = compiler.CompileGlslToSpv(glslSource, shaderKind, resolvedFilePath.c_str(), options);
    }

    bool compilationSuccess = result.GetCompilationStatus() == shaderc_compilation_status_success;

    if (compilationSuccess) {

        currentSpirvBinary = std::vector<uint32_t>(result.cbegin(), result.cend());
        FileIO::writeBinaryDataToFile(shaderManager.resolveSpirvPath(shaderFile), currentSpirvBinary);

        includedFilePaths = std::move(newIncludedFiles);
        lastCompileError.clear();

        {
            // NOTE: This causes a weird crash in ShaderC for some reason *for some shaders*
            //SCOPED_PROFILE_ZONE_NAMED("SPIR-V binary to ASM");
            //shaderc::AssemblyCompilationResult asmResult = compiler.CompileGlslToSpvAssembly(glslSource, shaderKind, resolvedFilePath.c_str(), options);
            //FileIO::writeBinaryDataToFile(shaderManager.resolveSpirvAssemblyPath(shaderFile), std::vector<char>(asmResult.cbegin(), asmResult.cend()));
        }

        {
            SCOPED_PROFILE_ZONE_NAMED("SPIR-V to HLSL");

            spirv_cross::CompilerHLSL::Options options {};
            options.shader_model = 66; // i.e. shader model 6.6

            spirv_cross::CompilerHLSL hlslCompiler { currentSpirvBinary };
            hlslCompiler.set_hlsl_options(options);

            try {
                std::string hlsl = hlslCompiler.compile();
                FileIO::writeBinaryDataToFile(shaderManager.resolveHlslPath(shaderFile), hlsl.data(), hlsl.size());
            } catch (const spirv_cross::CompilerError& compilerError) {
                ARKOSE_LOG(Verbose, "Failed to compile '{}' to HLSL: {}. Ignoring, for now.", shaderFile.path(), compilerError.what());
            }
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

    std::vector<std::string> missingFiles {};
    uint64_t latestTimestamp = 0;

    auto checkFile = [&](const std::string& file) {
        struct stat statResult {};
        if (stat(file.c_str(), &statResult) == 0) {
            uint64_t timestamp = statResult.st_mtime;
            latestTimestamp = std::max(timestamp, latestTimestamp);
        } else {
            missingFiles.push_back(file);
        }
    };

    if (scanForNewIncludes) {
        includedFilePaths = findAllIncludedFiles();
    }

    checkFile(resolvedFilePath);
    for (auto& file : includedFilePaths) {
        checkFile(file);
    }

    if (missingFiles.size() > 0) {
        ARKOSE_LOG(Error, "Shader file '{}' has {} non-existant file(s) in its include tree:", resolvedFilePath, missingFiles.size());
        for (std::string const& missingFile : missingFiles) {
            ARKOSE_LOG(Error, "  {}", missingFile);
        }
        ARKOSE_LOG_FATAL("Can't resolve edit timestamps, exiting");
    }

    lastEditTimestamp = latestTimestamp;
    return latestTimestamp;
}

std::vector<std::string> ShaderManager::CompiledShader::findAllIncludedFiles() const
{
    SCOPED_PROFILE_ZONE();

    // NOTE: If the resulting list does not line up with what the shader compiler
    // believes is the true set of includes we should expect some weird issues.

    std::vector<std::string> files {};

    std::vector<std::string> filesToTest { resolvedFilePath };
    while (filesToTest.size() > 0) {

        std::string fileToTest = filesToTest.back();
        filesToTest.pop_back();

        FileIO::readFileLineByLine(fileToTest, [&files, &fileToTest, & filesToTest, this](const std::string& line) {

            bool relativePath;
            std::string_view specifiedPath = findIncludedPathFromShaderCodeLine(line, relativePath);

            if (specifiedPath == "") {
                return FileIO::NextAction::Continue;
            }

            std::string includePath = (relativePath)
                ? std::format("{}{}", FileIO::extractDirectoryFromPath(fileToTest), specifiedPath)
                : shaderManager.resolveGlslPath(std::string(specifiedPath));

            if (std::find(files.begin(), files.end(), includePath) == files.end()) {
                files.push_back(includePath);
                filesToTest.push_back(includePath);
            }

            return FileIO::NextAction::Continue;
        });
    }

    return files;
}

std::string_view ShaderManager::CompiledShader::findIncludedPathFromShaderCodeLine(std::string_view line, bool& outIsRelative) const
{
    size_t includeIdx = line.find("#include");
    if (includeIdx == std::string::npos) {
        return "";
    }

    size_t commentStartIdx = line.find("//");

    size_t fileStartIdx = line.find('<', includeIdx);
    size_t fileEndIdx = line.find('>', fileStartIdx + 1);

    if (fileStartIdx != std::string::npos && fileEndIdx != std::string::npos && (commentStartIdx == std::string::npos || commentStartIdx > fileEndIdx)) {
        outIsRelative = false;
        return line.substr(fileStartIdx + 1, fileEndIdx - fileStartIdx - 1);
    }

    fileStartIdx = line.find('"', includeIdx);
    fileEndIdx = line.find('"', fileStartIdx + 1);

    if (fileStartIdx != std::string::npos && fileEndIdx != std::string::npos && (commentStartIdx == std::string::npos || commentStartIdx > fileEndIdx)) {
        outIsRelative = true;
        return line.substr(fileStartIdx + 1, fileEndIdx - fileStartIdx - 1);
    }

    return "";
}
