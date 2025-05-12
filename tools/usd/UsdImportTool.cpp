
#include <ark/vector.h>
#include <asset/MeshAsset.h>
#include <asset/SetAsset.h>
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

// usd light stuff
#include <pxr/usd/usdLux/lightAPI.h>

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

//ARK_DISABLE_OPTIMIZATIONS

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

std::filesystem::path filePathForMaterial(std::filesystem::path targetDirectory, pxr::UsdPrim const& materialPrim)
{
    std::string materialFileName = materialPrim.GetName().GetString() + MaterialAsset::AssetFileExtension;
    return targetDirectory / materialFileName;
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

std::unique_ptr<MeshAsset> createMeshAsset(pxr::UsdPrim const& meshPrim, pxr::UsdGeomBBoxCache& bboxCache, std::filesystem::path targetDirectory)
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

                ARKOSE_LOG(Error, "Mesh has UsdGeomSubset which we do not yet support! TODO!");
                return nullptr;
                //defineMeshSegmentAssetAndDependencies(meshSegment, meshPrim, usdGeomMesh, usdGeomSubset);
            }
        }

    } else {

        // Define the mesh asset directly from the UsdGeomMesh

        MeshSegmentAsset& meshSegment = lod0.meshSegments.emplace_back();

        UnindexedTriangleMesh triangleMesh;
        triangulateMesh(usdGeomMesh, triangleMesh); // maybe always worth doing?
        // if (isSingleIndexedTriangleMesh(usdGeomMesh)) {
        //     populateUnindexedTriangleMesh(usdGeomMesh, triangleMesh);
        // } else {
        //     triangulateMesh(usdGeomMesh, triangleMesh);
        // }

        meshSegment.positions = std::move(triangleMesh.positions);
        meshSegment.texcoord0s = std::move(triangleMesh.texcoords);
        meshSegment.normals = std::move(triangleMesh.normals);
        meshSegment.tangents = std::move(triangleMesh.tangents);

        meshSegment.processForImport();

        // Set up the material for this mesh

        if (meshPrim.HasAPI<UsdShadeMaterialBindingAPI>() || meshPrim.GetRelationship(UsdShadeTokens->materialBinding)) {
            UsdShadeMaterialBindingAPI materialBindingAPI { meshPrim };
            pxr::UsdShadeMaterial usdShadeMaterial = materialBindingAPI.GetDirectBinding().GetMaterial();
            std::filesystem::path materialFilePath = filePathForMaterial(targetDirectory, usdShadeMaterial.GetPrim());
            meshSegment.material = materialFilePath.generic_string();
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

Transform createTransformFromXformable(pxr::UsdGeomXformable const& xformable)
{
    pxr::GfMatrix4d localTransform;
    bool resetsXformStack;
    bool xformSuccess = xformable.GetLocalTransformation(&localTransform, &resetsXformStack);
    ARKOSE_ASSERT(xformSuccess && !resetsXformStack);

    // Extract scale from the matrix, sort of. This is not equivalent, since we don't consider shear & not axis-dependent scaling.
    // Let's see how far this takes us..
    double scaleApproxNoShear = localTransform.GetDeterminant();

    pxr::GfMatrix4d localTransformNoScaleShear = localTransform.RemoveScaleShear();
    localTransformNoScaleShear.Orthonormalize();

    pxr::GfVec3d translation = localTransformNoScaleShear.ExtractTranslation();
    pxr::GfQuatd orientation = localTransformNoScaleShear.ExtractRotationQuat();

    Transform transform;

    transform.setTranslation(vec3(static_cast<float>(translation[0]),
                                  static_cast<float>(translation[1]),
                                  static_cast<float>(translation[2])));

    transform.setOrientation(quat(vec3(static_cast<float>(orientation.GetImaginary()[0]),
                                       static_cast<float>(orientation.GetImaginary()[1]),
                                       static_cast<float>(orientation.GetImaginary()[2])),
                                  static_cast<float>(orientation.GetReal())));

    transform.setScale(static_cast<float>(scaleApproxNoShear));

    return transform;
}

pxr::UsdPrim findTransformableParent(pxr::UsdPrim const& prim)
{
    pxr::UsdPrim parent = prim.GetParent();
    while (!parent.IsPseudoRoot() && !parent.IsA<pxr::UsdGeomXform>()) {
        parent = parent.GetParent();
        ARKOSE_LOG(Info, "  curent parent '{}'", parent.GetPath().GetText());
    }

    // We should be creating a NodeAsset for each transformable object, so there should always be something here
    ARKOSE_ASSERT(parent.IsValid());

    return parent;
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
        ARKOSE_LOG(Error, "USD can't open file '{}'.", inputAsset);
        return 1;
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

    std::unique_ptr<SetAsset> setAsset = std::make_unique<SetAsset>();
    setAsset->name = inputAsset.stem().string();

    // Check asset "upAxis" and adjust accordingly
    {
        TfToken upAxis;
        if (stage->GetMetadata(UsdGeomTokens->upAxis, &upAxis)) {
            if (upAxis == UsdGeomTokens->y) {
                ARKOSE_LOG(Verbose, "Up-axis is Y, this is already in the coordinate system we expect!");
            } else if (upAxis == UsdGeomTokens->z) {
                ARKOSE_LOG(Info, "Up-axis is Z, rotating root to achieve a Y-up coordinate system");
                quat rotate90degAroundXAxis = ark::axisAngle(ark::globalX, ark::HALF_PI);
                setAsset->rootNode.transform.setOrientation(rotate90degAroundXAxis);
            } else {
                ARKOSE_LOG(Error, "Up-axis is '{}', which we do not yet support", upAxis.GetString());
            }
        }
    }

    // Check asset "metersPerUnit" and adjust accordingly
    {
        double metersPerUnit = 0.01;
        stage->GetMetadata(UsdGeomTokens->metersPerUnit, &metersPerUnit);

        if (metersPerUnit != 1.0) {
            ARKOSE_LOG(Info, "Asset is in {} meters per unit, scaling root to achieve a 1-meter-per-unit scale", metersPerUnit);

            float rootScale = 1.0f / static_cast<float>(metersPerUnit);
            setAsset->rootNode.transform.setScale(rootScale);
        } else {
            ARKOSE_LOG(Verbose, "Asset is in 1 meter per unit, this is already in the unit scale we expect!");
        }
    }

    std::unordered_map<std::string, NodeAsset*> nodeAssetMap;
    auto createNodeAsset = [&](pxr::UsdPrim const& prim) -> NodeAsset* {
        ARKOSE_ASSERT(prim.IsA<pxr::UsdGeomXformable>());
        pxr::UsdGeomXformable xformable { prim };

        pxr::UsdPrim parent = findTransformableParent(prim);

        NodeAsset* parentNodeAsset = nullptr;
        if (parent.IsPseudoRoot()) {
            // The pseudo root is technically not xformable, but we treat it as such
            // in our SetAsset hierarchy as the actual, non-psuedo root node.
            parentNodeAsset = &setAsset->rootNode;
        } else {
            std::string primPath = parent.GetPath().GetString();
            auto entry = nodeAssetMap.find(primPath);
            ARKOSE_ASSERT(entry != nodeAssetMap.end());
            parentNodeAsset = entry->second;
        }

        NodeAsset* nodeAsset = parentNodeAsset->createChildNode();
        nodeAsset->name = prim.GetName().GetString();
        nodeAsset->transform = createTransformFromXformable(xformable);

        nodeAssetMap[prim.GetPath().GetString()] = nodeAsset;

        return nodeAsset;
    };

    std::vector<std::filesystem::path> outputDependencies;
    u32 errorCount = 0;

    u32 numModels = 0;
    for (const pxr::UsdPrim& prim : stage->Traverse()) {

        if (!prim.IsActive()) {
            ARKOSE_LOG(Verbose, "Skipping inactive prim '{}'", prim.GetPath().GetText());
        }

        //ARKOSE_LOG(Info, "Found prim '{}'", prim.GetPath().GetText());

        // TODO: Treat a model (UsdModelAPI) as the SetAsset, so typically we get a single SetAsset per .usd-file, but if there's
        // metadata to indicate otherwise we can create multiple SetAssets from a single .usd-file.
        //prim.IsGroup(), prim.IsModel(), prim.IsComponent(), prim.IsSubComponent()
        if (prim.IsModel() && !prim.IsPseudoRoot()) {
            ARKOSE_LOG(Verbose, "Found Usd model (UsdModelAPI) prim '{}'", prim.GetPath().GetText());
            numModels += 1;
        }

        NodeAsset* currentNodeAsset = nullptr;
        if (prim.IsA<pxr::UsdGeomXformable>()) {
            currentNodeAsset = createNodeAsset(prim);
        }

        if (prim.IsA<pxr::UsdGeomMesh>()) {
            ARKOSE_LOG(Info,    " - MESH     {}", prim.GetPath().GetText());

            if (std::unique_ptr<MeshAsset> mesh = createMeshAsset(prim, bboxCache, targetDirectory)) {

                std::string meshFileName = mesh->name + MeshAsset::AssetFileExtension;
                std::filesystem::path meshFilePath = targetDirectory / meshFileName;

                mesh->writeToFile(meshFilePath, AssetStorage::Binary);
                outputDependencies.push_back(meshFilePath);

                ARKOSE_ASSERT(currentNodeAsset);
                currentNodeAsset->meshIndex = narrow_cast<i32>(setAsset->meshAssets.size());
                setAsset->meshAssets.emplace_back(meshFilePath.generic_string());

            } else {
                errorCount += 1;
            }

        } else if (prim.IsA<pxr::UsdShadeMaterial>()) {
            ARKOSE_LOG(Info,    " - MATERIAL {}", prim.GetPath().GetText());

            if (std::unique_ptr<MaterialAsset> material = createMaterialAsset(prim)) {

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

                std::filesystem::path materialFilePath = filePathForMaterial(targetDirectory, prim);
                material->writeToFile(materialFilePath, AssetStorage::Json); // TODO: Use binary storage!
                outputDependencies.push_back(materialFilePath);
            } else {
                errorCount += 1;
            }

        } else if (prim.IsA<pxr::UsdGeomCamera>()) {
            ARKOSE_LOG(Info,    " - CAMERA   {}", prim.GetPath().GetText());

            // TODO!

        } else if (prim.HasAPI<pxr::UsdLuxLightAPI>()) { 
            ARKOSE_LOG(Info,    " - LIGHT    {}", prim.GetPath().GetText());

            // TODO!

        } else {
            ARKOSE_LOG(Verbose, "            {}", prim.GetPath().GetText());
        }
    }

    if (numModels == 0) { 
        ARKOSE_LOG(Warning, "Found no models (UsdModelAPI) - interpreting the full file as a single model");
    } else if (numModels > 1) {
        ARKOSE_LOG(Warning, "Found more than one ({}) models (UsdModelAPI) - not yet supported, interpreting the full file as a single model", numModels);
    }

    // Write out the set asset
    {
        std::string setFileName = setAsset->name + SetAsset::AssetFileExtension;
        setAsset->writeToFile(targetDirectory / setFileName, AssetStorage::Json); // TODO: Use binary storage!
        outputDependencies.push_back(targetDirectory / setFileName);
    }

    // Create dependency file
    {
        std::string originalExt = inputAsset.extension().string();
        std::filesystem::path dependencyFile = targetDirectory / inputAsset.filename().replace_extension(originalExt + ".dep");
        ARKOSE_LOG(Info, "UsdImportTool: writing dependency file '{}'", dependencyFile);

        std::string dependencyData = "";

        for (std::filesystem::path const& dependency : outputDependencies) {
            dependencyData += fmt::format("OUTPUT: {}\n", dependency.generic_string());
        }

        FileIO::writeTextDataToFile(dependencyFile, dependencyData);
    }

    if (errorCount > 0) {
        ARKOSE_LOG(Error, "{} errors while importing asset '{}'", errorCount, inputAsset);
    }

    return errorCount ? 1 : 0;
}
