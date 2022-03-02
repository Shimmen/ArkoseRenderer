#include "Camera.h"

#include "math/Halton.h"
#include <moos/transform.h>
#include <imgui/imgui.h>

void Camera::newFrame(Badge<Scene>, Extent2D viewportSize, bool firstFrame)
{
    if (!firstFrame) {
        m_previousFrameViewFromWorld = viewMatrix();
        m_previousFrameProjectionFromView = projectionMatrix();
    }
    
    if (isFrustumJitteringEnabled()) {

        if (!firstFrame)
            m_previousFrameFrustumJitterPixelOffset = frustumJitterPixelOffset();

        int haltonSampleIdx = ((m_frameIndex++) % 8) + 1; // (+1 to avoid zero jitter)
        vec2 haltonSample01 = vec2(halton::generateHaltonSample(haltonSampleIdx, 3),
                                   halton::generateHaltonSample(haltonSampleIdx, 2));
        m_frustumJitterPixelOffset = haltonSample01 - vec2(0.5f); // (center over pixel)
    }

    // Reset at frame boundary
    if (!firstFrame)
        m_modified = false;

    m_viewportSize = viewportSize;
}

mat4 Camera::pixelProjectionMatrix() const
{
    // Ensures e.g. NDC (1,1) projects to (width-1,height-1)
    float roundingPixelsX = (float)viewportSize().width() - 0.001f;
    float roundingPixelsY = (float)viewportSize().height() - 0.001f;

    mat4 pixelFromNDC = moos::scale(vec3(roundingPixelsX, roundingPixelsY, 1.0f)) * moos::translate(vec3(0.5f, 0.5f, 0.0f)) * moos::scale(vec3(0.5f, 0.5f, 1.0f));
    return pixelFromNDC * projectionMatrix();
}

void Camera::lookAt(const vec3& position, const vec3& target, const vec3& up)
{
    m_position = position;

    vec3 forward = normalize(target - position);
    // TODO: Apparently I never bothered to implement lookRotation ...
    //m_orientation = moos::lookRotation(direction, up);
    vec3 right = cross(forward, up);
    vec3 properUp = cross(right, forward);
    mat3 orientationMat = mat3(right, properUp, -forward);
    m_orientation = moos::quatFromMatrix(mat4(orientationMat));

    setViewFromWorld(moos::lookAt(m_position, target, up));
}

float Camera::aspectRatio() const
{
    float width = static_cast<float>(viewportSize().width());
    float height = static_cast<float>(viewportSize().height());
    return (height > 1e-6f) ? (width / height) : 1.0f;
}

void Camera::setFieldOfView(float fov)
{
    if (std::abs(fov - m_fieldOfView) > 1e-6f) {
        m_fieldOfView = fov;
        markAsModified();
    }
}

// TODO: Add to mooslib instead of here!
bool operator==(const vec3& a, const vec3& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool operator==(const vec4& a, const vec4& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
bool operator==(const quat& a, const quat& b)
{
    return a.w == b.w && a.vec == b.vec;
}
bool operator==(const mat4& a, const mat4& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

void Camera::setPosition(vec3 p)
{
    if (p != m_position) {
        m_position = p;
        markAsModified();
    }
}

void Camera::moveBy(vec3 translation)
{
    if (length2(translation) > 1e-6f) {
        m_position += translation;
        markAsModified();
    }
}

void Camera::setOrientation(quat q)
{
    if (q != m_orientation) {
        m_orientation = q;
        markAsModified();
    }
}

void Camera::setViewFromWorld(mat4 viewFromWorld)
{
    if (viewFromWorld != m_viewFromWorld) {
        m_viewFromWorld = viewFromWorld;
        markAsModified();
    }
}

void Camera::setProjectionFromView(mat4 projectionFromView)
{
    if (projectionFromView != m_projectionFromView) {
        m_projectionFromView = projectionFromView;
        markAsModified();
    }

    // NOTE: We intentionally ignore the jittering when considering camera modified state
    if (m_frustumJitteringEnabled) {
        float uvOffsetX = float(frustumJitterPixelOffset().x) / viewportSize().width();
        float uvOffsetY = float(frustumJitterPixelOffset().y) / viewportSize().height();
        float ndcOffsetX = uvOffsetX * 2.0f;
        float ndcOffsetY = uvOffsetY * 2.0f;
        m_projectionFromView[2][0] += ndcOffsetX;
        m_projectionFromView[2][1] += ndcOffsetY;
    }
}

vec2 Camera::frustumJitterUVCorrection() const
{
    // Remove this frame's offset, we're now "neutral", then add previous frame's offset
    vec2 totalJitterPixelOffset = -frustumJitterPixelOffset() + previousFrameFrustumJitterPixelOffset();
    float x = totalJitterPixelOffset.x / float(viewportSize().width());
    float y = totalJitterPixelOffset.y / float(viewportSize().height());
    return vec2(x, y);
}

void Camera::renderExposureGUI()
{
    if (ImGui::RadioButton("Automatic exposure", useAutomaticExposure))
        useAutomaticExposure = true;
    if (ImGui::RadioButton("Manual exposure", !useAutomaticExposure))
        useAutomaticExposure = false;

    ImGui::Spacing();
    ImGui::Spacing();

    if (useAutomaticExposure)
        renderAutomaticExposureGUI();
    else
        renderManualExposureGUI();
}

void Camera::renderManualExposureGUI()
{
    // Aperture
    {
        constexpr float steps[] = { 1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f };
        constexpr int stepCount = sizeof(steps) / sizeof(steps[0]);
        constexpr float apertureMin = steps[0];
        constexpr float apertureMax = steps[stepCount - 1];

        ImGui::Text("Aperture f/%.1f", aperture);

        // A kind of snapping SliderFloat implementation
        {
            ImGui::SliderFloat("aperture", &aperture, apertureMin, apertureMax, "");

            int index = 1;
            for (; index < stepCount && aperture >= steps[index]; ++index) { }
            float distUp = std::abs(steps[index] - aperture);
            float distDown = std::abs(steps[index - 1] - aperture);
            if (distDown < distUp)
                index -= 1;

            aperture = steps[index];
        }
    }

    // Shutter speed
    {
        const int denominators[] = { 1000, 500, 400, 250, 125, 60, 30, 15, 8, 4, 2, 1 };
        const int denominatorCount = sizeof(denominators) / sizeof(denominators[0]);

        // Find the current value, snapped to the denominators
        int index = 1;
        {
            for (; index < denominatorCount && shutterSpeed >= (1.0f / denominators[index]); ++index) { }
            float distUp = std::abs(1.0f / denominators[index] - shutterSpeed);
            float distDown = std::abs(1.0f / denominators[index - 1] - shutterSpeed);
            if (distDown < distUp)
                index -= 1;
        }

        ImGui::Text("Shutter speed  1/%i s", denominators[index]);
        ImGui::SliderInt("shutter", &index, 0, denominatorCount - 1, "");

        shutterSpeed = 1.0f / denominators[index];
    }

    // ISO
    {
        int isoHundreds = int(iso + 0.5f) / 100;

        ImGui::Text("ISO %i", 100 * isoHundreds);
        ImGui::SliderInt("ISO", &isoHundreds, 1, 64, "");

        iso = float(isoHundreds * 100.0f);
    }
}

void Camera::renderAutomaticExposureGUI()
{
    ImGui::Text("Adaption rate", &adaptionRate);
    ImGui::SliderFloat("", &adaptionRate, 0.0001f, 2.0f, "%.4f", 5.0f);

    ImGui::Text("Exposure Compensation", &exposureCompensation);
    ImGui::SliderFloat("ECs", &exposureCompensation, -5.0f, +5.0f, "%.1f");
}
