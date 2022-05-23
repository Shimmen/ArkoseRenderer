#include "D3D12Backend.h"

#include "backend/d3d12/D3D12BindingSet.h"
#include "backend/d3d12/D3D12Buffer.h"
#include "backend/d3d12/D3D12ComputeState.h"
#include "backend/d3d12/D3D12RenderState.h"
#include "backend/d3d12/D3D12RenderTarget.h"
#include "backend/d3d12/D3D12Texture.h"
#include "core/Logging.h"
#include "rendering/AppState.h"

// The D3D12 "helper" library
#include <directx/d3dx12.h>

// The d3d HLSL compiler
#include <d3dcompiler.h>

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
    glfwGetFramebufferSize(m_window, &windowFramebufferWidth, &windowFramebufferHeight);

    m_swapChainExtent = Extent2D(windowFramebufferWidth, windowFramebufferHeight);
    //glfwSetFramebufferSizeCallback(window, static_cast<GLFWframebuffersizefun>([](GLFWwindow* window, int width, int height) {
    //                                   m_swapChainExtent = Extent2D(width, height);
    //                               }));

    if constexpr (d3d12debugMode) {
        ComPtr<ID3D12Debug> debugController;
        D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
        debugController->EnableDebugLayer();
    }

    if (auto hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create the device, exiting.");
    }

    // Create the default / direct / graphics command queue
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.NodeMask = 0;

        if (d3d12debugMode) {
            queueDesc.Flags |= D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT;
        }

        if (auto hr = device().CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: could not create the default command queue, exiting.");
        }
    }

    ComPtr<IDXGIFactory4> dxgiFactory;
    if (auto hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create the DXGI factory, exiting.");
    }

    DXGI_SWAP_CHAIN_DESC swapChainDesc;
    ::ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
    
    swapChainDesc.BufferDesc.Format = SwapChainFormat;
    swapChainDesc.BufferDesc.Width = windowFramebufferWidth;
    swapChainDesc.BufferDesc.Height = windowFramebufferHeight;

    swapChainDesc.SampleDesc.Count = 1; // No multisampling into the swap chain

    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = QueueSlotCount;
    
    swapChainDesc.OutputWindow = glfwGetWin32Window(m_window);
    swapChainDesc.Windowed = glfwGetWindowMonitor(m_window) == nullptr;
    
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    if (auto hr = dxgiFactory->CreateSwapChain(m_commandQueue.Get(), &swapChainDesc, &m_swapChain); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create a swapchain, exiting.");
    }

    /////////////////////////////////

    m_renderTargetViewDescriptorSize  = device().GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create a heap that can contains QueueSlotCount number of descriptors
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle {};
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = QueueSlotCount;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (auto hr = device().CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_renderTargetDescriptorHeap)); FAILED(hr)) {
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

    // Demo implementation
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

            static const D3D_SHADER_MACRO macros[] = { { "D3D12_SAMPLE_BASIC", "1" }, { nullptr, nullptr } };

            ComPtr<ID3DBlob> vertexShader;
            D3DCompile(m_demo.shaders, sizeof(m_demo.shaders),
                       "", macros, nullptr,
                       "VS_main", "vs_5_0", 0, 0, &vertexShader, nullptr);

            ComPtr<ID3DBlob> pixelShader;
            D3DCompile(m_demo.shaders, sizeof(m_demo.shaders),
                       "", macros, nullptr,
                       "PS_main", "ps_5_0", 0, 0, &pixelShader, nullptr);

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

            psoDesc.VS.BytecodeLength = vertexShader->GetBufferSize();
            psoDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();

            psoDesc.PS.BytecodeLength = pixelShader->GetBufferSize();
            psoDesc.PS.pShaderBytecode = pixelShader->GetBufferPointer();

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

            // Declare upload buffer data as 'static' so it persists after returning from this function.
            // Otherwise, we would need to explicitly wait for the GPU to copy data from the upload buffer
            // to vertex/index default buffers due to how the GPU processes commands asynchronously.
            static const Vertex vertices[4] = {
                // Upper Left
                { { -1.0f, 1.0f, 0 }, { 0, 0 } },
                // Upper Right
                { { 1.0f, 1.0f, 0 }, { 1, 0 } },
                // Bottom right
                { { 1.0f, -1.0f, 0 }, { 1, 1 } },
                // Bottom left
                { { -1.0f, -1.0f, 0 }, { 0, 1 } }
            };

            static const int indices[6] = {
                0, 1, 2, 2, 3, 0
            };

            static const int uploadBufferSize = sizeof(vertices) + sizeof(indices);
            static const auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            static const auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

            // Create upload buffer on CPU
            device().CreateCommittedResource(&uploadHeapProperties,
                                             D3D12_HEAP_FLAG_NONE,
                                             &uploadBufferDesc,
                                             D3D12_RESOURCE_STATE_GENERIC_READ,
                                             nullptr,
                                             IID_PPV_ARGS(&m_demo.uploadBuffer));

            // Create vertex & index buffer on the GPU
            // HEAP_TYPE_DEFAULT is on GPU, we also initialize with COPY_DEST state
            // so we don't have to transition into this before copying into them
            static const auto defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

            static const auto vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(vertices));
            device().CreateCommittedResource(&defaultHeapProperties,
                                             D3D12_HEAP_FLAG_NONE,
                                             &vertexBufferDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr,
                                             IID_PPV_ARGS(&m_demo.vertexBuffer));

            static const auto indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(indices));
            device().CreateCommittedResource(&defaultHeapProperties,
                                             D3D12_HEAP_FLAG_NONE,
                                             &indexBufferDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr,
                                             IID_PPV_ARGS(&m_demo.indexBuffer));

            // Create buffer views
            m_demo.vertexBufferView.BufferLocation = m_demo.vertexBuffer->GetGPUVirtualAddress();
            m_demo.vertexBufferView.SizeInBytes = sizeof(vertices);
            m_demo.vertexBufferView.StrideInBytes = sizeof(Vertex);

            m_demo.indexBufferView.BufferLocation = m_demo.indexBuffer->GetGPUVirtualAddress();
            m_demo.indexBufferView.SizeInBytes = sizeof(indices);
            m_demo.indexBufferView.Format = DXGI_FORMAT_R32_UINT;

            // Copy data on CPU into the upload buffer
            void* p;
            m_demo.uploadBuffer->Map(0, nullptr, &p);
            ::memcpy(p, vertices, sizeof(vertices));
            ::memcpy(static_cast<unsigned char*>(p) + sizeof(vertices), indices, sizeof(indices));
            m_demo.uploadBuffer->Unmap(0, nullptr);

            issueUploadCommand([this](ID3D12GraphicsCommandList& uploadCommandList) {

                // Copy data from upload buffer on CPU into the index/vertex buffer on the GPU
                uploadCommandList.CopyBufferRegion(m_demo.vertexBuffer.Get(), 0,
                                                   m_demo.uploadBuffer.Get(), 0, sizeof(vertices));
                uploadCommandList.CopyBufferRegion(m_demo.indexBuffer.Get(), 0,
                                                   m_demo.uploadBuffer.Get(), sizeof(vertices), sizeof(indices));

                // Barriers, batch them together
                const CD3DX12_RESOURCE_BARRIER barriers[2] = {
                    CD3DX12_RESOURCE_BARRIER::Transition(m_demo.vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
                    CD3DX12_RESOURCE_BARRIER::Transition(m_demo.indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER)
                };

                uploadCommandList.ResourceBarrier(2, barriers);

            });
        }
    }
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
    AppState appState { m_swapChainExtent, deltaTime, elapsedTime, m_currentFrameIndex, isRelativeFirstFrame };

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

        // Render the demo
        {
            D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle;
            CD3DX12_CPU_DESCRIPTOR_HANDLE::InitOffsetted(renderTargetHandle, m_renderTargetDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                         backBufferIndex, m_renderTargetViewDescriptorSize);

            D3D12_VIEWPORT viewport = { 0.0f, 0.0f,
                                        static_cast<float>(m_swapChainExtent.width()),
                                        static_cast<float>(m_swapChainExtent.height()),
                                        0.0f, 1.0f };
            D3D12_RECT scissorRect = { 0, 0, m_swapChainExtent.width(), m_swapChainExtent.height() };

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

void D3D12Backend::shutdown()
{
    // Drain the queue, wait for everything to finish
    for (auto& frameContext : m_frameContexts) {
        waitForFence(frameContext->frameFence.Get(), frameContext->frameFenceValue, frameContext->frameFenceEvent);
        CloseHandle(frameContext->frameFenceEvent);
    }
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
                                                              const Viewport& viewport, const BlendState& blendState, const RasterState& rasterState, const DepthState& depthState, const StencilState& stencilState)
{
    return std::make_unique<D3D12RenderState>(*this, renderTarget, vertexLayout, shader, stateBindings, viewport, blendState, rasterState, depthState, stencilState);
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
