#include "MeshAsset.h"

#include "asset/AssetCache.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "physics/PhysicsMesh.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <meshoptimizer.h>

namespace {
AssetCache<MeshAsset> s_meshAssetCache {};
}

MeshSegmentAsset::MeshSegmentAsset() = default;
MeshSegmentAsset::~MeshSegmentAsset() = default;

void MeshSegmentAsset::generateMeshlets()
{
    SCOPED_PROFILE_ZONE();

    constexpr size_t maxVertices = 64; // good for nvidia
    constexpr size_t maxTriangles = 124; // 126 is good for nvidia, but meshopt only supports multiples of 4
    constexpr float coneWeight = 0.0f; // no cone culling

    size_t vertCount = vertexCount();
    float const* vertexPositions = value_ptr(positions[0]);
    constexpr size_t vertexPositionStride = sizeof(vec3);

    size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, maxTriangles);
    std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
    std::vector<u32> meshoptMeshletVertexIndirection(maxMeshlets * maxVertices);
    std::vector<u8> meshoptMeshletTriangles(maxMeshlets * maxTriangles * 3);

    size_t meshletCount = meshopt_buildMeshlets(meshoptMeshlets.data(), meshoptMeshletVertexIndirection.data(), meshoptMeshletTriangles.data(),
                                                indices.data(), indices.size(), vertexPositions, vertCount, vertexPositionStride,
                                                maxVertices, maxTriangles, coneWeight);

    meshopt_Meshlet const& last = meshoptMeshlets[meshletCount - 1];
    meshoptMeshletVertexIndirection.resize(last.vertex_offset + last.vertex_count);
    meshoptMeshletTriangles.resize(last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3));
    meshoptMeshlets.resize(meshletCount);

    meshletData = MeshletDataAsset();
    meshletData->meshletVertexIndirection = meshoptMeshletVertexIndirection; // NOTE: Copy over directly

    u32 baseVertexOffset = 0;

    meshletData->meshlets.reserve(meshletCount);
    for (meshopt_Meshlet const& meshlet : meshoptMeshlets) {

        u32 baseIndexOffset = narrow_cast<u32>(meshletData->meshletIndices.size());

        // Remap meshlet indices onto the "global" index buffer
        for (u32 index = 0; index < meshlet.triangle_count * 3; ++index) {
            u8 triangleIndirection = meshoptMeshletTriangles[meshlet.triangle_offset + index];
            meshletData->meshletIndices.push_back(baseVertexOffset + triangleIndirection);
        }

        baseVertexOffset += meshlet.vertex_count;

        // Get bounds of meshlet for culling
        meshopt_Bounds bounds = meshopt_computeMeshletBounds(&meshoptMeshletVertexIndirection[meshlet.vertex_offset],
                                                             &meshoptMeshletTriangles[meshlet.triangle_offset], meshlet.triangle_count,
                                                             vertexPositions, vertCount, vertexPositionStride);

        meshletData->meshlets.push_back(MeshletAsset { .firstIndex = baseIndexOffset,
                                                       .triangleCount = meshlet.triangle_count,
                                                       .firstVertex = meshlet.vertex_offset,
                                                       .vertexCount = meshlet.vertex_count,
                                                       .center = vec3(bounds.center[0], bounds.center[1], bounds.center[2]),
                                                       .radius = bounds.radius });
    }
}

bool MeshSegmentAsset::hasSkinningData() const
{
    return jointIndices.size() == jointWeights.size() && jointIndices.size() == vertexCount();
}

size_t MeshSegmentAsset::vertexCount() const
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

std::vector<u8> MeshSegmentAsset::assembleVertexData(const VertexLayout& layout) const
{
    SCOPED_PROFILE_ZONE();

    size_t packedVertexSize = layout.packedVertexSize();
    size_t bufferSize = vertexCount() * packedVertexSize;

    std::vector<u8> dataVector {};
    dataVector.resize(bufferSize);
    u8* data = dataVector.data();

    // TODO: We should really be more strict about mismatching vertex counts and instead handle it at asset-level!
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
        case VertexComponent::JointWeight4F: {
            auto* inputData = reinterpret_cast<u8 const*>(value_ptr(*jointWeights.data()));
            offsetInFirstVertex += copyComponentData(inputData, jointWeights.size(), component);
        } break;
        case VertexComponent::JointIdx4U32: {
            std::vector<u32> jointIndicesU32;
            jointIndicesU32.reserve(jointIndices.size() * 4);
            for (ark::tvec4<u16> idxU16 : jointIndices) {
                jointIndicesU32.emplace_back(static_cast<u32>(idxU16.x));
                jointIndicesU32.emplace_back(static_cast<u32>(idxU16.y));
                jointIndicesU32.emplace_back(static_cast<u32>(idxU16.z));
                jointIndicesU32.emplace_back(static_cast<u32>(idxU16.w));
            }
            auto* inputData = reinterpret_cast<u8 const*>(jointIndicesU32.data());
            offsetInFirstVertex += copyComponentData(inputData, jointIndicesU32.size(), component);
        } break;
        default: {
            ARKOSE_LOG(Fatal, "Unable to assemble vertex data for unknown VertexComponent: '{}'", vertexComponentToString(component));
        } break;
        }
    }

    return dataVector;
}

MeshLODAsset::MeshLODAsset() = default;
MeshLODAsset::~MeshLODAsset() = default;

MeshAsset::MeshAsset() = default;
MeshAsset::~MeshAsset() = default;

MeshAsset* MeshAsset::load(std::string const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load material asset with invalid file extension: '{}'", filePath);
    }

    if (MeshAsset* cachedAsset = s_meshAssetCache.get(filePath)) {
        return cachedAsset;
    }

    auto newMeshAsset = std::make_unique<MeshAsset>();
    bool success = newMeshAsset->readFromFile(filePath);

    if (!success) {
        return nullptr;
    }

    return s_meshAssetCache.put(filePath, std::move(newMeshAsset));
}

MeshAsset* MeshAsset::manage(std::unique_ptr<MeshAsset>&& meshAsset)
{
    ARKOSE_ASSERT(!meshAsset->assetFilePath().empty());
    return s_meshAssetCache.put(std::string(meshAsset->assetFilePath()), std::move(meshAsset));
}

bool MeshAsset::readFromFile(std::string_view filePath)
{
    std::ifstream fileStream(std::string(filePath), std::ios::binary);
    if (not fileStream.is_open()) {
        return false;
    }

    cereal::BinaryInputArchive binaryArchive(fileStream);

    AssetHeader header;
    binaryArchive(header);

    if (header == AssetHeader(AssetMagicValue)) {

        binaryArchive(*this);

    } else {

        fileStream.seekg(0); // seek back to the start

        if (static_cast<char>(fileStream.peek()) != '{') {
            ARKOSE_LOG(Error, "Failed to parse json text for asset '{}'", filePath);
            return false;
        }

        cereal::JSONInputArchive jsonArchive(fileStream);
        jsonArchive(*this);
    }

    this->setAssetFilePath(filePath);

    if (name.empty()) {
        this->name = FileIO::removeExtensionFromPath(FileIO::extractFileNameFromPath(filePath));
    }

    return true;
}

bool MeshAsset::writeToFile(std::string_view filePath, AssetStorage assetStorage) const
{
    SCOPED_PROFILE_ZONE();

    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Error, "Trying to write asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    std::ofstream fileStream { std::string(filePath), std::ios::binary | std::ios::trunc };
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
        archive(cereal::make_nvp("mesh", *this));
    } break;
    }

    fileStream.close();
    return true;
}

std::vector<PhysicsMesh> MeshAsset::createPhysicsMeshes(size_t lodIdx) const
{
    ARKOSE_ASSERT(lodIdx < LODs.size());
    MeshLODAsset const& lod = LODs[lodIdx];

    std::vector<PhysicsMesh> physicsMeshes {};
    for (MeshSegmentAsset const& meshSegment : lod.meshSegments) {

        PhysicsMesh& physicsMesh = physicsMeshes.emplace_back();
        physicsMesh.positions = meshSegment.positions;
        physicsMesh.indices = meshSegment.indices;

        // TODO: Not yet implemented!
        // physicsMesh.material
    }

    return physicsMeshes;
}

PhysicsMesh MeshAsset::createUnifiedPhysicsMesh(size_t lodIdx) const
{
    ARKOSE_ASSERT(lodIdx < LODs.size());
    MeshLODAsset const& lod = LODs[lodIdx];

    PhysicsMesh physicsMesh;

    for (MeshSegmentAsset const& meshSegment : lod.meshSegments) {
        u32 segmentIndexOffset = narrow_cast<u32>(physicsMesh.positions.size());

        for (vec3 position : meshSegment.positions) {
            physicsMesh.positions.push_back(position);
        }

        for (u32 index : meshSegment.indices) {
            physicsMesh.indices.push_back(segmentIndexOffset + index);
        }
    }

    return physicsMesh;
}
