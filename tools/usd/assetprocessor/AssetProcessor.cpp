#include "AssetProcessor.h"

#include <asset/ImageAsset.h>
#include <asset/MaterialAsset.h>
#include <asset/MeshAsset.h>
#include <asset/SkeletonAsset.h>
#include <asset/AnimationAsset.h>
#include <core/Logging.h>

std::unique_ptr<ImageAsset> AssetProcessor::processImage(ImageAssetIdentifier imageAssetId)
{
	// The image case is a bit specific as we don't have an USD-native image type, just our source images (png, jpg, etc.)
	// ALSO, we need to know what kind of image we're dealing with, e.g. component count, sRGB, normal map.

    // todo!
    return {};
}

std::unique_ptr<MaterialAsset> AssetProcessor::processMaterial(AssetIdentifier assetId)
{
    return {};
}

std::unique_ptr<MeshAsset> AssetProcessor::processMesh(AssetIdentifier assetId)
{
    return {};
}

std::unique_ptr<SkeletonAsset> AssetProcessor::processSkeleton(AssetIdentifier assetId)
{
    return {};
}

std::unique_ptr<AnimationAsset> AssetProcessor::processAnimation(AssetIdentifier assetId)
{
    return {};
}
