#include "AssetProcessor.h"

#include <core/Logging.h>
//#include <pxr/usd/sdf/path.h>
//#include <pxr/usd/usd/prim.h>
//#include <pxr/usd/usd/primRange.h>
//#include <pxr/usd/usd/stage.h>

std::unique_ptr<ImageAsset> AssetProcessor::processImage(ImageAssetIdentifier imageAssetId)
{
	// The image case is a bit specific as we don't have an USD-native image type, just our source images (png, jpg, etc.)
	// ALSO, we need to know what kind of image we're dealing with, e.g. component count, sRGB, normal map.

    // todo!
}

/*
std::string AssetProcessor::processMaterial(AssetIdentifier assetId)
{

}

std::string AssetProcessor::processMesh(AssetIdentifier assetId)
{

}

std::string AssetProcessor::processSkeleton(AssetIdentifier assetId)
{

}

std::string AssetProcessor::processAnimation(AssetIdentifier assetId)
{
	
}
*/
