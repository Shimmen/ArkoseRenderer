#include "ShaderManager.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include "utility/StringHelpers.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <thread>
#include <sys/stat.h>
#include <spirv_cross.hpp>
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

                    if (compiledShader->compiledTimestamp == 0) { 
                        // This shader has only been registered but never compiled, so nothing to recompile
                        continue;
                    }

                    uint64_t latestTimestamp = compiledShader->findLatestEditTimestampInIncludeTree();
                    if (latestTimestamp <= compiledShader->compiledTimestamp) {
                        continue;
                    }

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
        switch (shaderFile.shaderStage()) {
        case ShaderStage::Vertex:
            identifier += "_VS";
            break;
        case ShaderStage::Fragment:
            identifier += "_FS";
            break;
        case ShaderStage::Compute:
            identifier += "_CS";
            break;
        case ShaderStage::RTRayGen:
            identifier += "_RAYGEN";
            break;
        case ShaderStage::RTClosestHit:
            identifier += "_CLOSESTHIT";
            break;
        case ShaderStage::RTAnyHit:
            identifier += "_ANYHIT";
            break;
        case ShaderStage::RTIntersection:
            identifier += "_INTERSECTION";
            break;
        case ShaderStage::RTMiss:
            identifier += "_MISS";
            break;
        case ShaderStage::Task:
            identifier += "_TASK";
            break;
        case ShaderStage::Mesh:
            identifier += "_MESH";
            break;
        case ShaderStage::Unknown:
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

std::string ShaderManager::resolveMetadataPath(ShaderFile const& shaderFile) const
{
    std::string metaName = createShaderIdentifier(shaderFile) + ".meta";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + metaName;
    return resolvedPath;
}

std::string ShaderManager::resolveHlslPath(ShaderFile const& shaderFile) const
{
    std::string hlslName = createShaderIdentifier(shaderFile) + ".hlsl";
    std::string resolvedPath = m_shaderBasePath + "/.cache/" + hlslName;
    return resolvedPath;
}

void ShaderManager::registerShaderFile( ShaderFile const& shaderFile)
{
    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);

    std::string identifer = createShaderIdentifier(shaderFile);

    auto entry = m_compiledShaders.find(identifer);
    if (entry == m_compiledShaders.end() || entry->second->lastCompileError.length() > 0) {

        const std::string& shaderName = shaderFile.path();
        std::string resolvedPath = resolveSourceFilePath(shaderName);

        if (!FileIO::isFileReadable(resolvedPath)) {
            ARKOSE_LOG(Error, "ShaderManager: file '{}' not found", shaderName);
        }

        // It's not compiled *yet*, but it's in a state where we can store compiled results, hence the name..
        m_compiledShaders[identifer] = std::make_unique<CompiledShader>(*this, shaderFile, resolvedPath);
    }
}

ShaderManager::SpirvData const& ShaderManager::spirv(const ShaderFile& shaderFile) const
{
    std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
    auto result = m_compiledShaders.find(createShaderIdentifier(shaderFile));

    // NOTE: This function should only be called from some backend, so if the
    //  file doesn't exist in the set of loaded shaders something is wrong,
    //  because the frontend makes sure to not run if shaders don't work.
    ARKOSE_ASSERT(result != m_compiledShaders.end());

    ShaderManager::CompiledShader& data = *result->second;
    if (data.currentSpirvBinary.size() > 0) {
        return data.currentSpirvBinary;
    } else {
        data.compileWithRetry(CompiledShader::TargetType::Spirv);
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

    ShaderManager::CompiledShader& data = *result->second;
    if (data.currentDxilBinary.size() > 0) {
        return data.currentDxilBinary;
    } else {
        data.compileWithRetry(CompiledShader::TargetType::DXIL);
        return data.currentDxilBinary;
    }
}

NamedConstantLookup ShaderManager::mergeNamedConstants(Shader const& shader) const
{
    std::vector<NamedConstant> mergedNamedConstants {};
    bool compatible = hasCompatibleNamedConstants(shader.files(), mergedNamedConstants);
    ARKOSE_ASSERTM(compatible, "ShaderManager: all shader files of a shader needs to have a compatible set of named constants, "
                               "i.e. no overlap, unless it's the exact same type and name and offset.");

    return NamedConstantLookup(mergedNamedConstants);
}

bool ShaderManager::hasCompatibleNamedConstants(std::vector<ShaderFile> const& shaderFiles, std::vector<NamedConstant>& outMergedConstants) const
{
    SCOPED_PROFILE_ZONE();

    auto getCompiledShader = [this](ShaderFile const& shaderFile) -> ShaderManager::CompiledShader const& {
        auto entry = m_compiledShaders.find(createShaderIdentifier(shaderFile));
        ARKOSE_ASSERT(entry != m_compiledShaders.end());
        ShaderManager::CompiledShader const& compiledShader = *entry->second;
        return compiledShader;
    };

    if (shaderFiles.size() <= 1) {
        std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
        auto const& compiledShader = getCompiledShader(shaderFiles[0]);
        outMergedConstants = compiledShader.namedConstants;
        return true;
    }

    std::vector<NamedConstant const*> constants;

    {
        std::lock_guard<std::mutex> dataLock(m_shaderDataMutex);
        for (ShaderFile const& shaderFile : shaderFiles) {
            auto const& compiledShader = getCompiledShader(shaderFile);

            if (compiledShader.compiledTimestamp == 0) {
                ARKOSE_LOG(Fatal, "ShaderManager: trying to check for compatible named constants on shader files that haven't yet been compiled. "
                                  "This function will never attempt to compile files for you, as it won't know what backend/compiled representation "
                                  "is needed, so it's expected that you don't call this until you're sure all of the files have successfully been compiled.");
            }

            for (NamedConstant const& constant : compiledShader.namedConstants) {
                constants.push_back(&constant);
            }
        }
    }

    if (constants.size() == 0) {
        outMergedConstants.clear();
        return true;
    }

    std::sort(constants.begin(), constants.end(), [&](NamedConstant const* lhs, NamedConstant const* rhs) { return lhs->offset < rhs->offset; });

    outMergedConstants.clear();
    outMergedConstants.push_back(*constants[0]);

    NamedConstant const* previousConstant = constants[0];
    for (size_t constantIdx = 1; constantIdx < constants.size(); ++constantIdx) {
        NamedConstant const* thisConstant = constants[constantIdx];

        if (thisConstant->offset > previousConstant->offset) {
            if (thisConstant->offset >= previousConstant->offset + previousConstant->size) { 
                // this constant is not overlapping with the previous one, i.e. this is the next one
                outMergedConstants.push_back(*thisConstant);
                previousConstant = thisConstant;
            } else {
                // this constant has an offset within the previous ones' range which is clearly not allowed
                outMergedConstants.clear();
                return false;
            }
        } else if (thisConstant->offset == previousConstant->offset) {
            if (thisConstant->size == previousConstant->size && thisConstant->name == previousConstant->name && thisConstant->type == previousConstant->type) { 
                // these two constants are identical, so overlap is what we'd expect! Just merge the stage flags
                outMergedConstants.back().stages = outMergedConstants.back().stages | thisConstant->stages;
                continue;
            } else {
                // same offset but different properties
                outMergedConstants.clear();
                return false;
            }
        }
    }

    return true;
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

bool ShaderManager::CompiledShader::tryLoadingFromBinaryCache(TargetType targetType)
{
    SCOPED_PROFILE_ZONE();

    std::string cachePath;

    switch (targetType) {
    case TargetType::Spirv:
        cachePath = shaderManager.resolveSpirvPath(shaderFile);
        break;
    case TargetType::DXIL:
        cachePath = shaderManager.resolveDxilPath(shaderFile);
        break;
    }

    struct stat statResult { };
    bool cacheExists = stat(cachePath.c_str(), &statResult) == 0;

    if (!cacheExists) {
        return false;
    }

    u64 includeTreeLatestTimestamp = findLatestEditTimestampInIncludeTree(true);
    if (statResult.st_mtime < includeTreeLatestTimestamp) {
        return false;
    }

    switch (targetType) {
    case TargetType::Spirv:
        currentSpirvBinary = FileIO::readBinaryDataFromFile<u32>(cachePath).value();
        break;
    case TargetType::DXIL:
        currentDxilBinary = FileIO::readBinaryDataFromFile<u8>(cachePath).value();
        break;
    }

    compiledTimestamp = statResult.st_mtime;
    lastCompileError.clear();

    // If there's a binary cache there should also be metadata available, assuming this shader needs it, so load that now
    readShaderMetadataFile();

    return true;
}

void ShaderManager::CompiledShader::compileWithRetry(TargetType targetType)
{
    if (tryLoadingFromBinaryCache(targetType)) {
        return;
    }

    do {
        if (!compile(targetType)) {
            ARKOSE_LOG(Error, "Shader file error: {}", lastCompileError);
#ifdef _WIN32
            ARKOSE_LOG(Error, "Edit & and save the shader, then ...");
            system("pause");
#else
            ARKOSE_LOG(Fatal, "Exiting due to bad shader at startup.");
#endif
        }
    } while (lastCompileError.size() > 0);
}

bool ShaderManager::CompiledShader::compile(TargetType targetType)
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

            if (collectNamedConstants()) {
                // For now it only contains info about named constants so we write it here
                writeShaderMetadataFile();
            }

            if constexpr (false) {
                // TODO: Add back through ShadercInterface
                //SCOPED_PROFILE_ZONE_NAMED("SPIR-V binary to ASM");
                //shaderc::AssemblyCompilationResult asmResult = compiler.CompileGlslToSpvAssembly(glslSource, shaderKind, resolvedFilePath.c_str(), options);
                //FileIO::writeBinaryDataToFile(shaderManager.resolveSpirvAssemblyPath(shaderFile), std::vector<char>(asmResult.cbegin(), asmResult.cend()));
            }

            #if WITH_D3D12
            if (targetType == TargetType::DXIL) {
                SCOPED_PROFILE_ZONE_NAMED("SPIR-V to HLSL");

                spirv_cross::CompilerHLSL hlslCompiler { currentSpirvBinary };

                spirv_cross::CompilerHLSL::Options options {};
                options.shader_model = 66; // i.e. shader model 6.6

                // NOTE: We use `ShaderBinding::storageBuffer` vs. `ShaderBinding::storageBufferReadonly` to differentiate the two types in the graphics
                // frontend but internally (i.e. in the backend) it's not used so there we can't know if a buffer is readonly or not. This is simply because
                // it doesn't matter for Vulkan when binding. However, in D3D12 we use a UAV vs. a SRV for this distinction. I feel it would likely be better
                // to use SRVs when a storage buffer is read-only but for now/simplicity let's just force them all to be UAVs.
                options.force_storage_buffer_as_uav = true;

                spv::ExecutionModel spvExecutionModel = spv::ExecutionModelMax;
                switch (shaderFile.shaderStage()) {
                case ShaderStage::Vertex:
                    spvExecutionModel = spv::ExecutionModelVertex;
                    break;
                case ShaderStage::Fragment:
                    spvExecutionModel = spv::ExecutionModelFragment;
                    break;
                case ShaderStage::Compute:
                    spvExecutionModel = spv::ExecutionModelGLCompute;
                    break;
                case ShaderStage::RTRayGen:
                    spvExecutionModel = spv::ExecutionModelRayGenerationKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderStage::RTClosestHit:
                    spvExecutionModel = spv::ExecutionModelClosestHitKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderStage::RTAnyHit:
                    spvExecutionModel = spv::ExecutionModelAnyHitKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderStage::RTIntersection:
                    spvExecutionModel = spv::ExecutionModelIntersectionKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderStage::RTMiss:
                    spvExecutionModel = spv::ExecutionModelMissKHR; // NOTE: Only works with KHR extension!
                    break;
                case ShaderStage::Task:
                    spvExecutionModel = spv::ExecutionModelTaskEXT;
                    break;
                case ShaderStage::Mesh:
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

                    auto result2 = DxcInterface::compileShader(shaderFile, hlslResolvedPath);
                    if ( result2->success() ) {
                        currentDxilBinary = std::vector<u8>(result2->begin(), result2->end());
                        FileIO::writeBinaryDataToFile(shaderManager.resolveDxilPath(shaderFile), currentDxilBinary);
                    } else {
                        ARKOSE_LOG(Error, "Failed to compile transpiled HLSL '{}': {}", hlslResolvedPath, result2->errorMessage());
                    }

                } catch (const spirv_cross::CompilerError& compilerError) {
                    ARKOSE_LOG(Info, "Failed to transpile '{}' to HLSL: {}. Ignoring, for now.", shaderFile.path(), compilerError.what());
                }
            }
            #endif

        } else {
            lastCompileError = result->errorMessage();
        }

    } break;

    case SourceType::HLSL: {
    #if WITH_D3D12

        if (targetType == TargetType::Spirv) {
            ARKOSE_LOG(Error, "Trying to compile HLSL source file into SPIR-V which is not yet supported!");
            return false;
        }

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
        ARKOSE_LOG(Error, "Trying to compile to HLSL file but we are not built with the D3D12 backend so the compiler is not available");
    #endif
    } break;
    }

    if (lastEditTimestamp == 0) {
        lastEditTimestamp = findLatestEditTimestampInIncludeTree();
    }
    compiledTimestamp = lastEditTimestamp;

    return compilationSuccess;
}

bool ShaderManager::CompiledShader::recompile()
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(currentSpirvBinary.size() > 0 || currentDxilBinary.size() > 0);

    // Assume that we need to compile whatever binaries we currently have loaded

    if (currentSpirvBinary.size() > 0) { 
        if (!compile(TargetType::Spirv)) { 
            return false;
        }
    }

    if (currentDxilBinary.size() > 0) {
        if (!compile(TargetType::DXIL)) {
            return false;
        }
    }

    return true;
}

bool ShaderManager::CompiledShader::collectNamedConstants()
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(currentSpirvBinary.size() > 0);

    namedConstants.clear();

    spirv_cross::Compiler compiler { currentSpirvBinary };
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    if (!resources.push_constant_buffers.empty()) {
        ARKOSE_ASSERT(resources.push_constant_buffers.size() == 1);

        spirv_cross::Resource const& pushConstantResource = resources.push_constant_buffers[0];
        spirv_cross::SPIRType const& pushConstantType = compiler.get_type(pushConstantResource.type_id);

        // With the NAMED_UNIFORMS macro all push constant blocks will contain exactly one struct with named members
        if (pushConstantType.member_types.size() != 1) {
            ARKOSE_LOG(Fatal, "ShaderManager: please use the NAMED_UNIFORMS macro to define push constants!");
        }

        const spirv_cross::TypeID& structTypeId = pushConstantType.member_types[0];
        const spirv_cross::SPIRType& structType = compiler.get_type(structTypeId);
        if (structType.basetype != spirv_cross::SPIRType::Struct) {
            ARKOSE_LOG(Fatal, "ShaderManager: please use the NAMED_UNIFORMS macro to define push constants!");
        }

        size_t memberCount = structType.member_types.size();
        for (int i = 0; i < memberCount; ++i) {

            spirv_cross::TypeID memberTypeId = structType.member_types[i];
            spirv_cross::SPIRType memberType = compiler.get_type(memberTypeId);

            std::string memberTypeName;
            switch (memberType.basetype) {
                using namespace spirv_cross;
            case SPIRType::Float:
                memberTypeName = "float";
                break;
            case SPIRType::UInt:
                memberTypeName = "uint";
                break;
            case SPIRType::Int:
                memberTypeName = "int";
                break;
            default:
                ARKOSE_LOG(Fatal, "ShaderManager: unknown type used for named constant");
                memberTypeName = "unknown";
                break;
            }

            if (memberType.columns > 1) {
                memberTypeName = fmt::format("{}{}", memberTypeName, memberType.columns);
            }
            if (memberType.vecsize > 1) {
                memberTypeName = fmt::format("{}{}", memberTypeName, memberType.vecsize);
            }

            std::string const& memberName = compiler.get_member_name(structTypeId, i);
            size_t offset = compiler.type_struct_member_offset(structType, i);
            size_t size = compiler.get_declared_struct_member_size(structType, i);

            NamedConstant& constant = namedConstants.emplace_back();

            constant.name = memberName;
            constant.type = memberTypeName;
            constant.offset = narrow_cast<u32>(offset);
            constant.size = narrow_cast<u32>(size);
            constant.stages = shaderFile.shaderStage();
        }
    }

    return namedConstants.size() > 0;
}

void ShaderManager::CompiledShader::writeShaderMetadataFile() const
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(namedConstants.size() > 0);

    std::string metadataContent {};
    for (NamedConstant const& constant : namedConstants) {
        metadataContent.append(fmt::format("{}:{}:{}:{}\n", constant.name, constant.type, constant.size, constant.offset));
    }

    std::string metadataPath = shaderManager.resolveMetadataPath(shaderFile);
    FileIO::writeTextDataToFile(metadataPath, metadataContent);
}

bool ShaderManager::CompiledShader::readShaderMetadataFile()
{
    SCOPED_PROFILE_ZONE();

    namedConstants.clear();

    std::string metadataPath = shaderManager.resolveMetadataPath(shaderFile);
    bool readSuccess = FileIO::readFileLineByLine(metadataPath, [&](std::string const& line) {

        NamedConstant& constant = namedConstants.emplace_back();
        constant.stages = shaderFile.shaderStage();

        StringHelpers::forEachToken(line, ':', [&](std::string_view token, size_t tokenIndex) {
            switch (tokenIndex) {
            case 0:
                constant.name = token;
                break;
            case 1:
                constant.type = token;
                break;
            case 2: {
                auto result = std::from_chars(token.data(), token.data() + token.size(), constant.size);
                ARKOSE_ASSERT(result.ec != std::errc::invalid_argument && result.ec != std::errc::result_out_of_range);
            } break;
            case 3: {
                auto result = std::from_chars(token.data(), token.data() + token.size(), constant.offset);
                ARKOSE_ASSERT(result.ec != std::errc::invalid_argument && result.ec != std::errc::result_out_of_range);
            } break;
            }
        });

        return FileIO::NextAction::Continue;
    });

    return readSuccess;
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
        ARKOSE_LOG(Fatal, "Can't resolve edit timestamps, exiting");
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
