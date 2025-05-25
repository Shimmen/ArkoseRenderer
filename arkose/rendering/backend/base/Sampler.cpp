#include "Sampler.h"

Sampler::Sampler(Backend& backend, Description& desc)
    : Resource(backend)
    , m_description(desc)
{
}
