#include "D3D12ComputeState.h"

#include "utility/Profiling.h"
#include "rendering/backend/d3d12/D3D12Backend.h"
#include "rendering/backend/d3d12/D3D12BindingSet.h"
#include "rendering/backend/shader/ShaderManager.h"

D3D12ComputeState::D3D12ComputeState(Backend& backend, Shader shader, StateBindings const& stateBindings)
    : ComputeState(backend, shader, stateBindings)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    D3D12Backend const& d3d12Backend = static_cast<D3D12Backend&>(backend);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc {};
    psoDesc.NodeMask = 0u;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    {
        ARKOSE_ASSERT(shader.files().size() == 1);
        std::vector<u8> const& codeBlob = ShaderManager::instance().dxil(shader.files()[0]);

        psoDesc.CS.pShaderBytecode = codeBlob.data();
        psoDesc.CS.BytecodeLength = codeBlob.size();
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
                ARKOSE_LOG(Warning, "D3D12ComputeState: named constant range has a range that doesn't subdivide into a number of 32-bit values. Rounding up. Is this fine?");
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

        // Not sure if it matters, but we do know this will only be used in a compute shader at this stage
        rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS
            | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;

        ComPtr<ID3DBlob> rootBlob;
        ComPtr<ID3DBlob> errorBlob;
        if (auto hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rootBlob, &errorBlob); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12ComputeState: failed to serialize root signature, exiting.");
        }

        if (auto hr = d3d12Backend.device().CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)); FAILED(hr)) {
            ARKOSE_LOG(Fatal, "D3D12ComputeState: failed to create root signature, exiting.");
        }

        psoDesc.pRootSignature = rootSignature.Get();
    }

    // TODO: Pipeline caching!
    psoDesc.CachedPSO.pCachedBlob = nullptr;
    psoDesc.CachedPSO.CachedBlobSizeInBytes = 0;

    if (auto hr = d3d12Backend.device().CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso)); FAILED(hr)) {
        ARKOSE_LOG(Fatal, "D3D12ComputeState: failed to create compute pipeline state, exiting.");
    }
}

D3D12ComputeState::~D3D12ComputeState()
{
}

void D3D12ComputeState::setName(const std::string& name)
{
    SCOPED_PROFILE_ZONE_GPURESOURCE();

    Resource::setName(name);
    pso->SetName(convertToWideString(name).c_str());
    rootSignature->SetName(convertToWideString(name + "_rootsig").c_str());
}
