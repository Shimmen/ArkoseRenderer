#include "D3D12Backend.h"

#include "backend/d3d12/D3D12BindingSet.h"
#include "backend/d3d12/D3D12Buffer.h"
#include "backend/d3d12/D3D12ComputeState.h"
#include "backend/d3d12/D3D12RenderState.h"
#include "backend/d3d12/D3D12RenderTarget.h"
#include "backend/d3d12/D3D12Texture.h"
#include "core/Logging.h"

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

    if constexpr (d3d12debugMode) {
        ComPtr<ID3D12Debug> debugController;
        D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
        debugController->EnableDebugLayer();
    }

    if (auto hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create the device, exiting.");
    }

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

    /////////////////////////////////

    m_currentFenceValue = 1;

    // Create fences for each frame so we can protect resources and wait for any given frame
    for (int i = 0; i < QueueSlotCount; ++i) {

        m_frameFenceEvents[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        m_fenceValues[i] = 0;

        if (auto hr = device().CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameFences[i])); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12Backend: failed to create frame fence, exiting.");
        }
    }

    for (int i = 0; i < QueueSlotCount; ++i) {
        swapChain().GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = QueueSlotCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (auto hr = device().CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_renderTargetDescriptorHeap)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12Backend: failed to create descriptor heaps, exiting.");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle { m_renderTargetDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };

    for (int i = 0; i < QueueSlotCount; ++i) {
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc;
        viewDesc.Format = SwapChainRenderTargetViewFormat;
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipSlice = 0;
        viewDesc.Texture2D.PlaneSlice = 0;

        device().CreateRenderTargetView(m_renderTargets[i].Get(), &viewDesc, rtvHandle);
        rtvHandle.ptr += INT64(m_renderTargetViewDescriptorSize);
    }

    /////////////////////////////////

    for (int i = 0; i < QueueSlotCount; ++i) {
        device().CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]));
        device().CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[i].Get(), nullptr, IID_PPV_ARGS(&m_commandLists[i]));
        m_commandLists[i]->Close();
    }

    m_viewport = { 0.0f, 0.0f,
                   static_cast<float>(windowFramebufferWidth),
                   static_cast<float>(windowFramebufferHeight),
                   0.0f, 1.0f };
    m_viewportRectScissor = { 0, 0, windowFramebufferWidth, windowFramebufferHeight };

    /////////////////////////////////

    // "The texture and mesh data is uploaded using an upload heap. This happens during the initialization and shows how to transfer data to the GPU.
    //  Ideally, this should be running on the copy queue but for the sake of simplicity it is run on the general graphics queue."

    // Create our upload fence, command list and command allocator. This will be only used while creating the mesh buffer and the texture to upload data to the GPU.
    ComPtr<ID3D12Fence> uploadFence;
    device().CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence));

    ComPtr<ID3D12CommandAllocator> uploadCommandAllocator;
    device().CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&uploadCommandAllocator));
    
    ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
    device().CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, uploadCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&uploadCommandList));

    // Demo impl
    {
        //CreateRootSignature();
        {
            // We have two root parameters, one is a pointer to a descriptor heap with a SRV, the second is a constant buffer view
            CD3DX12_ROOT_PARAMETER parameters[1];

            // Our constant buffer view
            parameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

            CD3DX12_ROOT_SIGNATURE_DESC descRootSignature;

            // Create the root signature
            descRootSignature.Init(1, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

            ComPtr<ID3DBlob> rootBlob;
            ComPtr<ID3DBlob> errorBlob;
            if (auto hr = D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob); FAILED(hr)) {
                ARKOSE_LOG(Fatal, "D3D12Backend: failed to serialize demo root signature, exiting.");
            }

            if (auto hr = device().CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&m_demo.rootSignature)); FAILED(hr)) {
                ARKOSE_LOG(Fatal, "D3D12Backend: failed to create demo root signature, exiting.");
            }
        }

        #if 1

        //CreatePipelineStateObject();
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
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            // Simple alpha blending
            psoDesc.BlendState.RenderTarget[0].BlendEnable = true;
            psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.SampleDesc.Count = 1;
            psoDesc.DepthStencilState.DepthEnable = false;
            psoDesc.DepthStencilState.StencilEnable = false;
            psoDesc.SampleMask = 0xFFFFFFFF;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

            if (auto hr = device().CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_demo.pso)); FAILED(hr)) {
                ARKOSE_LOG(Fatal, "D3D12Backend: failed to create demo graphics pipeline state, exiting.");
            }
        }

        //CreateMeshBuffers(uploadCommandList);
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

            // Copy data from upload buffer on CPU into the index/vertex buffer on
            // the GPU
            uploadCommandList->CopyBufferRegion(m_demo.vertexBuffer.Get(), 0,
                                                m_demo.uploadBuffer.Get(), 0, sizeof(vertices));
            uploadCommandList->CopyBufferRegion(m_demo.indexBuffer.Get(), 0,
                                                m_demo.uploadBuffer.Get(), sizeof(vertices), sizeof(indices));

            // Barriers, batch them together
            const CD3DX12_RESOURCE_BARRIER barriers[2] = {
                CD3DX12_RESOURCE_BARRIER::Transition(m_demo.vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
                CD3DX12_RESOURCE_BARRIER::Transition(m_demo.indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER)
            };

            uploadCommandList->ResourceBarrier(2, barriers);
        }

        #endif
    }

    uploadCommandList->Close();

    /////////////////////////////////

    // Execute the upload and finish the command list
    ID3D12CommandList* commandLists[] = { uploadCommandList.Get() };
    commandQueue().ExecuteCommandLists(std::extent<decltype(commandLists)>::value, commandLists);
    commandQueue().Signal(uploadFence.Get(), 1);

    auto waitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (waitEvent == nullptr) {
        ARKOSE_LOG(Fatal, "D3D12Backend: could not create a wait event, exiting.");
    }

    waitForFence(uploadFence.Get(), 1, waitEvent);

    // Clean up our upload handle
    uploadCommandAllocator->Reset();

    CloseHandle(waitEvent);
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
    static int frameIdx = 0;
    m_currentBackBuffer = frameIdx % QueueSlotCount;

    {
        waitForFence(m_frameFences[m_currentBackBuffer].Get(), m_fenceValues[m_currentBackBuffer], m_frameFenceEvents[m_currentBackBuffer]);
    }

    //PrepareRender();
    {
        m_commandAllocators[m_currentBackBuffer]->Reset();

        ID3D12GraphicsCommandList* commandList = m_commandLists[m_currentBackBuffer].Get();
        commandList->Reset(m_commandAllocators[m_currentBackBuffer].Get(), nullptr);

        D3D12_CPU_DESCRIPTOR_HANDLE renderTargetHandle;
        CD3DX12_CPU_DESCRIPTOR_HANDLE::InitOffsetted(renderTargetHandle, m_renderTargetDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                     m_currentBackBuffer, m_renderTargetViewDescriptorSize);

        commandList->OMSetRenderTargets(1, &renderTargetHandle, true, nullptr);
        commandList->RSSetViewports(1, &m_viewport);
        commandList->RSSetScissorRects(1, &m_viewportRectScissor);

        // Transition back buffer
        D3D12_RESOURCE_BARRIER barrier;
        barrier.Transition.pResource = m_renderTargets[m_currentBackBuffer].Get();
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        commandList->ResourceBarrier(1, &barrier);

        static const float clearColor[] = { 0.042f, 0.042f, 0.042f, 1.0f };
        commandList->ClearRenderTargetView(renderTargetHandle, clearColor, 0, nullptr);    
    }

    auto commandList = m_commandLists[m_currentBackBuffer].Get();

    //RenderImpl(commandList);
    {
        // base impl
        {
            // Set our state (shaders, etc.)
            commandList->SetPipelineState(m_demo.pso.Get());

            // Set our root signature
            commandList->SetGraphicsRootSignature(m_demo.rootSignature.Get());
        }

        // demo impl
        {
            
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->IASetVertexBuffers(0, 1, &m_demo.vertexBufferView);
            commandList->IASetIndexBuffer(&m_demo.indexBufferView);
            commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
        }
    }

    //FinalizeRender();
    {
        // Transition the swap chain back to present
        D3D12_RESOURCE_BARRIER barrier;
        barrier.Transition.pResource = m_renderTargets[m_currentBackBuffer].Get();
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        auto commandList = m_commandLists[m_currentBackBuffer].Get();
        commandList->ResourceBarrier(1, &barrier);

        commandList->Close();

        // Execute our commands
        ID3D12CommandList* commandLists[] = { commandList };
        commandQueue().ExecuteCommandLists(std::extent<decltype(commandLists)>::value, commandLists);
    }

    // Present
    {
        swapChain().Present(1, 0);

        // Mark the fence for the current frame.
        const auto fenceValue = m_currentFenceValue;
        commandQueue().Signal(m_frameFences[m_currentBackBuffer].Get(), fenceValue);
        m_fenceValues[m_currentBackBuffer] = fenceValue;
        ++m_currentFenceValue;
    }

    frameIdx += 1;

    return true;
}

void D3D12Backend::shutdown()
{
    // Drain the queue, wait for everything to finish
    for (int i = 0; i < QueueSlotCount; ++i) {
        waitForFence(m_frameFences[i].Get(), m_fenceValues[i], m_frameFenceEvents[i]);
        CloseHandle(m_frameFenceEvents[i]);
    }
}

void D3D12Backend::waitForFence(ID3D12Fence* fence, UINT64 completionValue, HANDLE waitEvent) const
{
    if (fence->GetCompletedValue() < completionValue) {
        fence->SetEventOnCompletion(completionValue, waitEvent);
        WaitForSingleObject(waitEvent, INFINITE);
    }
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
