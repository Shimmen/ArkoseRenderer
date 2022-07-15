#include "Resource.h"

Resource::Resource()
    : m_backend(nullptr)
{
}

Resource::Resource(Backend& backend)
    : m_backend(&backend)
{
}

Resource::Resource(Resource&& other) noexcept
    : m_backend(other.m_backend)
{
    other.m_backend = nullptr;
}

const std::string& Resource::name() const
{
    return m_name;
}

void Resource::setName(const std::string& name)
{
    m_name = name;
}

void Resource::setReusable(Badge<Registry>, bool reusable)
{
    m_reusable = reusable;
}

void Resource::setOwningRegistry(Badge<Registry>, Registry* registry)
{
    m_owningRegistry = registry;
}

Resource& Resource::operator=(Resource&& other) noexcept
{
    m_backend = other.m_backend;
    other.m_backend = nullptr;
    return *this;
}