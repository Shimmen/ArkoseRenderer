#ifndef VISIBILITY_BUFFER_COMMON_GLSL
#define VISIBILITY_BUFFER_COMMON_GLSL

// Adapted from http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/

struct BarycentricDeriv
{
    vec3 lambda;
    vec3 ddx;
    vec3 ddy;
};

BarycentricDeriv CalcFullBary(vec4 p0, vec4 p1, vec4 p2, vec2 pixelNdc, vec2 windowSizePx)
{
    BarycentricDeriv ret;

    vec3 invW = 1.0 / vec3(p0.w, p1.w, p2.w);

    vec2 ndc0 = p0.xy * invW.x;
    vec2 ndc1 = p1.xy * invW.y;
    vec2 ndc2 = p2.xy * invW.z;

    float invDet = 1.0 / determinant(mat2(ndc2 - ndc1, ndc0 - ndc1));
    ret.ddx = vec3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.ddy = vec3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
    float ddxSum = dot(ret.ddx, vec3(1.0, 1.0, 1.0));
    float ddySum = dot(ret.ddy, vec3(1.0, 1.0, 1.0));

    vec2 deltaVec = pixelNdc - ndc0;
    float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
    float interpW = 1.0 / interpInvW;

    ret.lambda.x = interpW * (invW[0] + deltaVec.x * ret.ddx.x + deltaVec.y * ret.ddy.x);
    ret.lambda.y = interpW * (0.0     + deltaVec.x * ret.ddx.y + deltaVec.y * ret.ddy.y);
    ret.lambda.z = interpW * (0.0     + deltaVec.x * ret.ddx.z + deltaVec.y * ret.ddy.z);

    ret.ddx *= (2.0 / windowSizePx.x);
    ret.ddy *= (2.0 / windowSizePx.y);
    ddxSum  *= (2.0 / windowSizePx.x);
    ddySum  *= (2.0 / windowSizePx.y);

    // TODO: Consider if we want this y-flip with Vulkan?!
    ret.ddy *= -1.0;
    ddySum  *= -1.0;

    float interpW_ddx = 1.0 / (interpInvW + ddxSum);
    float interpW_ddy = 1.0 / (interpInvW + ddySum);

    ret.ddx = interpW_ddx * (ret.lambda * interpInvW + ret.ddx) - ret.lambda;
    ret.ddy = interpW_ddy * (ret.lambda * interpInvW + ret.ddy) - ret.lambda;  

    return ret;
}

////////////////////////////////////////////////////////////////////////////////
// Basic interpolation building blocks

float InterpolateBasic(BarycentricDeriv deriv, float v0, float v1, float v2)
{
    return dot(vec3(v0, v1, v2), deriv.lambda);
}

vec3 InterpolateWithDerivatives(BarycentricDeriv deriv, float v0, float v1, float v2)
{
    vec3 mergedV = vec3(v0, v1, v2);
    vec3 ret;
    ret.x = dot(mergedV, deriv.lambda);
    ret.y = dot(mergedV, deriv.ddx);
    ret.z = dot(mergedV, deriv.ddy);
    return ret;
}

////////////////////////////////////////////////////////////////////////////////
// Complex interpolation functions

vec3 InterpolateVec3(BarycentricDeriv deriv, vec3 v0, vec3 v1, vec3 v2)
{
    vec3 result;
    result.x = InterpolateBasic(deriv, v0.x, v1.x, v2.x);
    result.y = InterpolateBasic(deriv, v0.y, v1.y, v2.y);
    result.z = InterpolateBasic(deriv, v0.z, v1.z, v2.z);
    return result;
}

vec4 InterpolateVec4(BarycentricDeriv deriv, vec4 v0, vec4 v1, vec4 v2)
{
    vec4 result;
    result.x = InterpolateBasic(deriv, v0.x, v1.x, v2.x);
    result.y = InterpolateBasic(deriv, v0.y, v1.y, v2.y);
    result.z = InterpolateBasic(deriv, v0.z, v1.z, v2.z);
    result.w = InterpolateBasic(deriv, v0.w, v1.w, v2.w);
    return result;
}

#endif // VISIBILITY_BUFFER_COMMON_GLSL
