#include <ark/color.h>
#include <ark/material.h>
#include <ark/matrix.h>
#include <ark/quaternion.h>
#include <ark/random.h>
#include <ark/spd.h>
#include <ark/transform.h>
#include <ark/vector.h>

#include <fmt/format.h>
#include <limits>

int main()
{
    using namespace ark;

    // TODO: Maybe create some actual unit tests, not just this compilation & manual verification program

    fmt::print("Numeric types:\n");
    {
        {
            i8 a = std::numeric_limits<i8>::max();
            i16 b = std::numeric_limits<i16>::max();
            i32 c = std::numeric_limits<i32>::max();
            i64 d = std::numeric_limits<i64>::max();
            fmt::print(" signed integers: i8={}, i16={}, i32={}, i64={}\n", a, b, c, d);
        }

        {
            u8 a = std::numeric_limits<u8>::max();
            u16 b = std::numeric_limits<u16>::max();
            u32 c = std::numeric_limits<u32>::max();
            u64 d = std::numeric_limits<u64>::max();
            fmt::print(" unsigned integers: u8={}, u16={}, u32={}, u64={}\n", a, b, c, d);
        }

        {
            f32 a = std::numeric_limits<f32>::max();
            f64 b = std::numeric_limits<f64>::max();
            fmt::print(" float types: f32={}, u64={}\n", a, b);
        }
    }

    fmt::print("vec2:\n");
    {
        vec2 fv2 { 1, 1 };
        float fv2l = length(fv2);
        vec2 fv2n = normalize(fv2);

        // should not compile:
        //ivec2 iv2 { 1, 1 };
        //i32 iv2l = length(iv2);
        //ivec2 iv2n = normalize(iv2);
    }

    fmt::print("vec3:\n");
    {
        // TODO
    }

    fmt::print("vec4:\n");
    {
        vec4 a = { 1, 2, 3, 4 };
        vec4 b = { 40, 30, 20, 10 };
        float d = dot(a, b);
        fmt::print(" SIMD vec4 dot product gives {}, correct is {}\n", d, 200.0f);
    }

    fmt::print("mat3:\n");
    {
        mat3 a = { { 1, 3, 2 },
                   { 2, 2, 1 },
                   { 3, 1, 3 } };
        mat3 aT = transpose(a);
        mat3 aInv = inverse(a);
        fmt::print(" check inverse ...\n");
    }

    fmt::print("mat4:\n");
    {
        mat4 a = { { 1, 3, 2, 2 },
                   { 2, 2, 1, 1 },
                   { 3, 1, 3, 2 },
                   { 4, 4, 4, 4 } };
        mat4 aInv = inverse(a);
        fmt::print(" check inverse ...\n");

        mat4 b = { { 1, 5, 9, 13 },
                   { 2, 6, 10, 14 },
                   { 3, 7, 11, 15 },
                   { 4, 8, 12, 16 } };
        mat4 bT = transpose(b);
        fmt::print(" check transpose ...\n");
    }

    fmt::print("quat:\n");
    {
        quat q = axisAngle(globalUp, HALF_PI);
        float diff1 = distance(q * globalRight, globalForward);
        assert(diff1 < 1e-6f);
        float diff2 = distance(rotateVector(q, globalRight), globalForward);
        assert(diff2 < 1e-6f);
    }

    fmt::print("transformations:\n");
    {
        mat4 s1 = scale(10.0f);
        mat4 s2 = scale(vec3(1, 2, 3));
        mat4 t = translate(vec3(4, 5, 6));
        mat4 r = rotate(axisAngle(globalZ, PI));

        mat4 cam = lookAt(vec3(0, 0, 0), vec3(10, 10, 10));

        mat4 proj1 = perspectiveProjectionToVulkanClipSpace(toRadians(45), 1.0f, 0.01f, 1000.0f);
        mat4 proj2 = perspectiveProjectionToOpenGLClipSpace(toRadians(45), 1.0f, 0.01f, 1000.0f);

        fmt::print(" check matrices ...\n");

        mat4 orth1a = orthographicProjection(-1.0f, +1.0f, +1.0f, -1.0f, -1.0f, +1.0f, OrthographicProjectionDepthMode::ZeroToOne);
        mat4 orth1b = orthographicProjectionToVulkanClipSpace(2.0f, -1.0f, +1.0f);
        fmt::print(" check 1a and 1b are identical ...\n");

        mat4 orth2a = orthographicProjection(-1.0f, +1.0f, -1.0f, +1.0f, -1.0f, +1.0f, OrthographicProjectionDepthMode::NegativeOneToOne);
        mat4 orth2b = orthographicProjectionToOpenGLClipSpace(2.0f, -1.0f, +1.0f);
        fmt::print(" check 2a and 2b are identical ...\n");

        vec4 frustumPlanes[6];
        extractWorldFrustumPlanesFromViewProjection(proj1 * cam, frustumPlanes);
    }

    fmt::print("random:\n");
    {
        Random random { 12345u };
        f32 a = random.randomFloat<f32>();
        f64 b = random.randomFloatInRange<f64>(100.0, 200.0);

        i8 c = random.randomIntInRange<i32>(-100, +100);
        u64 d = random.randomIntInRange<u64>(0u, 10'000'000'000'000'000'000u);

        Random& threadRandom = Random::instanceForThisThread();
        (void)threadRandom.randomFloat();

        fmt::print(" check random values ...\n");
    }

    // etc..
}
