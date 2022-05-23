#include "D3D12Backend.h"

#include "backend/d3d12/D3D12BindingSet.h"
#include "backend/d3d12/D3D12Buffer.h"
#include "backend/d3d12/D3D12ComputeState.h"
#include "backend/d3d12/D3D12RenderState.h"
#include "backend/d3d12/D3D12RenderTarget.h"
#include "backend/d3d12/D3D12Texture.h"

D3D12Backend::D3D12Backend(Badge<Backend>, GLFWwindow* window, const AppSpecification& appSpecification)
    : m_window(window)
{
    // TODO!
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

bool D3D12Backend::executeFrame(const Scene&, RenderPipeline&, float elapsedTime, float deltaTime)
{
    // TODO!
    return true;
}

void D3D12Backend::shutdown()
{
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
