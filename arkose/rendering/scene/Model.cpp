#include "Model.h"

bool Model::hasProxy() const
{
    return m_proxy != nullptr;
}

const Model& Model::proxy() const
{
    if (hasProxy()) {
        return *m_proxy;
    }

    // If no proxy is set, use self as proxy
    return *this;
}

void Model::setProxy(std::unique_ptr<Model> proxy)
{
    m_proxy = std::move(proxy);
}
