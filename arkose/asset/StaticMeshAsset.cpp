#include "StaticMeshAsset.h"

#include "core/Assert.h"
#include "core/Logging.h"
#include "physics/PhysicsMesh.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <meshoptimizer.h>
#include <mutex>

namespace {
    static std::mutex s_staticMeshAssetCacheMutex {};
    static std::unordered_map<std::string, std::unique_ptr<StaticMeshAsset>> s_staticMeshAssetCache {};
}

StaticMeshSegmentAsset::StaticMeshSegmentAsset() = default;
StaticMeshSegmentAsset::~StaticMeshSegmentAsset() = default;

void StaticMeshSegmentAsset::generateMeshlets()
{
    SCOPED_PROFILE_ZONE();

    constexpr size_t maxVertices = 64;
    constexpr size_t maxTriangles = 124;
    constexpr float coneWeight = 0.0f; // no cone culling

    // Reset the meshlet data for this segment
    meshletData = MeshletData();

    size_t vertCount = vertexCount();
    float const* vertexPositions = value_ptr(positions[0]);
    constexpr size_t vertexPositionStride = sizeof(vec3);

    size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, maxTriangles);
    std::vector<meshopt_Meshlet> meshlets(maxMeshlets);
    meshletData->vertices.resize(maxMeshlets * maxVertices);
    meshletData->triangles.resize(maxMeshlets * maxTriangles * 3);

    size_t meshletCount = meshopt_buildMeshlets(meshlets.data(), meshletData->vertices.data(), meshletData->triangles.data(),
                                                indices.data(), indices.size(), vertexPositions, vertCount, vertexPositionStride,
                                                maxVertices, maxTriangles, coneWeight);

    meshopt_Meshlet const& last = meshlets[meshletCount - 1];
    meshletData->vertices.resize(last.vertex_offset + last.vertex_count);
    meshletData->triangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
    meshlets.resize(meshletCount);

    meshletData->meshlets.reserve(meshletCount);
    for (meshopt_Meshlet const& meshlet : meshlets) {
        meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshletData->vertices[meshlet.vertex_offset],
                                                             &meshletData->triangles[meshlet.triangle_offset], meshlet.triangle_count,
                                                             vertexPositions, vertCount, vertexPositionStride);

        meshletData->meshlets.push_back(Meshlet { .vertices = std::span<u32>(meshletData->vertices.data() + meshlet.vertex_offset, meshlet.vertex_count),
                                                  .triangles = std::span<u8>(meshletData->triangles.data() + meshlet.triangle_offset, meshlet.triangle_count),
                                                  .center = vec3(bounds.center[0], bounds.center[1], bounds.center[2]),
                                                  .radius = bounds.radius });
    }
}

size_t StaticMeshSegmentAsset::vertexCount() const
{
    size_t count = positions.size();

    ARKOSE_ASSERT(normals.size() == count);
    if (texcoord0s.size() > 0) {
        ARKOSE_ASSERT(texcoord0s.size() == count);

        // TODO: Ensure we have tangents whenever we have UVs!
        //ARKOSE_ASSERT(tangents.size() == count);
    }

    return count;
}

std::vector<u8> StaticMeshSegmentAsset::assembleVertexData(const VertexLayout& layout) const
{
    SCOPED_PROFILE_ZONE();

    size_t packedVertexSize = layout.packedVertexSize();
    size_t bufferSize = vertexCount() * packedVertexSize;

    std::vector<u8> dataVector {};
    dataVector.resize(bufferSize);
    u8* data = dataVector.data();

    // FIXME: This only really works for float components. However, for now we only have floating point components.
    constexpr std::array<float, 4> floatOnes { 1, 1, 1, 1 };

    size_t offsetInFirstVertex = 0u;

    auto copyComponentData = [&](u8 const* input, size_t inputCount, VertexComponent component) {
        size_t componentSize = vertexComponentSize(component);
        for (size_t vertexIdx = 0, count = vertexCount(); vertexIdx < count; ++vertexIdx) {
            u8* destination = data + offsetInFirstVertex + vertexIdx * packedVertexSize;
            const u8* source = (vertexIdx < inputCount)
                ? &input[vertexIdx * componentSize]
                : (u8*)floatOnes.data();
            std::memcpy(destination, source, componentSize);
        }
        return componentSize;
    };

    for (VertexComponent component : layout.components()) {
        switch (component) {
        case VertexComponent::Position3F: {
            auto* inputData = reinterpret_cast<u8 const*>(value_ptr(*positions.data()));
            offsetInFirstVertex += copyComponentData(inputData, positions.size(), component);
        } break;
        case VertexComponent::Normal3F: {
            auto* inputData = reinterpret_cast<u8 const*>(value_ptr(*normals.data()));
            offsetInFirstVertex += copyComponentData(inputData, normals.size(), component);
        } break;
        case VertexComponent::TexCoord2F: {
            auto* inputData = reinterpret_cast<u8 const*>(value_ptr(*texcoord0s.data()));
            offsetInFirstVertex += copyComponentData(inputData, texcoord0s.size(), component);
        } break;
        case VertexComponent::Tangent4F: {
            auto* inputData = reinterpret_cast<u8 const*>(value_ptr(*tangents.data()));
            offsetInFirstVertex += copyComponentData(inputData, tangents.size(), component);
        } break;
        default: {
            ARKOSE_LOG_FATAL("Unable to assemble vertex data for unknown VertexComponent: '{}'", vertexComponentToString(component));
        } break;
        }
    }

    return dataVector;
}

StaticMeshLODAsset::StaticMeshLODAsset() = default;
StaticMeshLODAsset::~StaticMeshLODAsset() = default;

StaticMeshAsset::StaticMeshAsset() = default;
StaticMeshAsset::~StaticMeshAsset() = default;

StaticMeshAsset* StaticMeshAsset::loadFromArkmsh(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, StaticMeshAsset::AssetFileExtension)) {
        ARKOSE_LOG(Warning, "Trying to load material asset with invalid file extension: '{}'", filePath);
    }

    {
        SCOPED_PROFILE_ZONE_NAMED("Static mesh cache - load");
        std::scoped_lock<std::mutex> lock { s_staticMeshAssetCacheMutex };

        auto entry = s_staticMeshAssetCache.find(filePath);
        if (entry != s_staticMeshAssetCache.end()) {
            return entry->second.get();
        }
    }

    std::ifstream fileStream(filePath, std::ios::binary);
    if (not fileStream.is_open()) {
        return nullptr;
    }

    std::unique_ptr<StaticMeshAsset> newStaticMeshAsset {};

    cereal::BinaryInputArchive binaryArchive(fileStream);

    AssetHeader header;
    binaryArchive(header);

    if (header == AssetHeader(AssetMagicValue)) {

        newStaticMeshAsset = std::make_unique<StaticMeshAsset>();
        binaryArchive(*newStaticMeshAsset);

    } else {

        fileStream.seekg(0); // seek back to the start

        if (static_cast<char>(fileStream.peek()) != '{') {
            ARKOSE_LOG(Error, "Failed to parse json text for static mesh asset '{}'", filePath);
            return nullptr;
        }

        cereal::JSONInputArchive jsonArchive(fileStream);

        newStaticMeshAsset = std::make_unique<StaticMeshAsset>();
        jsonArchive(*newStaticMeshAsset);
    }

    newStaticMeshAsset->m_assetFilePath = filePath;

    {
        SCOPED_PROFILE_ZONE_NAMED("Static mesh cache - store");
        std::scoped_lock<std::mutex> lock { s_staticMeshAssetCacheMutex };
        s_staticMeshAssetCache[filePath] = std::move(newStaticMeshAsset);
        return s_staticMeshAssetCache[filePath].get();
    }
}

bool StaticMeshAsset::writeToArkmsh(std::string_view filePath, AssetStorage assetStorage)
{
    SCOPED_PROFILE_ZONE();

    if (not AssetHelpers::isValidAssetPath(filePath, StaticMeshAsset::AssetFileExtension)) {
        ARKOSE_LOG(Error, "Trying to write static mesh asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    ARKOSE_ASSERT(m_assetFilePath.empty() || m_assetFilePath == filePath);
    m_assetFilePath = filePath;

    std::ofstream fileStream { m_assetFilePath, std::ios::binary | std::ios::trunc };
    if (not fileStream.is_open()) {
        return false;
    }

    switch (assetStorage) {
    case AssetStorage::Binary: {
        cereal::BinaryOutputArchive archive(fileStream);
        archive(AssetHeader(AssetMagicValue));
        archive(*this);
    } break;
    case AssetStorage::Json: {
        cereal::JSONOutputArchive archive(fileStream);
        archive(cereal::make_nvp("static_mesh", *this));
    } break;
    }

    fileStream.close();
    return true;
}

std::vector<PhysicsMesh> StaticMeshAsset::createPhysicsMeshes(size_t lodIdx) const
{
    ARKOSE_ASSERT(lodIdx < LODs.size());
    StaticMeshLODAsset const& lod = LODs[lodIdx];

    std::vector<PhysicsMesh> physicsMeshes {};
    for (StaticMeshSegmentAsset const& meshSegment : lod.meshSegments) {

        PhysicsMesh& physicsMesh = physicsMeshes.emplace_back();
        physicsMesh.positions = meshSegment.positions;
        physicsMesh.indices = meshSegment.indices;

        // TODO: Not yet implemented!
        // physicsMesh.material
    }

    return physicsMeshes;
}
