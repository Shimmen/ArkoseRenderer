#pragma once

#include <ark/badge.h>
#include <string>

class Backend;
class Registry;

class Resource {
public:
    Resource();
    explicit Resource(Backend&);
    virtual ~Resource() = default;

    Resource(Resource&) = delete;
    Resource& operator=(Resource&) = delete;

    Resource(Resource&&) noexcept;
    Resource& operator=(Resource&&) noexcept;

    const std::string& name() const;
    virtual void setName(const std::string&);

    void setReusable(Badge<Registry>, bool reusable);
    bool reusable(Badge<Registry>) const { return m_reusable; }

    void setOwningRegistry(Badge<Registry>, Registry* registry);
    Registry* owningRegistry(Badge<Registry>) { return m_owningRegistry; }

protected:
    bool hasBackend() const { return m_backend != nullptr; }
    Backend& backend() { return *m_backend; }
    const Backend& backend() const { return *m_backend; }

private:
    Backend* m_backend { nullptr };
    Registry* m_owningRegistry { nullptr };
    bool m_reusable { false };
    std::string m_name {};
};
