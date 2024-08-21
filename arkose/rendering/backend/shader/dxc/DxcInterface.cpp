#include "DxcInterface.h"

#include "rendering/backend/d3d12/D3D12Common.h"
#include "utility/FileIO.h"
#include <dxcapi.h>
#include <unordered_set>

static wchar_t const* entryPointNameForShaderFile(ShaderFile const& shaderFile)
{
    switch (shaderFile.shaderStage()) {
    case ShaderStage::Vertex:
        return L"VS_main";
    case ShaderStage::Fragment:
        return L"PS_main";
    case ShaderStage::Compute:
        return L"CS_main";
    case ShaderStage::RTRayGen:
        return L"RAYGEN_main";
    case ShaderStage::RTClosestHit:
        return L"CLOSESTHIT_main";
    case ShaderStage::RTAnyHit:
        return L"ANYHIT_main";
    case ShaderStage::RTIntersection:
        return L"INTERSECTION_main";
    case ShaderStage::RTMiss:
        return L"MISS_main";
    case ShaderStage::Task:
        return L"TASK_main";
    case ShaderStage::Mesh:
        return L"MESH_main";
    case ShaderStage::Unknown:
        ARKOSE_LOG(Warning, "Can't find entry point name for for shader file of unknown type ('{}'), defaulting to 'main'", shaderFile.path());
        return L"main";
    default:
        ASSERT_NOT_REACHED();
    }
}

static wchar_t const* shaderModelForShaderFile(ShaderFile const& shaderFile)
{
    switch (shaderFile.shaderStage()) {
    case ShaderStage::Vertex:
        return L"vs_6_6";
    case ShaderStage::Fragment:
        return L"ps_6_6";
    case ShaderStage::Compute:
        return L"cs_6_6";
    case ShaderStage::RTRayGen:
        return L"raygeneration_6_6";
    case ShaderStage::RTClosestHit:
        return L"closesthit_6_6";
    case ShaderStage::RTAnyHit:
        return L"anyhit_6_6";
    case ShaderStage::RTIntersection:
        return L"intersection_6_6";
    case ShaderStage::RTMiss:
        return L"miss_6_6";
    case ShaderStage::Task:
        return L"as_6_6";
    case ShaderStage::Mesh:
        return L"ms_6_6";
    case ShaderStage::Unknown:
        ARKOSE_LOG(Fatal, "Can't find shader model for for shader file of unknown type ('{}'), exiting", shaderFile.path());
    default:
        ASSERT_NOT_REACHED();
    }
}

class DxcResult final : public CompilationResult<u8> {
public:
    explicit DxcResult(ComPtr<IDxcBlob>&& compiledCode, std::vector<std::string>&& includedFiles, std::string errorMessage)
        : m_compiledCode(std::move(compiledCode))
        , m_errorMessage(std::move(errorMessage))
        , m_includedFiles(std::move(includedFiles))
    {
    }

    virtual bool success() const override
    {
        return m_errorMessage.empty();
    }

    virtual std::string errorMessage() const override
    {
        return m_errorMessage;
    }

    virtual std::vector<std::string> const& includedFiles() const override
    {
        return m_includedFiles;
    }

    virtual const_iterator begin() const override
    {
        return static_cast<u8 const*>(m_compiledCode->GetBufferPointer());
    }

    virtual const_iterator end() const override
    {
        return static_cast<u8 const*>(m_compiledCode->GetBufferPointer()) + m_compiledCode->GetBufferSize();
    }

private:
    ComPtr<IDxcBlob> m_compiledCode;
    std::vector<std::string> m_includedFiles;
    std::string m_errorMessage;
};

class ArkoseDxcIncludeHandler : public IDxcIncludeHandler {
public:
    ArkoseDxcIncludeHandler(ComPtr<IDxcLibrary> library)
        : m_library(library)
    {
    }

    HRESULT STDMETHODCALLTYPE LoadSource(_In_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource) override
    {
        // TODO: Make this work properly!

        uint32_t codePage = CP_UTF8;
        ComPtr<IDxcBlobEncoding> pEncoding;
        HRESULT hr = m_library->CreateBlobFromFile(pFilename, &codePage, pEncoding.GetAddressOf());

        if (SUCCEEDED(hr)) {

            std::string filenameUtf8 = convertFromWideString(pFilename);
            m_includedFiles.insert(filenameUtf8);

            *ppIncludeSource = pEncoding.Detach();
        } else {
            // TODO: Handle include errors slightly more gracefully
            ARKOSE_LOG(Error, "DxcIncluder: could not find file '{}', exiting", convertFromWideString(pFilename));
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override
    {
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }

    std::vector<std::string> includedFiles() const
    {
        return std::vector<std::string>(m_includedFiles.begin(), m_includedFiles.end());
    }

private:
    ComPtr<IDxcLibrary> m_library;
    std::unordered_set<std::string> m_includedFiles {};
};

std::unique_ptr<CompilationResult<u8>> DxcInterface::compileShader(ShaderFile const& shaderFile, std::string_view resolvedFilePath)
{
    // Useful info https://simoncoenen.com/blog/programming/graphics/DxcCompiling

    // TODO: Probably don't do this for every file we compile?
    ComPtr<IDxcLibrary> library;
    if (auto hr = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "DxcInterface: failed to create dxc library, exiting.");
    }

    // TODO: Probably don't do this for every file we compile?
    auto includeHandler = ComPtr<ArkoseDxcIncludeHandler>(new ArkoseDxcIncludeHandler(library));
    //library->CreateIncludeHandler(&includeHandler);

    // TODO: Probably don't do this for every file we compile?
    ComPtr<IDxcCompiler> compiler;
    if (auto hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "DxcInterface: failed to create dxc compiler, exiting.");
    }

    // NOTE: This code will produced unsigned binaries which will generate D3D12 warnings in the output log. There are fixes to this,
    //       but it's a bit complex for this little test sample I have right now. When we want to add proper shader compilation, and
    //       probably also go through HLSL->DXIL->runtime, we should implement this fully. Here are some useful links:
    //       https://github.com/microsoft/DirectXShaderCompiler/issues/2550
    //       https://www.wihlidal.com/blog/pipeline/2018-09-16-dxil-signing-post-compile/
    //       https://github.com/gwihlidal/dxil-signing

    // Always just assume UTF-8 for the input file
    uint32_t codePage = CP_UTF8;

    ComPtr<IDxcBlobEncoding> sourceBlob;
    // TODO: Probably use library->CreateBlobWithEncodingFromPinned(..) to create from text instead of a file
    if (auto hr = library->CreateBlobFromFile(convertToWideString(resolvedFilePath).c_str(), &codePage, &sourceBlob); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "DxcInterface: failed to create source blob for shader, exiting.");
    }

    // Collect macro definitions
    std::vector<std::wstring> wideStrings;
    std::vector<DxcDefine> dxcDefines;
    for (ShaderDefine const& define : shaderFile.defines()) {

        DxcDefine dxcDefine;

        wideStrings.emplace_back(convertToWideString(define.symbol));
        dxcDefine.Name = wideStrings.back().c_str();

        if (define.value.has_value()) {
            wideStrings.emplace_back(convertToWideString(define.value.value()));
            dxcDefine.Value = wideStrings.back().c_str();
        } else {
            dxcDefine.Value = L"1";
        }

        dxcDefines.push_back(dxcDefine);
    }

    wchar_t const* entryPointName = ::entryPointNameForShaderFile(shaderFile);
    wchar_t const* shaderModel = shaderModelForShaderFile(shaderFile);

    ComPtr<IDxcOperationResult> compilationResult;
    auto hr = compiler->Compile(sourceBlob.Get(), convertToWideString(resolvedFilePath).c_str(),
                                entryPointName, shaderModel,
                                nullptr, 0,
                                dxcDefines.data(), narrow_cast<u32>(dxcDefines.size()),
                                includeHandler.Get(),
                                &compilationResult);

    if (SUCCEEDED(hr)) {
        compilationResult->GetStatus(&hr);
    }

    if (FAILED(hr)) {
        char const* errorMessage = nullptr;

        if (compilationResult) {
            ComPtr<IDxcBlobEncoding> errorsBlob;
            hr = compilationResult->GetErrorBuffer(&errorsBlob);
            if (SUCCEEDED(hr) && errorsBlob) {
                errorMessage = reinterpret_cast<const char*>(errorsBlob->GetBufferPointer());
            }
        }

        return std::make_unique<DxcResult>(nullptr, std::vector<std::string>(), errorMessage);

    } else {
        ComPtr<IDxcBlob> compiledCode;
        if (auto hr = compilationResult->GetResult(&compiledCode); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "DxcInterface: failed to get dxc compilation results, exiting.");
        }

        return std::make_unique<DxcResult>(std::move(compiledCode), includeHandler->includedFiles(), "");
    }

    ASSERT_NOT_REACHED();
}

std::string DxcInterface::entryPointNameForShaderFile(ShaderFile const& shaderFile)
{
    const wchar_t* entryPointWideStr = ::entryPointNameForShaderFile(shaderFile);
    return convertFromWideString(entryPointWideStr);
}
