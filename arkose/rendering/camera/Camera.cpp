#include "Camera.h"

#include "math/Halton.h"
#include <moos/transform.h>
#include <imgui/imgui.h>

class Scene;

void Camera::preRender(Badge<Scene>)
{
    // TODO: Consider if we really should expect m_viewFromWorld and m_projectionFromView to be set up at this point, or if we should do it here from parameters?

    if (m_frustumJitteringEnabled) {

        int haltonSampleIdx = ((m_frameIndex++) % 8) + 1; // (+1 to avoid zero jitter)
        vec2 haltonSample01 = vec2(halton::generateHaltonSample(haltonSampleIdx, 3),
                                   halton::generateHaltonSample(haltonSampleIdx, 2));
        vec2 jitterPixelOffset = haltonSample01 - vec2(0.5f); // (center over pixel)

        float uvOffsetX = float(jitterPixelOffset.x) / viewport().width();
        float uvOffsetY = float(jitterPixelOffset.y) / viewport().height();
        float ndcOffsetX = uvOffsetX * 2.0f;
        float ndcOffsetY = uvOffsetY * 2.0f;

        m_projectionFromView[2][0] += ndcOffsetX;
        m_projectionFromView[2][1] += ndcOffsetY;
        m_frustumJitterPixelOffset = jitterPixelOffset;
    }
}

void Camera::postRender(Badge<Scene>)
{
    m_previousFrameViewFromWorld = viewMatrix();
    m_previousFrameProjectionFromView = projectionMatrix();

    if (isFrustumJitteringEnabled()) {
        m_previousFrameFrustumJitterPixelOffset = frustumJitterPixelOffset();
    }

    // We reset here at the frame boundary now when we've rendered with this exact camera
    m_modified = false;
}

mat4 Camera::pixelProjectionMatrix() const
{
    // Ensures e.g. NDC (1,1) projects to (width-1,height-1)
    float roundingPixelsX = (float)viewport().width() - 0.001f;
    float roundingPixelsY = (float)viewport().height() - 0.001f;

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
    float width = static_cast<float>(viewport().width());
    float height = static_cast<float>(viewport().height());
    return (height > 1e-6f) ? (width / height) : 1.0f;
}

void Camera::setFocalLength(float focalLength)
{
    if (std::abs(focalLength - m_focalLength) > 1e-4f) {
        m_focalLength = focalLength;
        markAsModified();
    }
}

void Camera::setFocusDepth(float focusDepth)
{
    if (std::abs(m_focusDepth - focusDepth) > 1e-2f) { // (1 cm)
        m_focusDepth = focusDepth;
        markAsModified();
    }
}

float Camera::fieldOfView() const
{
    return calculateFieldOfView(m_focalLength, m_sensorSize);
}

void Camera::setFieldOfView(float fov)
{
    float focalLength = calculateFocalLength(fov, m_sensorSize);
    setFocalLength(focalLength);
}

float Camera::calculateFieldOfView(float focalLength, vec2 sensorSize)
{
    // See formula: https://www.edmundoptics.co.uk/knowledge-center/application-notes/imaging/understanding-focal-length-and-field-of-view/
    //  fov = 2atan(H / 2f)

    const float f = std::max(1.0f, focalLength);
    const float H = sensorSize.y; // we want vertical anglular field of view
    float fov = 2.0f * atan2(H, 2.0f * f);

    return fov;
}

float Camera::calculateFocalLength(float fieldOfView, vec2 sensorSize)
{
    //          fov = 2atan(H / 2f)
    //      fov / 2 = atan(H / 2f)
    // tan(fov / 2) = H / 2f
    //           2f = H / tan(fov / 2)
    //            f = H / 2tan(fov / 2)

    const float& fov = fieldOfView;
    const float& H = sensorSize.y; // we want vertical anglular field of view
    float focalLength = H / (2.0f * tan(fov / 2.0f));

    return focalLength;
}

vec2 Camera::calculateAdjustedSensorSize(vec2 sensorSize, Extent2D viewportSize)
{
    float framebufferAspectRatio = static_cast<float>(viewportSize.width()) / static_cast<float>(viewportSize.height());
    return vec2(sensorSize.y * framebufferAspectRatio, sensorSize.y);
}

vec2 Camera::calculateSensorPixelSize(vec2 sensorSize, Extent2D viewportSize)
{
    // NOTE: x and y will be identical since we assume square pixels (for now).
    // Later we might want to consider non-square pixels and instead of "adjusting"
    // the sensor size we will use a crop of it.
    vec2 adjustedSensorSize = calculateAdjustedSensorSize(sensorSize, viewportSize);
    return vec2(adjustedSensorSize.x / viewportSize.width(), adjustedSensorSize.y / viewportSize.height());
}

float Camera::calculateAcceptableCircleOfConfusion(vec2 sensorSize, Extent2D viewportSize)
{
    // NOTE: There are classical answers for this based on various properties of the eye and film.
    // However, in this context we mostly care about if we're going to blur the pixel or not for a
    // DoF-like effect. For this it makes sense to consider anything CoC less than a pixel's size
    // to be in focus, hence we're basing the calculation on that.
    vec2 pixelSizeInSensor = calculateSensorPixelSize(sensorSize, viewportSize);
    float circleRadius = std::min(pixelSizeInSensor.x, pixelSizeInSensor.y);
    return circleRadius;

}

float Camera::convertCircleOfConfusionToPixelUnits(float circleOfConfusion, vec2 sensorSize, Extent2D viewportSize)
{
    // NOTE: We're still assuming square pixels..
    float pixelFromSensorMillimeters = 1.0f / calculateSensorPixelSize(sensorSize, viewportSize).x;
    return circleOfConfusion * pixelFromSensorMillimeters;
}

float Camera::calculateDepthOfField(float acceptibleCircleOfConfusionMM, float focalLengthMM, float fNumber, float focusDepthM)
{
    // See approximate formula: https://en.wikipedia.org/wiki/Depth_of_field#Factors_affecting_depth_of_field
    // DOF = (2u^2 N c) / f^2

    const float c = acceptibleCircleOfConfusionMM / 1000.0f; // (mm) -> (m)
    const float f = std::max(1.0f, focalLengthMM) / 1000.0f; // (mm) -> (m)
    const float& u = focusDepthM; // (m)
    const float& N = fNumber;

    return (2.0f * moos::square(u) * N * c) / moos::square(f);
}

vec2 Camera::calculateDepthOfFieldRange(float focusDepthM, float depthOfField)
{
    float halfField = depthOfField / 2.0f;
    float rangeMin = std::max(0.0f, focusDepthM - halfField);
    float rangeMax = std::max(0.0f, focusDepthM + halfField);
    return vec2(rangeMin, rangeMax);
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
}

vec2 Camera::frustumJitterUVCorrection() const
{
    // Remove this frame's offset, we're now "neutral", then add previous frame's offset
    vec2 totalJitterPixelOffset = -frustumJitterPixelOffset() + previousFrameFrustumJitterPixelOffset();
    float x = totalJitterPixelOffset.x / float(viewport().width());
    float y = totalJitterPixelOffset.y / float(viewport().height());
    return vec2(x, y);
}

void Camera::drawGui(bool includeContainingWindow)
{
    if (includeContainingWindow) {
        ImGui::Begin("Camera");
    }

    ImGui::Text("Focal length (f):   %.1f mm", focalLengthMillimeters());
    ImGui::Text("Effective VFOV:     %.1f degrees", moos::toDegrees(fieldOfView()));

    vec2 sensorPixelSize = calculateSensorPixelSize(m_sensorSize, viewport());
    ImGui::Text("Sensor size:        %.1f x %.1f mm", m_sensorSize.x, m_sensorSize.y);
    ImGui::Text("Sensor pixel size:  %.4f x %.4f mm", sensorPixelSize.x, sensorPixelSize.y);

    ImGui::Separator();

    ImGui::Text("Focus depth:        %.2f m", focusDepth());

    float acceptibleCocMm = calculateAcceptableCircleOfConfusion(m_sensorSize, viewport());
    float acceptibleCocPx = convertCircleOfConfusionToPixelUnits(acceptibleCocMm, m_sensorSize, viewport());
    float acceptibleDof = calculateDepthOfField(acceptibleCocMm, focalLengthMillimeters(), fNumber(), focusDepth());
    vec2 acceptibleDofRange = calculateDepthOfFieldRange(focusDepth(), acceptibleDof);
    ImGui::Text("Acceptable DOF:     %.2f m (range: %.2f m to %.2f m)", acceptibleDof, acceptibleDofRange.x, acceptibleDofRange.y);
    ImGui::Text("                    (using CoC of %.3f mm or %.2f px)", acceptibleCocMm, acceptibleCocPx);

    ImGui::Separator();

    if (ImGui::TreeNode("Focus controls")) {
        // TODO: Implement auto-focus!
        bool manualFocus = true;
        ImGui::RadioButton("Manual focus", manualFocus);

        if (manualFocus) {
            ImGui::SliderFloat("Focus depth", &m_focusDepth, 0.25f, 100.0f);
        } else {
            NOT_YET_IMPLEMENTED();
        }
        
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Exposure controls")) {
        drawExposureGui();
        ImGui::TreePop();
    }

    if (includeContainingWindow) {
        ImGui::End();
    }
}

void Camera::drawExposureGui()
{
    if (ImGui::RadioButton("Automatic exposure", useAutomaticExposure))
        useAutomaticExposure = true;
    if (ImGui::RadioButton("Manual exposure", !useAutomaticExposure))
        useAutomaticExposure = false;

    if (useAutomaticExposure) {
        drawAutomaticExposureGui();
    } else {
        drawManualExposureGui();
    }
}

void Camera::drawManualExposureGui()
{
    // Aperture
    {
        constexpr float steps[] = { 1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f };
        constexpr int stepCount = sizeof(steps) / sizeof(steps[0]);
        constexpr float apertureMin = steps[0];
        constexpr float apertureMax = steps[stepCount - 1];

        ImGui::Text("Aperture f/%.1f - f-number", aperture);

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

void Camera::drawAutomaticExposureGui()
{
    ImGui::Text("Adaption rate", &adaptionRate);
    ImGui::SliderFloat("", &adaptionRate, 0.0001f, 2.0f, "%.4f", ImGuiSliderFlags_Logarithmic);

    ImGui::Text("Exposure Compensation", &exposureCompensation);
    ImGui::SliderFloat("ECs", &exposureCompensation, -5.0f, +5.0f, "%.1f");
}
