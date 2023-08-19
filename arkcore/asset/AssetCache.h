#pragma once

#include "utility/Profiling.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

template<typename AssetType>
class AssetCache final {
public:
    AssetCache() = default;
    ~AssetCache() = default;

    AssetType* get(std::string const& path)
    {
        SCOPED_PROFILE_ZONE_NAMED("Asset cache - get");
        std::scoped_lock<std::mutex> lock { m_cacheMutex };

        auto entry = m_cache.find(path);
        if (entry != m_cache.end()) {
            return entry->second.get();
        } else {
            return nullptr;
        }
    }

    AssetType* put(std::string const& path, std::unique_ptr<AssetType>&& asset)
    {
        SCOPED_PROFILE_ZONE_NAMED("Asset cache - put");
        std::scoped_lock<std::mutex> lock { m_cacheMutex };

        m_cache[path] = std::move(asset);
        return m_cache[path].get();
    }

private:
    std::mutex m_cacheMutex {};
    std::unordered_map<std::string, std::unique_ptr<AssetType>> m_cache {};
};
