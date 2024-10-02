#pragma once

#include "core/Logging.h"
#include <ark/handle.h>
#include <algorithm>
#include <vector>

template<typename ResourceType, typename HandleType>
class ResourceList final {

public:

    explicit ResourceList(const char* name, size_t capacity)
        : m_name(name)
        , m_capacity(capacity)
    {
        // This will potentially waste some memory but will also ensure zero allocations
        m_resources.reserve(m_capacity);
        m_resourcesMetadata.reserve(m_capacity);
        m_freeList.reserve(m_capacity);
        m_deferredDeleteList.reserve(m_capacity);
    }

    ~ResourceList()
    {
        // right now this only happens at shutdown, so we can ignore it
        //ARKOSE_ASSERT(m_resources.size() == 0);
    }

    size_t capacity() const
    {
        return m_capacity;
    }

    size_t size() const
    {
        return m_actualSize;
    }

    bool isValidHandle(HandleType handle) const
    {
        if (not handle.valid()) {
            return false;
        }

        if (handle.index() >= m_resources.size()) {
            return false;
        }

        ResourceMetadata const& resourceMetadata = m_resourcesMetadata[handle.index()];
        return resourceMetadata.alive;
    }

    HandleType add(ResourceType&& resource)
    {
        HandleType handle;

        if (m_freeList.size() > 0) {

            handle = m_freeList.back();
            m_freeList.pop_back();

            m_resources[handle.index()] = std::move(resource);
            m_resourcesMetadata[handle.index()] = ResourceMetadata();

        } else {

            uint64_t handleIdx = m_resources.size();
            if (handleIdx >= m_capacity) {
                ARKOSE_LOG(Fatal, "Ran out of capacity for {}, exiting.", m_name);
            }

            handle = HandleType(handleIdx);
            m_resources.emplace_back(std::move(resource));
            m_resourcesMetadata.emplace_back();
        }

        m_actualSize += 1;

        return handle;
    }

    ResourceType& get(HandleType handle)
    {
        ARKOSE_ASSERT(isValidHandle(handle));
        return m_resources[handle.index()];
    }

    ResourceType const& get(HandleType handle) const
    {
        ARKOSE_ASSERT(isValidHandle(handle));
        return m_resources[handle.index()];
    }

    ResourceType& set(HandleType handle, ResourceType&& resource)
    {
        ARKOSE_ASSERT(isValidHandle(handle));
        m_resources[handle.index()] = std::move(resource);
        return m_resources[handle.index()];
    }

    void markPersistent(HandleType handle)
    {
        ResourceMetadata& resourceMetadata = getMetadata(handle);
        resourceMetadata.persistent = true;
        resourceMetadata.referenceCount = 0;
    }

    void addReference(HandleType handle)
    {
        ResourceMetadata& resourceMetadata = getMetadata(handle);
        if (!resourceMetadata.persistent) {
            resourceMetadata.referenceCount += 1;
        }
    }

    bool removeReference(HandleType handle, size_t currentFrame)
    {
        ResourceMetadata& resourceMetadata = getMetadata(handle);

        if (resourceMetadata.persistent) {
            ARKOSE_ASSERT(resourceMetadata.referenceCount == 0);
            return false;
        }

        ARKOSE_ASSERT(resourceMetadata.referenceCount > 0);
        resourceMetadata.referenceCount -= 1;

        bool noRemainingReferences = resourceMetadata.referenceCount == 0;

        if (noRemainingReferences) {
            resourceMetadata.zeroReferencesAtFrame = currentFrame;
            m_deferredDeleteList.push_back(handle);
        }

        return noRemainingReferences;
    }

    template<typename DeleterFunction>
    size_t processDeferredDeletes(size_t currentFrame, size_t deferFrames, DeleterFunction&& deleterFunction)
    {
        if (m_deferredDeleteList.empty()) {
            return 0;
        }

        size_t numDeletes = 0;

        for (int64_t idx = std::ssize(m_deferredDeleteList) - 1; idx >= 0; idx -= 1) {

            HandleType handle = m_deferredDeleteList[idx];
            ResourceMetadata& resourceMetadata = getMetadata(handle);

            // Persistent resources should never be put into this list!
            ARKOSE_ASSERT(!resourceMetadata.persistent);

            bool removeFromList = false;
            bool deleteResource = false;

            // Since the delete was requested we have regained enough references to keep us alive, remove from this list
            if (resourceMetadata.referenceCount > 0) {
                removeFromList = true;
                resourceMetadata.zeroReferencesAtFrame = SIZE_MAX;
            }

            ARKOSE_ASSERT(currentFrame >= resourceMetadata.zeroReferencesAtFrame);
            if (currentFrame - resourceMetadata.zeroReferencesAtFrame > deferFrames) {
                removeFromList = true;
                deleteResource = true;
            }

            if (deleteResource) {

                ResourceType& resource = m_resources[handle.index()];
                deleterFunction(handle, resource);

                resourceMetadata.alive = false;
                resourceMetadata.referenceCount = 0;
                //resourceMetadata.zeroReferencesAtFrame = SIZE_MAX;

                m_freeList.push_back(handle);

                numDeletes += 1;
            }

            if (removeFromList) {
                // Remove from the list, only pop from the back
                if (idx < std::ssize(m_deferredDeleteList) - 1) {
                    std::swap(m_deferredDeleteList[idx], m_deferredDeleteList.back());
                }
                m_deferredDeleteList.pop_back();
            }
        }

        ARKOSE_ASSERT(numDeletes <= m_actualSize);
        m_actualSize -= numDeletes;

        return numDeletes;
    }

    // TODO: Implement iterator for ResourceList
    template<typename CallbackFunction>
    void forEachResource(CallbackFunction&& callback) const
    {
        for (size_t idx = 0; idx < size(); ++idx) {
            if (m_resourcesMetadata[idx].alive) {
                callback(m_resources[idx]);
            }
        }
    }

    std::span<ResourceType const> resourceSpan() const
    {
        return std::span<ResourceType const> { m_resources.data(), size() };
    }

private:
    struct ResourceMetadata {
        bool alive { true };
        bool persistent { false };
        size_t referenceCount { 1 };
        size_t zeroReferencesAtFrame { SIZE_MAX };
        // TODO: Add some kind of generation value to track use-after-free?
    };

    ResourceMetadata& getMetadata(HandleType handle)
    {
        ARKOSE_ASSERT(handle.valid());
        ARKOSE_ASSERT(handle.index() < m_resourcesMetadata.size());

        ResourceMetadata& resourceMetadata = m_resourcesMetadata[handle.index()];
        ARKOSE_ASSERT(resourceMetadata.alive);

        return resourceMetadata;
    }

    std::vector<ResourceType> m_resources {};
    std::vector<ResourceMetadata> m_resourcesMetadata {};

    std::vector<HandleType> m_freeList {};
    std::vector<HandleType> m_deferredDeleteList {};
    size_t m_actualSize { 0 };

    const char* m_name = "ResourceList";
    size_t m_capacity { 0 };
};
