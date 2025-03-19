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
#include <mikktspace.h>

namespace {
AssetCache<MeshAsset> s_meshAssetCache {};
}

MeshSegmentAsset::MeshSegmentAsset() = default;
MeshSegmentAsset::~MeshSegmentAsset() = default;

void MeshSegmentAsset::processForImport()
{
    // We want to generate MikkTSpace tangents - or, if not possible, generate arbitrary tangents (e.g. no texcoords)
    // The meshes may or may not already have tangents, but let's still regenerate them with the proper MikkTSpace tangent space
    // as it's cheap to do and there are definitely assets out there with broken and incorrect tangents.

    // To generate tangents a non-indexed mesh is needed
    if (isIndexedMesh()) {
        flattenToNonIndexedMesh();
    }

    // Generate the tangents
    generateTangents();

    // Convert back to an indexed mesh
    convertToIndexedMesh();

    // Optimize the mesh - non-destructive!
    optimize();

    // Generate meshlets
    generateMeshlets();
}

bool MeshSegmentAsset::isIndexedMesh() const
{
    return indices.size() > 0;
}

void MeshSegmentAsset::flattenToNonIndexedMesh()
{
    if (!isIndexedMesh()) {
        return;
    }

    std::vector<vec3> newPositions {};
    std::vector<vec3> newNormals {};
    std::vector<vec2> newTexcoord0s {};
    std::vector<vec4> newTangents {};
    std::vector<ark::tvec4<u16>> newJointIndices {};
    std::vector<vec4> newJointWeights {};

    for (u32 index : indices) {
        newPositions.push_back(positions[index]);
        newNormals.push_back(normals[index]);
        if (hasTextureCoordinates()) {
            newTexcoord0s.push_back(texcoord0s[index]);
        }
        if (hasTangents()) {
            newTangents.push_back(tangents[index]);
        }
        if (hasSkinningData()) {
            newJointIndices.push_back(jointIndices[index]);
            newJointWeights.push_back(jointWeights[index]);
        }
    }

    positions = std::move(newPositions);
    normals = std::move(newNormals);
    texcoord0s = std::move(newTexcoord0s);
    tangents = std::move(newTangents);
    jointIndices = std::move(newJointIndices);
    jointWeights = std::move(newJointWeights);

    indices.clear();

    // This is effectively invalidated by the flattening
    meshletData.reset();
}

void MeshSegmentAsset::convertToIndexedMesh()
{
    ARKOSE_ASSERT(!isIndexedMesh());

    // Generate vertex remap table

#define APPEND_STREAM(dataStream) streams.push_back({ dataStream.data(), sizeof(decltype(dataStream[0])), sizeof(decltype(dataStream[0])) })
    std::vector<meshopt_Stream> streams;

    APPEND_STREAM(positions);
    APPEND_STREAM(normals);
    if (hasTextureCoordinates()) {
        APPEND_STREAM(texcoord0s);
    }
    if (hasTangents()) {
        APPEND_STREAM(tangents);
    }
    if (hasSkinningData()) {
        APPEND_STREAM(jointIndices);
        APPEND_STREAM(jointWeights);
    }

#undef APPEND_STREAM

    size_t unindexedVertexCount = vertexCount();
    size_t indexCount = unindexedVertexCount; // since we're currently unindexed

    std::vector<u32> remap(unindexedVertexCount);
    size_t newVertexCount = meshopt_generateVertexRemapMulti(remap.data(), nullptr, indexCount, unindexedVertexCount, streams.data(), streams.size());

    // Create the new index buffer

    indices.resize(indexCount);
    meshopt_remapIndexBuffer(indices.data(), nullptr, indexCount, remap.data());

    // Create the new vertex buffers

    remapVertexData(remap, newVertexCount);
}

void MeshSegmentAsset::optimize()
{
    ARKOSE_ASSERT(isIndexedMesh());

    // Optimize for vertex caching

    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), vertexCount());

    // Optimize for overdraw

    constexpr float overdrawThreshold = 1.05f;

    float const* vertexPositions = value_ptr(positions[0]);
    size_t vertexPositionStride = sizeof(decltype(positions[0]));
    meshopt_optimizeOverdraw(indices.data(), indices.data(), indices.size(), vertexPositions, vertexCount(), vertexPositionStride, overdrawThreshold);

    // Optimize for vertex fetching

    std::vector<u32> vertFetchRemap(vertexCount());
    size_t numUniqueVertices = meshopt_optimizeVertexFetchRemap(vertFetchRemap.data(), indices.data(), indices.size(), vertexCount());
    meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(), vertFetchRemap.data());
    remapVertexData(vertFetchRemap, numUniqueVertices); // if we've just re-indexed this mesh, then the vertex count should remain unchanged
    
}

void MeshSegmentAsset::remapVertexData(std::vector<u32> const& remapTable, size_t newVertexCount)
{
    meshopt_remapVertexBuffer(positions.data(), positions.data(), positions.size(), sizeof(decltype(positions[0])), remapTable.data());
    positions.resize(newVertexCount);

    meshopt_remapVertexBuffer(normals.data(), normals.data(), normals.size(), sizeof(decltype(normals[0])), remapTable.data());
    normals.resize(newVertexCount);

    if (texcoord0s.size() > 0) {
        meshopt_remapVertexBuffer(texcoord0s.data(), texcoord0s.data(), texcoord0s.size(), sizeof(decltype(texcoord0s[0])), remapTable.data());
        texcoord0s.resize(newVertexCount);
    }

    if (tangents.size() > 0) {
        meshopt_remapVertexBuffer(tangents.data(), tangents.data(), tangents.size(), sizeof(decltype(tangents[0])), remapTable.data());
        tangents.resize(newVertexCount);
    }

    if (jointIndices.size() > 0) {
        meshopt_remapVertexBuffer(jointIndices.data(), jointIndices.data(), jointIndices.size(), sizeof(decltype(jointIndices[0])), remapTable.data());
        jointIndices.resize(newVertexCount);
    }

    if (jointWeights.size() > 0) {
        meshopt_remapVertexBuffer(jointWeights.data(), jointWeights.data(), jointWeights.size(), sizeof(decltype(jointWeights[0])), remapTable.data());
        jointWeights.resize(newVertexCount);
    }
}

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

void MeshSegmentAsset::generateTangents()
{
    SCOPED_PROFILE_ZONE();

    tangents.clear();

    bool generatedMikktspaceTangents = false;
    if (hasTextureCoordinates()) {

        // Generate proper MikkTSpace tangents

        // From the MikkTSpace documentation in mikktspace.h:
        //   "Note that the results are returned unindexed. It is possible to generate a new index list
        //    But averaging/overwriting tangent spaces by using an already existing index list WILL produce INCRORRECT results.
        //    DO NOT! use an already existing index list."
        ARKOSE_ASSERT(!isIndexedMesh());

        tangents.resize(vertexCount());

        SMikkTSpaceInterface mikktspaceInterface;

        mikktspaceInterface.m_getNumFaces = [](SMikkTSpaceContext const* pContext) -> int {
            auto* mesh = static_cast<MeshSegmentAsset*>(pContext->m_pUserData);
            return narrow_cast<int>(mesh->vertexCount()) / 3;
        };

        mikktspaceInterface.m_getNumVerticesOfFace = [](SMikkTSpaceContext const* pContext, const int iFace) -> int {
            return 3;
        };

        mikktspaceInterface.m_getPosition = [](SMikkTSpaceContext const* pContext, float fvPosOut[], const int iFace, const int iVert) -> void {
            auto* mesh = static_cast<MeshSegmentAsset*>(pContext->m_pUserData);
            vec3 position = mesh->positions[3 * iFace + iVert];
            fvPosOut[0] = position.x;
            fvPosOut[1] = position.y;
            fvPosOut[2] = position.z;
        };

        mikktspaceInterface.m_getNormal = [](SMikkTSpaceContext const* pContext, float fvNormOut[], const int iFace, const int iVert) -> void {
            auto* mesh = static_cast<MeshSegmentAsset*>(pContext->m_pUserData);
            vec3 normal = mesh->normals[3 * iFace + iVert];
            fvNormOut[0] = normal.x;
            fvNormOut[1] = normal.y;
            fvNormOut[2] = normal.z;
        };

        mikktspaceInterface.m_getTexCoord = [](SMikkTSpaceContext const* pContext, float fvTexcOut[], const int iFace, const int iVert) -> void {
            auto* mesh = static_cast<MeshSegmentAsset*>(pContext->m_pUserData);
            vec2 texcoord = mesh->texcoord0s[3 * iFace + iVert];
            fvTexcOut[0] = texcoord.x;
            fvTexcOut[1] = texcoord.y;
        };

        mikktspaceInterface.m_setTSpace = nullptr;
        mikktspaceInterface.m_setTSpaceBasic = [](SMikkTSpaceContext const* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert) -> void {
            auto* mesh = static_cast<MeshSegmentAsset*>(pContext->m_pUserData);
            vec4& tangent = mesh->tangents[3 * iFace + iVert];
            tangent.x = fvTangent[0];
            tangent.y = fvTangent[1];
            tangent.z = fvTangent[2];
            tangent.w = fSign;

            // This seems like quite the hack, and I'm not sure why the MikkTSpace library returns such
            // a tangent vector. Possibly to indicate it's a degenerate triangle or something akin to that?
            // However, we need "valid" tangents (length == 1) for all triangles so let's at least write
            // something valid in these cases.
            if (length(tangent.xyz()) == 0.0f) {
                tangent = vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
        };

        SMikkTSpaceContext mikktspaceContext;
        mikktspaceContext.m_pInterface = &mikktspaceInterface;
        mikktspaceContext.m_pUserData = this;

        generatedMikktspaceTangents = genTangSpaceDefault(&mikktspaceContext);

        ARKOSE_ASSERT(generatedMikktspaceTangents);
    }

    if (!generatedMikktspaceTangents) {

        // Was not able to generate MikkTSpace tangents so fall back onto an arbitrary tangent space

        tangents.reserve(vertexCount());
        for (vec3 n : normals) {

            vec3 orthogonal = ark::globalRight;
            if (std::abs(dot(n, orthogonal)) > 0.99f) {
                orthogonal = ark::globalForward;
            }

            orthogonal = normalize(orthogonal - dot(n, orthogonal) * n);

            float d = dot(n, orthogonal);
            ARKOSE_ASSERT(ark::isEffectivelyZero(d, 1e-4f));

            tangents.emplace_back(orthogonal.x, orthogonal.y, orthogonal.z, 1.0f);
        }
    }
}

bool MeshSegmentAsset::hasTextureCoordinates() const
{
    ARKOSE_ASSERT(texcoord0s.size() == 0 || texcoord0s.size() == positions.size());
    return texcoord0s.size() > 0;
}

bool MeshSegmentAsset::hasTangents() const
{
    ARKOSE_ASSERT(tangents.size() == 0 || tangents.size() == positions.size());
    ARKOSE_ASSERT(tangents.size() == 0 || tangents.size() == texcoord0s.size());
    return tangents.size() > 0;
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

        // Ensure we have tangents whenever we have texcoords
        //ARKOSE_ASSERT(tangents.size() == count);
    }

    // Ensure if we have any kind of skinning data, it all adds up
    if (jointIndices.size() > 0 || jointWeights.size() > 0) {
        ARKOSE_ASSERT(jointIndices.size() == jointWeights.size());
        ARKOSE_ASSERT(jointIndices.size() == count);
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
