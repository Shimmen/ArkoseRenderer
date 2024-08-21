#include "D3D12Backend.h"

#include "rendering/backend/d3d12/D3D12BindingSet.h"
#include "rendering/backend/d3d12/D3D12Buffer.h"
#include "rendering/backend/d3d12/D3D12CommandList.h"
#include "rendering/backend/d3d12/D3D12ComputeState.h"
#include "rendering/backend/d3d12/D3D12DescriptorHeapAllocator.h"
#include "rendering/backend/d3d12/D3D12RenderState.h"
#include "rendering/backend/d3d12/D3D12RenderTarget.h"
#include "rendering/backend/d3d12/D3D12Texture.h"
#include "core/Logging.h"
#include "rendering/AppState.h"
#include "rendering/Registry.h"
#include "rendering/RenderPipeline.h"
#include "system/System.h"
#include "utility/FileIO.h"
#include <backends/imgui_impl_dx12.h>

// D3D12 "helper" library
#include <d3dx12/d3dx12.h>

// Surface setup
#include <dxgi1_6.h>
#include <dxgidebug.h>

// DirectX Agility SDK setup
// See https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/ for more info
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = ARKOSE_AGILITY_SDK_VERSION; }
extern "C" { __declspec(dllexport) extern const char8_t* D3D12SDKPath = u8".\\D3D12\\"; }

#if defined(TRACY_ENABLE)
#define SCOPED_PROFILE_ZONE_GPU(commandList, nameLiteral) TracyD3D12Zone(m_tracyD3D12Context, commandList, nameLiteral);
#define SCOPED_PROFILE_ZONE_GPU_DYNAMIC(commandList, nameString) TracyD3D12ZoneTransient(m_tracyD3D12Context, TracyConcat(ScopedProfileZone, nameString), commandList, nameString.c_str(), nameString.size());
#else
#define SCOPED_PROFILE_ZONE_GPU(commandList, nameLiteral)
#define SCOPED_PROFILE_ZONE_GPU_DYNAMIC(commandList, nameString)
#endif

static void d3d12DebugMessagCallback(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context)
{
    switch (severity) {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    case D3D12_MESSAGE_SEVERITY_ERROR:
        ARKOSE_LOG(Error, "D3D12 debug message: {}", description);
        break;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        ARKOSE_LOG(Warning, "D3D12 debug message: {}", description);
        break;
    case D3D12_MESSAGE_SEVERITY_INFO:
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
        ARKOSE_LOG(Info, "D3D12 debug message: {}", description);
        break;
    }
}

D3D12Backend::D3D12Backend(Badge<Backend>, const AppSpecification& appSpecification)
{
    //
    // The basis of this implementation comes from here: https://gpuopen.com/learn/hellod3d12-directx-12-sdk-sample/
    //

    m_windowFramebufferExtent = System::get().windowFramebufferSize();

    /////////////////////////////////

    if constexpr (d3d12debugMode) {
        ComPtr<ID3D12Debug1> debugController;
        D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
        debugController->EnableDebugLayer();
        debugController->SetEnableSynchronizedCommandQueueValidation(true);
        //debugController->SetEnableGPUBasedValidation(true); // NOTE: Enabling this seems to break rendering?
    }

    // Enable "experimental" feature of shader model 6
    if (auto hr = D3D12EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModels, nullptr, 0); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not enable shader model 6 support, exiting.");
    }

    /////////////////////////////////

    // Pick the best adapter (physical device) to use

    u32 dxgiFlags = 0;
    if constexpr (d3d12debugMode) {
        dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }

    ComPtr<IDXGIFactory1> dxgiFactory;
    if (auto hr = CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&dxgiFactory)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create DXGI factory, exiting.");
    }

    ComPtr<IDXGIFactory6> dxgiFactory6;
    if (auto hr = dxgiFactory.As(&dxgiFactory6); SUCCEEDED(hr)) {
        dxgiFactory6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&m_dxgiAdapter));
    } else {
        // just pick the first one in the list (can be improved..)
        dxgiFactory->EnumAdapters1(0, &m_dxgiAdapter);
    }
    
    DXGI_ADAPTER_DESC1 adapterDesc;
    if (auto hr = m_dxgiAdapter->GetDesc1(&adapterDesc); SUCCEEDED(hr)) {
        ARKOSE_LOG(Info, "D3D12Backend: using adapter '{}'", convertFromWideString(adapterDesc.Description));
    }

    // Create the device

    m_device = createDeviceAtMaxSupportedFeatureLevel();

    if constexpr (d3d12debugMode) {
        if (auto hr = m_device->QueryInterface(IID_PPV_ARGS(&m_debugDevice)); FAILED(hr)) {
            ARKOSE_LOG(Warning, "D3D12Backend: failed to create debug device.");
        }

        if (auto hr = m_device->QueryInterface(IID_PPV_ARGS(&m_infoQueue)); SUCCEEDED(hr)) {
            ComPtr<ID3D12InfoQueue1> infoQueue1;
            if (hr = m_infoQueue.As<ID3D12InfoQueue1>(&infoQueue1); SUCCEEDED(hr)) {
                infoQueue1->RegisterMessageCallback(d3d12DebugMessagCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, nullptr);
            } else {
                ARKOSE_LOG(Warning, "D3D12Backend: failed to register message callback.");
            }
        } else {
            ARKOSE_LOG(Warning, "D3D12Backend: failed to create info queue.");
        }

        // Can reduce overall performance, but it will give us a stable clock & consistent measurements
        device().SetStablePowerState(true);
    }

    /////////////////////////////////

    D3D12MA::ALLOCATOR_DESC allocatorDesc {};
    allocatorDesc.pAdapter = m_dxgiAdapter.Get();
    allocatorDesc.pDevice = m_device.Get();

    allocatorDesc.PreferredBlockSize = 0; // use default size
    allocatorDesc.pAllocationCallbacks = nullptr;

    allocatorDesc.Flags = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED | D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;

    if (auto hr = D3D12MA::CreateAllocator(&allocatorDesc, m_memoryAllocator.GetAddressOf()); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create memory allocator, exiting.");
    }

    /////////////////////////////////

    // Create global descriptor heaps & allocators for them
    m_copyableDescriptorHeapAllocator = std::make_unique<D3D12DescriptorHeapAllocator>(device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false, 100'000);
    m_shaderVisibleDescriptorHeapAllocator = std::make_unique<D3D12DescriptorHeapAllocator>(device(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, 100'000);

    m_samplerDescriptorHeapAllocator = std::make_unique<D3D12DescriptorHeapAllocator>(device(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true, 2048);

    m_commandQueue = createDefaultCommandQueue();
    m_swapChain = createSwapChain(m_commandQueue.Get());
    createWindowRenderTarget();

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

            if (d3d12debugMode) {
                std::string commandListDebugName = fmt::format("FrameContext{}CommandList", i);
                frameContext.commandList->SetName(convertToWideString(commandListDebugName).data());
            }

            frameContext.commandList->Close();
        }

        // Create upload buffer
        {
            static constexpr size_t registryUploadBufferSize = 32 * 1024 * 1024;
            frameContext.uploadBuffer = std::make_unique<UploadBuffer>(*this, registryUploadBufferSize);
        }
    }

    // Setup Dear ImGui
    {
        // No need to ever move this descriptor so might as well put it directly into the shader visible heap
        D3D12DescriptorAllocation fontDescriptor = shaderVisibleDescriptorHeapAllocator().allocate(1);
        ImGui_ImplDX12_Init(&device(), QueueSlotCount, SwapChainRenderTargetViewFormat,
                            shaderVisibleDescriptorHeapAllocator().heap(),
                            fontDescriptor.firstCpuDescriptor,
                            fontDescriptor.firstGpuDescriptor);
        ImGui_ImplDX12_CreateDeviceObjects();
    }

    #if defined(TRACY_ENABLE)
    m_tracyD3D12Context = TracyD3D12Context(&device(), m_commandQueue.Get());
    #endif
}

D3D12Backend::~D3D12Backend()
{
    // Before destroying stuff, make sure we're done with all scheduled work
    completePendingOperations();

    m_pipelineRegistry.reset();

    m_swapchainDepthTexture.reset();
    for (auto& frameContext : m_frameContexts) {
        frameContext.reset();
    }

    ImGui_ImplDX12_Shutdown();

    #if defined(TRACY_ENABLE)
    TracyD3D12Destroy(m_tracyD3D12Context);
    #endif

    m_memoryAllocator.Reset();
}

void D3D12Backend::renderPipelineDidChange(RenderPipeline& renderPipeline)
{
    // TODO: Probably move this function out of the backend specific stuf
    //reconstructRenderPipelineResources(renderPipeline);

    SCOPED_PROFILE_ZONE_BACKEND();

    size_t numFrameManagers = m_frameContexts.size();
    ARKOSE_ASSERT(numFrameManagers == QueueSlotCount);

    Registry* previousRegistry = m_pipelineRegistry.get();
    Registry* registry = new Registry(*this, *m_mockWindowRenderTarget, previousRegistry);

    renderPipeline.constructAll(*registry);

    m_pipelineRegistry.reset(registry);

    m_relativeFrameIndex = 0;
}

void D3D12Backend::shadersDidRecompile(const std::vector<std::string>& shaderNames, RenderPipeline& renderPipeline)
{
    if (shaderNames.size() > 0) {
        renderPipelineDidChange(renderPipeline);
    }
}

void D3D12Backend::newFrame()
{
    ImGui_ImplDX12_NewFrame();
}

bool D3D12Backend::executeFrame(RenderPipeline& renderPipeline, float elapsedTime, float deltaTime)
{
    bool isRelativeFirstFrame = m_relativeFrameIndex < m_frameContexts.size();
    AppState appState { m_windowFramebufferExtent, deltaTime, elapsedTime, m_currentFrameIndex, isRelativeFirstFrame };

    uint32_t frameContextIndex = m_nextSwapchainBufferIndex % m_frameContexts.size();
    FrameContext& frameContext = *m_frameContexts[frameContextIndex];

    // Can we not have separate frame context index from swapchain image index? Or am I just mixing up things?
    uint32_t backBufferIndex = frameContextIndex;

    {
        SCOPED_PROFILE_ZONE_BACKEND_NAMED("Waiting for fence");
        waitForFence(frameContext.frameFence.Get(), frameContext.frameFenceValue, frameContext.frameFenceEvent);
    }

    // NOTE: We're ignoring any time spent waiting for the fence, as that would factor e.g. GPU time & sync into the CPU time
    double cpuFrameStartTime = System::get().timeSinceStartup();

    // Draw frame
    {
        ID3D12CommandAllocator* commandAllocator = frameContext.commandAllocator.Get();
        commandAllocator->Reset();

        ID3D12GraphicsCommandList* commandList = frameContext.commandList.Get();
        commandList->Reset(commandAllocator, nullptr);

        // Bind the global CBV/SRV/UAV descriptor heap as it will be used for all shader data bindings
        ID3D12DescriptorHeap* globalCbvSrvUavDescriptorHeap = shaderVisibleDescriptorHeapAllocator().heap();
        commandList->SetDescriptorHeaps(1, &globalCbvSrvUavDescriptorHeap);

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

        // Assign the render target handle of the current swapchain image to the mock window render target
        m_mockWindowRenderTarget->colorRenderTargetHandles[0] = renderTargetHandle;

        UploadBuffer& uploadBuffer = *frameContext.uploadBuffer;
        uploadBuffer.reset();

        Registry& registry = *m_pipelineRegistry;
        D3D12CommandList cmdList { *this, commandList };

        {
            SCOPED_PROFILE_ZONE_GPU(commandList, "Render Pipeline");
            renderPipeline.forEachNodeInResolvedOrder(registry, [&](RenderPipelineNode& node, const RenderPipelineNode::ExecuteCallback& nodeExecuteCallback) {
                std::string nodeName = node.name();

                SCOPED_PROFILE_ZONE_DYNAMIC(nodeName, 0x00ffff);
                double cpuStartTime = System::get().timeSinceStartup();

                // NOTE: This works assuming we never modify the list of nodes (add/remove/reorder)
                //uint32_t nodeStartTimestampIdx = nextTimestampQueryIdx++;
                //uint32_t nodeEndTimestampIdx = nextTimestampQueryIdx++;
                //node.timer().reportGpuTime(elapsedSecondsBetweenTimestamps(nodeStartTimestampIdx, nodeEndTimestampIdx));

                cmdList.beginDebugLabel(nodeName);
                //vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frameContext.timestampQueryPool, nodeStartTimestampIdx);

                nodeExecuteCallback(appState, cmdList, uploadBuffer);
                //cmdList.endNode({}); // ??

                //vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frameContext.timestampQueryPool, nodeEndTimestampIdx);
                cmdList.endDebugLabel();

                double cpuElapsed = System::get().timeSinceStartup() - cpuStartTime;
                node.timer().reportCpuTime(cpuElapsed);
            });
        }

        cmdList.beginDebugLabel("GUI");
        {
            SCOPED_PROFILE_ZONE_GPU(commandList, "GUI");
            SCOPED_PROFILE_ZONE_BACKEND_NAMED("GUI Rendering");

            ImGui::Render();
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }
        cmdList.endDebugLabel();

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

    // NOTE: We're ignoring any time relating to submitting & presenting, as that would factor e.g. GPU time & sync into the CPU time
    double cpuFrameElapsedTime = System::get().timeSinceStartup() - cpuFrameStartTime;
    renderPipeline.timer().reportCpuTime(cpuFrameElapsedTime);

    TracyD3D12Collect(m_tracyD3D12Context);
    TracyD3D12NewFrame(m_tracyD3D12Context);

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
    m_nextSwapchainBufferIndex += 1;

    Extent2D currentFramebufferExtent = System::get().windowFramebufferSize();
    if (currentFramebufferExtent != m_windowFramebufferExtent) {
        recreateSwapChain();

        // As the window render target changed we also have to recreate the render pipeline & its resource
        renderPipelineDidChange(renderPipeline);
    }

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
    bool success = issueUploadCommand([&](ID3D12GraphicsCommandList& uploadCommandList) {

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

    return success;

}

bool D3D12Backend::issueOneOffCommand(const std::function<void(ID3D12GraphicsCommandList&)>& callback) const
{
    ComPtr<ID3D12Fence> uploadFence;
    if (auto hr = device().CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create fence for one-off command, exiting.");
    }

    ComPtr<ID3D12CommandAllocator> commandAllocator;
    if (auto hr = device().CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create command allocator for one-off command, exiting.");
    }

    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (auto hr = device().CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create command list for one-off command, exiting.");
    }

    if (d3d12debugMode) {
        commandList->SetName(L"TemporaryCommandList");
    }

    callback(*commandList.Get());

    commandList->Close();

    ID3D12CommandList* commandLists[] = { commandList.Get() };
    m_commandQueue->ExecuteCommandLists(std::extent<decltype(commandLists)>::value, commandLists);
    m_commandQueue->Signal(uploadFence.Get(), 1);

    auto waitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    ARKOSE_ASSERT(waitEvent != nullptr);

    waitForFence(uploadFence.Get(), 1, waitEvent);

    commandAllocator->Reset();
    CloseHandle(waitEvent);

    // TODO: How can we detect if something went wrong?
    return true;
}

bool D3D12Backend::issueUploadCommand(const std::function<void(ID3D12GraphicsCommandList&)>& callback) const
{
    // "The texture and mesh data is uploaded using an upload heap. This happens during the initialization and shows how to transfer data to the GPU.
    //  Ideally, this should be running on the copy queue but for the sake of simplicity it is run on the general graphics queue."
    return issueOneOffCommand(callback);
}

D3D12DescriptorHeapAllocator& D3D12Backend::copyableDescriptorHeapAllocator()
{
    return *m_copyableDescriptorHeapAllocator.get();
}

D3D12DescriptorHeapAllocator& D3D12Backend::shaderVisibleDescriptorHeapAllocator()
{
    return *m_shaderVisibleDescriptorHeapAllocator.get();
}

D3D12DescriptorHeapAllocator& D3D12Backend::samplerDescriptorHeapAllocator()
{
    return *m_samplerDescriptorHeapAllocator.get();
}

ComPtr<ID3D12Device> D3D12Backend::createDeviceAtMaxSupportedFeatureLevel() const
{
    ComPtr<ID3D12Device> device;

    // Create device at the min-spec feature level for Arkose
    constexpr D3D_FEATURE_LEVEL minSpecFeatureLevel { D3D_FEATURE_LEVEL_12_0 };
    if (auto hr = D3D12CreateDevice(m_dxgiAdapter.Get(), minSpecFeatureLevel, IID_PPV_ARGS(&device)); FAILED(hr)) {
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
            if (hr = D3D12CreateDevice(m_dxgiAdapter.Get(), query.MaxSupportedFeatureLevel, IID_PPV_ARGS(&device)); FAILED(hr)) {
                ARKOSE_LOG(Fatal, "D3D12Backend: could not create the device at max feature level, exiting.");
            }
            currentFeatureLevel = query.MaxSupportedFeatureLevel;
        }
    } else {
        ARKOSE_LOG(Warning, "D3D12Backend: could not check feature support for the device, we'll just stick to 12.0.");
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS d3d12Options {};
    if (auto hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &d3d12Options, sizeof(d3d12Options)); SUCCEEDED(hr)) {
        if ( d3d12Options.ResourceBindingTier != D3D12_RESOURCE_BINDING_TIER_3 ) {
            ARKOSE_LOG(Fatal, "D3D12Backend: this device does not support resource binding tier 3, which is required for this engine. Sorry!");
        }
    } else {
        ARKOSE_LOG(Error, "D3D12Backend: failed to get device options! We will have to assume some things, hopefully that won't cause any issues");
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

    if (auto hr = device->SetName(L"Arkose Renderer"); FAILED(hr)) {
        ARKOSE_LOG(Warning, "D3D12Backend: failed to set device name");
    }

    return device;
}

ComPtr<ID3D12CommandQueue> D3D12Backend::createDefaultCommandQueue() const
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0; // for GPU 0

    if constexpr (d3d12debugMode) {
        queueDesc.Flags |= D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;
    }

    ComPtr<ID3D12CommandQueue> commandQueue;
    if (auto hr = device().CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create the default command queue, exiting.");
    }

    return commandQueue;
}

ComPtr<IDXGISwapChain4> D3D12Backend::createSwapChain(ID3D12CommandQueue* commandQueue) const
{
    UINT dxgiFactoryFlags = 0;
    if constexpr (d3d12debugMode) {
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }

    ComPtr<IDXGIFactory4> dxgiFactory;
    if (auto hr = CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create the DXGI factory, exiting.");
    }

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc {};

    swapChainDesc.Width = UINT(m_windowFramebufferExtent.width());
    swapChainDesc.Height = UINT(m_windowFramebufferExtent.height());

    swapChainDesc.Format = SwapChainFormat; // TODO: Maybe query for best format instead?

    // No stereo/VR rendering
    swapChainDesc.Stereo = false;

    // No multisampling into the swap chain (if you want multisampling, just resolve before final target).
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;

    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    swapChainDesc.BufferCount = QueueSlotCount;

    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;

    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // TODO: Investigate the different ones

    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = SwapChainFlags;

    ComPtr<IDXGISwapChain1> swapChain1;
    if (auto hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, System::get().win32WindowHandle(), &swapChainDesc, nullptr, nullptr, swapChain1.GetAddressOf()); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create swapchain, exiting.");
    }

    // We want api version 4 for the GetCurrentBackBufferIndex function
    ComPtr<IDXGISwapChain4> swapChain4;
    swapChain1.As(&swapChain4);

    return swapChain4;
}

void D3D12Backend::createWindowRenderTarget()
{
    // Create depth texture for rendering to the swapchain texture
    {
        Texture::Description depthTextureDesc;
        depthTextureDesc.extent = m_windowFramebufferExtent;
        depthTextureDesc.format = Texture::Format::Depth24Stencil8;
        m_swapchainDepthTexture = std::make_unique<D3D12Texture>(*this, depthTextureDesc);
    }

    // Create the "mock" texture and render target for rendering to this
    {
        m_mockSwapchainTexture = std::make_unique<D3D12Texture>();

        m_mockSwapchainTexture->m_description.extent = m_windowFramebufferExtent;
        m_mockSwapchainTexture->m_description.format = Texture::Format::Unknown;

        m_mockSwapchainTexture->textureResource = nullptr;
        m_mockSwapchainTexture->resourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_mockSwapchainTexture->dxgiFormat = SwapChainRenderTargetViewFormat;

        auto attachments = std::vector<RenderTarget::Attachment>({ { RenderTarget::AttachmentType::Color0, m_mockSwapchainTexture.get(), LoadOp::Clear, StoreOp::Store },
                                                                   { RenderTarget::AttachmentType::Depth, m_swapchainDepthTexture.get(), LoadOp::Clear, StoreOp::Store } });
        m_mockWindowRenderTarget = std::make_unique<D3D12RenderTarget>(*this, attachments);
    }
}

void D3D12Backend::recreateSwapChain()
{
    while (true) {
        m_windowFramebufferExtent = System::get().windowFramebufferSize();

        // Don't render while minimized
        if (m_windowFramebufferExtent.hasZeroArea()) {
            ARKOSE_LOG(Info, "D3D12Backend: rendering paused since there are no pixels to draw to.");
            System::get().waitEvents();
        } else {
            break;
        }
    }

    // Tear down all resources referencing the swap chain

    waitForDeviceIdle();

    for (auto& frameContext : m_frameContexts) {
        frameContext->renderTarget.Reset();
    }

    waitForDeviceIdle();

    m_swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, SwapChainFlags);
    m_nextSwapchainBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

    waitForDeviceIdle();

    for (int i = 0; i < QueueSlotCount; ++i) {
        auto& frameContext = *m_frameContexts[i];

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

            // TODO: Put this RTV handle in the frame context itself so we don't have to recalculate it every time like this.
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle {};
            rtvHandle.InitOffsetted(m_renderTargetDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), i, m_renderTargetViewDescriptorSize);

            device().CreateRenderTargetView(frameContext.renderTarget.Get(), &viewDesc, rtvHandle);
        }
    }

    createWindowRenderTarget();
}

std::unique_ptr<Buffer> D3D12Backend::createBuffer(size_t size, Buffer::Usage usage)
{
    return std::make_unique<D3D12Buffer>(*this, size, usage);
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

std::unique_ptr<RenderState> D3D12Backend::createRenderState(RenderTarget const& renderTarget, std::vector<VertexLayout> const& vertexLayouts,
                                                             Shader const& shader, StateBindings const& stateBindings,
                                                             RasterState const& rasterState, DepthState const& depthState, StencilState const& stencilState)
{
    return std::make_unique<D3D12RenderState>(*this, renderTarget, vertexLayouts, shader, stateBindings, rasterState, depthState, stencilState);
}

std::unique_ptr<ComputeState> D3D12Backend::createComputeState(Shader const& shader, StateBindings const& stateBindings)
{
    return std::make_unique<D3D12ComputeState>(*this, shader, stateBindings);
}

std::unique_ptr<BottomLevelAS> D3D12Backend::createBottomLevelAccelerationStructure(std::vector<RTGeometry> geometries, BottomLevelAS const* copySource)
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

std::unique_ptr<UpscalingState> D3D12Backend::createUpscalingState(UpscalingTech, UpscalingQuality, Extent2D renderRes, Extent2D outputDisplayRes)
{
    return nullptr;
}
