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

    template<typename Func>
    AssetType* getOrCreate(std::filesystem::path const& path, Func&& createCallback)
    {
        SCOPED_PROFILE_ZONE();
        std::scoped_lock<std::mutex> lock { m_cacheMutex };

        auto entry = m_cache.find(path);
        if (entry != m_cache.end()) {
            return entry->second.get();
        } else {
            std::unique_ptr<AssetType> newAsset = createCallback();
            if (newAsset != nullptr) {
                m_cache[path] = std::move(newAsset);
                return m_cache[path].get();
            } else {
                return nullptr;
            }
        }
    }

    AssetType* put(std::filesystem::path const& path, std::unique_ptr<AssetType>&& asset)
    {
        SCOPED_PROFILE_ZONE();
        std::scoped_lock<std::mutex> lock { m_cacheMutex };

        m_cache[path] = std::move(asset);
        return m_cache[path].get();
    }

private:
    std::mutex m_cacheMutex {};
    std::unordered_map<std::filesystem::path, std::unique_ptr<AssetType>> m_cache {};
};
