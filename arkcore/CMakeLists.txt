
set(ARKCORE_SOURCES

  # asset
  arkcore/asset/AnimationAsset.cpp
  arkcore/asset/AnimationAsset.h
  arkcore/asset/ImageAsset.cpp
  arkcore/asset/ImageAsset.h
  arkcore/asset/LevelAsset.cpp
  arkcore/asset/LevelAsset.h
  arkcore/asset/MaterialAsset.cpp
  arkcore/asset/MaterialAsset.h
  arkcore/asset/MeshAsset.cpp
  arkcore/asset/MeshAsset.h
  arkcore/asset/SerialisationHelpers.h
  arkcore/asset/SetAsset.cpp
  arkcore/asset/SetAsset.h
  arkcore/asset/SkeletonAsset.cpp
  arkcore/asset/SkeletonAsset.h
  arkcore/asset/TextureCompressor.cpp
  arkcore/asset/TextureCompressor.h
    # external
    arkcore/asset/external/CubeLUT.cpp
    arkcore/asset/external/CubeLUT.h
    arkcore/asset/external/DDSImage.cpp
    arkcore/asset/external/DDSImage.h
    arkcore/asset/external/IESProfile.cpp
    arkcore/asset/external/IESProfile.h
    # import
    arkcore/asset/import/AssetImporter.cpp
    arkcore/asset/import/AssetImporter.h
    arkcore/asset/import/GltfLoader.cpp
    arkcore/asset/import/GltfLoader.h
    # misc
    arkcore/asset/misc/ImageBakeSpec.cpp
    arkcore/asset/misc/ImageBakeSpec.h

  # core
  arkcore/core/Assert.cpp
  arkcore/core/Assert.h
  arkcore/core/Badge.h
  arkcore/core/CommandLine.cpp
  arkcore/core/CommandLine.h
  arkcore/core/Logging.h
  arkcore/core/Types.h
    # math
    arkcore/core/math/Fibonacci.cpp
    arkcore/core/math/Fibonacci.h
    arkcore/core/math/Frustum.cpp
    arkcore/core/math/Frustum.h
    arkcore/core/math/Halton.cpp
    arkcore/core/math/Halton.h
    arkcore/core/math/Plane.h
    arkcore/core/math/Sphere.h
    # memory
    arkcore/core/memory/BumpAllocator.h
    arkcore/core/memory/MemoryManager.cpp
    arkcore/core/memory/MemoryManager.h
    # parallel
    arkcore/core/parallel/ParallelFor.h
    arkcore/core/parallel/PollableTask.cpp
    arkcore/core/parallel/PollableTask.h
    arkcore/core/parallel/Task.cpp
    arkcore/core/parallel/Task.h
    arkcore/core/parallel/TaskGraph.cpp
    arkcore/core/parallel/TaskGraph.h

  # physics
  arkcore/physics/HandleTypes.h
  arkcore/physics/MotionType.h
  arkcore/physics/PhysicsMaterial.h
  arkcore/physics/PhysicsMesh.h

  # rendering
  arkcore/rendering/BlendMode.h
  arkcore/rendering/Brdf.h
  arkcore/rendering/ImageFilter.h
  arkcore/rendering/ImageWrapMode.h
  arkcore/rendering/Vertex.cpp
  arkcore/rendering/Vertex.h
  arkcore/rendering/VertexAllocation.h

  # scene
  arkcore/scene/EnvironmentMap.h
  arkcore/scene/ProbeGrid.cpp
  arkcore/scene/ProbeGrid.h
  arkcore/scene/Transform.cpp
  arkcore/scene/Transform.h

  # utility
  arkcore/utility/EnumHelpers.h
  arkcore/utility/Extent.h
  arkcore/utility/FileDialog.cpp
  arkcore/utility/FileDialog.h
  arkcore/utility/FileIO.cpp
  arkcore/utility/FileIO.h
  arkcore/utility/Hash.h
  arkcore/utility/ParseContext.cpp
  arkcore/utility/ParseContext.h
  arkcore/utility/Profiling.h
  arkcore/utility/StringHelpers.h

  PARENT_SCOPE
  )
