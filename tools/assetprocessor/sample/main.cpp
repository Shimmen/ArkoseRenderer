
#include <ark/vector.h>
#include <asset/MeshAsset.h>
#include <core/Logging.h>

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

// triangulation stuff
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/hd/vtBufferSource.h>
#include <pxr/usdImaging/usdImaging/meshAdapter.h>

// indexification and mesh optimizing etc.
#include <meshoptimizer.h>

// tangent space generation
#include <mikktspace.h>

struct UnindexedTriangleMesh {
    std::vector<vec3> positions;
    std::vector<vec2> texcoords;
    std::vector<vec3> normals;
    std::vector<vec4> tangents;
};

void generateGeometricFaceNormals(UnindexedTriangleMesh& triangleMesh)
{
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
                shouldGenerateSmoothNormals = true;
            } else {
                // NOTE: For now we're not distinguishing between faceVarying vs uniform interpolation.
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
        ARKOSE_ASSERT(normal.GetLength() > 0.99f);

        pxr::GfVec3f tangent, bitangent;
        normal.BuildOrthonormalFrame(&tangent, &bitangent);

        triangleMesh.tangents.emplace_back(tangent[0], tangent[1], tangent[2], 1.0f);
    }
}

void generateMikkTSpaceTangents(UnindexedTriangleMesh& triangleMesh)
{
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

void defineMeshAssetAndDependencies(pxr::UsdPrim const& componentPrim, pxr::UsdGeomBBoxCache& bboxCache)
{
    auto meshAsset = std::make_unique<MeshAsset>();
    meshAsset->name = componentPrim.GetName().GetText();

    //pxr::GfBBox3d aabb = usdGeomMesh.ComputeLocalBound(pxr::UsdTimeCode(0.0f));
    pxr::GfBBox3d aabb = bboxCache.ComputeLocalBound(componentPrim);
    pxr::GfVec3d aabbMin = aabb.GetRange().GetMin();
    pxr::GfVec3d aabbMax = aabb.GetRange().GetMax();
    meshAsset->boundingBox.min = vec3(aabbMin[0], aabbMin[1], aabbMin[2]);
    meshAsset->boundingBox.max = vec3(aabbMax[0], aabbMax[1], aabbMax[2]);

    MeshLODAsset& lod0 = meshAsset->LODs.emplace_back();

    for (auto const& childPrim : componentPrim.GetDescendants()) {
        ARKOSE_LOG(Info, " descendant: {}", childPrim.GetPath().GetText());

        if (childPrim.IsA<pxr::UsdGeomMesh>()) {
            pxr::UsdGeomMesh usdGeomMesh{ childPrim };

            ARKOSE_LOG(Info, "  making mesh segment!");

            MeshSegmentAsset& meshSegment = lod0.meshSegments.emplace_back();
            //segment.name = childPrim.GetName().GetText(); // TODO!

            auto materialAsset = std::make_unique<MaterialAsset>();
            materialAsset->name = fmt::format("{}_DisplayMat", childPrim.GetName().GetString());

            // TODO: Is this not working..? Seems to always return an identity matrix. OTOH, I'm not sure
            // how it would know what I want, as it depends on what I consider the "root" for the mesh.
            // Will probably have to use the static variant of the function where I supply the xform ops
            // to it and it bakes it down to a single 4x4 matrix.
            pxr::GfMatrix4d packedXform;
            bool resetsXformStack;
            bool xformSuccess = usdGeomMesh.GetLocalTransformation(&packedXform, &resetsXformStack);
            ARKOSE_ASSERT(xformSuccess && !resetsXformStack);

            UnindexedTriangleMesh triangleMesh;
            if (isSingleIndexedTriangleMesh(usdGeomMesh)) {
                populateUnindexedTriangleMesh(usdGeomMesh, triangleMesh);
            } else {
                triangulateMesh(usdGeomMesh, triangleMesh);
            }

            generateTangents(triangleMesh);
            indexifyMesh(triangleMesh, meshSegment);

            //generateLODs(meshSegment);
            //optimizeMesh(meshSegment);

            // Set up the material for this mesh

            pxr::UsdAttribute doubleSidedAttr = usdGeomMesh.GetDoubleSidedAttr();
            if (doubleSidedAttr.HasValue()) {
                doubleSidedAttr.Get<bool>(&materialAsset->doubleSided);
            }

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

            // ..

            materialAsset->writeToFile(materialAsset->name + ".arkmat", AssetStorage::Json);
            meshSegment.setPathToMaterial(std::string(materialAsset->assetFilePath()));
        }
    }

    meshAsset->writeToFile(meshAsset->name + ".arkmsh", AssetStorage::Json);
}

int main()
{
    //std::string filePath = "Kitchen_set/Kitchen_set.usd";
    //std::string filePath = "Kitchen_set/assets/Toaster/Toaster.usd";
    //std::string filePath = "NewSponza_IvyGrowth_USD_YUp.usda";
    std::string filePath = "Ehrengrab_Johannes_Benk.usdz";

    if (!pxr::UsdStage::IsSupportedFile(filePath)) {
        ARKOSE_LOG_FATAL("USD can't open file '{}'.", filePath);
    }

    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(filePath);

    if (!stage) {
        ARKOSE_LOG_FATAL("Failed to open USD stage.");
    }

    pxr::UsdGeomBBoxCache bboxCache{ pxr::UsdTimeCode(0.0f), pxr::UsdGeomImageable::GetOrderedPurposeTokens() };

    pxr::SdfLayerHandle rootLayer = stage->GetRootLayer();

    for (const pxr::UsdPrim& prim : stage->Traverse()) {

        if (prim.IsGroup()) {
            //ARKOSE_LOG(Info, "Found a model '{}'", prim.GetPath().GetText());
            continue;
        }

        bool isComponent = pxr::UsdModelAPI(prim).IsKind(pxr::KindTokens->component);
        bool isSubcomponent = pxr::UsdModelAPI(prim).IsKind(pxr::KindTokens->subcomponent);

        if (isComponent) {
            ARKOSE_LOG(Info, "Found a component: {}", prim.GetPath().GetText());
            defineMeshAssetAndDependencies(prim, bboxCache);
        } else {
            /*
            if (prim.IsA<pxr::UsdGeomMesh>()) {
                ARKOSE_LOG(Info, "              MESH {}", prim.GetPath().GetText());
            } else if (prim.IsA<pxr::UsdGeomXform>()) {
                ARKOSE_LOG(Info, "             XFORM {}", prim.GetPath().GetText());
            } else {
                ARKOSE_LOG(Info, "                   {}", prim.GetPath().GetText());
            }
            */
        }
    }

    return 0;
}