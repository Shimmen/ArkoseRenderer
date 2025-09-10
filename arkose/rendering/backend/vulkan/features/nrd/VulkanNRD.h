#pragma once

#if WITH_NRD

#include "rendering/backend/base/ExternalFeature.h"
#include <vulkan/vulkan.h>

#include <NRD.h>
#include <NRDDescs.h>
#include <NRDSettings.h>

class VulkanBackend;

class VulkanNRD {
public:
    VulkanNRD(VulkanBackend&);
    ~VulkanNRD();

    bool isReadyToUse() const;

    nrd::Instance* nrdInstance() { return m_nrdInstance; }

private:
    VulkanBackend& m_backend;
    nrd::Instance* m_nrdInstance { nullptr };
};

class VulkanNRDSigmaShadowExternalFeature : public ExternalFeature {
public:
    VulkanNRDSigmaShadowExternalFeature(VulkanBackend&, VulkanNRD&, ExternalFeatureCreateParamsNRDSigmaShadow const&);
    ~VulkanNRDSigmaShadowExternalFeature();

    void evaluate(ExternalFeatureEvaluateParamsNRDSigmaShadow const&) const;

private:
    VulkanNRD& m_nrd;
};

#endif // WITH_NRD
