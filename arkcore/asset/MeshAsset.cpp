#include "MeshAsset.h"

#include "asset/AssetCache.h"
#include "asset/TextureCompressor.h"
#include "core/Assert.h"
#include "core/Logging.h"
#include "physics/PhysicsMesh.h"
#include "utility/FileIO.h"
#include "utility/Profiling.h"
#include <ark/defer.h>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <meshoptimizer.h>
#include <mikktspace.h>

#if PLATFORM_WINDOWS
#include <omm.hpp>
#endif

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

    // Generate flat normals if none are available
    if (!hasNormals()) {
        generateFlatNormals();
    }

    // Generate the tangents
    generateTangents();

    // Convert back to an indexed mesh
    convertToIndexedMesh();

    // Optimize the mesh - non-destructive!
    optimize();

    // Generate meshlets
    generateMeshlets();

    // Disable for now - these maps should ideally operate on the compressed images,
    // which makes the deferred compression a little tricky.. but later we can perhaps
    // compress the image in-line with the mesh import specifically for meshes with
    // materials with masked blend mode. A little complex, but should work.
    // Either way, OMMs aren't fully implemented yet, so this is a waste of time.
    #if 0//PLATFORM_WINDOWS
    // Generate opacity micro-maps, if relevant
    MaterialAsset* materialAsset = MaterialAsset::load(material);
    if (materialAsset && materialAsset->blendMode == BlendMode::Masked) {
        generateOpacityMicroMap();
    }
    #endif
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
        if (hasNormals()) {
            newNormals.push_back(normals[index]);
        }
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

    if (hasMorphTargets()) {
        std::vector<MorphTargetAsset> newMorphTargets {};
        newMorphTargets.reserve(morphTargets.size());

        for (MorphTargetAsset& morphTarget : morphTargets) {
            MorphTargetAsset& newMorphTarget = newMorphTargets.emplace_back();
            newMorphTarget.name = morphTarget.name;

            for (u32 index : indices) {
                newMorphTarget.positions.push_back(morphTarget.positions[index]);
                if (morphTarget.normals.size() > 0) {
                    newMorphTarget.normals.push_back(morphTarget.normals[index]);
                }
                if (morphTarget.tangents.size() > 0) {
                    newMorphTarget.tangents.push_back(morphTarget.tangents[index]);
                }
            }
        }

        morphTargets = std::move(newMorphTargets);
    }

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
    if (hasMorphTargets()) {
        for (MorphTargetAsset& morphTarget : morphTargets) {
            APPEND_STREAM(morphTarget.positions);
            if (morphTarget.normals.size() > 0) {
                APPEND_STREAM(morphTarget.normals);
            }
            if (morphTarget.tangents.size() > 0) {
                APPEND_STREAM(morphTarget.tangents);
            }
        }
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

    for (MorphTargetAsset& morphTarget : morphTargets) {
        meshopt_remapVertexBuffer(morphTarget.positions.data(), morphTarget.positions.data(), morphTarget.positions.size(), sizeof(decltype(morphTarget.positions[0])), remapTable.data());
        morphTarget.positions.resize(newVertexCount);
        if (morphTarget.normals.size() > 0) {
            meshopt_remapVertexBuffer(morphTarget.normals.data(), morphTarget.normals.data(), morphTarget.normals.size(), sizeof(decltype(morphTarget.normals[0])), remapTable.data());
            morphTarget.normals.resize(newVertexCount);
        }
        if (morphTarget.tangents.size() > 0) {
            meshopt_remapVertexBuffer(morphTarget.tangents.data(), morphTarget.tangents.data(), morphTarget.tangents.size(), sizeof(decltype(morphTarget.tangents[0])), remapTable.data());
            morphTarget.tangents.resize(newVertexCount);
        }
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

void MeshSegmentAsset::generateFlatNormals()
{
    ARKOSE_ASSERT(!isIndexedMesh());
    ARKOSE_ASSERT(normals.size() == 0);
    ARKOSE_ASSERT(positions.size() % 3 == 0);

    normals.reserve(positions.size());

    for (size_t idx = 0; idx < positions.size(); idx += 3) {
        size_t idx0 = idx + 0;
        size_t idx1 = idx + 1;
        size_t idx2 = idx + 2;

        vec3 v1 = positions[idx1] - positions[idx0];
        vec3 v2 = positions[idx2] - positions[idx0];
        vec3 normal = normalize(cross(v1, v2));

        normals.push_back(normal);
        normals.push_back(normal);
        normals.push_back(normal);
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

#if PLATFORM_WINDOWS
void MeshSegmentAsset::generateOpacityMicroMap()
{
    SCOPED_PROFILE_ZONE();

    omm::BakerCreationDesc desc {};
    desc.type = omm::BakerType::CPU;

    desc.messageInterface.userArg = nullptr;
    desc.messageInterface.messageCallback = [](omm::MessageSeverity severity, const char* message, void* userArg) {
        ARKOSE_LOG(Info, "OMM-SDK message: [{}] {}", severity, message);
    };

    omm::Baker ommBaker;
    omm::Result res = omm::CreateBaker(desc, &ommBaker);

    if (res != omm::Result::SUCCESS) {
        ARKOSE_LOG(Error, "Failed to create OMM baker, will not generate opacity micro-maps.");
        return;
    }

    ark::AtScopeExit destroyBaker([&]() {
        omm::Result destroyRes = omm::DestroyBaker(ommBaker);
        ARKOSE_ASSERT(destroyRes == omm::Result::SUCCESS);
    });

    //

    MaterialAsset* materialAsset = MaterialAsset::load(material);
    if (!materialAsset) {
        ARKOSE_LOG(Error, "Failed to load material asset'{}', will not generate opacity micro-map.", material);
        return;
    }

    ARKOSE_ASSERT(materialAsset->blendMode == BlendMode::Masked);
    ARKOSE_ASSERT(materialAsset->baseColor.has_value());

    ImageAsset* baseColorImage = ImageAsset::load(materialAsset->baseColor->image);
    if (!baseColorImage) {
        ARKOSE_LOG(Error, "Failed to load base color image for material '{}', will not generate opacity micro-map.", materialAsset->name);
        return;
    }

    //
    // Create the texture
    //

    ImageAsset* alphaImageAsset = baseColorImage;

    std::unique_ptr<ImageAsset> decompressedAsset;
    if (baseColorImage->hasCompressedFormat()) {
        TextureCompressor textureCompressor;
        decompressedAsset = textureCompressor.decompressToRGBA32F(*baseColorImage);
        alphaImageAsset = decompressedAsset.get();
    }

    // TODO: Use some lower mip, probably, maybe?! OMM-SDK recommends using a single, fixed mip for all ray tracing,
    // and similarly use that exact same mip for the OMM creation, so we should probably do that.
    static constexpr u32 mipIndex = 0;

    Extent3D targetMipExtent = alphaImageAsset->extentAtMip(mipIndex);

    std::vector<float> dstAlphaPixelData;
    dstAlphaPixelData.reserve(targetMipExtent.width() * targetMipExtent.height() * targetMipExtent.depth());

    switch (alphaImageAsset->format()) {
    case ImageFormat::RGBA8: {
        std::span<const u8> srcPixelDataU8 = alphaImageAsset->pixelDataForMip(mipIndex);
        for (size_t i = 0; i < srcPixelDataU8.size(); i += 4) {
            u8 a = srcPixelDataU8[i + 3];
            dstAlphaPixelData.push_back(static_cast<float>(a) / 255.0f);
        }
    } break;
    case ImageFormat::RGBA32F: {
        std::span<const u8> srcPixelDataU8 = alphaImageAsset->pixelDataForMip(mipIndex);
        std::span<const f32> srcPixelDataF32 = std::span<const f32>(
            reinterpret_cast<f32 const*>(srcPixelDataU8.data()),
            srcPixelDataU8.size() / sizeof(f32));
        for (size_t i = 0; i < srcPixelDataF32.size(); i += 4) {
            f32 a = srcPixelDataF32[i + 3];
            dstAlphaPixelData.push_back(a);
        }
    } break;
    default:
        ARKOSE_LOG(Error, "Unsupported image format '{}' for baking opacity micro-maps, will not generate opacity micro-map.", baseColorImage->format());
        return;
    }

    omm::Cpu::TextureMipDesc mipDesc;
    mipDesc.width = targetMipExtent.width();
    mipDesc.height = targetMipExtent.height();
    mipDesc.textureData = dstAlphaPixelData.data();

    omm::Cpu::TextureDesc texDesc;
    texDesc.format = omm::Cpu::TextureFormat::FP32;
    texDesc.mipCount = 1;
    texDesc.mips = &mipDesc;

    omm::Cpu::Texture ommTexture;
    omm::Result createTexRes = omm::Cpu::CreateTexture(ommBaker, texDesc, &ommTexture);

    if (createTexRes != omm::Result::SUCCESS) {
        ARKOSE_LOG(Error, "Failed to create OMM texture, will not generate opacity micro-map.");
        return;
    }

    ark::AtScopeExit destroyTexture([&]() {
        omm::Result destroyRes = omm::Cpu::DestroyTexture(ommBaker, ommTexture);
        ARKOSE_ASSERT(destroyRes == omm::Result::SUCCESS);
    });

    //
    // Set up the baking parameters
    //

    omm::Cpu::BakeInputDesc bakeDesc {};

    bakeDesc.bakeFlags = omm::Cpu::BakeFlags::None;
    // bakeDesc.bakeFlags |= omm::Cpu::BakeFlags::EnableValidation;

    bakeDesc.texture = ommTexture;
    bakeDesc.alphaMode = omm::AlphaMode::Test;
    bakeDesc.alphaCutoff = materialAsset->maskCutoff;

    // Use mag filter here, I think that makes most sense..?
    ImageFilter imageFilter = materialAsset->baseColor->magFilter;

    switch (imageFilter) {
    case ImageFilter::Nearest:
        bakeDesc.runtimeSamplerDesc.filter = omm::TextureFilterMode::Nearest;
        break;
    case ImageFilter::Linear:
        bakeDesc.runtimeSamplerDesc.filter = omm::TextureFilterMode::Linear;
        break;
    default:
        ASSERT_NOT_REACHED();
    }

    // All/both need to be the same for this to work..
    ARKOSE_ASSERT(materialAsset->baseColor->wrapModes.u == materialAsset->baseColor->wrapModes.v);
    ImageWrapMode wrapMode = materialAsset->baseColor->wrapModes.u;

    switch (wrapMode) {
    case ImageWrapMode::Repeat:
        bakeDesc.runtimeSamplerDesc.addressingMode = omm::TextureAddressMode::Wrap;
        break;
    case ImageWrapMode::MirroredRepeat:
        bakeDesc.runtimeSamplerDesc.addressingMode = omm::TextureAddressMode::Mirror;
        break;
    case ImageWrapMode::ClampToEdge:
        bakeDesc.runtimeSamplerDesc.addressingMode = omm::TextureAddressMode::Clamp;
        break;
    default:
        ARKOSE_LOG(Error, "Unsupported wrap mode for opacity micro-map: '{}', will not generate opacity micro-map.", wrapMode);
        return;
    }

    bakeDesc.texCoordFormat = omm::TexCoordFormat::UV32_FLOAT;
    bakeDesc.texCoordStrideInBytes = sizeof(vec2);
    bakeDesc.texCoords = texcoord0s.data();

    bakeDesc.indexFormat = omm::IndexFormat::UINT_32;
    bakeDesc.indexBuffer = indices.data();
    bakeDesc.indexCount = narrow_cast<u32>(indices.size());

    // Consider if we want to support other modes here.. For now, I think 2-state with force-opaque makes most sense,
    // as we mainly want to speed up indirect rays where the exact alpha-maksed shapes of objects don't matter too much.
    constexpr bool twoState = true;
    if (twoState) {
        bakeDesc.format = omm::Format::OC1_2_State;
        bakeDesc.unknownStatePromotion = omm::UnknownStatePromotion::ForceOpaque;
    } else {
        bakeDesc.format = omm::Format::OC1_4_State;
        bakeDesc.unknownStatePromotion = omm::UnknownStatePromotion::Nearest;
    }

    //
    // Bake!
    //

    omm::Cpu::BakeResult ommBakeResult;
    omm::Result bakeRes = omm::Cpu::Bake(ommBaker, bakeDesc, &ommBakeResult);

    if (bakeRes != omm::Result::SUCCESS) {
        ARKOSE_LOG(Error, "Failed to bake OMM, will not generate opacity micro-map.");
        return;
    }

    ark::AtScopeExit destroyBakeResult([&]() {
        omm::Result destroyRes = omm::Cpu::DestroyBakeResult(ommBakeResult);
        ARKOSE_ASSERT(destroyRes == omm::Result::SUCCESS);
    });

    //
    // Read the result from the baker
    //

    omm::Cpu::BakeResultDesc const* bakeResultDesc = nullptr;
    omm::Result getResultRes = omm::Cpu::GetBakeResultDesc(ommBakeResult, &bakeResultDesc);

    if (getResultRes != omm::Result::SUCCESS) {
        ARKOSE_LOG(Error, "Failed to get OMM bake result description, will not be able to write out opacity micro-map.");
        return;
    }

    //
    // Serialize OMM results
    //

    if (bakeResultDesc) {

        // Debug output from OMM-SDK that can be used to verify that things look alright.
        // omm::Debug::SaveAsImages(ommBaker, bakeDesc, bakeResultDesc, { .path = "TestOutput", .oneFile = true });

        omm::Cpu::DeserializedDesc serializeDesc {};
        serializeDesc.flags = omm::Cpu::SerializeFlags::Compress;
        serializeDesc.numResultDescs = 1;
        serializeDesc.resultDescs = bakeResultDesc;

        omm::Cpu::SerializedResult ommSerializedResult;
        omm::Result serializeRes = omm::Cpu::Serialize(ommBaker, serializeDesc, &ommSerializedResult);

        if (serializeRes != omm::Result::SUCCESS) {
            ARKOSE_LOG(Error, "Failed to serialize OMM bake results, will not be able to write out opacity micro-map.");
            return;
        }

        ark::AtScopeExit destroySerializedResult([&]() {
            omm::Result destroyRes = omm::Cpu::DestroySerializedResult(ommSerializedResult);
            ARKOSE_ASSERT(destroyRes == omm::Result::SUCCESS);
        });

        omm::Cpu::BlobDesc const* blobDesc;
        omm::Result getSerializedRes = omm::Cpu::GetSerializedResultDesc(ommSerializedResult, &blobDesc);

        if (getSerializedRes != omm::Result::SUCCESS) {
            ARKOSE_LOG(Error, "Failed to get OMM serialized data, will not be able to write out opacity micro-map.");
            return;
        }

        //
        // "Attach" data to this mesh segment asset
        //

        std::byte const* blobDataBytes = static_cast<std::byte*>(blobDesc->data);

        OpacityMicroMapDataAsset ommDataAsset {};
        ommDataAsset.ommSdkSerializedData = std::vector<std::byte>(blobDataBytes, blobDataBytes + blobDesc->size);

        this->opacityMicroMapData = ommDataAsset;
    }
}
#endif

bool MeshSegmentAsset::hasTextureCoordinates() const
{
    ARKOSE_ASSERT(texcoord0s.size() == 0 || texcoord0s.size() == positions.size());
    return texcoord0s.size() > 0;
}

bool MeshSegmentAsset::hasNormals() const
{
    ARKOSE_ASSERT(normals.size() == 0 || normals.size() == positions.size());
    return normals.size() > 0;
}

bool MeshSegmentAsset::hasTangents() const
{
    ARKOSE_ASSERT(tangents.size() == 0 || tangents.size() == positions.size());
    return tangents.size() > 0;
}

bool MeshSegmentAsset::hasSkinningData() const
{
    return jointIndices.size() == jointWeights.size() && jointIndices.size() == vertexCount();
}

bool MeshSegmentAsset::hasMorphTargets() const
{
    return morphTargets.size() > 0;
}

size_t MeshSegmentAsset::vertexCount() const
{
    size_t count = positions.size();

    ARKOSE_ASSERT(texcoord0s.size() == 0 || texcoord0s.size() == count);
    ARKOSE_ASSERT(normals.size() == 0 || normals.size() == count);
    ARKOSE_ASSERT(tangents.size() == 0 || tangents.size() == count);

    // Ensure if we have any kind of skinning data, it all adds up
    if (jointIndices.size() > 0 || jointWeights.size() > 0) {
        ARKOSE_ASSERT(jointIndices.size() == jointWeights.size());
        ARKOSE_ASSERT(jointIndices.size() == count);
    }

    if (hasMorphTargets()) {
        for (MorphTargetAsset const& morphTarget : morphTargets) {
            ARKOSE_ASSERT(morphTarget.positions.size() == count);
            ARKOSE_ASSERT(morphTarget.normals.size() == 0 || morphTarget.normals.size() == count);
            ARKOSE_ASSERT(morphTarget.tangents.size() == 0 || morphTarget.tangents.size() == count);
        }
    }

    return count;
}

std::vector<u8> MeshSegmentAsset::assembleVertexData(VertexLayout const& layout, size_t firstVertex, size_t numVertices) const
{
    SCOPED_PROFILE_ZONE();

    if (numVertices == 0) {
        numVertices = vertexCount();
        if (firstVertex > 0) {
            ARKOSE_ASSERT(numVertices > firstVertex);
            numVertices -= firstVertex;
        }
    }

    ARKOSE_ASSERT(firstVertex + numVertices <= vertexCount());

    size_t packedVertexSize = layout.packedVertexSize();
    size_t bufferSize = numVertices * packedVertexSize;

    std::vector<u8> dataVector {};
    dataVector.resize(bufferSize);
    u8* data = dataVector.data();

    size_t offsetInFirstVertex = 0u;

    auto copyComponentDataWithTransformation = [&]<typename SrcT, typename DstT, typename Func>(std::vector<SrcT> const& input, DstT defaultValue, Func&& transformer) {
        for (size_t vertexIdx = firstVertex; vertexIdx < firstVertex + numVertices; ++vertexIdx) {
            size_t dstIdx = vertexIdx - firstVertex;
            u8* destination = data + offsetInFirstVertex + dstIdx * packedVertexSize;
            DstT source = (vertexIdx < input.size()) ? transformer(input[vertexIdx]) : defaultValue;
            std::memcpy(destination, &source, sizeof(DstT));
        }
        return sizeof(DstT);
    };

    auto copyComponentData = [&]<typename T>(std::vector<T> const& input, T defaultValue) {
        return copyComponentDataWithTransformation(input, defaultValue, [](T value) { return value; });
    };

    for (VertexComponent component : layout.components()) {
        switch (component) {
        case VertexComponent::Position3F: {
            offsetInFirstVertex += copyComponentData(positions, vec3(0.0f));
        } break;
        case VertexComponent::Normal3F: {
            offsetInFirstVertex += copyComponentData(normals, vec3(0.0f, 0.0f, 1.0f));
        } break;
        case VertexComponent::TexCoord2F: {
            offsetInFirstVertex += copyComponentData(texcoord0s, vec2(0.0f));
        } break;
        case VertexComponent::Tangent4F: {
            offsetInFirstVertex += copyComponentData(tangents, vec4(1.0f, 0.0f, 0.0f, 1.0f));
        } break;
        case VertexComponent::JointWeight4F: {
            offsetInFirstVertex += copyComponentData(jointWeights, vec4(0.0f));
        } break;
        case VertexComponent::JointIdx4U32: {
            auto transformJointIdxToU32 = [](ark::tvec4<u16> idxU16) -> uvec4 {
                return uvec4(static_cast<u32>(idxU16.x),
                             static_cast<u32>(idxU16.y),
                             static_cast<u32>(idxU16.z),
                             static_cast<u32>(idxU16.w));
            };
            offsetInFirstVertex += copyComponentDataWithTransformation(jointIndices, uvec4(0), transformJointIdxToU32);
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

MeshAsset* MeshAsset::load(std::filesystem::path const& filePath)
{
    SCOPED_PROFILE_ZONE();

    if (!isValidAssetPath(filePath)) {
        ARKOSE_LOG(Warning, "Trying to load mesh asset with invalid file extension: '{}'", filePath);
    }

    return s_meshAssetCache.getOrCreate(filePath, [&]() {
        auto newMeshAsset = std::make_unique<MeshAsset>();
        if (newMeshAsset->readFromFile(filePath)) {
            return newMeshAsset;
        } else {
            return std::unique_ptr<MeshAsset>();
        }
    });
}

MeshAsset* MeshAsset::manage(std::unique_ptr<MeshAsset>&& meshAsset)
{
    ARKOSE_ASSERT(!meshAsset->assetFilePath().empty());
    return s_meshAssetCache.put(meshAsset->assetFilePath(), std::move(meshAsset));
}

bool MeshAsset::readFromFile(std::filesystem::path const& filePath)
{
    std::ifstream fileStream(filePath, std::ios::binary);
    if (!fileStream.is_open()) {
        ARKOSE_LOG(Error, "Failed to load mesh asset at path '{}'", filePath);
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

    setAssetFilePath(filePath);

    if (name.empty()) {
        name = filePath.stem().string();
    }

    return true;
}

bool MeshAsset::writeToFile(std::filesystem::path const& filePath, AssetStorage assetStorage) const
{
    SCOPED_PROFILE_ZONE();

    if (not isValidAssetPath(filePath)) {
        ARKOSE_LOG(Error, "Trying to write asset to file with invalid extension: '{}'", filePath);
        return false;
    }

    std::ofstream fileStream { filePath, std::ios::binary | std::ios::trunc };
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
