#include <utility/FileIO.h>
#include <utility/ToolUtilities.h>
#include <asset/misc/ShaderCompileSpec.h>

#include <slang.h>

static void printDiagnostics(slang::IBlob* diagnosticsBlob, bool asError = true)
{
    if (diagnosticsBlob) {
        size_t messageSize = diagnosticsBlob->getBufferSize();
        char const* message = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
        if (messageSize > 0 && message) {
            std::string diagString { message, messageSize };
            if (asError) {
                ARKOSE_LOG(Error, "ShaderCompilerTool: slang diagnostics: {}", diagString);
            } else {
                ARKOSE_LOG(Warning, "ShaderCompilerTool: slang diagnostics: {}", diagString);
            }
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        // TODO: Add support for named command line arguments!
        ARKOSE_LOG(Error, "ShaderCompilerTool: not enough arguments!");
        return 1;
    }

    std::filesystem::path shaderSpecPath = argv[1];
    std::filesystem::path shaderBaseDirectory = argv[2];

    //
    // Setup directories
    //

    std::vector<char const*> shaderSearchPaths {};

    std::string shaderBaseDirectoryStr = shaderBaseDirectory.string();
    shaderSearchPaths.push_back(shaderBaseDirectoryStr.c_str());

    //
    // Load shader spec
    //

    auto compileSpec = ShaderCompileSpec::loadFromFile(shaderSpecPath);

    if (compileSpec == nullptr) {
        ARKOSE_LOG(Error, "ShaderCompilerTool: failed to load shader spec from file '{}'", shaderSpecPath);
        return 1;
    }

    size_t numFiles = compileSpec->shaderFiles.size();
    size_t numPermutations = compileSpec->permutations.size();
    size_t numCompilations = numFiles * numPermutations;
    ARKOSE_LOG(Info, "ShaderCompilerTool: will compile a total of {} permutations for {} files ({} binaries)", numPermutations, numFiles, numCompilations);

    //
    // Create global session
    //

    slang::IBlob* diagnosticsBlob = nullptr;

    SlangGlobalSessionDesc globalSessionDesc = {};
    globalSessionDesc.enableGLSL = true;

    slang::IGlobalSession* globalSession;
    if (SLANG_FAILED(slang::createGlobalSession(&globalSessionDesc, &globalSession))) {
        ARKOSE_LOG(Error, "ShaderCompilerTool: Failed to create slang global session!");
        return 1;
    }

    // Register targets

    std::vector<slang::TargetDesc> targets;

    size_t spirvTargetIdx = targets.size();
    slang::TargetDesc& spirvTargetDesc = targets.emplace_back();
    spirvTargetDesc.format = SLANG_SPIRV;
    spirvTargetDesc.profile = globalSession->findProfile("spirv_1_6");

#if 0
    size_t dxilTargetIdx = targets.size();
    slang::TargetDesc& dxiTargetDesc = targets.emplace_back();
    dxiTargetDesc.format = SLANG_DXIL;
    dxiTargetDesc.profile = globalSession->findProfile("sm_6_6");
#endif

    //
    // Process each shader file & permutation
    //

    size_t compilationIdx = 0;
    for (ShaderCompileSpec::ShaderFileSpec const& file : compileSpec->shaderFiles) {

        // Load file from disk

        std::filesystem::path shaderFilePath = shaderBaseDirectory / file.second;
        std::optional<std::string> shaderFileText = FileIO::readFile(shaderFilePath);

        if (!shaderFileText.has_value()) {
            ARKOSE_LOG(Error, "ShaderCompilerTool: Failed to load shader file from path '{}', skipping", shaderFilePath.string());
            continue;
        }

        // Translate the shader stage

        ShaderStage shaderFileStage = file.first;
        SlangStage slangStage = SLANG_STAGE_NONE;
        switch (shaderFileStage) {
        case ShaderStage::Vertex:
            slangStage = SLANG_STAGE_VERTEX;
            break;
        case ShaderStage::Fragment:
            slangStage = SLANG_STAGE_FRAGMENT;
            break;
        case ShaderStage::Compute:
            slangStage = SLANG_STAGE_COMPUTE;
            break;
        case ShaderStage::RTRayGen:
            slangStage = SLANG_STAGE_RAY_GENERATION;
            break;
        case ShaderStage::RTMiss:
            slangStage = SLANG_STAGE_MISS;
            break;
        case ShaderStage::RTClosestHit:
            slangStage = SLANG_STAGE_CLOSEST_HIT;
            break;
        case ShaderStage::RTAnyHit:
            slangStage = SLANG_STAGE_ANY_HIT;
            break;
        case ShaderStage::RTIntersection:
            slangStage = SLANG_STAGE_INTERSECTION;
            break;
        case ShaderStage::Task:
            slangStage = SLANG_STAGE_AMPLIFICATION;
            break;
        case ShaderStage::Mesh:
            slangStage = SLANG_STAGE_MESH;
            break;
        }

        // Compile each permutation

        for (ShaderCompileSpec::SymbolValuePairSet const& permutation : compileSpec->permutations) {

            ARKOSE_LOG(Info, "ShaderCompilerTool: compiling file+permutation {}/{}", ++compilationIdx, numCompilations);

            // Register macros

            std::vector<slang::PreprocessorMacroDesc> macros {};
            for (const auto& [symbol, value] : permutation) {
                macros.push_back({ symbol.c_str(), value.c_str() });
            }

            // Set compiler options

            std::vector<slang::CompilerOptionEntry> options {};

            auto addCapability = [&](char const* capability) {
                slang::CompilerOptionEntry& entry = options.emplace_back();
                entry.name = slang::CompilerOptionName::Capability;
                entry.value.kind = slang::CompilerOptionValueKind::String;
                entry.value.stringValue0 = capability;
            };

            // Enable default capabilities we rely on
            addCapability("vk_mem_model");
            addCapability("SPV_GOOGLE_user_type");
            addCapability("spvDerivativeControl");
            addCapability("spvImageQuery");
            addCapability("spvImageGatherExtended");
            addCapability("spvSparseResidency");
            addCapability("spvMinLod");
            addCapability("spvFragmentFullyCoveredEXT");

            // TODO: don't set max debug info for release builds
            slang::CompilerOptionEntry& debInfoEntry = options.emplace_back();
            debInfoEntry.name = slang::CompilerOptionName::DebugInformation;
            debInfoEntry.value.kind = slang::CompilerOptionValueKind::Int;
            debInfoEntry.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_MAXIMAL;

            //
            // Create session
            //

            slang::SessionDesc sessionDesc = {};

            sessionDesc.targets = targets.data();
            sessionDesc.targetCount = targets.size();

            sessionDesc.compilerOptionEntries = options.data();
            sessionDesc.compilerOptionEntryCount = (uint32_t)options.size();

            sessionDesc.preprocessorMacros = macros.data();
            sessionDesc.preprocessorMacroCount = (SlangInt)macros.size();

            sessionDesc.searchPaths = shaderSearchPaths.data();
            sessionDesc.searchPathCount = (SlangInt)shaderSearchPaths.size();

            sessionDesc.allowGLSLSyntax = true;

            slang::ISession* session;
            if (SLANG_FAILED(globalSession->createSession(sessionDesc, &session))) {
                ARKOSE_LOG(Error, "ShaderCompilerTool: Failed to create slang session!");
                return 1;
            }

            //
            // Create shader module
            //

            std::string moduleName = shaderFilePath.filename().generic_string();
            std::string modulePath = shaderFilePath.generic_string();

            slang::IModule* module = session->loadModuleFromSourceString(moduleName.c_str(), modulePath.c_str(), shaderFileText.value().c_str(), &diagnosticsBlob);

            if (diagnosticsBlob) {
                printDiagnostics(diagnosticsBlob, module == nullptr);
            }

            if (module == nullptr) {
                ARKOSE_LOG(Error, "ShaderCompilerTool: Failed to create slang module for file '{}'!", modulePath);
                return 1;
            }

            //
            // Find the entry point(s)
            //

            slang::IEntryPoint* entryPoint;
            if (SLANG_FAILED(module->findAndCheckEntryPoint("main", slangStage, &entryPoint, &diagnosticsBlob))) {
                printDiagnostics(diagnosticsBlob, entryPoint == nullptr);
                ARKOSE_LOG(Error, "ShaderCompilerTool: Failed to find entry point 'main' in module");
                return 1;
            }

            // No defined entry points, just using glsl style entry point so if we find it, it must be index 0
            size_t entryPointIdx = 0;

            //
            // Compose a program from the module + entry point
            //

            std::vector<slang::IComponentType*> componentTypes = { module, entryPoint };

            slang::IComponentType* composedProgram;
            if (SLANG_FAILED(session->createCompositeComponentType(componentTypes.data(), (SlangInt)componentTypes.size(), &composedProgram, &diagnosticsBlob))) {
                printDiagnostics(diagnosticsBlob, composedProgram == nullptr);
                return 1;
            }

            //
            // Link program
            //

            slang::IComponentType* linkedProgram;
            if (SLANG_FAILED(composedProgram->link(&linkedProgram, &diagnosticsBlob))) {
                printDiagnostics(diagnosticsBlob, linkedProgram == nullptr);
                return 1;
            }

            //
            // Write out code for each target
            //

            {
                slang::IBlob* spirvBlob;
                if (SLANG_FAILED(linkedProgram->getEntryPointCode(entryPointIdx, spirvTargetIdx, &spirvBlob, &diagnosticsBlob))) {
                    printDiagnostics(diagnosticsBlob, spirvBlob == nullptr);
                    return 1;
                }

                // TODO: Write to .spv file!
                void const* spirvData = spirvBlob->getBufferPointer();
                size_t spirvDataSize = spirvBlob->getBufferSize();
            }

            #if 0
            {
                slang::IBlob* dxilBlob;
                if (SLANG_FAILED(linkedProgram->getEntryPointCode(entryPointIdx, dxilTargetIdx, &dxilBlob, &diagnosticsBlob))) {
                    printDiagnostics(diagnosticsBlob, dxilBlob == nullptr);
                    return 1;
                }

                // TODO: Write to .dxil file!
                void const* dxilData = dxilBlob->getBufferPointer();
                size_t dxilDataSize = dxilBlob->getBufferSize();
            }
            #endif

        }
    }

    ARKOSE_LOG(Info, "ShaderCompilerTool: compilation done.");

    return toolReturnCode();
}
