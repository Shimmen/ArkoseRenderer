#ifndef RAY_TRACING_GLSL
#define RAY_TRACING_GLSL

#if defined(RAY_TRACING_BACKEND_NV)
#include <rayTracing/nvRayTracing.glsl>
#elif defined(RAY_TRACING_BACKEND_KHR)
#include <rayTracing/khrRayTracing.glsl>
#else
#error "No shader ray tracing backend defined!"
#endif

#endif // RAY_TRACING_GLSL
