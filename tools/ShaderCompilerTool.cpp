#include <utility/ToolUtilities.h>

#include "slang.h"

void printDiagnostics(slang::IBlob* diagnosticsBlob)
{
    if (diagnosticsBlob) {
        size_t messageSize = diagnosticsBlob->getBufferSize();
        char const* message = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
        if (messageSize > 0 && message) {
            std::string diagString { message, messageSize };
            ARKOSE_LOG(Error, "ShaderCompilerTool: slang diagnostics: {}", diagString);
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        // TODO: Add support for named command line arguments!
        // ARKOSE_LOG(Error, "ShaderCompilerTool: not enough arguments!");
        // return 1;
    }

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

    //
    // Create session
    //

    std::vector<slang::TargetDesc> targets;
    std::vector<slang::PreprocessorMacroDesc> macros {};
    std::vector<slang::CompilerOptionEntry> options {};

    // Register targets

    size_t spirvTargetIdx = targets.size();
    slang::TargetDesc& spirvTargetDesc = targets.emplace_back();
    spirvTargetDesc.format = SLANG_SPIRV;
    spirvTargetDesc.profile = globalSession->findProfile("spirv_1_5");

    size_t dxilTargetIdx = targets.size();
    slang::TargetDesc& dxiTargetDesc = targets.emplace_back();
    dxiTargetDesc.format = SLANG_DXIL;
    dxiTargetDesc.profile = globalSession->findProfile("sm_6_6");

    // Register macros

    // todo!
    macros.push_back({ "ARKOSE", "1" });

    // Set compiler options

    // todo!

    slang::SessionDesc sessionDesc = {};

    sessionDesc.targets = targets.data();
    sessionDesc.targetCount = targets.size();

    sessionDesc.compilerOptionEntries = options.data();
    sessionDesc.compilerOptionEntryCount = (uint32_t)options.size();

    sessionDesc.preprocessorMacros = macros.data();
    sessionDesc.preprocessorMacroCount = (SlangInt)macros.size();

    // TODO: Set this up correctly!
    const char* searchPaths[] = { "./shaders/" };
    sessionDesc.searchPaths = searchPaths;
    sessionDesc.searchPathCount = 1;

    sessionDesc.allowGLSLSyntax = true;

    slang::ISession* session;
    if (SLANG_FAILED(globalSession->createSession(sessionDesc, &session))) {
        ARKOSE_LOG(Error, "ShaderCompilerTool: Failed to create slang session!");
        return 1;
    }

    //
    // Create shader module
    //

    // TODO: Load from file!
    char const* moduleName = "TestVertexShaderModule";
    char const* modulePath = "TestVertexShaderModule.glsl";
    std::string source = R"(
        #version 460
        layout(location = 0) in vec3 inPosition;
        layout(location = 0) out vec3 outColor;
        void main() {
            gl_Position = vec4(inPosition, 1.0);
            outColor = vec3(1.0, 0.0, 1.0);
        }
    )";

    slang::IModule* module = session->loadModuleFromSourceString(moduleName, modulePath, source.c_str(), &diagnosticsBlob);

    if (diagnosticsBlob) {
        printDiagnostics(diagnosticsBlob);
    }

    if (module == nullptr) {
        ARKOSE_LOG(Error, "ShaderCompilerTool: Failed to create slang module!");
        return 1;
    }

    //
    // Find the entry point(s)
    //

    slang::IEntryPoint* entryPoint;
    if (SLANG_FAILED(module->findAndCheckEntryPoint("main", SLANG_STAGE_VERTEX, &entryPoint, &diagnosticsBlob))) {
        printDiagnostics(diagnosticsBlob);
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
        printDiagnostics(diagnosticsBlob);
        return 1;
    }

    //
    // Link program
    //

    slang::IComponentType* linkedProgram;
    if (SLANG_FAILED(composedProgram->link(&linkedProgram, &diagnosticsBlob))) {
        printDiagnostics(diagnosticsBlob);
        return 1;
    }

    //
    // Write out code for each target
    //

    {
        slang::IBlob* spirvBlob;
        if (SLANG_FAILED(linkedProgram->getEntryPointCode(entryPointIdx, spirvTargetIdx, &spirvBlob, &diagnosticsBlob))) {
            printDiagnostics(diagnosticsBlob);
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
            printDiagnostics(diagnosticsBlob);
            return 1;
        }

        // TODO: Write to .dxil file!
        void const* dxilData = dxilBlob->getBufferPointer();
        size_t dxilDataSize = dxilBlob->getBufferSize();
    }
    #endif

    return toolReturnCode();
}
