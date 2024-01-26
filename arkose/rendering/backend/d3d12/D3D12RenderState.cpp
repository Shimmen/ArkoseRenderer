#include "D3D12RenderState.h"

#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/d3d12/D3D12BindingSet.h"
#include "rendering/backend/d3d12/D3D12Texture.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "utility/Profiling.h"

D3D12RenderState::D3D12RenderState(Backend& backend, RenderTarget const& renderTarget, std::vector<VertexLayout> vertexLayouts,
                                   Shader shader, StateBindings const& stateBindings,
                                   RasterState rasterState, DepthState depthState, StencilState stencilState)
    : RenderState(backend, renderTarget, std::move(vertexLayouts), shader, stateBindings, rasterState, depthState, stencilState)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    D3D12Backend const& d3d12Backend = static_cast<D3D12Backend&>(backend);

    psoDesc = {};

    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescriptors {};
    for (size_t vertexLayoutIdx = 0; vertexLayoutIdx < vertexLayouts.size(); ++vertexLayoutIdx) {

        u32 currentOffset = 0;

        VertexLayout const& vertexLayout = vertexLayouts[vertexLayoutIdx];
        for (VertexComponent const& component : vertexLayout.components()) {

            D3D12_INPUT_ELEMENT_DESC inputElementDesc;
            inputElementDesc.SemanticIndex = 0; // not supported
            inputElementDesc.InputSlot = vertexLayoutIdx;

            // TODO: Use modern SV_* semantics: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semanticso09 ..?
            switch (component) {
            case VertexComponent::Position2F:
                inputElementDesc.SemanticName = "POSITION";
                inputElementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
                break;
            case VertexComponent::Position3F:
                inputElementDesc.SemanticName = "POSITION";
                inputElementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            case VertexComponent::Normal3F:
                inputElementDesc.SemanticName = "NORMAL";
                inputElementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            case VertexComponent::TexCoord2F:
                inputElementDesc.SemanticName = "TEXCOORD";
                inputElementDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
                break;
            case VertexComponent::Tangent4F:
                inputElementDesc.SemanticName = "TANGENT";
                inputElementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            case VertexComponent::Color3F:
            case VertexComponent::JointIdx4U32:
            case VertexComponent::JointWeight4F:
            case VertexComponent::Velocity3F:
                NOT_YET_IMPLEMENTED();
                break;
            }

            inputElementDesc.AlignedByteOffset = currentOffset;
            currentOffset += narrow_cast<u32>(vertexComponentSize(component));

            // No support for per-instance vertex data
            inputElementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            inputElementDesc.InstanceDataStepRate = 0;

            inputElementDescriptors.push_back(inputElementDesc);
        }
    }

    psoDesc.InputLayout.NumElements = narrow_cast<u32>(inputElementDescriptors.size());
    psoDesc.InputLayout.pInputElementDescs = inputElementDescriptors.data();

    for (ShaderFile const& file : shader.files()) {

        std::vector<u8> const& codeBlob = ShaderManager::instance().dxil(file);

        switch (file.type()) {
        case ShaderFileType::Vertex:
            psoDesc.VS.pShaderBytecode = codeBlob.data();
            psoDesc.VS.BytecodeLength = codeBlob.size();
            break;
        case ShaderFileType::Fragment:
            psoDesc.PS.pShaderBytecode = codeBlob.data();
            psoDesc.PS.BytecodeLength = codeBlob.size();
            break;
        case ShaderFileType::Task:
            NOT_YET_IMPLEMENTED();
            break;
        case ShaderFileType::Mesh:
            NOT_YET_IMPLEMENTED();
            break;
        default:
            ASSERT_NOT_REACHED();
        }
    }

    // Create the root signature
    {
        std::vector<D3D12_ROOT_PARAMETER> allRootParameters {};
        stateBindings.forEachBindingSet([&](u32 setIndex, BindingSet const& bindingSet) {
            auto const& d3d12BindingSet = static_cast<D3D12BindingSet const&>(bindingSet);
            for (D3D12_ROOT_PARAMETER const& undecidedRootParameter : d3d12BindingSet.rootParameters) {
                D3D12_ROOT_PARAMETER rootParameter;

                switch (undecidedRootParameter.ParameterType) {
                case D3D12_ROOT_PARAMETER_TYPE_CBV:
                case D3D12_ROOT_PARAMETER_TYPE_SRV:
                case D3D12_ROOT_PARAMETER_TYPE_UAV: {
                    ARKOSE_ASSERT(undecidedRootParameter.Descriptor.RegisterSpace == D3D12BindingSet::UndecidedRegisterSpaceValue);
                    rootParameter = undecidedRootParameter;
                    rootParameter.Descriptor.RegisterSpace = setIndex;
                } break;
                default:
                    NOT_YET_IMPLEMENTED();
                }

                allRootParameters.push_back(rootParameter);
            }
        });

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc {};
        rootSignatureDesc.NumParameters = narrow_cast<u32>(allRootParameters.size());
        rootSignatureDesc.pParameters = allRootParameters.data();

        rootSignatureDesc.NumStaticSamplers = 0;
        rootSignatureDesc.pStaticSamplers = nullptr;

        // NOTE: Can use flags to allow/deny visibility for a whole root signature
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        // From the documentation: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_root_signature_flags
        // "The app is opting in to using the Input Assembler (requiring an input layout that defines a set of vertex buffer bindings).
        //  Omitting this flag can result in one root argument space being saved on some hardware. Omit this flag if the Input Assembler
        //  is not required, though the optimization is minor."
        rootSignatureDesc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rootBlob;
        ComPtr<ID3DBlob> errorBlob;
        if (auto hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootBlob, &errorBlob); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12RenderState: failed to serialize root signature, exiting.");
        }

        if (auto hr = d3d12Backend.device().CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12RenderState: failed to create root signature, exiting.");
        }

        psoDesc.pRootSignature = rootSignature.Get();
    }

    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = 0xFFFFFFFF; // ??

    psoDesc.NumRenderTargets = renderTarget.colorAttachmentCount();
    for (UINT attachmentIdx = 0; attachmentIdx < 8; ++attachmentIdx) {
        RenderTarget::AttachmentType attachmentType { attachmentIdx };
        if (Texture* attachedTexture = renderTarget.attachment(attachmentType)) {

            auto const& d3d12Texture = static_cast<D3D12Texture const&>(*attachedTexture);
            psoDesc.RTVFormats[attachmentIdx] = d3d12Texture.dxgiFormat;

            D3D12_RENDER_TARGET_BLEND_DESC& renderTargetBlendState = psoDesc.BlendState.RenderTarget[attachmentIdx];

            RenderTarget::Attachment const& attachment = renderTarget.colorAttachments()[attachmentIdx]; // hacky, we can improve the interface..
            switch (attachment.blendMode) {
            case RenderTargetBlendMode::None:
                renderTargetBlendState.BlendEnable = false;
                break;
            case RenderTargetBlendMode::Additive:
                renderTargetBlendState.BlendEnable = true;
                renderTargetBlendState.SrcBlend = D3D12_BLEND_SRC_COLOR;
                renderTargetBlendState.DestBlend = D3D12_BLEND_DEST_COLOR;
                renderTargetBlendState.BlendOp = D3D12_BLEND_OP_ADD;
                renderTargetBlendState.SrcBlendAlpha = D3D12_BLEND_ONE; // replace alpha with new value
                renderTargetBlendState.DestBlendAlpha = D3D12_BLEND_ZERO;
                renderTargetBlendState.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                renderTargetBlendState.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                break;
            case RenderTargetBlendMode::AlphaBlending:
                renderTargetBlendState.BlendEnable = true;
                renderTargetBlendState.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                renderTargetBlendState.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                renderTargetBlendState.BlendOp = D3D12_BLEND_OP_ADD;
                renderTargetBlendState.SrcBlendAlpha = D3D12_BLEND_ONE;
                renderTargetBlendState.DestBlendAlpha = D3D12_BLEND_ZERO;
                renderTargetBlendState.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                renderTargetBlendState.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                break;
            default:
                ASSERT_NOT_REACHED();
            }

        } else {
            psoDesc.RTVFormats[attachmentIdx] = DXGI_FORMAT_UNKNOWN;
        }
    }

    if (renderTarget.hasDepthAttachment()) {
        RenderTarget::Attachment const& depthAttachment = renderTarget.depthAttachment().value();
        auto const& d3d12DepthTexture = static_cast<D3D12Texture const&>(*depthAttachment.texture);
        ARKOSE_ASSERT(d3d12DepthTexture.hasDepthFormat());
        psoDesc.DSVFormat = d3d12DepthTexture.dxgiFormat;
    } else {
        psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    }

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    
    switch (rasterState.polygonMode) {
    case PolygonMode::Filled:
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        break;
    case PolygonMode::Lines:
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        break;
    case PolygonMode::Points:
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        break;
    }

    if (rasterState.backfaceCullingEnabled) {
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    } else {
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    }

    psoDesc.RasterizerState.DepthClipEnable = false;
    psoDesc.RasterizerState.MultisampleEnable = false;
    psoDesc.RasterizerState.AntialiasedLineEnable = false; // maybe?

    switch (rasterState.frontFace) {
    case TriangleWindingOrder::Clockwise:
        psoDesc.RasterizerState.FrontCounterClockwise = false;
        break;
    case TriangleWindingOrder::CounterClockwise:
        psoDesc.RasterizerState.FrontCounterClockwise = true;
        break;
    }

    // No multisampling support for now..
    ARKOSE_ASSERT(!renderTarget.requiresMultisampling());
    psoDesc.SampleDesc.Count = 1;

    psoDesc.DepthStencilState.DepthEnable = depthState.testDepth;
    psoDesc.DepthStencilState.DepthWriteMask = depthState.writeDepth
        ? D3D12_DEPTH_WRITE_MASK_ALL
        : D3D12_DEPTH_WRITE_MASK_ZERO;
    switch (depthState.compareOp) {
    case DepthCompareOp::Less:
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        break;
    case DepthCompareOp::LessThanEqual:
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        break;
    case DepthCompareOp::Greater:
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
        break;
    case DepthCompareOp::GreaterThanEqual:
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        break;
    case DepthCompareOp::Equal:
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    // No stencil support for now..
    ARKOSE_ASSERT(stencilState.mode == StencilMode::Disabled);
    psoDesc.DepthStencilState.StencilEnable = false;

    // TODO: Pipeline caching!
    psoDesc.CachedPSO.pCachedBlob = nullptr;
    psoDesc.CachedPSO.CachedBlobSizeInBytes = 0;

    if (auto hr = d3d12Backend.device().CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12RenderState: failed to create graphics pipeline state, exiting.");
    }
}

D3D12RenderState::~D3D12RenderState()
{
    if (!hasBackend())
        return;
    // TODO
}

void D3D12RenderState::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);

    // TODO
}
