#pragma once
#include "Game-Lib/Rendering/Asset/AssetDiagnostic.h"

#include <FileFormat/Novus/Animation/Animation.h>

#include <span>

namespace AnimationLoading
{
    namespace Animation = FileFormat::Animation;

    template <typename View>
    struct AssetReadResult
    {
        View view;
        AssetLoading::Diagnostic diagnostic;

        explicit operator bool() const { return !diagnostic; }
    };

    struct RigFamilyAssetView
    {
        Animation::RigFamilyAsset root;
        std::span<const Animation::RigFamilyJoint> joints;
        std::span<const Animation::RigFamilyChain> chains;
        std::span<const u32> chainJointIndices;
    };

    struct SkeletonAssetView
    {
        Animation::SkeletonAsset root;
        std::span<const Animation::SkeletonJoint> joints;
        std::span<const Animation::SkeletonFamilyBinding> familyBindings;
        std::span<const Animation::SkeletonPropagationRule> propagationRules;
        std::span<const Animation::HierarchyDepthRange> hierarchyDepthRanges;
        std::span<const u32> hierarchyDepthJointIndices;
        std::span<const Animation::SkeletonAttachment> attachments;
    };

    struct AnimationClipAssetView
    {
        Animation::AnimationClipAsset root;
        std::span<const Animation::AnimationTrack> tracks;
        std::span<const vec3> translationSamples;
        std::span<const quat> rotationSamples;
        std::span<const vec3> scaleSamples;
        std::span<const Animation::SynchronizationMarker> synchronizationMarkers;
        std::span<const Animation::ActionWindow> actionWindows;
        std::span<const Animation::AnimationEvent> events;
        std::span<const u8> eventPayloadBytes;
    };

    struct AnimationSetAssetView
    {
        Animation::AnimationSetAsset root;
        std::span<const Animation::AnimationSetSlot> slots;
        std::span<const Animation::AnimationClipVariant> variants;
    };

    struct AnimationGraphAssetView
    {
        Animation::AnimationGraphAsset root;
        std::span<const u8> defaultParameterData;
        std::span<const Animation::GraphParameter> parameters;
        std::span<const Animation::GraphCommand> commands;
        std::span<const Animation::GraphActionSlot> actionSlots;
        std::span<const Animation::GraphStateMigrationEntry> stateMigrationEntries;
        std::span<const u8> controllerProgram;
        std::span<const u8> poseProgram;
    };

    struct BoneMaskAssetView
    {
        Animation::BoneMaskAsset root;
        std::span<const u8> jointWeights;
    };

    struct IKRigAssetView
    {
        Animation::IKRigAsset root;
        std::span<const Animation::IKChain> chains;
        std::span<const u32> chainJointIndices;
        std::span<const Animation::IKJointLimit> jointLimits;
        std::span<const Animation::IKSolver> solvers;
    };

    struct RetargetProfileAssetView
    {
        Animation::RetargetProfileAsset root;
        std::span<const Animation::RetargetJointMapping> jointMappings;
    };

    struct AnimationBoundsAssetView
    {
        Animation::AnimationBoundsAsset root;
        std::span<const Animation::ClipBounds> clips;
        std::span<const Animation::AnimatedBoundsSegment> segments;
    };

    using RigFamilyAssetReadResult = AssetReadResult<RigFamilyAssetView>;
    using SkeletonAssetReadResult = AssetReadResult<SkeletonAssetView>;
    using AnimationClipAssetReadResult = AssetReadResult<AnimationClipAssetView>;
    using AnimationSetAssetReadResult = AssetReadResult<AnimationSetAssetView>;
    using AnimationGraphAssetReadResult = AssetReadResult<AnimationGraphAssetView>;
    using BoneMaskAssetReadResult = AssetReadResult<BoneMaskAssetView>;
    using IKRigAssetReadResult = AssetReadResult<IKRigAssetView>;
    using RetargetProfileAssetReadResult = AssetReadResult<RetargetProfileAssetView>;
    using AnimationBoundsAssetReadResult = AssetReadResult<AnimationBoundsAssetView>;

    // Decodes CPU-side animation payloads into borrowed views over their serialized arrays.
    // The views expose offline-prepared data without copying or rebuilding it at runtime.
    class AnimationAssetReader
    {
      public:
        static RigFamilyAssetReadResult ReadRigFamily(std::span<const u8> payload);
        static SkeletonAssetReadResult ReadSkeleton(std::span<const u8> payload);
        static AnimationClipAssetReadResult ReadAnimationClip(std::span<const u8> payload);
        static AnimationSetAssetReadResult ReadAnimationSet(std::span<const u8> payload);
        static AnimationGraphAssetReadResult ReadAnimationGraph(std::span<const u8> payload);
        static BoneMaskAssetReadResult ReadBoneMask(std::span<const u8> payload);
        static IKRigAssetReadResult ReadIKRig(std::span<const u8> payload);
        static RetargetProfileAssetReadResult ReadRetargetProfile(std::span<const u8> payload);
        static AnimationBoundsAssetReadResult ReadAnimationBounds(std::span<const u8> payload);
    };
} // namespace AnimationLoading
