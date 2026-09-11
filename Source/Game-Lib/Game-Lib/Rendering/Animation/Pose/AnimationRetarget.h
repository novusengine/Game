#pragma once
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetReader.h"

#include <array>
#include <vector>

namespace Animation
{
    // Applies an explicit exceptional bridge between two unrelated family schemas.
    class FamilyPoseBridge
    {
      public:
        bool Initialize(const AnimationLoading::RetargetProfileAssetView& profile, const AnimationLoading::RigFamilyAssetView& source, const AnimationLoading::RigFamilyAssetView& target);
        bool ConvertPose(std::span<const FileFormat::Animation::LocalTransform> source, std::vector<FileFormat::Animation::LocalTransform>& target, bool additive = false) const;
      private:
        std::vector<FileFormat::Animation::RetargetJointMapping> _mappings;
        std::vector<FileFormat::Animation::LocalTransform> _source;
        std::vector<FileFormat::Animation::LocalTransform> _target;
    };

    // Resolves an authored family binding once and converts parent-local poses.
    // Calibration rotates rest-relative deltas and scales translations by bone length.
    class FamilyPoseBinding
    {
      public:
        bool Initialize(const AnimationLoading::SkeletonAssetView& skeleton, const AnimationLoading::RigFamilyAssetView& family);
        bool ToFamily(std::span<const FileFormat::Animation::LocalTransform> source, std::span<FileFormat::Animation::LocalTransform> familyPose, bool additive = false) const;
        bool FromFamily(std::span<const FileFormat::Animation::LocalTransform> familyPose, std::span<FileFormat::Animation::LocalTransform> target, bool additive = false, bool correctChains = false, const FamilyPoseBinding* sourceBinding = nullptr, std::span<const FileFormat::Animation::LocalTransform> sourcePose = {}) const;
        bool CorrectStance(const FamilyPoseBinding& sourceBinding, std::span<const FileFormat::Animation::LocalTransform> source, std::span<FileFormat::Animation::LocalTransform> target) const;
        u64 GetSignature() const { return _signature; }
        u32 GetFamilyJointCount() const { return static_cast<u32>(_canonical.size()); }

      private:
        std::vector<FileFormat::Animation::SkeletonFamilyBinding> _bindings;
        std::vector<FileFormat::Animation::LocalTransform> _rest;
        std::vector<FileFormat::Animation::LocalTransform> _canonical;
        std::vector<FileFormat::Animation::LocalTransform> _reference;
        std::vector<FileFormat::Animation::SkeletonPropagationRule> _propagation;
        std::vector<i32> _parents;
        struct Chain
        {
            u64 semanticID = 0;
            bool isLeg = false;
            std::array<u32, 3> target;
            u32 familyEffector = 0;
            vec3 familyRestEffector = {};
            vec3 targetRestEffector = {};
            f32 proportion = 1.0f;
            f32 reach = 0.0f;
            i32 anchor = -1;
            vec3 restEffectorInAnchor = {};
            mat3x3 restAnchorBasis = mat3x3(1);
        };
        std::vector<Chain> _chains;
        std::vector<FileFormat::Animation::SkeletonJoint> _joints;
        std::vector<FileFormat::Animation::SkeletonJoint> _familyJoints;
        u64 _signature = 0;
    };
}
