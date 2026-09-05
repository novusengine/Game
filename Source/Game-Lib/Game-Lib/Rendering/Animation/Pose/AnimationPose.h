#pragma once
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetReader.h"

#include <Base/Types.h>

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace Animation
{
    struct alignas(16) AffineFloat3x4
    {
        vec4 row0 = vec4(1.0f, 0.0f, 0.0f, 0.0f);
        vec4 row1 = vec4(0.0f, 1.0f, 0.0f, 0.0f);
        vec4 row2 = vec4(0.0f, 0.0f, 1.0f, 0.0f);
    };

    static_assert(sizeof(AffineFloat3x4) == 48);
    static_assert(alignof(AffineFloat3x4) == 16);
    static_assert(offsetof(AffineFloat3x4, row0) == 0);
    static_assert(offsetof(AffineFloat3x4, row1) == 16);
    static_assert(offsetof(AffineFloat3x4, row2) == 32);

    enum class PoseEvaluationError : u8
    {
        None,
        JointCountMismatch,
        InvalidParent,
        MultipleRoots,
        NoRoot,
        NonFiniteTransform
    };

    struct PoseEvaluationResult
    {
        std::vector<mat4a> modelTransforms;
        std::vector<AffineFloat3x4> palette;
        PoseEvaluationError error = PoseEvaluationError::None;
        u32 errorJointIndex = std::numeric_limits<u32>::max();

        explicit operator bool() const { return error == PoseEvaluationError::None; }
    };

    // Builds model-space joint transforms and GPU skinning palettes from local skeleton poses.
    // The helpers keep pose composition and blending independent from playback policy.
    mat4a ComposeLocalTransform(const FileFormat::Animation::LocalTransform& transform);
    AffineFloat3x4 PackAffineFloat3x4(const mat4a& transform);
    mat4a UnpackAffineFloat3x4(const AffineFloat3x4& transform);
    vec3 TransformPoint(const AffineFloat3x4& transform, const vec3& point);
    vec3 TransformVector(const AffineFloat3x4& transform, const vec3& vector);
    bool ResolveBoneMask(const AnimationLoading::BoneMaskAssetView& mask, FileFormat::Animation::PoseDomainType poseDomainType, FileFormat::AssetID poseDomainAssetID, u32 jointCount, std::vector<f32>& outputWeights);
    bool BlendOverrideLocalPoses(std::span<const FileFormat::Animation::LocalTransform> basePose, std::span<const FileFormat::Animation::LocalTransform> layerPose, f32 layerWeight, std::span<const f32> maskWeights, std::span<FileFormat::Animation::LocalTransform> output);
    bool BuildAdditiveLocalPose(std::span<const FileFormat::Animation::LocalTransform> sourcePose, std::span<const FileFormat::Animation::LocalTransform> referencePose, std::span<FileFormat::Animation::LocalTransform> output);
    bool ApplyAdditiveLocalPose(std::span<const FileFormat::Animation::LocalTransform> basePose, std::span<const FileFormat::Animation::LocalTransform> additivePose, f32 layerWeight, std::span<const f32> maskWeights, std::span<FileFormat::Animation::LocalTransform> output);
    bool UpdateModelTransforms(std::span<const FileFormat::Animation::SkeletonJoint> joints, std::span<const FileFormat::Animation::LocalTransform> localPose, u32 firstJointIndex, std::span<mat4a> modelTransforms);
    PoseEvaluationResult EvaluatePose(std::span<const FileFormat::Animation::SkeletonJoint> joints, std::span<const FileFormat::Animation::LocalTransform> localPose);
    bool BlendLocalPoses(std::span<const FileFormat::Animation::LocalTransform> outgoing, std::span<const FileFormat::Animation::LocalTransform> incoming, f32 weight, std::span<FileFormat::Animation::LocalTransform> output);
    PoseEvaluationResult EvaluateRestPose(const AnimationLoading::SkeletonAssetView& skeleton);
} // namespace Animation
