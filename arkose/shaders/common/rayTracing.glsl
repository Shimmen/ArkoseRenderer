#ifndef RAY_TRACING_GLSL
#define RAY_TRACING_GLSL

#if defined(RAY_TRACING_BACKEND_NV)
#include <rayTracing/nvRayTracing.glsl>
#elif defined(RAY_TRACING_BACKEND_KHR)
#include <rayTracing/khrRayTracing.glsl>
#else
#error "No shader ray tracing backend defined!"
#endif

#include <shared/RTData.h>

// Corresponding to published binding set "SceneRTMeshDataSet"
#define DeclareCommonBindingSet_RTMesh(index) \
    layout(set = 1, binding = 0, scalar) buffer readonly RTTriangleMeshesBlock { RTTriangleMesh _rtMeshes[]; };  \
    layout(set = 1, binding = 1, scalar) buffer readonly RTIndicesBlock        { uint           _rtIndices[]; }; \
    layout(set = 1, binding = 2, scalar) buffer readonly RTVerticesBlock       { RTVertex       _rtVertices[]; };

#define rtmesh_getMesh(index) _rtMeshes[index]
#define rtmesh_getIndex(index) _rtIndices[index]
#define rtmesh_getVertex(index) _rtVertices[index]

#endif // RAY_TRACING_GLSL
