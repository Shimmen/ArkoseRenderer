#include "ExternalFeature.h"

ExternalFeature::ExternalFeature(Backend& backend, ExternalFeatureType type)
    : Resource(backend)
    , m_type(type)
{
}

float ExternalFeature::queryParameterF(ExternalFeatureParameter param)
{
    ARKOSE_LOG(Error, "ExternalFeature: querying unsupported parameter ({}) for this feature", param);
    return 0.0f;
}
