#pragma once
#include "level_native.h"
namespace P4MapKit
{
struct FAnimationAssetNative { std::array<std::uint32_t, 4> SkeletonGuid{}; FReadError Error; };
inline FAnimationAssetNative ReadAnimationAssetNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End)
{
    FAnimationAssetNative Out; FByteReader R(Body, Start, End);
    bool Good = LevelNative::ObjectId(R);
    for (auto& Part : Out.SkeletonGuid) Good = Good && R.U32(Part);
    std::size_t Used = 0; if (Good) LevelNative::Done(R, Used);
    Out.Error = R.Error; return Out;
}
}
