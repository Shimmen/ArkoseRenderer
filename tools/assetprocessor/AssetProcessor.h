#pragma once

#include "AssetIdentifier.h"
#include <memory>

#include <asset/ImageAsset.h>
#include <asset/MaterialAsset.h>
#include <asset/MeshAsset.h>
#include <asset/SkeletonAsset.h>
#include <asset/AnimationAsset.h>

namespace AssetProcessor {

std::unique_ptr<ImageAsset> processImage(ImageAssetIdentifier);
std::unique_ptr<MaterialAsset> processMaterial(AssetIdentifier);
std::unique_ptr<MeshAsset> processMesh(AssetIdentifier);
std::unique_ptr<SkeletonAsset> processSkeleton(AssetIdentifier);
std::unique_ptr<AnimationAsset> processAnimation(AssetIdentifier);

};
