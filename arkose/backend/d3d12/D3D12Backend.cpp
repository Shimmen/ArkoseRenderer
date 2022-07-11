#include "D3D12Backend.h"

#include "backend/d3d12/D3D12BindingSet.h"
#include "backend/d3d12/D3D12Buffer.h"
#include "backend/d3d12/D3D12ComputeState.h"
#include "backend/d3d12/D3D12RenderState.h"
#include "backend/d3d12/D3D12RenderTarget.h"
#include "backend/d3d12/D3D12Texture.h"
#include "core/Logging.h"
#include "rendering/AppState.h"
#include "utility/FileIO.h"

// The DirectX Compiler API
#include <dxcapi.h>

// Surface setup
#include <dxgi1_4.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

D3D12Backend::D3D12Backend(Badge<Backend>, GLFWwindow* window, const AppSpecification& appSpecification)
    : m_window(window)
{
    //
    // The basis of this implementation comes from here: https://gpuopen.com/learn/hellod3d12-directx-12-sdk-sample/
    //

    int windowFramebufferWidth, windowFramebufferHeight;
    glfwGetFramebufferSize(window, &windowFramebufferWidth, &windowFramebufferHeight);
    m_windowFramebufferExtent = { windowFramebufferWidth, windowFramebufferHeight };
    //glfwSetFramebufferSizeCallback(window, static_cast<GLFWframebuffersizefun>([](GLFWwindow* window, int width, int height) {
    //                                   m_windowFramebufferExtent = Extent2D(width, height);
    //                               }));

    if constexpr (d3d12debugMode) {
        ComPtr<ID3D12Debug> debugController;
        D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
        debugController->EnableDebugLayer();
    }

    // Enable "experimental" feature of shader model 6
    if (auto hr = D3D12EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModels, nullptr, 0); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not enable shader model 6 support, exiting.");
    }

    m_device = createDeviceAtMaxSupportedFeatureLevel();
    m_commandQueue = createDefaultCommandQueue();
    m_swapChain = createSwapChain(m_window, m_commandQueue.Get());

    /////////////////////////////////

    m_renderTargetViewDescriptorSize  = device().GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create a heap that can contains QueueSlotCount number of descriptors
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle {};
    {
        D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
        descriptorHeapDesc.NumDescriptors = QueueSlotCount;
        descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (auto hr = device().CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&m_renderTargetDescriptorHeap)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: failed to create descriptor heaps, exiting.");
        }

        rtvHandle = m_renderTargetDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    }

    // Set up frame contexts
    for (int i = 0; i < QueueSlotCount; ++i) {

        if (m_frameContexts[i] == nullptr)
            m_frameContexts[i] = std::make_unique<FrameContext>();
        FrameContext& frameContext = *m_frameContexts[i];

        // Create fences for each frame so we can protect resources and wait for any given frame
        {
            if (auto hr = device().CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frameContext.frameFence)); FAILED(hr)) {
                ARKOSE_LOG(Fatal, "D3D12Backend: failed to create frame fence, exiting.");
            }

            frameContext.frameFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            frameContext.frameFenceValue = 0;
        }

        // Get the render target for the respective target in the swap chain
        if (auto hr = swapChain().GetBuffer(i, IID_PPV_ARGS(&frameContext.renderTarget)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: failed to get buffer from swap chain, exiting.");
        }

        // Create a render target view for each target in the swap chain
        {
            D3D12_RENDER_TARGET_VIEW_DESC viewDesc;
            viewDesc.Format = SwapChainRenderTargetViewFormat;
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = 0;
            viewDesc.Texture2D.PlaneSlice = 0;

            device().CreateRenderTargetView(frameContext.renderTarget.Get(), &viewDesc, rtvHandle);
            rtvHandle.Offset(m_renderTargetViewDescriptorSize);
        }

        // Create command allocator and command list for the frame context
        {
            if (auto hr = device().CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frameContext.commandAllocator)); FAILED(hr)) {
                ARKOSE_LOG(Fatal, "D3D12Backend: failed to create command allocator, exiting.");
            }

            if (auto hr = device().CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frameContext.commandAllocator.Get(), nullptr, IID_PPV_ARGS(&frameContext.commandList)); FAILED(hr)) {
                ARKOSE_LOG(Fatal, "D3D12Backend: failed to create command list, exiting.");
            }
            
            frameContext.commandList->Close();
        }
    }

    /////////////////////////////////

    setUpDemo();
}

D3D12Backend::~D3D12Backend()
{
    // TODO!
}

void D3D12Backend::renderPipelineDidChange(RenderPipeline& pipeline)
{
}

void D3D12Backend::shadersDidRecompile(const std::vector<std::string>& shaderNames, RenderPipeline& pipeline)
{
}

void D3D12Backend::newFrame()
{
}

bool D3D12Backend::executeFrame(const Scene& scene, RenderPipeline& pipeline, float elapsedTime, float deltaTime)
{
    bool isRelativeFirstFrame = m_relativeFrameIndex < m_frameContexts.size();
    AppState appState { m_windowFramebufferExtent, deltaTime, elapsedTime, m_currentFrameIndex, isRelativeFirstFrame };

    uint32_t frameContextIndex = m_currentFrameIndex % m_frameContexts.size();
    FrameContext& frameContext = *m_frameContexts[frameContextIndex];

    // Can we not have separate frame context index from swapchain image index? Or am I just mixing up things?
    uint32_t backBufferIndex = frameContextIndex;

    {
        SCOPED_PROFILE_ZONE_BACKEND_NAMED("Waiting for fence");
        waitForFence(frameContext.frameFence.Get(), frameContext.frameFenceValue, frameContext.frameFenceEvent);
    }

    // Draw frame
    {
        ID3D12CommandAllocator* commandAllocator = frameContext.commandAllocator.Get();
        commandAllocator->Reset();

        ID3D12GraphicsCommandList* commandList = frameContext.commandList.Get();
        commandList->Reset(commandAllocator, nullptr);

        // Transition swapchain buffer to be a render target
        D3D12_RESOURCE_BARRIER presentToRenderTargetBarrier;
        presentToRenderTargetBarrier.Transition.pResource = frameContext.renderTarget.Get();
        presentToRenderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        presentToRenderTargetBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        presentToRenderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        presentToRenderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        presentToRenderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &presentToRenderTargetBarrier);

        D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle;
        CD3DX12_CPU_DESCRIPTOR_HANDLE::InitOffsetted(renderTargetHandle, m_renderTargetDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                     backBufferIndex, m_renderTargetViewDescriptorSize);

        // TODO: Replace with real drawing!
        renderDemo(renderTargetHandle, commandList);

        // Transition the swap chain back to present
        D3D12_RESOURCE_BARRIER renderTargetToPresentBarrier;
        renderTargetToPresentBarrier.Transition.pResource = frameContext.renderTarget.Get();
        renderTargetToPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        renderTargetToPresentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        renderTargetToPresentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        renderTargetToPresentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        renderTargetToPresentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &renderTargetToPresentBarrier);

        commandList->Close();

        // Execute our commands (i.e. submit)
        ID3D12CommandList* commandLists[] = { commandList };
        commandQueue().ExecuteCommandLists(std::extent<decltype(commandLists)>::value, commandLists);
    }

    // Present
    {
        UINT syncInterval = 1; // i.e. normal vsync
        UINT presentFlags = 0;
        swapChain().Present(syncInterval, presentFlags);

        // Mark the fence for the current frame
        frameContext.frameFenceValue = m_nextSequentialFenceValue++;
        commandQueue().Signal(frameContext.frameFence.Get(), frameContext.frameFenceValue);
    }

    m_currentFrameIndex += 1;
    m_relativeFrameIndex += 1;

    return true;
}

void D3D12Backend::completePendingOperations()
{
    waitForDeviceIdle();
}

void D3D12Backend::waitForFence(ID3D12Fence* fence, UINT64 completionValue, HANDLE waitEvent) const
{
    if (fence->GetCompletedValue() >= completionValue) {
        return;
    }

    if (auto hr = fence->SetEventOnCompletion(completionValue, waitEvent); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not attach event to fence value completion, exiting.");
    }

    if (WaitForSingleObject(waitEvent, INFINITE) != WAIT_OBJECT_0) {
        ARKOSE_LOG(Error, "D3D12Backend: failed waiting for event (for fence), exiting.");
    }
}

void D3D12Backend::waitForDeviceIdle()
{
    for (auto& frameContext : m_frameContexts) {

        ID3D12Fence* fence = frameContext->frameFence.Get();
        uint64_t fenceValueForSignal = ++frameContext->frameFenceValue;

        if (auto hr = commandQueue().Signal(fence, fenceValueForSignal); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: could not signal fence for a wait device idle call, exiting.");
        }

        waitForFence(fence, fenceValueForSignal, frameContext->frameFenceEvent);
    }
}

bool D3D12Backend::setBufferDataUsingMapping(ID3D12Resource& bufferResource, const uint8_t* data, size_t size, size_t offset)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    ARKOSE_ASSERT(bufferResource.GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER);
    ARKOSE_ASSERT(bufferResource.GetDesc().Width >= offset + size);

    if (size == 0) {
        return true;
    }

    void* mappedMemory = nullptr;

    if (auto hr = bufferResource.Map(0, nullptr, &mappedMemory); FAILED(hr)) {
        ARKOSE_LOG(Error, "D3D12Backend: could not map buffer resource.");
        return false;
    }

    uint8_t* destination = ((uint8_t*)mappedMemory) + offset;
    std::memcpy(destination, data, size);

    bufferResource.Unmap(0, nullptr);

    return true;
}

bool D3D12Backend::setBufferDataUsingStagingBuffer(D3D12Buffer& buffer, const uint8_t* data, size_t size, size_t offset)
{
    SCOPED_PROFILE_ZONE_BACKEND();

    ARKOSE_ASSERT(buffer.size() >= offset + size);

    if (size == 0) {
        return true;
    }

    const auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ComPtr<ID3D12Resource> uploadBuffer;
    auto hr = device().CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
                                               &uploadBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&uploadBuffer));
    if (FAILED(hr)) {
        ARKOSE_LOG(Error, "D3D12Backend: could create upload buffer.");
        return false;
    }

    if (!setBufferDataUsingMapping(*uploadBuffer.Get(), data, size, 0)) {
        ARKOSE_LOG(Error, "D3D12Backend: failed to set data to upload buffer.");
        return false;
    }

    // Make sure we reset back to this resource state when we're done
    auto baseResourceState = buffer.resourceState;
    auto idealCopyState = D3D12_RESOURCE_STATE_COPY_DEST;

    ID3D12Resource* bufferResource = buffer.bufferResource.Get();
    issueUploadCommand([&](ID3D12GraphicsCommandList& uploadCommandList) {

        if (baseResourceState != idealCopyState) {
            auto transitionBeforeCopy = CD3DX12_RESOURCE_BARRIER::Transition(bufferResource, baseResourceState, D3D12_RESOURCE_STATE_COPY_DEST);
            uploadCommandList.ResourceBarrier(1, &transitionBeforeCopy);
        }

        // Copy data from upload buffer on CPU into the buffer on the GPU
        uploadCommandList.CopyBufferRegion(bufferResource, offset,
                                           uploadBuffer.Get(), 0, size);

        if (baseResourceState != idealCopyState) {
            auto transitionAfterCopy = CD3DX12_RESOURCE_BARRIER::Transition(bufferResource, D3D12_RESOURCE_STATE_COPY_DEST, baseResourceState);
            uploadCommandList.ResourceBarrier(1, &transitionAfterCopy);
        }
    });

    return true;

}

void D3D12Backend::issueUploadCommand(const std::function<void(ID3D12GraphicsCommandList&)>& callback) const
{
    // "The texture and mesh data is uploaded using an upload heap. This happens during the initialization and shows how to transfer data to the GPU.
    //  Ideally, this should be running on the copy queue but for the sake of simplicity it is run on the general graphics queue."

    // Create our upload fence, command list and command allocator. This will be only used while creating the mesh buffer and the texture to upload data to the GPU.
    ComPtr<ID3D12Fence> uploadFence;
    if (auto hr = device().CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create fence for one-off upload command, exiting.");
    }

    ComPtr<ID3D12CommandAllocator> uploadCommandAllocator;
    if (auto hr = device().CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadCommandAllocator)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create command allocator for one-off upload command, exiting.");
    }

    ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
    if (auto hr = device().CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadCommandList)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create command list for one-off upload command, exiting.");
    }

    callback(*uploadCommandList.Get());

    uploadCommandList->Close();

    ID3D12CommandList* commandLists[] = { uploadCommandList.Get() };
    m_commandQueue->ExecuteCommandLists(std::extent<decltype(commandLists)>::value, commandLists);
    m_commandQueue->Signal(uploadFence.Get(), 1);

    auto waitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    ARKOSE_ASSERT(waitEvent != nullptr);

    waitForFence(uploadFence.Get(), 1, waitEvent);

    uploadCommandAllocator->Reset();
    CloseHandle(waitEvent);

}

ComPtr<ID3D12Device> D3D12Backend::createDeviceAtMaxSupportedFeatureLevel() const
{
    ComPtr<ID3D12Device> device;

    // Create device at the min-spec feature level for Arkose
    constexpr D3D_FEATURE_LEVEL minSpecFeatureLevel { D3D_FEATURE_LEVEL_12_0 };
    if (auto hr = D3D12CreateDevice(nullptr, minSpecFeatureLevel, IID_PPV_ARGS(&device)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create the device for feature level 12.0, exiting.");
    }
    
    D3D_FEATURE_LEVEL currentFeatureLevel = minSpecFeatureLevel;

    // If we now have a device, see if we can get a new one at higher feature level (we want the highest possible)

    std::array<D3D_FEATURE_LEVEL, 3> featureLevelsToQuery = { D3D_FEATURE_LEVEL_12_0,
                                                              D3D_FEATURE_LEVEL_12_1,
                                                              D3D_FEATURE_LEVEL_12_2 };

    D3D12_FEATURE_DATA_FEATURE_LEVELS query {};
    query.NumFeatureLevels = UINT(featureLevelsToQuery.size());
    query.pFeatureLevelsRequested = featureLevelsToQuery.data();
    if (auto hr = device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &query, sizeof(query)); SUCCEEDED(hr)) {
        if (currentFeatureLevel < query.MaxSupportedFeatureLevel) {
            if (auto hr = D3D12CreateDevice(nullptr, query.MaxSupportedFeatureLevel, IID_PPV_ARGS(&device)); FAILED(hr)) {
                ARKOSE_LOG(Fatal, "D3D12Backend: could not create the device at max feature level, exiting.");
            }
            currentFeatureLevel = query.MaxSupportedFeatureLevel;
        }
    } else {
        ARKOSE_LOG(Warning, "D3D12Backend: could not check feature support for the device, we'll just stick to 12.0.");
    }

    switch (currentFeatureLevel) {
    case D3D_FEATURE_LEVEL_12_0:
        ARKOSE_LOG(Info, "D3D12Backend: using device at feature level 12.0");
        break;
    case D3D_FEATURE_LEVEL_12_1:
        ARKOSE_LOG(Info, "D3D12Backend: using device at feature level 12.1");
        break;
    case D3D_FEATURE_LEVEL_12_2:
        ARKOSE_LOG(Info, "D3D12Backend: using device at feature level 12.2");
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    return device;
}

ComPtr<ID3D12CommandQueue> D3D12Backend::createDefaultCommandQueue() const
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0; // for GPU 0

    if (d3d12debugMode) {
        queueDesc.Flags |= D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;
    }

    ComPtr<ID3D12CommandQueue> commandQueue;
    if (auto hr = device().CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create the default command queue, exiting.");
    }

    return commandQueue;
}

ComPtr<IDXGISwapChain> D3D12Backend::createSwapChain(GLFWwindow* window, ID3D12CommandQueue* commandQueue) const
{
    ComPtr<IDXGIFactory4> dxgiFactory;
    if (auto hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create the DXGI factory, exiting.");
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc {};

    swapChainDesc.OutputWindow = glfwGetWin32Window(window);
    swapChainDesc.Windowed = glfwGetWindowMonitor(window) == nullptr;

    swapChainDesc.BufferDesc.Width = UINT(m_windowFramebufferExtent.width());
    swapChainDesc.BufferDesc.Height = UINT(m_windowFramebufferExtent.height());

    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    // No multisampling into the swap chain (if you want multisampling, just resolve before final target).
    swapChainDesc.SampleDesc.Count = 1;

    swapChainDesc.BufferCount = QueueSlotCount;
    swapChainDesc.BufferDesc.Format = SwapChainFormat; // TODO: Maybe query for best format instead?
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // TODO: Investigate the different ones

    ComPtr<IDXGISwapChain> swapChain;
    if (auto hr = dxgiFactory->CreateSwapChain(commandQueue, &swapChainDesc, &swapChain); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create swapchain, exiting.");
    }

    return swapChain;
}

std::unique_ptr<Buffer> D3D12Backend::createBuffer(size_t size, Buffer::Usage usage, Buffer::MemoryHint memoryHint)
{
    return std::make_unique<D3D12Buffer>(*this, size, usage, memoryHint);
}

std::unique_ptr<RenderTarget> D3D12Backend::createRenderTarget(std::vector<RenderTarget::Attachment> attachments)
{
    return std::make_unique<D3D12RenderTarget>(*this, attachments);
}

std::unique_ptr<Texture> D3D12Backend::createTexture(Texture::Description desc)
{
    return std::make_unique<D3D12Texture>(*this, desc);
}

std::unique_ptr<BindingSet> D3D12Backend::createBindingSet(std::vector<ShaderBinding> shaderBindings)
{
    return std::make_unique<D3D12BindingSet>(*this, shaderBindings);
}

std::unique_ptr<RenderState> D3D12Backend::createRenderState(const RenderTarget& renderTarget, const VertexLayout& vertexLayout,
                                                              const Shader& shader, const StateBindings& stateBindings,
                                                              const BlendState& blendState, const RasterState& rasterState, const DepthState& depthState, const StencilState& stencilState)
{
    return std::make_unique<D3D12RenderState>(*this, renderTarget, vertexLayout, shader, stateBindings, blendState, rasterState, depthState, stencilState);
}

std::unique_ptr<ComputeState> D3D12Backend::createComputeState(const Shader& shader, std::vector<BindingSet*> bindingSets)
{
    return std::make_unique<D3D12ComputeState>(*this, shader, bindingSets);
}

std::unique_ptr<BottomLevelAS> D3D12Backend::createBottomLevelAccelerationStructure(std::vector<RTGeometry> geometries)
{
    return nullptr;
}

std::unique_ptr<TopLevelAS> D3D12Backend::createTopLevelAccelerationStructure(uint32_t maxInstanceCount, std::vector<RTGeometryInstance> initialInstances)
{
    return nullptr;
}

std::unique_ptr<RayTracingState> D3D12Backend::createRayTracingState(ShaderBindingTable& sbt, const StateBindings& stateBindings, uint32_t maxRecursionDepth)
{
    return nullptr;
}

void D3D12Backend::setUpDemo()
{
    // Create root signature
    {
        // NOTE: We currently aren't using this root signature, or rather, it's a no-op right now.

        CD3DX12_ROOT_PARAMETER parameter;

        // Our constant buffer view
        parameter.InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

        // Create the root signature
        CD3DX12_ROOT_SIGNATURE_DESC descRootSignature;
        descRootSignature.Init(1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> rootBlob;
        ComPtr<ID3DBlob> errorBlob;
        if (auto hr = D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: failed to serialize demo root signature, exiting.");
        }

        if (auto hr = device().CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&m_demo.rootSignature)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: failed to create demo root signature, exiting.");
        }
    }

    // Create pipeline state object
    {
        static const D3D12_INPUT_ELEMENT_DESC layout[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                                                           { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };

        ComPtr<IDxcLibrary> library;
        if (auto hr = DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: failed to create dxc library, exiting.");
        }

        ComPtr<IDxcCompiler> compiler;
        if (auto hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: failed to create dxc compiler, exiting.");
        }

        auto compileHlslFile = [&](const wchar_t* filePath, const wchar_t* entryPoint, const wchar_t* shaderModel, std::vector<DxcDefine> defines = {}) -> ComPtr<IDxcBlob> {
            // TODO: Probably use library->CreateBlobWithEncodingFromPinned(..) to create from text instead of a file

            // NOTE: This code will produced unsigned binaries which will generate D3D12 warnings in the output log. There are fixes to this,
            //       but it's a bit complex for this little test funciton I have right now. When we want to add proper shader compilation, and
            //       probably also go through HLSL->DXIL->runtime, we should implement this fully. Here are some useful links:
            //       https://github.com/microsoft/DirectXShaderCompiler/issues/2550
            //       https://www.wihlidal.com/blog/pipeline/2018-09-16-dxil-signing-post-compile/
            //       https://github.com/gwihlidal/dxil-signing

            // Always just assume UTF-8 for the input file
            uint32_t codePage = CP_UTF8;

            ComPtr<IDxcBlobEncoding> sourceBlob;
            if (auto hr = library->CreateBlobFromFile(filePath, &codePage, &sourceBlob); FAILED(hr)) {
                ARKOSE_LOG(Fatal, "D3D12Backend: failed to create source blob for shader, exiting.");
            }

            ComPtr<IDxcOperationResult> compilationResult;
            auto hr = compiler->Compile(sourceBlob.Get(), filePath,
                                        entryPoint, shaderModel,
                                        nullptr, 0,
                                        defines.data(), UINT32(defines.size()),
                                        nullptr, // generated HLSL code, no includes needed
                                        &compilationResult);

            if (SUCCEEDED(hr)) {
                compilationResult->GetStatus(&hr);
            }

            if (FAILED(hr)) {
                if (compilationResult) {
                    ComPtr<IDxcBlobEncoding> errorsBlob;
                    hr = compilationResult->GetErrorBuffer(&errorsBlob);
                    if (SUCCEEDED(hr) && errorsBlob) {
                        auto* errorMessage = reinterpret_cast<const char*>(errorsBlob->GetBufferPointer());
                        ARKOSE_LOG(Fatal, "D3D12Backend: failed to compile generated HLSL: {}", errorMessage);
                    }
                }
            } else {
                ComPtr<IDxcBlob> compiledCode;
                if (auto hr = compilationResult->GetResult(&compiledCode); FAILED(hr)) {
                    ARKOSE_LOG(Fatal, "D3D12Backend: failed to get dxc compilation results, exiting.");
                }

                return compiledCode;
            }

            return nullptr;
        };

        const wchar_t* hlslSourceName = L"shaders/d3d12-bootstrap/demo.hlsl";
        DxcDefine define { .Name = L"D3D12_SAMPLE_BASIC", .Value = L"1" };
        ComPtr<IDxcBlob> vertexCode = compileHlslFile(hlslSourceName, L"VS_main", L"vs_6_0", { define });
        ComPtr<IDxcBlob> pixelCode = compileHlslFile(hlslSourceName, L"PS_main", L"ps_6_0", { define });

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

        psoDesc.VS.BytecodeLength = vertexCode->GetBufferSize();
        psoDesc.VS.pShaderBytecode = vertexCode->GetBufferPointer();

        psoDesc.PS.BytecodeLength = pixelCode->GetBufferSize();
        psoDesc.PS.pShaderBytecode = pixelCode->GetBufferPointer();

        psoDesc.pRootSignature = m_demo.rootSignature.Get();

        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = SwapChainRenderTargetViewFormat;
        psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

        psoDesc.InputLayout.NumElements = std::extent<decltype(layout)>::value;
        psoDesc.InputLayout.pInputElementDescs = layout;

        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

        // Simple alpha blending
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
        psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        psoDesc.SampleDesc.Count = 1; // no multisampling

        psoDesc.DepthStencilState.DepthEnable = false;
        psoDesc.DepthStencilState.StencilEnable = false;

        psoDesc.SampleMask = 0xFFFFFFFF;

        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        if (auto hr = device().CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_demo.pso)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: failed to create demo graphics pipeline state, exiting.");
        }
    }

    // Create mesh buffers
    {
        struct Vertex {
            float position[3];
            float uv[2];
        };

        const Vertex vertices[4] = {
            // Upper Left
            { { -0.5f, 0.5f, 0 }, { 0, 0 } },
            // Upper Right
            { { 0.5f, 0.5f, 0 }, { 1, 0 } },
            // Bottom right
            { { 0.5f, -0.5f, 0 }, { 1, 1 } },
            // Bottom left
            { { -0.5f, -0.5f, 0 }, { 0, 1 } }
        };

        const int indices[6] = {
            0, 1, 2, 2, 3, 0
        };

        static const int uploadBufferSize = sizeof(vertices) + sizeof(indices);
        auto uploadBuffer = std::make_unique<D3D12Buffer>(*this, uploadBufferSize, Buffer::Usage::Transfer, Buffer::MemoryHint::TransferOptimal);

        m_demo.vertexBuffer = std::make_unique<D3D12Buffer>(*this, sizeof(vertices), Buffer::Usage::Vertex, Buffer::MemoryHint::GpuOnly);
        m_demo.indexBuffer = std::make_unique<D3D12Buffer>(*this, sizeof(indices), Buffer::Usage::Index, Buffer::MemoryHint::GpuOnly);

        setBufferDataUsingStagingBuffer(*m_demo.vertexBuffer, reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices));
        setBufferDataUsingStagingBuffer(*m_demo.indexBuffer, reinterpret_cast<const uint8_t*>(indices), sizeof(indices));

        // Create buffer views
        m_demo.vertexBufferView.BufferLocation = m_demo.vertexBuffer->bufferResource->GetGPUVirtualAddress();
        m_demo.vertexBufferView.SizeInBytes = UINT(m_demo.vertexBuffer->size());
        m_demo.vertexBufferView.StrideInBytes = sizeof(Vertex);

        m_demo.indexBufferView.BufferLocation = m_demo.indexBuffer->bufferResource->GetGPUVirtualAddress();
        m_demo.indexBufferView.SizeInBytes = UINT(m_demo.indexBuffer->size());
        m_demo.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    }
}

void D3D12Backend::renderDemo(D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle, ID3D12GraphicsCommandList* commandList)
{
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f,
                                static_cast<float>(m_windowFramebufferExtent.width()),
                                static_cast<float>(m_windowFramebufferExtent.height()),
                                0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, LONG(m_windowFramebufferExtent.width()), LONG(m_windowFramebufferExtent.height()) };

    commandList->OMSetRenderTargets(1, &renderTargetHandle, true, nullptr);
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    static const float clearColor[] = { 0.042f, 0.042f, 0.042f, 1.0f };
    commandList->ClearRenderTargetView(renderTargetHandle, clearColor, 0, nullptr);

    // Set our state (shaders, etc.)
    commandList->SetPipelineState(m_demo.pso.Get());

    // Set our root signature
    commandList->SetGraphicsRootSignature(m_demo.rootSignature.Get());

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &m_demo.vertexBufferView);
    commandList->IASetIndexBuffer(&m_demo.indexBufferView);
    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}
