#include "D3D12RenderState.h"

#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/d3d12/D3D12BindingSet.h"
#include "rendering/backend/d3d12/D3D12Texture.h"
#include "rendering/backend/shader/ShaderManager.h"
#include "utility/Profiling.h"
#include <d3dx12/d3dx12.h>

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
        u32 nextSemanticIndex = 0; // for GLSL->HLSL transpiled sources

        VertexLayout const& vertexLayout = vertexLayouts[vertexLayoutIdx];
        for (VertexComponent const& component : vertexLayout.components()) {

            D3D12_INPUT_ELEMENT_DESC inputElementDesc;
            inputElementDesc.SemanticIndex = 0;
            inputElementDesc.InputSlot = narrow_cast<UINT>(vertexLayoutIdx);

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
                inputElementDesc.SemanticName = "COLOR";
                inputElementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            case VertexComponent::JointIdx4U32:
                inputElementDesc.SemanticName = "BLENDINDICES";
                inputElementDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
                break;
            case VertexComponent::JointWeight4F:
                inputElementDesc.SemanticName = "BLENDWEIGHT";
                inputElementDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            case VertexComponent::Velocity3F:
                NOT_YET_IMPLEMENTED();
                inputElementDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            }

            // NOTE: If we're getting HLSL source transpiled from GLSL all input attributes will have
            // the name TEXCOORD with increasing semantic index, starting at 0! For now, let's just
            // override the more "logical" semantics with this simple scheme.
            inputElementDesc.SemanticName = "TEXCOORD";
            inputElementDesc.SemanticIndex = nextSemanticIndex++;

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

        switch (file.shaderStage()) {
        case ShaderStage::Vertex:
            psoDesc.VS.pShaderBytecode = codeBlob.data();
            psoDesc.VS.BytecodeLength = codeBlob.size();
            break;
        case ShaderStage::Fragment:
            psoDesc.PS.pShaderBytecode = codeBlob.data();
            psoDesc.PS.BytecodeLength = codeBlob.size();
            break;
        case ShaderStage::Task:
            NOT_YET_IMPLEMENTED();
            break;
        case ShaderStage::Mesh:
            NOT_YET_IMPLEMENTED();
            break;
        default:
            ASSERT_NOT_REACHED();
        }
    }

    m_namedConstantLookup = ShaderManager::instance().mergeNamedConstants(shader);

    // Create the root signature
    {
        std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> descriptorRangeStorage {}; // storage for if copies are needed

        std::vector<D3D12_ROOT_PARAMETER> rootParameters {};
        std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplerDescriptions {};

        stateBindings.forEachBindingSet([&](u32 setIndex, BindingSet const& bindingSet) {
            auto const& d3d12BindingSet = static_cast<D3D12BindingSet const&>(bindingSet);

            // TODO: Support embedded descriptors as well? E.g. if it's only a single descriptor.
            ARKOSE_ASSERT(d3d12BindingSet.rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE);

            // Make a copy of the binding set's descriptor ranges as we need to adjust their register space values
            auto& copiedDescriptorRanges = descriptorRangeStorage.emplace_back(d3d12BindingSet.descriptorRanges);

            // Set correct register space for the root parameters now when they are decided on
            for (D3D12_DESCRIPTOR_RANGE& descriptorRange : copiedDescriptorRanges) {
                ARKOSE_ASSERT(descriptorRange.RegisterSpace == D3D12BindingSet::UndecidedRegisterSpace);
                descriptorRange.RegisterSpace = setIndex;
            }

            // .. and make a copy of the root parameter as we now need to point it at the new descriptor range
            D3D12_ROOT_PARAMETER& copiedRootParameter = rootParameters.emplace_back(d3d12BindingSet.rootParameter);
            copiedRootParameter.DescriptorTable.pDescriptorRanges = copiedDescriptorRanges.data();
            ARKOSE_ASSERT(copiedRootParameter.DescriptorTable.NumDescriptorRanges == copiedDescriptorRanges.size());

            // If we have any static sampler, also make a copy of those and assign register space
            for (D3D12_STATIC_SAMPLER_DESC const& staticSampler : d3d12BindingSet.staticSamplers) {
                D3D12_STATIC_SAMPLER_DESC& copiedStaticSampler = staticSamplerDescriptions.emplace_back(staticSampler);
                ARKOSE_ASSERT(copiedStaticSampler.RegisterSpace == D3D12BindingSet::UndecidedRegisterSpace);
                copiedStaticSampler.RegisterSpace = setIndex;
            }
        });

        if (!m_namedConstantLookup.empty()) {
            u32 numUsedBytes = m_namedConstantLookup.totalOccupiedSize();
            if (numUsedBytes % 4 != 0) {
                ARKOSE_LOG(Warning, "D3D12RenderState: named constant range has a range that doesn't subdivide into a number of 32-bit values. Rounding up. Is this fine?");
                numUsedBytes = ark::roundUp(numUsedBytes, 4u);
            }

            D3D12_ROOT_PARAMETER namedConstantsRootParam;
            namedConstantsRootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            namedConstantsRootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            namedConstantsRootParam.Constants.ShaderRegister = 0; // always use index zero for named constants
            namedConstantsRootParam.Constants.RegisterSpace = 0; // always use space zero for named constants
            namedConstantsRootParam.Constants.Num32BitValues = numUsedBytes / 4;

            rootParameters.push_back(namedConstantsRootParam);
        }

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc {};
        rootSignatureDesc.NumParameters = narrow_cast<u32>(rootParameters.size());
        rootSignatureDesc.pParameters = rootParameters.data();

        rootSignatureDesc.NumStaticSamplers = narrow_cast<u32>(staticSamplerDescriptions.size());
        rootSignatureDesc.pStaticSamplers = staticSamplerDescriptions.data();

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
            char const* errorMessage = static_cast<char const*>(errorBlob->GetBufferPointer());
            ARKOSE_LOG(Fatal, "D3D12RenderState: failed to serialize root signature:\n{}exiting.", errorMessage);
        }

        if (auto hr = d3d12Backend.device().CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12RenderState: failed to create root signature, exiting.");
        }

        psoDesc.pRootSignature = rootSignature.Get();
    }

    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = 0xFFFFFFFF; // ??

    psoDesc.NumRenderTargets = narrow_cast<UINT>(renderTarget.colorAttachmentCount());
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

    if (stencilState.mode != StencilMode::Disabled) {
        psoDesc.DepthStencilState.StencilEnable = true;
        switch (stencilState.mode) {
        case StencilMode::AlwaysWrite:
            // Test
            psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            psoDesc.DepthStencilState.StencilReadMask = 0x00;
            // Writing
            psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
            psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            psoDesc.DepthStencilState.StencilWriteMask = 0xff;
            break;

        case StencilMode::ReplaceIfGreaterOrEqual:
            // Test
            psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            psoDesc.DepthStencilState.StencilReadMask = 0xff;
            // Writing
            psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
            psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            psoDesc.DepthStencilState.StencilWriteMask = 0xff;
            break;

        case StencilMode::PassIfEqual:
            // Test
            psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
            psoDesc.DepthStencilState.StencilReadMask = 0xff;
            // Writing
            psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
            psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
            psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            psoDesc.DepthStencilState.StencilWriteMask = 0x00;
            break;

        default:
            ASSERT_NOT_REACHED();
        }

        // For now, no separate front/back treatment supported
        psoDesc.DepthStencilState.BackFace = psoDesc.DepthStencilState.FrontFace;
    } else {
        psoDesc.DepthStencilState.StencilEnable = false;
        psoDesc.DepthStencilState.StencilReadMask = 0x00;
        psoDesc.DepthStencilState.StencilWriteMask = 0x00;
        psoDesc.DepthStencilState.FrontFace = {};
        psoDesc.DepthStencilState.BackFace = {};
    }

    // TODO: Pipeline caching!
    psoDesc.CachedPSO.pCachedBlob = nullptr;
    psoDesc.CachedPSO.CachedBlobSizeInBytes = 0;

    if (auto hr = d3d12Backend.device().CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12RenderState: failed to create graphics pipeline state, exiting.");
    }
}

D3D12RenderState::~D3D12RenderState()
{
}

void D3D12RenderState::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);
    pso->SetName(convertToWideString(name).c_str());
    rootSignature->SetName(convertToWideString(name + "_rootsig").c_str());
}
