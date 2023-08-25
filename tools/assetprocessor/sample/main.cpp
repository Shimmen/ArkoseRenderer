
#include <core/Logging.h>
#include <asset/MeshAsset.h>

#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/pointBased.h>

int main()
{
    // Path to the USD file
    std::string filePath = "Kitchen_set/Kitchen_set.usd";

    // Create a USD stage
    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(filePath);

    if (!stage) {
        ARKOSE_LOG_FATAL("Failed to open USD stage.");
        return 1;
    }

    // Get the root layer of the USD stage
    pxr::SdfLayerHandle rootLayer = stage->GetRootLayer();

    // Iterate over the prims in the stage
    for (const pxr::UsdPrim& prim : stage->Traverse()) {
        // Check if the prim is a point-based primitive
        if (prim.IsA<pxr::UsdGeomPointBased>()) {
            ARKOSE_LOG("Found a point-based primitive: {}", prim.GetPath());

            // Perform operations specific to USDGeomPointBased

            // Example: Accessing the points attribute
            pxr::UsdGeomPointBased pointBased(prim);
            pxr::UsdAttribute pointsAttr = pointBased.GetPointsAttr();

            // Example: Getting the number of points
            pxr::VtArray<pxr::GfVec3f> points;
            pointsAttr.Get(&points);
            ARKOSE_LOG("Number of points: {}", points.size());

            // Example: Accessing the position of the first point
            //if (!points.empty()) {
            //    pxr::GfVec3f firstPoint = points[0];
            //    std::cout << "Position of the first point: (" << firstPoint[0] << ", "
            //              << firstPoint[1] << ", " << firstPoint[2] << ")" << std::endl;
            //}
        }
    }

    auto meshAsset = std::make_unique<MeshAsset>();
    meshAsset->minLOD = 0;
    meshAsset->maxLOD = 1;

    return 0;
}
