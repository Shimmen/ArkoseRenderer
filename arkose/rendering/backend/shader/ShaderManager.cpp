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
#include <spirv_hlsl.hpp>

#include "rendering/backend/shader/shaderc/ShadercInterface.h"
#if WITH_D3D12
#include "rendering/backend/shader/dxc/DxcInterface.h"
#endif

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

std::string ShaderManager::resolveSourceFilePath(std::string const& name) const
{
    std::string resolvedPath = m_shaderBasePath + "/" + name;
    return resolvedPath;
}

std::string ShaderManager::createShaderIdentifier(const ShaderFile& shaderFile) const
{
    std::string identifier = shaderFile.path();

    // In HLSL you often pack all related (e.g. vertex & pixel) shaders together in a single file.
    // We need unique identifiers for each "ShaderFile" i.e. compiled unit, so we add this type
    // identifier to the identifier to solve that for the HLSL case.
    if (shaderFile.path().ends_with(".hlsl")) {
        switch (shaderFile.type()) {
        case ShaderFileType::Vertex:
            identifier += "_VS";
            break;
        case ShaderFileType::Fragment:
            identifier += "_FS";
            break;
        case ShaderFileType::Compute:
            identifier += "_CS";
            break;
        case ShaderFileType::RTRaygen:
            identifier += "_RAYGEN";
            break;
        case ShaderFileType::RTClosestHit:
            identifier += "_CLOSESTHIT";
            break;
        case ShaderFileType::RTAnyHit:
            identifier += "_ANYHIT";
            break;
        case ShaderFileType::RTIntersection:
            identifier += "_INTERSECTION";
            break;
        case ShaderFileType::RTMiss:
            identifier += "_MISS";
            break;
        case ShaderFileType::Task:
            identifier += "_TASK";
            break;
        case ShaderFileType::Mesh:
            identifier += "_MESH";
            break;
        case ShaderFileType::Unknown:
            // ignore
            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }
    }

    if (shaderFile.defines().size() > 0) {
        // TODO: Should we maybe hash the define identifier here to cut down on its length?
        std::string defineIdentifier = shaderFile.definesIdentifier();
        identifier += "_" + defineIdentifier;
    }

    return identifier;
}

std::string ShaderManager::resolveDxilPath(ShaderFile const& shaderFile) const
{
    std::string dxilName = createShaderIdentifier(shaderFile) + ".dxil";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + dxilName;
    return resolvedPath;
}

std::string ShaderManager::resolveSpirvPath(ShaderFile const& shaderFile) const
{
    std::string spirvName = createShaderIdentifier(shaderFile) + ".spv";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + spirvName;
    return resolvedPath;
}

std::string ShaderManager::resolveSpirvAssemblyPath(ShaderFile const& shaderFile) const
{
    std::string asmName = createShaderIdentifier(shaderFile) + ".spv-asm";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + asmName;
    return resolvedPath;
}

std::string ShaderManager::resolveHlslPath(ShaderFile const& shaderFile) const
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
        std::string resolvedPath = resolveSourceFilePath(shaderName);

        if (!FileIO::isFileReadable(resolvedPath)) {
            return "file '" + shaderName + "' not found";
        }

        auto compiledShader = std::make_unique<CompiledShader>(*this, shaderFile, resolvedPath);
        if (compiledShader->tryLoadingFromBinaryCache() == false) {
            compiledShader->recompile();
        }

        m_compiledShaders[identifer] = std::move(compiledShader);
    }

    CompiledShader& compiledShader = *m_compiledShaders[identifer];
    if (compiledShader.currentSpirvBinary.empty() && compiledShader.currentDxilBinary.empty()) {
        return compiledShader.lastCompileError;
    }

    return {};
}

ShaderManager::SpirvData const& ShaderManager::spirv(const ShaderFile& shaderFile) const
{
    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_compiledShaders.find(createShaderIdentifier(shaderFile));

    // NOTE: This function should only be called from some backend, so if the
    //  file doesn't exist in the set of loaded shaders something is wrong,
    //  because the frontend makes sure to not run if shaders don't work.
    ARKOSE_ASSERT(result != m_compiledShaders.end());

    ShaderManager::CompiledShader const& data = *result->second;
    if (data.currentSpirvBinary.size() > 0) {
        return data.currentSpirvBinary;
    } else {
        ARKOSE_LOG(Fatal, "Trying to get a SPIR-V binary from a shader that was not compiled for SPIR-V, exiting.");
        return data.currentSpirvBinary;
    }
}

ShaderManager::DXILData const& ShaderManager::dxil(ShaderFile const& shaderFile) const
{
    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_compiledShaders.find(createShaderIdentifier(shaderFile));

    // NOTE: This function should only be called from some backend, so if the
    //  file doesn't exist in the set of loaded shaders something is wrong,
    //  because the frontend makes sure to not run if shaders don't work.
    ARKOSE_ASSERT(result != m_compiledShaders.end());

    const ShaderManager::CompiledShader& data = *result->second;
    if (data.currentDxilBinary.size() > 0) {
        return data.currentDxilBinary;
    } else {
        ARKOSE_LOG(Fatal, "Trying to get a DXIL binary from a shader that was not compiled for DXIL, exiting.");
        return data.currentDxilBinary;
    }
}

ShaderManager::CompiledShader::CompiledShader(ShaderManager& manager, const ShaderFile& shaderFile, std::string resolvedPath)
    : shaderManager(manager)
    , shaderFile(shaderFile)
    , resolvedFilePath(std::move(resolvedPath))
{
    if (resolvedFilePath.ends_with(".hlsl")) {
        sourceType = SourceType::HLSL;
    } else {
        sourceType = SourceType::GLSL;
    }
}

bool ShaderManager::CompiledShader::tryLoadingFromBinaryCache()
{
    SCOPED_PROFILE_ZONE();

    std::string spirvPath = shaderManager.resolveSpirvPath(shaderFile);
    std::string dxilPath = shaderManager.resolveDxilPath(shaderFile);

    struct stat spirvStatResult {};
    bool spirvCacheExists = stat(spirvPath.c_str(), &spirvStatResult) == 0;

    struct stat dxilStatResult { };
    bool dxilCacheExists = stat(dxilPath.c_str(), &dxilStatResult) == 0;

    // Do we allow a shader to be compile with just one or the other binary representation,
    // or should we require both of them to exist? The latter is stricter but works better
    // when actually working against DXIL.. in the future we probably need some system
    // which queries for the actual files we want..
    bool allowEitherOr = false;

    if (allowEitherOr) {
        if (!spirvCacheExists && !dxilCacheExists) {
            return false;
        }
    } else {
        if (!spirvCacheExists || !dxilCacheExists) {
            return false;
        }
    }

    u64 includeTreeLatestTimestamp = findLatestEditTimestampInIncludeTree(true);
    u64 cachedTimestamp = 0;

    if (spirvCacheExists) {
        if (spirvStatResult.st_mtime < includeTreeLatestTimestamp) {
            return false;
        }
        cachedTimestamp = spirvStatResult.st_mtime;
    }

    if (dxilCacheExists) {
        if (dxilStatResult.st_mtime < includeTreeLatestTimestamp) {
            return false;
        }
        cachedTimestamp = dxilStatResult.st_mtime;
    }

    if (spirvCacheExists) {
        currentSpirvBinary = FileIO::readBinaryDataFromFile<u32>(spirvPath).value();
    }
    if (dxilCacheExists) {
        currentDxilBinary = FileIO::readBinaryDataFromFile<u8>(dxilPath).value();
    }

    compiledTimestamp = cachedTimestamp;
    lastCompileError.clear();

    return true;
}

bool ShaderManager::CompiledShader::recompile()
{
    SCOPED_PROFILE_ZONE();

    bool compilationSuccess = false;

    switch (sourceType) {
    case SourceType::GLSL: {

        auto result = ShadercInterface::compileShader(shaderFile, resolvedFilePath);
        compilationSuccess = result->success();

        if (compilationSuccess) {

            currentSpirvBinary = std::vector<u32>(result->begin(), result->end());
            FileIO::writeBinaryDataToFile(shaderManager.resolveSpirvPath(shaderFile), currentSpirvBinary);

            includedFilePaths = result->includedFiles();
            lastCompileError.clear();

            if constexpr (false) {
                // TODO: Add back through ShadercInterface
                //SCOPED_PROFILE_ZONE_NAMED("SPIR-V binary to ASM");
                //shaderc::AssemblyCompilationResult asmResult = compiler.CompileGlslToSpvAssembly(glslSource, shaderKind, resolvedFilePath.c_str(), options);
                //FileIO::writeBinaryDataToFile(shaderManager.resolveSpirvAssemblyPath(shaderFile), std::vector<char>(asmResult.cbegin(), asmResult.cend()));
            }

            if constexpr (true) {
                SCOPED_PROFILE_ZONE_NAMED("SPIR-V to HLSL");

                spirv_cross::CompilerHLSL hlslCompiler { currentSpirvBinary };

                spirv_cross::CompilerHLSL::Options options {};
                options.shader_model = 60; // i.e. shader model 6.0 (todo: use latest version of DXC and SM 6.6+)

                spv::ExecutionModel spvExecutionModel = spv::ExecutionModelMax;
                switch (shaderFile.type()) {
                case ShaderFileType::Vertex:
                    spvExecutionModel = spv::ExecutionModelVertex;
                    break;
                case ShaderFileType::Fragment:
                    spvExecutionModel = spv::ExecutionModelFragment;
                    break;
                case ShaderFileType::Compute:
                    spvExecutionModel = spv::ExecutionModelGLCompute;
                    break;
                case ShaderFileType::RTRaygen:
                    spvExecutionModel = spv::ExecutionModelRayGenerationKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderFileType::RTClosestHit:
                    spvExecutionModel = spv::ExecutionModelClosestHitKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderFileType::RTAnyHit:
                    spvExecutionModel = spv::ExecutionModelAnyHitKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderFileType::RTIntersection:
                    spvExecutionModel = spv::ExecutionModelIntersectionKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderFileType::RTMiss:
                    spvExecutionModel = spv::ExecutionModelMissKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderFileType::Task:
                    spvExecutionModel = spv::ExecutionModelTaskEXT;
                    break;
                case ShaderFileType::Mesh:
                    spvExecutionModel = spv::ExecutionModelMeshEXT;
                    break;
                default:
                    ASSERT_NOT_REACHED();
                    break;
                }

                std::string hlslEntryPoint = DxcInterface::entryPointNameForShaderFile(shaderFile);
                hlslCompiler.rename_entry_point("main", hlslEntryPoint, spvExecutionModel);
                options.use_entry_point_name = true; // note: required for the entry point renaming

                // Compiles and remaps vertex attributes at specific locations to a fixed semantic. The default is TEXCOORD# where # denotes location.
                // Matrices are unrolled to vectors with notation ${SEMANTIC}_#, where # denotes row. $SEMANTIC is either TEXCOORD# or a semantic name specified here.
                //hlslCompiler.add_vertex_attribute_remap({ .location = 0, .semantic = "POSITION" });
                //hlslCompiler.add_vertex_attribute_remap({ .location = 1, .semantic = "TEXCOORD" });

                hlslCompiler.set_hlsl_options(options);

                try {
                    std::string hlslResolvedPath = shaderManager.resolveHlslPath(shaderFile);

                    std::string hlsl = hlslCompiler.compile();
                    FileIO::writeBinaryDataToFile(hlslResolvedPath, hlsl.data(), hlsl.size());

                    #if WITH_D3D12
                    auto result2 = DxcInterface::compileShader(shaderFile, hlslResolvedPath);
                    if ( result2->success() ) {
                        currentDxilBinary = std::vector<u8>(result2->begin(), result2->end());
                        FileIO::writeBinaryDataToFile(shaderManager.resolveDxilPath(shaderFile), currentDxilBinary);
                    } else {
                        ARKOSE_LOG(Error, "Failed to compile transpiled HLSL '{}': {}", hlslResolvedPath, result2->errorMessage());
                    }
                    #else
                    #error "Trying to compile to DXIL (DirectX Intermediate Language) but we are not built with the D3D12 backend so the compiler is not available"
                    #endif

                } catch (const spirv_cross::CompilerError& compilerError) {
                    ARKOSE_LOG(Info, "Failed to transpile '{}' to HLSL: {}. Ignoring, for now.", shaderFile.path(), compilerError.what());
                }
            }

        } else {
            lastCompileError = result->errorMessage();
        }

    } break;

    case SourceType::HLSL: {
    #if WITH_D3D12

        auto result = DxcInterface::compileShader(shaderFile, resolvedFilePath);
        compilationSuccess = result->success();

        if (compilationSuccess) {

            currentDxilBinary = std::vector<u8>(result->begin(), result->end());
            FileIO::writeBinaryDataToFile(shaderManager.resolveDxilPath(shaderFile), currentDxilBinary);

            includedFilePaths = result->includedFiles();
            lastCompileError.clear();

        } else {
            lastCompileError = result->errorMessage();
        }

    #else
        #error "Trying to compile to DXIL (DirectX Intermediate Language) but we are not built with the D3D12 backend so the compiler is not available"
    #endif
    } break;
    }

    if (lastEditTimestamp == 0) {
        lastEditTimestamp = findLatestEditTimestampInIncludeTree();
    }
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
                ? fmt::format("{}{}", FileIO::extractDirectoryFromPath(fileToTest), specifiedPath)
                : shaderManager.resolveSourceFilePath(std::string(specifiedPath));

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
