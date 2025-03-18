#pragma once

#include "AssetIdentifier.h"
#include <memory>

class ImageAsset;
class MaterialAsset;
class MeshAsset;
class SkeletonAsset;
class AnimationAsset;

namespace AssetProcessor {

std::unique_ptr<ImageAsset> processImage(ImageAssetIdentifier);
std::unique_ptr<MaterialAsset> processMaterial(AssetIdentifier);
std::unique_ptr<MeshAsset> processMesh(AssetIdentifier);
std::unique_ptr<SkeletonAsset> processSkeleton(AssetIdentifier);
std::unique_ptr<AnimationAsset> processAnimation(AssetIdentifier);

};
