
#include <ark/vector.h>
#include <asset/MeshAsset.h>
#include <asset/TextureCompressor.h>
#include <core/Logging.h>
#include <utility/Profiling.h>

// token stuff
#include <pxr/base/tf/token.h>
#include <pxr/usd/kind/registry.h>

// generic primvar access
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

// usd mesh stuff
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/xform.h>

// usd camera stuff
#include <pxr/usd/usdGeom/camera.h>

// triangulation stuff
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/usdImaging/usdImaging/meshAdapter.h>

// indexification and mesh optimizing etc.
#include <meshoptimizer.h>

// tangent space generation
#include <mikktspace.h>

// Everything is namespaced with Usd* or similar anyway.. A lot of typing can be saved with this.
// In the future we should probably do the same but ensure it doesn't leak into headers.
using namespace pxr;

ARK_DISABLE_OPTIMIZATIONS

struct UnindexedTriangleMesh {
    std::vector<vec3> positions;
    std::vector<vec2> texcoords;
    std::vector<vec3> normals;
    std::vector<vec4> tangents;
};

void generateGeometricFaceNormals(UnindexedTriangleMesh& triangleMesh)
{
    SCOPED_PROFILE_ZONE();

    ARKOSE_ASSERT(triangleMesh.normals.size() == 0);
    ARKOSE_ASSERT(triangleMesh.positions.size() > 0);
    triangleMesh.normals.resize(triangleMesh.positions.size());

    size_t triangleCount = triangleMesh.positions.size() / 3;
    for (size_t triIdx = 0; triIdx < triangleCount; ++triIdx) {

        size_t idx0 = 3 * triIdx + 0;
        size_t idx1 = 3 * triIdx + 1;
        size_t idx2 = 3 * triIdx + 2;

        vec3 v0 = triangleMesh.positions[idx0];
        vec3 v1 = triangleMesh.positions[idx1];
        vec3 v2 = triangleMesh.positions[idx2];

        // NOTE: Assuming right-handed, CCW triangle winding
        vec3 n = normalize(cross(v1 - v0, v2 - v0));

        triangleMesh.normals[idx0] = n;
        triangleMesh.normals[idx1] = n;
        triangleMesh.normals[idx2] = n;

    }
}

void generateSmoothNormals(UnindexedTriangleMesh& triangleMesh)
{
    // TODO: Add up normals for similar/identical vertices and normalize at the end
    ASSERT_NOT_REACHED();
}

bool isSingleIndexedTriangleMesh(pxr::UsdGeomMesh const& usdMesh)
{
    SCOPED_PROFILE_ZONE();

    pxr::UsdAttribute subdivAttr = usdMesh.GetSubdivisionSchemeAttr();
    pxr::TfToken subdivToken;
    subdivAttr.Get(&subdivToken);
    if (subdivToken.GetString() != pxr::UsdGeomTokens->none) {
        // This is a subdivision mesh, won't be single-indexed or triangle based
        return false;
    }

    pxr::UsdAttribute faceVertexCountAttr = usdMesh.GetFaceVertexCountsAttr();
    ARKOSE_ASSERT(faceVertexCountAttr.HasValue()); // TODO: It has to exist, right?!
    pxr::VtArray<int> faceVertexCounts;
    faceVertexCountAttr.Get(&faceVertexCounts);

    for (int faceVertexCount : faceVertexCounts) {
        if (faceVertexCount != 3) {
            return false;
        }
    }

    pxr::UsdPrim const& meshPrim = usdMesh.GetPrim();
    pxr::UsdGeomPrimvarsAPI primvarsAPI{ meshPrim };

    pxr::UsdAttribute pointsAttr = usdMesh.GetPointsAttr();
    ARKOSE_ASSERT(pointsAttr.HasValue());
    pxr::VtValue pointsAttrValue;
    pointsAttr.Get(&pointsAttrValue);
    ARKOSE_ASSERT(pointsAttrValue.IsArrayValued());
    size_t pointsCount = pointsAttrValue.GetArraySize();

    // just checking..
    pxr::UsdAttribute faceVertexIndicesAttr = usdMesh.GetFaceVertexIndicesAttr();
    ARKOSE_ASSERT(faceVertexIndicesAttr.HasValue()); // TODO: It has to exist, right?!

    bool hasOtherIndexBuffers = false;
    bool hasNormalsPrimvar = false;

    std::vector<pxr::UsdGeomPrimvar> primvars = primvarsAPI.GetPrimvarsWithValues();
    ARKOSE_LOG(Info, "Mesh prim {} has {} primvars with values", meshPrim.GetPath().GetText(), primvars.size());

    for (pxr::UsdGeomPrimvar const& primvar : primvars) {

        if (primvar.GetPrimvarName() == pxr::UsdGeomTokens->normals) {
            hasNormalsPrimvar = true;
        }

        ARKOSE_LOG(Info, "  primvar '{}' (indexed={})", primvar.GetName().GetText(), primvar.IsIndexed());
        if (primvar.IsIndexed()) {
            if (primvar.GetPrimvarName() == pxr::UsdGeomTokens->normals ||
                primvar.GetPrimvarName() == pxr::TfToken("st") ||
                primvar.GetPrimvarName() == pxr::TfToken("st0")) {
                hasOtherIndexBuffers |= true;
            }
        } else {
            pxr::UsdAttribute attr = primvar.GetAttr();
            pxr::VtValue value;
            attr.Get(&value);

            size_t numItems = 1;
            if (value.IsArrayValued()) {
                numItems = value.GetArraySize();
            }

            if (numItems == pointsCount) {
                ARKOSE_LOG(Info, "    has {} array items, same as number of points, assumed to use the same indices", numItems);
            } else {
                ARKOSE_LOG(Info, "    has {} array items, DIFFERENT from the number of points, not sure how to interpret this...", numItems);
            }
        }
    }

    if (hasOtherIndexBuffers) {
        return false;
    }

    // If there is no primvar:normals authored there should be a normals attribute (assuming it's not a subdivision mesh)
    // also just checking.. if the normals attribute is not authored it should mean there is a primvar:normals instead which we will already have picked up on
    if (!hasNormalsPrimvar) {
        ARKOSE_LOG(Info, "  does NOT have a normals primvar, checking non-primvar attribute...");
        pxr::UsdAttribute normalsAttr = usdMesh.GetNormalsAttr();
        if (normalsAttr.HasAuthoredValue()) {
            pxr::VtValue value;
            normalsAttr.Get(&value);
            ARKOSE_ASSERT(value.IsArrayValued());
            size_t normalsCount = value.GetArraySize();
            if (normalsCount == pointsCount) {
                ARKOSE_LOG(Info, "    has {} array items, same as number of points, assumed to use the same indices", normalsCount);
            } else {
                ARKOSE_LOG(Info, "    has {} array items, DIFFERENT from the number of points, not sure how to interpret this...", normalsCount);
            }
        } else {
            ARKOSE_LOG(Info, "    mesh does NOT have any normals defined, we will have to generate them");
        }
    }

    return true;
}

void populateUnindexedTriangleMesh(pxr::UsdGeomMesh const& usdMesh, UnindexedTriangleMesh& triangleMesh)
{
    SCOPED_PROFILE_ZONE();

    // NOTE: Assumed that the mesh is single-indexed and triangle based!

    //
    // Collect all attributes
    //

    pxr::UsdAttribute faceVertexIndicesAttr = usdMesh.GetFaceVertexIndicesAttr();

    pxr::UsdAttribute pointsAttr = usdMesh.GetPointsAttr();
    pxr::UsdAttribute normalsAttr = usdMesh.GetNormalsAttr();
    pxr::UsdAttribute texcoordsAttr = pxr::UsdAttribute();

    pxr::UsdPrim const& meshPrim = usdMesh.GetPrim();
    pxr::UsdGeomPrimvarsAPI primvarsAPI{ meshPrim };

    pxr::UsdGeomPrimvar normalsPrimvar = primvarsAPI.GetPrimvar(pxr::UsdGeomTokens->normals);
    if (normalsPrimvar.HasValue()) {
        normalsAttr = normalsPrimvar.GetAttr();
    }

    // TODO: Find texcoord primvars from the material inputs instead of just guessing (it's usually not going to be correct this way...)
    pxr::UsdGeomPrimvar stPrimvar = primvarsAPI.GetPrimvar(pxr::TfToken("st"));
    pxr::UsdGeomPrimvar st0Primvar = primvarsAPI.GetPrimvar(pxr::TfToken("st0"));
    if (st0Primvar.HasValue()) {
        texcoordsAttr = st0Primvar.GetAttr();
    } else {
        ARKOSE_ASSERT(stPrimvar.HasValue());
        texcoordsAttr = stPrimvar.GetAttr();
    }

    //
    // Unindex vertices (required for tangent generation) & populate intermediate mesh
    //

    pxr::VtArray<int> indices;
    ARKOSE_ASSERT(faceVertexIndicesAttr.Get(&indices));

    pxr::VtArray<pxr::GfVec3f> points;
    ARKOSE_ASSERT(pointsAttr.Get(&points));

    pxr::VtArray<pxr::GfVec3f> normals;
    ARKOSE_ASSERT(normalsAttr.Get(&normals));

    pxr::VtArray<pxr::GfVec2f> texcoords;
    ARKOSE_ASSERT(texcoordsAttr.Get(&texcoords));

    ARKOSE_ASSERT(points.size() > 0);
    ARKOSE_ASSERT(points.size() == normals.size());
    ARKOSE_ASSERT(points.size() == texcoords.size());

    size_t indexCount = indices.size();
    triangleMesh.positions.reserve(indexCount);
    triangleMesh.texcoords.reserve(indexCount);
    triangleMesh.normals.reserve(indexCount);

    for (int index : indices) {

        pxr::GfVec3f point = points[index];
        pxr::GfVec2f texcoord = texcoords[index];
        pxr::GfVec3f normal = normals[index];

        // why not, eh?
        normal.Normalize();

        triangleMesh.positions.emplace_back(point[0], point[1], point[2]);
        triangleMesh.texcoords.emplace_back(texcoord[0], texcoord[1]);
        triangleMesh.normals.emplace_back(normal[0], normal[1], normal[2]);

    }
}

void triangulateMesh(pxr::UsdGeomMesh const& usdMesh, UnindexedTriangleMesh& triangleMesh)
{
    SCOPED_PROFILE_ZONE();

    // TODO: This whole implementation is pretty sketchy.. needs some good verification & testing

    pxr::UsdPrim const& meshPrim = usdMesh.GetPrim();
    pxr::UsdGeomPrimvarsAPI primvarsAPI{ meshPrim };

    pxr::UsdImagingMeshAdapter adapter;
    pxr::VtValue topology = adapter.GetTopology(meshPrim, meshPrim.GetPath(), pxr::UsdTimeCode::Default());
    if (topology.IsEmpty()) {
        throw std::logic_error("triangulation failed!");
    } else {
        //ARKOSE_LOG(Info, "Mesh '{}' has topology '{}'", topology.);
    }

    pxr::HdMeshUtil meshUtil{ &topology.Get<pxr::HdMeshTopology>(), meshPrim.GetPath() };

    pxr::VtVec3iArray indices;
    pxr::VtIntArray primitiveParams;
    meshUtil.ComputeTriangleIndices(&indices, &primitiveParams);
    const size_t numTriangles = indices.size();

    auto pointsAttr = usdMesh.GetPointsAttr();
    pxr::VtArray<pxr::GfVec3f> indexedPoints;
    pointsAttr.Get(&indexedPoints);

    for (size_t i = 0; i < numTriangles; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            pxr::GfVec3f point = indexedPoints[indices[i][j]];
            triangleMesh.positions.emplace_back(point[0], point[1], point[2]);
        }
    }

    // Triangulate normal attribute (or generate them)
    //
    // Note from UsdGeomMesh.h:
    //
    // Normals should not be authored on a subdivision mesh, since subdivision
    // algorithms define their own normals. They should only be authored for
    // polygonal meshes (_subdivisionScheme_ = "none").
    //
    // The _normals_ attribute inherited from UsdGeomPointBased is not a generic
    // primvar, but the number of elements in this attribute will be determined by
    // its _interpolation_.  See \ref UsdGeomPointBased::GetNormalsInterpolation() .
    // If _normals_ and _primvars:normals_ are both specified, the latter has
    // precedence.  If a polygonal mesh specifies __neither__ _normals_ nor
    // _primvars:normals_, then it should be treated and rendered as faceted,
    // with no attempt to compute smooth normals.
    //
    {
        bool shouldGenerateNormals = false;
        bool shouldGenerateSmoothNormals = false;

        /// - __catmullClark__: The default, Catmull-Clark subdivision; preferred
        /// for quad-dominant meshes (generalizes B-splines); interpolation
        /// of point data is smooth (non-linear)
        /// - __loop__: Loop subdivision; preferred for purely triangular meshes;
        /// interpolation of point data is smooth (non-linear)
        /// - __bilinear__: Subdivision reduces all faces to quads (topologically
        /// similar to "catmullClark"); interpolation of point data is bilinear
        /// - __none__: No subdivision, i.e. a simple polygonal mesh; interpolation
        /// of point data is linear
        ///
        /// Polygonal meshes are typically lighter weight and faster to render,
        /// depending on renderer and render mode.  Use of "bilinear" will produce
        /// a similar shape to a polygonal mesh and may offer additional guarantees
        /// of watertightness and additional subdivision features (e.g. holes) but
        /// may also not respect authored normals.
        pxr::UsdAttribute subdivAttr = usdMesh.GetSubdivisionSchemeAttr();
        pxr::TfToken subdivToken;
        subdivAttr.Get(&subdivToken);
        if (subdivToken == pxr::UsdGeomTokens->none) {

            ///.  The fallback interpolation, if left unspecified, is UsdGeomTokens->vertex ,
            /// which will generally produce smooth shading on a polygonal mesh.
            /// To achieve partial or fully faceted shading of a polygonal mesh
            /// with normals, one should use UsdGeomTokens->faceVarying or
            /// UsdGeomTokens->uniform interpolation.
            pxr::TfToken normalsInterpToken = usdMesh.GetNormalsInterpolation();
            if (normalsInterpToken == pxr::UsdGeomTokens->vertex) {
                shouldGenerateNormals = true;
                shouldGenerateSmoothNormals = true;
            } else {
                // NOTE: For now we're not distinguishing between faceVarying vs uniform interpolation.
                shouldGenerateNormals = true;
                shouldGenerateSmoothNormals = false;
            }

        } else {

            ARKOSE_LOG(Info, "Encountered subdivision mesh '{}' ({}). We do not yet support that so the mesh will be treated as-is and smooth normals will be generated for it.", meshPrim.GetName().GetText(), subdivToken.GetString());
            shouldGenerateNormals = true;
            shouldGenerateSmoothNormals = true;

        }

        if (shouldGenerateNormals) {
            if (shouldGenerateSmoothNormals) {
                // TODO: Implement!
                // TODO: Maybe we should ask the USD subdivision to just generate a mesh with the required subdivision, and that will include normals
                ARKOSE_LOG(Warning, "Should generate smooth normals, but not yet implemented, generating geometric face normals instead.");
                //generateSmoothNormals(triangleMesh);
                generateGeometricFaceNormals(triangleMesh);
            } else {
                generateGeometricFaceNormals(triangleMesh);
            }
        } else {

            bool hasNormals = false;
            pxr::VtArray<pxr::GfVec3f> normals;

            pxr::UsdGeomPrimvar normalsPrimvar = primvarsAPI.GetPrimvar(pxr::TfToken("normals"));
            if (normalsPrimvar.HasValue()) {
                hasNormals = normalsPrimvar.Get(&normals);
            } else {
                pxr::UsdAttribute normalsAttr = usdMesh.GetNormalsAttr();
                hasNormals = normalsAttr.Get(&normals);
            }

            hasNormals = hasNormals && normals.size() > 0;
            ARKOSE_ASSERT(hasNormals);

            // Create temp buffer source for the normal buffer
            pxr::TfToken name("temp");
            pxr::VtValue normalsVal(normals);
            pxr::HdVtBufferSource buffer(name, normalsVal);

            // Specify a value for storing the triangulated normals
            pxr::VtValue triangulatedNormalsVal;

            // NOTE: This function is only for **face varying** primvars!
            bool success = meshUtil.ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(), static_cast<int>(buffer.GetNumElements()), buffer.GetTupleType().type, &triangulatedNormalsVal);
            ARKOSE_ASSERT(success);

            pxr::VtArray<pxr::GfVec3f> triangulatedNormals = triangulatedNormalsVal.Get<pxr::VtArray<pxr::GfVec3f>>();

            for (size_t i = 0; i < numTriangles; ++i) {
                for (size_t j = 0; j < 3; ++j) {
                    // TODO: Does this line up with the non-indexed positions..? No, probably not. How do we ensure that?!
                    pxr::GfVec3f normal = triangulatedNormals[i * 3 + j];
                    triangleMesh.normals.emplace_back(normal[0], normal[1], normal[2]);
                }
            }
        }
    }

    // Triangulate UV coordinates
    {
        pxr::UsdGeomPrimvar stPrimvar = primvarsAPI.GetPrimvar(pxr::TfToken("st"));
        if (!stPrimvar.HasValue()) {
            stPrimvar = primvarsAPI.GetPrimvar(pxr::TfToken("st0"));
        }

        if (stPrimvar.HasValue()) {

            pxr::VtArray<pxr::GfVec2f> STs;
            stPrimvar.Get(&STs);

            pxr::TfToken interpolation = stPrimvar.GetInterpolation();
            int elementSize = stPrimvar.GetElementSize();

            // Create temp buffer source for the ST buffer
            pxr::TfToken name("temp");
            pxr::VtValue stsVal(STs);
            pxr::HdVtBufferSource buffer(name, stsVal);

            // Specify a value for storing the triangulated STs
            pxr::VtValue triangulatedSTsVal;

            bool success = meshUtil.ComputeTriangulatedFaceVaryingPrimvar(buffer.GetData(), static_cast<int>(buffer.GetNumElements()), buffer.GetTupleType().type, &triangulatedSTsVal);
            ARKOSE_ASSERT(success);

            pxr::VtArray<pxr::GfVec2f> triangulatedSTs = triangulatedSTsVal.Get<pxr::VtArray<pxr::GfVec2f>>();

            for (size_t i = 0; i < numTriangles; ++i) {
                for (size_t j = 0; j < 3; ++j) {
                    // TODO: Does this line up with the non-indexed positions..? No, probably not. How do we ensure that?!
                    pxr::GfVec2f st = triangulatedSTs[i * 3 + j];
                    triangleMesh.texcoords.emplace_back(st[0], st[1]);
                }
            }
        }
    }

    ARKOSE_LOG(Info, "After triangulation: {} triangles with {} vertices, {} normals, {} UVs", numTriangles,
               triangleMesh.positions.size(), triangleMesh.normals.size(), triangleMesh.texcoords.size());
}

void indexifyMesh(UnindexedTriangleMesh const& triangleMesh, MeshSegmentAsset& meshSegmentAsset)
{
    SCOPED_PROFILE_ZONE();

    size_t nonIndexedVertexCount = triangleMesh.positions.size();
    size_t indexCount = nonIndexedVertexCount;

    bool indexTexcoords = triangleMesh.texcoords.size() > 0;
    bool indexNormals = triangleMesh.normals.size() > 0;
    bool indexTangents = triangleMesh.tangents.size() > 0;

    std::vector<meshopt_Stream> vertexStreams {};
    vertexStreams.push_back({ .data = reinterpret_cast<void const*>(triangleMesh.positions.data()),
                              .size = sizeof(vec3),
                              .stride = sizeof(vec3) });

    if (indexTexcoords) {
        vertexStreams.push_back({ .data = reinterpret_cast<void const*>(triangleMesh.texcoords.data()),
                                  .size = sizeof(vec2),
                                  .stride = sizeof(vec2) });
    }

    if (indexNormals) {
        vertexStreams.push_back({ .data = reinterpret_cast<void const*>(triangleMesh.normals.data()),
                                  .size = sizeof(vec3),
                                  .stride = sizeof(vec3) });
    }

    if (indexTangents) {
        vertexStreams.push_back({ .data = reinterpret_cast<void const*>(triangleMesh.tangents.data()),
                                  .size = sizeof(vec4),
                                  .stride = sizeof(vec4) });
    }

    std::vector<u32> remapTable(indexCount); // allocate temporary memory for the remap table
    size_t vertexCount = meshopt_generateVertexRemapMulti(remapTable.data(), nullptr, nonIndexedVertexCount, nonIndexedVertexCount, vertexStreams.data(), vertexStreams.size());

    ARKOSE_LOG(Info, "Remapped {} vertices to {} vertices (with {} indices)", nonIndexedVertexCount, vertexCount, indexCount);

    // Generate index buffer

    meshSegmentAsset.indices.resize(indexCount);
    meshopt_remapIndexBuffer(meshSegmentAsset.indices.data(), nullptr, indexCount, remapTable.data());

    // Generate indexed vertex buffers

    meshSegmentAsset.positions.resize(vertexCount);
    meshopt_remapVertexBuffer(meshSegmentAsset.positions.data(), triangleMesh.positions.data(), nonIndexedVertexCount, sizeof(vec3), remapTable.data());

    if (indexTexcoords) {
        meshSegmentAsset.texcoord0s.resize(vertexCount);
        meshopt_remapVertexBuffer(meshSegmentAsset.texcoord0s.data(), triangleMesh.texcoords.data(), nonIndexedVertexCount, sizeof(vec2), remapTable.data());
    }

    if (indexNormals) {
        meshSegmentAsset.normals.resize(vertexCount);
        meshopt_remapVertexBuffer(meshSegmentAsset.normals.data(), triangleMesh.normals.data(), nonIndexedVertexCount, sizeof(vec3), remapTable.data());
    }

    if (indexTangents) {
        meshSegmentAsset.tangents.resize(vertexCount);
        meshopt_remapVertexBuffer(meshSegmentAsset.tangents.data(), triangleMesh.tangents.data(), nonIndexedVertexCount, sizeof(vec4), remapTable.data());
    }
}

void optimizeMesh(MeshSegmentAsset& meshSegmentAsset)
{
    // TODO: Perform in-place optimizations on our indexed mesh!
    NOT_YET_IMPLEMENTED();
}

void generateArbitraryTangentSpace(UnindexedTriangleMesh& triangleMesh)
{
    // We can only generate proper tangents if we have texcoordinates. If not, define arbitrary tangents, orthogonal to the normals
    for (vec3 n : triangleMesh.normals) {

        // hacky conversions...
        pxr::GfVec3f normal = pxr::GfVec3f(n.x, n.y, n.z);
        if (normal.GetLength() < 0.99f) {
            ARKOSE_LOG(Warning, "Normal length is not 1.0 when generating tangent - using arbitrary (1,0,0) normal instead");
            normal = pxr::GfVec3f(1.0f, 0.0f, 0.0f);
        }

        pxr::GfVec3f tangent, bitangent;
        normal.BuildOrthonormalFrame(&tangent, &bitangent);

        triangleMesh.tangents.emplace_back(tangent[0], tangent[1], tangent[2], 1.0f);
    }
}

void generateMikkTSpaceTangents(UnindexedTriangleMesh& triangleMesh)
{
    SCOPED_PROFILE_ZONE();

    size_t vertexCount = triangleMesh.positions.size();

    // We can only generate proper tangents if we have texcoordinates. If not, define arbitrary tangents, orthogonal to the normals
    if (triangleMesh.texcoords.size() == 0) {
        for (vec3 n : triangleMesh.normals) {
            // TODO: Pick a valid tangent, orthogonal to the normal!
            triangleMesh.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
        }
        return;
    }

    ARKOSE_ASSERT(triangleMesh.texcoords.size() == vertexCount);
    ARKOSE_ASSERT(triangleMesh.normals.size() == vertexCount);
    triangleMesh.tangents.resize(vertexCount);

    SMikkTSpaceInterface mikktspaceInterface;

    mikktspaceInterface.m_getNumFaces = [](SMikkTSpaceContext const* pContext) -> int {
        auto* mesh = static_cast<UnindexedTriangleMesh*>(pContext->m_pUserData);
        ARKOSE_ASSERT(mesh->positions.size() % 3 == 0);
        return static_cast<int>(mesh->positions.size()) / 3; // TODO: Make narrow_cast for this!
    };

    mikktspaceInterface.m_getNumVerticesOfFace = [](SMikkTSpaceContext const* pContext, const int iFace) -> int {
        return 3; // NOTE: Mesh should already be triangulated at this point!
    };

    mikktspaceInterface.m_getPosition = [](SMikkTSpaceContext const* pContext, float fvPosOut[], const int iFace, const int iVert) -> void {
        auto* mesh = static_cast<UnindexedTriangleMesh*>(pContext->m_pUserData);
        vec3 position = mesh->positions[3 * iFace + iVert];
        fvPosOut[0] = position.x;
        fvPosOut[1] = position.y;
        fvPosOut[2] = position.z;
    };

    mikktspaceInterface.m_getNormal = [](SMikkTSpaceContext const* pContext, float fvNormOut[], const int iFace, const int iVert) -> void {
        auto* mesh = static_cast<UnindexedTriangleMesh*>(pContext->m_pUserData);
        vec3 normal = mesh->normals[3 * iFace + iVert];
        fvNormOut[0] = normal.x;
        fvNormOut[1] = normal.y;
        fvNormOut[2] = normal.z;
    };

    mikktspaceInterface.m_getTexCoord = [](SMikkTSpaceContext const* pContext, float fvTexcOut[], const int iFace, const int iVert) -> void {
        auto* mesh = static_cast<UnindexedTriangleMesh*>(pContext->m_pUserData);
        vec2 texcoord = mesh->texcoords[3 * iFace + iVert];
        fvTexcOut[0] = texcoord.x;
        fvTexcOut[1] = texcoord.y;
    };

    mikktspaceInterface.m_setTSpace = nullptr;
    mikktspaceInterface.m_setTSpaceBasic = [](SMikkTSpaceContext const* pContext, const float fvTangent[], const float fSign, const int iFace, const int iVert) -> void {
        auto* mesh = static_cast<UnindexedTriangleMesh*>(pContext->m_pUserData);
        vec4& tangent = mesh->tangents[3 * iFace + iVert];
        tangent.x = fvTangent[0];
        tangent.y = fvTangent[1];
        tangent.z = fvTangent[2];
        tangent.w = fSign;
    };

    SMikkTSpaceContext mikktspaceContext;
    mikktspaceContext.m_pInterface = &mikktspaceInterface;
    mikktspaceContext.m_pUserData = &triangleMesh;

    bool success = genTangSpaceDefault(&mikktspaceContext);
    ARKOSE_ASSERT(success);
}

void generateTangents(UnindexedTriangleMesh& triangleMesh)
{
    SCOPED_PROFILE_ZONE();

    size_t vertexCount = triangleMesh.positions.size();
    ARKOSE_ASSERT(vertexCount > 0);
    ARKOSE_ASSERT(triangleMesh.normals.size() == vertexCount);

    // We can only generate proper tangents if we have texcoordinates. If not, define arbitrary tangents, orthogonal to the normals
    if (triangleMesh.texcoords.size() != vertexCount) {
        generateArbitraryTangentSpace(triangleMesh);
    } else {
        generateMikkTSpaceTangents(triangleMesh);
    }
}

std::unique_ptr<MaterialAsset> createDisplayColorMaterial(pxr::UsdPrim const& meshPrim,
                                                          pxr::UsdGeomMesh const& usdGeomMesh)
{
    SCOPED_PROFILE_ZONE();

    auto materialAsset = std::make_unique<MaterialAsset>();
    materialAsset->name = fmt::format("{}_displaycolor", meshPrim.GetName().GetString());

    pxr::UsdAttribute displayColorAttr = usdGeomMesh.GetDisplayColorAttr();
    if (displayColorAttr.HasValue()) {
        pxr::VtArray<pxr::GfVec3f> displayColor;
        displayColorAttr.Get(&displayColor);

        if (displayColor.size() > 0) {
            pxr::GfVec3f color = displayColor[0]; // TODO: Handle this more correctly!
            materialAsset->colorTint = vec4(color[0], color[1], color[2], 1.0f);
            materialAsset->blendMode = BlendMode::Opaque;
        }
    }

    pxr::UsdAttribute displayOpacityAttr = usdGeomMesh.GetDisplayOpacityAttr();
    if (displayOpacityAttr.HasValue()) {
        float displayOpacity;
        displayOpacityAttr.Get<float>(&displayOpacity);

        materialAsset->colorTint.w = displayOpacity;
        if (displayOpacity < 1.0f) {
            materialAsset->blendMode = BlendMode::Translucent;
        }
    }

    pxr::UsdAttribute doubleSidedAttr = usdGeomMesh.GetDoubleSidedAttr();
    if (doubleSidedAttr.HasValue()) {
        doubleSidedAttr.Get<bool>(&materialAsset->doubleSided);
    }

    return materialAsset;
}

template<typename T>
T readUsdAttributeValue(UsdPrim const& prim, TfToken attributeNameToken)
{
    UsdAttribute attribute = prim.GetAttribute(attributeNameToken);

    T attributeValue;
    if (attribute.Get(&attributeValue)) {
        // ARKOSE_LOG(Verbose, "Read attribute '{}' with value '{}'", attributeName, attributeValue);
        return attributeValue;
    } else {
        ARKOSE_LOG(Error, "Failed to read attribute '{}' for the requested type", attributeNameToken.GetString());
        return T();
    }
}

template<typename T>
T readUsdAttributeValue(UsdPrim const& prim, std::string_view attributeName)
{
    TfToken nameToken { attributeName.data() };
    return readUsdAttributeValue<T>(prim, nameToken);
}

ImageWrapMode createImageWrapMode(TfToken usdUvTextureWrap)
{
    if (usdUvTextureWrap == TfToken("black")) {
        ARKOSE_LOG(Warning, "Using `ImageWrapMode::ClampToEdge` in place of 'black', which should probably be a wrap to black border");
        return ImageWrapMode::ClampToEdge;
    } else if (usdUvTextureWrap == TfToken("clamp")) {
        return ImageWrapMode::ClampToEdge;
    } else if (usdUvTextureWrap == TfToken("repeat")) {
        return ImageWrapMode::Repeat;
    } else if (usdUvTextureWrap == TfToken("mirror")) {
        return ImageWrapMode::MirroredRepeat;
    } else if (usdUvTextureWrap == TfToken("useMetadata")) {
        ARKOSE_LOG(Warning, "Using `ImageWrapMode::ClampToEdge` in place of 'useMetadata', which can be whatever.. todo!");
        return ImageWrapMode::ClampToEdge;
    } else {
        ASSERT_NOT_REACHED();
    }
}

MaterialInput createMaterialInputForUsdUVTexture(UsdPrim usdUvTexturePrim)
{
    SCOPED_PROFILE_ZONE();

    MaterialInput materialInput {};
  
    if (UsdAttribute inputFileAttr = usdUvTexturePrim.GetAttribute(TfToken("inputs:file"))) {
        SdfAssetPath fileAssetPath;
        if (inputFileAttr.Get(&fileAssetPath)) {
            materialInput.image = fileAssetPath.GetAssetPath();
        }

        TfToken wrapS = readUsdAttributeValue<TfToken>(usdUvTexturePrim, "inputs:wrapS");
        materialInput.wrapModes.u = createImageWrapMode(wrapS);

        TfToken wrapT = readUsdAttributeValue<TfToken>(usdUvTexturePrim, "inputs:wrapT");
        materialInput.wrapModes.v = createImageWrapMode(wrapT);

        // We only have 2D textures here, but let's at least set the w-component to something reasonable
        materialInput.wrapModes.w = materialInput.wrapModes.u;

        // TODO: Handle scale (should expand to a scale node before the uv reading for this)
        // TODO: Handle bias (should expand to a UV addition node the uv reading for this)
        // TODO: Handle fallback (maybe add a fallback per MaterialInput? Currently we only have one for the entire material

    } else {
        // TODO: If this is a normals input specifically it just means we want to read the normal at this point -- which in the world
        // of movies where they use subdivision and no normal maps -- means just read the normals primvar. So we might have to do some
        // interpretation here.. but in short, it semantically means that this is the "shading normals" input for the shader graph.
        ARKOSE_LOG(Warning, "No file input specified for UsdUVTexture prim '{}'.", usdUvTexturePrim.GetPath().GetAsString());
    }

    return materialInput;
}

MaterialInput createMaterialInput(UsdPrim const& shaderPrim, MaterialAsset& materialAsset, UsdAttribute attribute)
{
    SCOPED_PROFILE_ZONE();

    SdfPathVector connections;
    if (attribute.GetConnections(&connections)) {
        
        ARKOSE_ASSERT(connections.size() == 1); // no reason for more than one connection here, surely?
        SdfPath const& connection = connections.front();
        //ARKOSE_LOG(Info, " connection = '{}'", connection.GetAsString());

        UsdPrim shaderInputPrim = shaderPrim.GetStage()->GetPrimAtPath(connection.GetPrimPath());
        ARKOSE_ASSERT(shaderInputPrim.GetTypeName() == UsdShadeTokens->Shader);

        TfToken shaderNodeType = readUsdAttributeValue<TfToken>(shaderInputPrim, UsdShadeTokens->infoId);
        if (shaderNodeType == TfToken("UsdUVTexture")) {
            return createMaterialInputForUsdUVTexture(shaderInputPrim);
        } else if (shaderNodeType == TfToken("UsdPrimvarReader_float2")) {
            NOT_YET_IMPLEMENTED();
        } else {
            NOT_YET_IMPLEMENTED();
        }

    } else {
        GfVec3f vectorValue;
        bool success = attribute.Get(&vectorValue);
        //ARKOSE_ASSERT(success);

        // TODO: Should we add a fallback value to each material input? I suppose it's more flexible than what we have now,
        // if we want to have a proper material graph implementation.
        MaterialInput materialInput {};
        return materialInput;
    }
}

void createMaterialFromUsdPreviewSurface(MaterialAsset& materialAsset, UsdPrim const& shaderPrim)
{
    SCOPED_PROFILE_ZONE();

    // Documentation: https://openusd.org/release/spec_usdpreviewsurface.html

    materialAsset.brdf = Brdf::Default;

    UsdAttribute useSpecularWorkflowAttr = shaderPrim.GetAttribute(TfToken("inputs:useSpecularWorkflow"));
    int useSpecularWorkflow;
    if (useSpecularWorkflowAttr.Get(&useSpecularWorkflow)) {
        ARKOSE_ASSERT(useSpecularWorkflow == 0); // For now (or maybe always?) we want to use the specular workflow
    }

    UsdAttribute diffuseColorAttr = shaderPrim.GetAttribute(TfToken("inputs:diffuseColor"));
    materialAsset.baseColor = createMaterialInput(shaderPrim, materialAsset, diffuseColorAttr);

    UsdAttribute emissiveColorAttr = shaderPrim.GetAttribute(TfToken("inputs:emissiveColor"));
    materialAsset.emissiveColor = createMaterialInput(shaderPrim, materialAsset, emissiveColorAttr);

    UsdAttribute normalAttr = shaderPrim.GetAttribute(TfToken("inputs:normal"));
    materialAsset.normalMap = createMaterialInput(shaderPrim, materialAsset, normalAttr);

    // TODO: Read roughness & metallic which we need to combine into a single texture! Occlusion could also be baked into this

    // TODO: Move the tint out to the input, maybe? Aligns more nicely with UsdPreviewSurface and many other materials definitions as well.
    GfVec3f diffuseColorConstant;
    if (diffuseColorAttr.Get(&diffuseColorConstant)) {
        materialAsset.colorTint = vec4(diffuseColorConstant[0], diffuseColorConstant[1], diffuseColorConstant[1], 1.0f);
    }

    // These factors are also effectively just tints of the inputs, so should probably also be inside the inputs
    materialAsset.metallicFactor = 1.0f;
    materialAsset.roughnessFactor = 1.0f;

    UsdAttribute metallicAttr = shaderPrim.GetAttribute(TfToken("inputs:metallic"));
    metallicAttr.Get(&materialAsset.metallicFactor);

    UsdAttribute roughnessAttr = shaderPrim.GetAttribute(TfToken("inputs:roughness"));
    roughnessAttr.Get(&materialAsset.roughnessFactor);

    // Determine blending
    {
        // TODO: Both of these can of course be connected to some other inputs, so we can't just assume constant values!
        UsdAttribute opacityAttr = shaderPrim.GetAttribute(TfToken("inputs:opacity"));
        UsdAttribute opacityThresholdAttr = shaderPrim.GetAttribute(TfToken("inputs:opacityThreshold"));

        float opacity;
        if (opacityAttr.Get<float>(&opacity)) {

            float opacityThreshold;
            if (opacityThresholdAttr.Get<float>(&opacityThreshold)) {
                if (opacityThreshold == 0.0f) {
                    materialAsset.blendMode = BlendMode::Opaque;
                } else {
                    materialAsset.blendMode = BlendMode::Masked;
                    materialAsset.maskCutoff = opacityThreshold;
                }
            } else {
                materialAsset.blendMode = BlendMode::Translucent;
                materialAsset.colorTint.w = opacity;
            }

        } else {
            materialAsset.blendMode = BlendMode::Opaque;
        }
    }

    // TODO: Where would we get this from?
    materialAsset.doubleSided = false;
}

std::unique_ptr<MaterialAsset> createMaterialAsset(pxr::UsdPrim const& materialPrim)
{
    SCOPED_PROFILE_ZONE();

    // NOTE: Compare to this python example in reverse:
    // https://github.com/PixarAnimationStudios/OpenUSD/blob/release/extras/usd/tutorials/simpleShading/generate_simpleShading.py

    auto materialAsset = std::make_unique<MaterialAsset>();
    materialAsset->name = materialPrim.GetName().GetString();

    //ARKOSE_LOG(Info, "Material named '{}':", materialAsset->name);

    pxr::UsdShadeMaterial usdShadeMaterial { materialPrim };

    for (pxr::UsdShadeOutput const& displacementOutput : usdShadeMaterial.GetDisplacementOutputs()) {
        if (displacementOutput.HasConnectedSource()) {
            ARKOSE_LOG(Warning, "We can't yet handle displacement, ignoring displacement output");
        }
    }

    std::vector<pxr::UsdShadeOutput> surfaceOutputs = usdShadeMaterial.GetSurfaceOutputs();
    ARKOSE_ASSERT(surfaceOutputs.size() == 1); // TODO: Handle multiple outputs!
    pxr::UsdShadeOutput& surfaceOutput = surfaceOutputs.front();

    // Surely it needs something connected to be valid?
    ARKOSE_ASSERT(surfaceOutput.HasConnectedSource());
    ARKOSE_ASSERT(surfaceOutput.GetConnectedSources().size() == 1);
    pxr::UsdShadeConnectionSourceInfo& sourceInfo = surfaceOutput.GetConnectedSources().front();
    pxr::UsdShadeConnectableAPI shadeConnectableAPI = sourceInfo.source;

    //ARKOSE_LOG(Info, " material is bound to shader '{}'", shadeConnectableAPI.GetPath().GetString());
    pxr::UsdAttribute shaderInfoIdAttr = shadeConnectableAPI.GetPrim().GetAttribute(UsdShadeTokens->infoId);
    pxr::TfToken shaderInfoIdToken;
    if (shaderInfoIdAttr.Get<pxr::TfToken>(&shaderInfoIdToken)) {
        //ARKOSE_LOG(Info, "  shader is of type '{}'", shaderInfoIdToken.GetString());
    }

    if (shaderInfoIdToken == pxr::TfToken("UsdPreviewSurface")) {
        createMaterialFromUsdPreviewSurface(*materialAsset, shadeConnectableAPI.GetPrim());
    } else {
        NOT_YET_IMPLEMENTED();
    }

    return materialAsset;
}

std::unique_ptr<MeshAsset> createMeshAsset(pxr::UsdPrim const& meshPrim, pxr::UsdGeomBBoxCache& bboxCache)
{
    SCOPED_PROFILE_ZONE();

    pxr::UsdGeomMesh usdGeomMesh { meshPrim };

    auto meshAsset = std::make_unique<MeshAsset>();
    meshAsset->name = meshPrim.GetName().GetText();

    // pxr::GfBBox3d aabb = usdGeomMesh.ComputeLocalBound(pxr::UsdTimeCode(0.0f));
    pxr::GfBBox3d aabb = bboxCache.ComputeLocalBound(meshPrim);
    pxr::GfVec3d aabbMin = aabb.GetRange().GetMin();
    pxr::GfVec3d aabbMax = aabb.GetRange().GetMax();
    meshAsset->boundingBox.min = vec3(static_cast<float>(aabbMin[0]), static_cast<float>(aabbMin[1]), static_cast<float>(aabbMin[2]));
    meshAsset->boundingBox.max = vec3(static_cast<float>(aabbMax[0]), static_cast<float>(aabbMax[1]), static_cast<float>(aabbMax[2]));

    MeshLODAsset& lod0 = meshAsset->LODs.emplace_back();

    bool hasAnySubsets = false;
    for (auto const& childPrim : meshPrim.GetDescendants()) {
        if (childPrim.IsA<pxr::UsdGeomSubset>()) {
            hasAnySubsets = true;
            break;
        }
    }

    if (hasAnySubsets) {


        // Define the mesh asset in terms of the UsdGeomSubset's under the UsdGeomMesh
        for (auto const& childPrim : meshPrim.GetDescendants()) {
            if (childPrim.IsA<pxr::UsdGeomSubset>()) {
                pxr::UsdGeomSubset usdGeomSubset { childPrim };
                MeshSegmentAsset& meshSegment = lod0.meshSegments.emplace_back();

                NOT_YET_IMPLEMENTED();
                //defineMeshSegmentAssetAndDependencies(meshSegment, meshPrim, usdGeomMesh, usdGeomSubset);
            }
        }

    } else {

        // Define the mesh asset directly from the UsdGeomMesh

        MeshSegmentAsset& meshSegment = lod0.meshSegments.emplace_back();

        // TODO: Is this not working..? Seems to always return an identity matrix. OTOH, I'm not sure
        // how it would know what I want, as it depends on what I consider the "root" for the mesh.
        // Will probably have to use the static variant of the function where I supply the xform ops
        // to it and it bakes it down to a single 4x4 matrix.
        //pxr::GfMatrix4d localTransform;
        //bool resetsXformStack;
        //bool xformSuccess = usdGeomMesh.GetLocalTransformation(&localTransform, &resetsXformStack);
        //ARKOSE_ASSERT(xformSuccess && !resetsXformStack);

        pxr::GfMatrix4d worldTransform = usdGeomMesh.ComputeLocalToWorldTransform(pxr::UsdTimeCode());

        UnindexedTriangleMesh triangleMesh;
        triangulateMesh(usdGeomMesh, triangleMesh); // maybe always worth doing?
        // if (isSingleIndexedTriangleMesh(usdGeomMesh)) {
        //     populateUnindexedTriangleMesh(usdGeomMesh, triangleMesh);
        // } else {
        //     triangulateMesh(usdGeomMesh, triangleMesh);
        // }

        generateTangents(triangleMesh);
        indexifyMesh(triangleMesh, meshSegment);

        // generateLODs(meshSegment);
        // optimizeMesh(meshSegment);

        // Set up the material for this mesh

        if (meshPrim.HasAPI<UsdShadeMaterialBindingAPI>() || meshPrim.GetRelationship(UsdShadeTokens->materialBinding)) {
            UsdShadeMaterialBindingAPI materialBindingAPI { meshPrim };
            pxr::UsdShadeMaterial usdShadeMaterial = materialBindingAPI.GetDirectBinding().GetMaterial();
            std::string materialName = usdShadeMaterial.GetPrim().GetName().GetString();

            std::filesystem::path importedMaterialPath = std::filesystem::path(materialName).replace_extension(MaterialAsset::AssetFileExtension);
            meshSegment.material = importedMaterialPath.generic_string();
        }
        // TODO: Handle basic display-color materials in some way.
        //else if (usdGeomMesh.GetDisplayColorPrimvar().IsDefined()) {
        //    material = createDisplayColorMaterial(meshPrim, usdGeomMesh);
        //}
    }

    return meshAsset;
}

void defineMeshSegmentAssetAndDependencies(MeshSegmentAsset& meshSegment,
                                           pxr::UsdPrim const& meshPrim,
                                           pxr::UsdGeomMesh const& usdGeomMesh,
                                           pxr::UsdGeomSubset const& usdGeomSubset)
{
    NOT_YET_IMPLEMENTED();
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        // TODO: Add support for named command line arguments!
        ARKOSE_LOG(Error, "UsdImportTool: must be called as\n> UsdImportTool <SourceUsdFile> <TargetDirectory>");
        return 1;
    }

    std::filesystem::path inputAsset = argv[1];
    ARKOSE_LOG(Info, "UsdImportTool: importing asset '{}'", inputAsset);

    std::filesystem::path targetDirectory = argv[2];
    ARKOSE_LOG(Info, "UsdImportTool: will write results to '{}'", targetDirectory);

    if (!pxr::UsdStage::IsSupportedFile(inputAsset.string())) {
        ARKOSE_LOG(Fatal, "USD can't open file '{}'.", inputAsset);
    }

    FileIO::ensureDirectory(targetDirectory);

    pxr::UsdStageRefPtr stage;
    {
        SCOPED_PROFILE_ZONE_NAMED("Load stage");
        ARKOSE_LOG(Info, "Loading stage '{}' ...", inputAsset);

        // Defer as much loading as possible - we might not load all data and we can possibly manually multi-thread it later
        auto initialLoadSet = UsdStage::InitialLoadSet::LoadNone;
        stage = pxr::UsdStage::Open(inputAsset.string(), initialLoadSet);
    }

    if (stage) {
        ARKOSE_LOG(Verbose, "  loaded stage");
    } else {
        ARKOSE_LOG(Fatal, "Failed to open USD stage.");
    }

    pxr::UsdGeomBBoxCache bboxCache{ pxr::UsdTimeCode(0.0f), pxr::UsdGeomImageable::GetOrderedPurposeTokens() };

    //pxr::SdfLayerHandle rootLayer = stage->GetRootLayer();

    for (const pxr::UsdPrim& prim : stage->Traverse()) {
        SCOPED_PROFILE_ZONE_NAMED("ForEachPrim");

        //if (prim.IsGroup()) {
        //    //ARKOSE_LOG(Info, "Found a model '{}'", prim.GetPath().GetText());
        //    continue;
        //}

        //bool isComponent = pxr::UsdModelAPI(prim).IsKind(pxr::KindTokens->component);
        //bool isSubcomponent = pxr::UsdModelAPI(prim).IsKind(pxr::KindTokens->subcomponent);

        if (prim.IsA<pxr::UsdGeomMesh>()) {
            ARKOSE_LOG(Info,    " - MESH     {}", prim.GetPath().GetText());

            std::unique_ptr<MeshAsset> mesh = createMeshAsset(prim, bboxCache);

            std::string meshFileName = mesh->name + MeshAsset::AssetFileExtension;
            mesh->writeToFile(targetDirectory / meshFileName, AssetStorage::Binary);

        } else if (prim.IsA<pxr::UsdGeomXform>()) {
            ARKOSE_LOG(Verbose, " - XFORM    {}", prim.GetPath().GetText());
        } else if (prim.IsA<pxr::UsdGeomCamera>()) {
            ARKOSE_LOG(Verbose, " - CAMERA   {}", prim.GetPath().GetText());
        } else if (prim.IsA<pxr::UsdShadeMaterial>()) {
            ARKOSE_LOG(Info,    " - MATERIAL {}", prim.GetPath().GetText());

            std::unique_ptr<MaterialAsset> material = createMaterialAsset(prim);

            auto processImage = [&](std::optional<MaterialInput>& materialInput, bool isNormalMap) {
                if (materialInput && !materialInput->image.empty()) {
                    std::filesystem::path imageRelativePath = materialInput->image;
                    std::filesystem::path imageSourcePath = inputAsset.parent_path() / imageRelativePath;
                    std::unique_ptr<ImageAsset> imageAsset = ImageAsset::createFromSourceAsset(imageSourcePath);

                    if (imageAsset->numMips() == 1) {
                        imageAsset->generateMipmaps();
                    }

                    if (!imageAsset->hasCompressedFormat()) {
                        TextureCompressor textureCompressor {};
                        if (isNormalMap) {
                            imageAsset = textureCompressor.compressBC5(*imageAsset);
                        } else {
                            imageAsset = textureCompressor.compressBC7(*imageAsset);
                        }
                    }

                    // Write out new & processed image asset
                    std::filesystem::path imageNewRelativePath = imageRelativePath.replace_extension(ImageAsset::AssetFileExtension);
                    imageAsset->writeToFile(targetDirectory / imageNewRelativePath, AssetStorage::Binary);

                    // Re-target material input to use the new processed image
                    materialInput->image = imageNewRelativePath.generic_string();
                }
            };

            processImage(material->baseColor, false);
            processImage(material->emissiveColor, false);
            processImage(material->normalMap, true);
            processImage(material->bentNormalMap, true);
            processImage(material->materialProperties, false);

            std::string materialFileName = material->name + MaterialAsset::AssetFileExtension;
            material->writeToFile(targetDirectory / materialFileName, AssetStorage::Json); // TODO: Use binary storage!

        } else {
            ARKOSE_LOG(Verbose, "            {}", prim.GetPath().GetText());
        }
    }

    return 0;
}
