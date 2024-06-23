
set(ARKOSE_SOURCES

  # animation
  arkose/animation/Animation.cpp
  arkose/animation/Animation.h

  # application
  arkose/application/Arkose.cpp
  arkose/application/Arkose.h
    # apps
    arkose/application/apps/App.h
    arkose/application/apps/BootstrappingApp.cpp
    arkose/application/apps/BootstrappingApp.h
    arkose/application/apps/MeshViewerApp.cpp
    arkose/application/apps/MeshViewerApp.h
    arkose/application/apps/ShowcaseApp.cpp
    arkose/application/apps/ShowcaseApp.h
    arkose/application/apps/SSSDemo.cpp
    arkose/application/apps/SSSDemo.h
      # geodata
      arkose/application/apps/geodata/GeodataApp.cpp
      arkose/application/apps/geodata/GeodataApp.h
      # pathtracer
      arkose/application/apps/pathtracer/PathTracerApp.cpp
      arkose/application/apps/pathtracer/PathTracerApp.h

  # physics
  arkose/physics/PhysicsScene.cpp
  arkose/physics/PhysicsScene.h
    # backend
    arkose/physics/backend/PhysicsLayers.h
      # base
      arkose/physics/backend/base/PhysicsBackend.cpp
      arkose/physics/backend/base/PhysicsBackend.h
      # jolt
      arkose/physics/backend/jolt/JoltPhysicsBackend.cpp
      arkose/physics/backend/jolt/JoltPhysicsBackend.h
      arkose/physics/backend/jolt/JoltVisualiser.cpp
      arkose/physics/backend/jolt/JoltVisualiser.h

  # rendering
  arkose/rendering/AppState.h
  arkose/rendering/Drawable.h
  arkose/rendering/DrawKey.cpp
  arkose/rendering/DrawKey.h
  arkose/rendering/GpuScene.cpp
  arkose/rendering/GpuScene.h
  arkose/rendering/Icon.cpp
  arkose/rendering/Icon.h
  arkose/rendering/IconManager.cpp
  arkose/rendering/IconManager.h
  arkose/rendering/Material.h
  arkose/rendering/NodeDependency.h
  arkose/rendering/Registry.cpp
  arkose/rendering/Registry.h
  arkose/rendering/RenderPipeline.cpp
  arkose/rendering/RenderPipeline.h
  arkose/rendering/RenderPipelineNode.cpp
  arkose/rendering/RenderPipelineNode.h
  arkose/rendering/ResourceList.h
  arkose/rendering/SkeletalMesh.cpp
  arkose/rendering/SkeletalMesh.h
  arkose/rendering/Skeleton.cpp
  arkose/rendering/Skeleton.h
  arkose/rendering/StaticMesh.cpp
  arkose/rendering/StaticMesh.h
  arkose/rendering/UpscalingParameters.h
  arkose/rendering/VertexManager.cpp
  arkose/rendering/VertexManager.h
    # backend
    arkose/rendering/backend/Resource.cpp
    arkose/rendering/backend/Resource.h
    arkose/rendering/backend/Resources.h
      # base
      arkose/rendering/backend/base/AccelerationStructure.cpp
      arkose/rendering/backend/base/AccelerationStructure.h
      arkose/rendering/backend/base/Backend.cpp
      arkose/rendering/backend/base/Backend.h
      arkose/rendering/backend/base/BindingSet.cpp
      arkose/rendering/backend/base/BindingSet.h
      arkose/rendering/backend/base/Buffer.cpp
      arkose/rendering/backend/base/Buffer.h
      arkose/rendering/backend/base/CommandList.h
      arkose/rendering/backend/base/ComputeState.cpp
      arkose/rendering/backend/base/ComputeState.h
      arkose/rendering/backend/base/RayTracingState.cpp
      arkose/rendering/backend/base/RayTracingState.h
      arkose/rendering/backend/base/RenderState.cpp
      arkose/rendering/backend/base/RenderState.h
      arkose/rendering/backend/base/RenderTarget.cpp
      arkose/rendering/backend/base/RenderTarget.h
      arkose/rendering/backend/base/Texture.cpp
      arkose/rendering/backend/base/Texture.h
      arkose/rendering/backend/base/UpscalingState.cpp
      arkose/rendering/backend/base/UpscalingState.h
      # d3d12
      # (added separately)
      # shader
      arkose/rendering/backend/shader/CompilationResult.h
      arkose/rendering/backend/shader/NamedConstant.h
      arkose/rendering/backend/shader/NamedConstantLookup.cpp
      arkose/rendering/backend/shader/NamedConstantLookup.h
      arkose/rendering/backend/shader/Shader.cpp
      arkose/rendering/backend/shader/Shader.h
      arkose/rendering/backend/shader/ShaderFile.cpp
      arkose/rendering/backend/shader/ShaderFile.h
      arkose/rendering/backend/shader/ShaderManager.cpp
      arkose/rendering/backend/shader/ShaderManager.h
      arkose/rendering/backend/shader/ShaderStage.h
        # dxc
        # (added separately)
        # shaderc
        arkose/rendering/backend/shader/shaderc/ShadercInterface.cpp
        arkose/rendering/backend/shader/shaderc/ShadercInterface.h
      # util
      arkose/rendering/backend/util/ClearValue.h
      arkose/rendering/backend/util/DrawCall.h
      arkose/rendering/backend/util/IndexType.h
      arkose/rendering/backend/util/StateBindings.cpp
      arkose/rendering/backend/util/StateBindings.h
      arkose/rendering/backend/util/UploadBuffer.cpp
      arkose/rendering/backend/util/UploadBuffer.h
      arkose/rendering/backend/util/VramStats.h
      # vulkan
      # (added separately)
    # debug
    arkose/rendering/debug/DebugDrawer.cpp
    arkose/rendering/debug/DebugDrawer.h
    # forward
    arkose/rendering/forward/ForwardModes.h
    arkose/rendering/forward/ForwardRenderNode.cpp
    arkose/rendering/forward/ForwardRenderNode.h
    arkose/rendering/forward/PrepassNode.cpp
    arkose/rendering/forward/PrepassNode.h
    # lighting
    arkose/rendering/lighting/LightingComposeNode.cpp
    arkose/rendering/lighting/LightingComposeNode.h
    # meshlet
    arkose/rendering/meshlet/MeshletCuller.cpp
    arkose/rendering/meshlet/MeshletCuller.h
    arkose/rendering/meshlet/MeshletDebugNode.cpp
    arkose/rendering/meshlet/MeshletDebugNode.h
    arkose/rendering/meshlet/MeshletForwardRenderNode.cpp
    arkose/rendering/meshlet/MeshletForwardRenderNode.h
    arkose/rendering/meshlet/MeshletIndirectHelper.cpp
    arkose/rendering/meshlet/MeshletIndirectHelper.h
    arkose/rendering/meshlet/MeshletManager.cpp
    arkose/rendering/meshlet/MeshletManager.h
    arkose/rendering/meshlet/MeshletView.h
    arkose/rendering/meshlet/MeshletVisibilityBufferRenderNode.cpp
    arkose/rendering/meshlet/MeshletVisibilityBufferRenderNode.h
    arkose/rendering/meshlet/VisibilityBufferDebugNode.cpp
    arkose/rendering/meshlet/VisibilityBufferDebugNode.h
    # nodes
    arkose/rendering/nodes/BloomNode.cpp
    arkose/rendering/nodes/BloomNode.h
    arkose/rendering/nodes/DDGINode.cpp
    arkose/rendering/nodes/DDGINode.h
    arkose/rendering/nodes/DDGIProbeDebug.cpp
    arkose/rendering/nodes/DDGIProbeDebug.h
    arkose/rendering/nodes/DebugDrawNode.cpp
    arkose/rendering/nodes/DebugDrawNode.h
    arkose/rendering/nodes/DepthOfFieldNode.cpp
    arkose/rendering/nodes/DepthOfFieldNode.h
    arkose/rendering/nodes/DirectionalLightShadowNode.cpp
    arkose/rendering/nodes/DirectionalLightShadowNode.h
    arkose/rendering/nodes/FinalNode.cpp
    arkose/rendering/nodes/FinalNode.h
    arkose/rendering/nodes/FXAANode.cpp
    arkose/rendering/nodes/FXAANode.h
    arkose/rendering/nodes/LocalLightShadowNode.cpp
    arkose/rendering/nodes/LocalLightShadowNode.h
    arkose/rendering/nodes/PickingNode.cpp
    arkose/rendering/nodes/PickingNode.h
    arkose/rendering/nodes/RTReflectionsNode.cpp
    arkose/rendering/nodes/RTReflectionsNode.h
    arkose/rendering/nodes/RTSphereLightShadowNode.cpp
    arkose/rendering/nodes/RTSphereLightShadowNode.h
    arkose/rendering/nodes/RTVisualisationNode.cpp
    arkose/rendering/nodes/RTVisualisationNode.h
    arkose/rendering/nodes/SkyViewNode.cpp
    arkose/rendering/nodes/SkyViewNode.h
    arkose/rendering/nodes/SSAONode.cpp
    arkose/rendering/nodes/SSAONode.h
    arkose/rendering/nodes/TAANode.cpp
    arkose/rendering/nodes/TAANode.h
    arkose/rendering/nodes/TonemapNode.cpp
    arkose/rendering/nodes/TonemapNode.h
    arkose/rendering/nodes/VisibilityBufferShadingNode.cpp
    arkose/rendering/nodes/VisibilityBufferShadingNode.h
    # pathtracer
    arkose/rendering/pathtracer/PathTracerNode.cpp
    arkose/rendering/pathtracer/PathTracerNode.h
    # postprocess
    arkose/rendering/postprocess/CASNode.cpp
    arkose/rendering/postprocess/CASNode.h
    arkose/rendering/postprocess/FogNode.cpp
    arkose/rendering/postprocess/FogNode.h
    arkose/rendering/postprocess/SSSSNode.cpp
    arkose/rendering/postprocess/SSSSNode.h
    # upscaling
    arkose/rendering/upscaling/UpscalingNode.cpp
    arkose/rendering/upscaling/UpscalingNode.h
    # util
    arkose/rendering/util/BlendModeUtil.h
    arkose/rendering/util/ScopedDebugZone.h

  # scene
  arkose/scene/MeshInstance.cpp
  arkose/scene/MeshInstance.h
  arkose/scene/Scene.cpp
  arkose/scene/Scene.h
    # camera
    arkose/scene/camera/Camera.cpp
    arkose/scene/camera/Camera.h
    arkose/scene/camera/CameraController.cpp
    arkose/scene/camera/CameraController.h
    arkose/scene/camera/FpsCameraController.cpp
    arkose/scene/camera/FpsCameraController.h
    arkose/scene/camera/MapCameraController.cpp
    arkose/scene/camera/MapCameraController.h
    # editor
    arkose/scene/editor/EditorGizmo.cpp
    arkose/scene/editor/EditorGizmo.h
    arkose/scene/editor/EditorObject.h
    # lights
    arkose/scene/lights/DirectionalLight.cpp
    arkose/scene/lights/DirectionalLight.h
    arkose/scene/lights/IESProfile.cpp
    arkose/scene/lights/IESProfile.h
    arkose/scene/lights/Light.cpp
    arkose/scene/lights/Light.h
    arkose/scene/lights/LightAttenuation.cpp
    arkose/scene/lights/LightAttenuation.h
    arkose/scene/lights/SphereLight.cpp
    arkose/scene/lights/SphereLight.h
    arkose/scene/lights/SpotLight.cpp
    arkose/scene/lights/SpotLight.h

  # system
  arkose/system/Gamepad.h
  arkose/system/Input.cpp
  arkose/system/Input.h
  arkose/system/InputAction.h
  arkose/system/System.cpp
  arkose/system/System.h
    # glfw
    arkose/system/glfw/SystemGlfw.cpp
    arkose/system/glfw/SystemGlfw.h

  # utility
  arkose/utility/AvgAccumulator.h
  arkose/utility/AvgElapsedTimer.cpp
  arkose/utility/AvgElapsedTimer.h
  arkose/utility/CapList.h

  arkose/main.cpp

  PARENT_SCOPE
  )
