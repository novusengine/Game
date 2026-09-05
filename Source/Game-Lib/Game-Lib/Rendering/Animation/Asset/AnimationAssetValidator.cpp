#include "AnimationAssetValidator.h"

#include <Base/Math/Math.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    namespace Animation = FileFormat::Animation;
    using AssetLoading::Diagnostic;
    using AssetLoading::DiagnosticCode;
    using AssetLoading::DiagnosticStage;

    constexpr u16 SKELETON_JOINT_FLAGS = Animation::SkeletonJointFlags_Deformation | Animation::SkeletonJointFlags_Helper | Animation::SkeletonJointFlags_Extension;
    constexpr u32 TRACK_FLAGS = Animation::AnimationTrackFlags_HasTranslationSamples | Animation::AnimationTrackFlags_HasRotationSamples | Animation::AnimationTrackFlags_HasScaleSamples;
    constexpr u8 CLIP_FLAGS = Animation::AnimationClipFlags_Looping | Animation::AnimationClipFlags_Additive;
    constexpr u32 EVENT_FLAGS = 1u;
    constexpr f32 QUATERNION_LENGTH_TOLERANCE = 0.01f;
    constexpr f32 INVERSE_BIND_TOLERANCE = 0.005f;

    template <typename Result>
    Result Fail(DiagnosticCode code, std::string_view field, u32 index = Diagnostic::NO_INDEX, u64 observed = 0, u64 expected = 0)
    {
        Result result;
        result.diagnostic = {code, field, index, observed, expected};
        result.diagnostic.stage = DiagnosticStage::Validate;
        return result;
    }

    template <typename Result>
    Result FailDependency(std::string_view field, FileFormat::AssetID dependencyAssetID, u32 index = Diagnostic::NO_INDEX)
    {
        Result result = Fail<Result>(DiagnosticCode::MissingDependency, field, index);
        result.diagnostic.dependencyAssetID = dependencyAssetID;
        result.diagnostic.stage = DiagnosticStage::ResolveDependency;
        return result;
    }

    Diagnostic FailDiagnostic(DiagnosticCode code, std::string_view field, u64 observed = 0, u64 expected = 0)
    {
        Diagnostic diagnostic = {code, field, Diagnostic::NO_INDEX, observed, expected};
        diagnostic.stage = DiagnosticStage::Compatibility;
        return diagnostic;
    }

    bool IsRangeValid(u32 offset, u32 count, size_t size)
    {
        return static_cast<u64>(offset) + count <= size;
    }

    bool IsFinite(const vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool IsFinite(const quat& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    }

    bool IsFinite(const mat4a& value)
    {
        for (u32 column = 0; column < 4; ++column)
            for (u32 row = 0; row < 3; ++row)
                if (!std::isfinite(value[column][row]))
                    return false;
        return true;
    }

    bool IsUnitQuaternion(const quat& value)
    {
        return IsFinite(value) && std::abs(glm::dot(value, value) - 1.0f) <= QUATERNION_LENGTH_TOLERANCE;
    }

    bool IsValidTransform(const Animation::LocalTransform& transform)
    {
        return IsFinite(transform.translation) && IsUnitQuaternion(transform.rotation) && IsFinite(transform.scale) &&
               std::abs(transform.scale.x) > std::numeric_limits<f32>::epsilon() && std::abs(transform.scale.y) > std::numeric_limits<f32>::epsilon() &&
               std::abs(transform.scale.z) > std::numeric_limits<f32>::epsilon() && transform.reserved0 == 0 && transform.reserved1 == 0;
    }

    bool IsValidBounds(const Animation::Bounds& bounds)
    {
        return IsFinite(bounds.center) && std::isfinite(bounds.sphereRadius) && bounds.sphereRadius >= 0.0f && IsFinite(bounds.extents) && bounds.extents.x >= 0.0f && bounds.extents.y >= 0.0f && bounds.extents.z >= 0.0f && bounds.reserved == 0;
    }

    bool Contains(const Animation::Bounds& outer, const Animation::Bounds& inner)
    {
        constexpr f32 tolerance = 1.0e-6f;
        for (u32 axis = 0; axis < 3; ++axis)
            if (inner.center[axis] - inner.extents[axis] < outer.center[axis] - outer.extents[axis] - tolerance || inner.center[axis] + inner.extents[axis] > outer.center[axis] + outer.extents[axis] + tolerance)
                return false;
        return true;
    }

    bool IsIdentity(const mat4a& value)
    {
        for (u32 column = 0; column < 4; ++column)
        {
            for (u32 row = 0; row < 3; ++row)
            {
                const f32 expected = column == row ? 1.0f : 0.0f;
                if (std::abs(value[column][row] - expected) > INVERSE_BIND_TOLERANCE)
                    return false;
            }
        }
        return true;
    }

    bool IsReservedZero(const u8* values, size_t count)
    {
        for (size_t index = 0; index < count; ++index)
            if (values[index] != 0)
                return false;
        return true;
    }
} // namespace

namespace AnimationLoading
{
    SkeletonAssetReadResult AnimationAssetValidator::Validate(SkeletonAssetReadResult result)
    {
        const Animation::SkeletonAsset& root = result.view.root;
        if (root.flags != 0)
            return Fail<SkeletonAssetReadResult>(DiagnosticCode::UnsupportedFlags, "SkeletonAsset.flags", Diagnostic::NO_INDEX, root.flags, 0);
        if (root.reserved != 0)
            return Fail<SkeletonAssetReadResult>(DiagnosticCode::ReservedValueNonZero, "SkeletonAsset.reserved", Diagnostic::NO_INDEX, root.reserved, 0);
        if (result.view.joints.empty())
            return Fail<SkeletonAssetReadResult>(DiagnosticCode::EmptyRequiredRange, "joints");
        if (result.view.hierarchyDepthJointIndices.size() != result.view.joints.size())
            return Fail<SkeletonAssetReadResult>(DiagnosticCode::CountMismatch, "hierarchyDepthJointIndices", Diagnostic::NO_INDEX, result.view.hierarchyDepthJointIndices.size(), result.view.joints.size());

        u32 rootCount = 0;
        u16 maximumDepth = 0;
        std::vector<mat4a> modelTransforms(result.view.joints.size());
        for (u32 index = 0; index < result.view.joints.size(); ++index)
        {
            const Animation::SkeletonJoint& joint = result.view.joints[index];
            if (joint.semanticID == 0)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "joints.semanticID", index);
            for (u32 previous = 0; previous < index; ++previous)
                if (result.view.joints[previous].semanticID == joint.semanticID)
                    return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "joints.semanticID", index, joint.semanticID);
            if ((joint.flags & ~SKELETON_JOINT_FLAGS) != 0)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::UnsupportedFlags, "joints.flags", index, joint.flags, SKELETON_JOINT_FLAGS);
            if ((joint.flags & Animation::SkeletonJointFlags_Deformation) != 0 && (joint.flags & Animation::SkeletonJointFlags_Helper) != 0)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "joints.flags", index, joint.flags);
            if (joint.reserved != 0)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::ReservedValueNonZero, "joints.reserved", index, joint.reserved, 0);
            if (!IsValidTransform(joint.restTransform))
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::NonFiniteValue, "joints.restTransform", index);
            if (!IsFinite(joint.inverseBindTransform))
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::NonFiniteValue, "joints.inverseBindTransform", index);

            const mat4a localTransform = Math::AffineMatrix::TransformMatrix(joint.restTransform.translation, joint.restTransform.rotation, joint.restTransform.scale);
            if (joint.parentJointIndex == -1)
            {
                ++rootCount;
                if (joint.hierarchyDepth != 0)
                    return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "joints.hierarchyDepth", index, joint.hierarchyDepth, 0);
                modelTransforms[index] = localTransform;
            }
            else
            {
                if (joint.parentJointIndex < 0 || static_cast<u32>(joint.parentJointIndex) >= index)
                    return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidIndex, "joints.parentJointIndex", index, joint.parentJointIndex, index);
                const Animation::SkeletonJoint& parent = result.view.joints[joint.parentJointIndex];
                if (joint.hierarchyDepth != parent.hierarchyDepth + 1)
                    return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "joints.hierarchyDepth", index, joint.hierarchyDepth, parent.hierarchyDepth + 1);
                modelTransforms[index] = Math::AffineMatrix::MatrixMul(modelTransforms[joint.parentJointIndex], localTransform);
            }
            maximumDepth = std::max(maximumDepth, joint.hierarchyDepth);
            if (!IsIdentity(Math::AffineMatrix::MatrixMul(modelTransforms[index], joint.inverseBindTransform)))
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "joints.inverseBindTransform", index);
            if (joint.familyJointIndex < -1)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidIndex, "joints.familyJointIndex", index, joint.familyJointIndex);
            if (joint.familyJointIndex >= 0 && root.rigFamilyAssetID == FileFormat::INVALID_ASSET_ID)
                return FailDependency<SkeletonAssetReadResult>("SkeletonAsset.rigFamilyAssetID", root.rigFamilyAssetID, index);
        }
        if (rootCount != 1)
            return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "joints.rootCount", Diagnostic::NO_INDEX, rootCount, 1);
        if (result.view.hierarchyDepthRanges.size() != static_cast<size_t>(maximumDepth) + 1)
            return Fail<SkeletonAssetReadResult>(DiagnosticCode::CountMismatch, "hierarchyDepthRanges", Diagnostic::NO_INDEX, result.view.hierarchyDepthRanges.size(), static_cast<u64>(maximumDepth) + 1);

        std::vector<bool> depthJointSeen(result.view.joints.size(), false);
        u32 expectedDepthOffset = 0;
        for (u32 depth = 0; depth < result.view.hierarchyDepthRanges.size(); ++depth)
        {
            const Animation::HierarchyDepthRange& range = result.view.hierarchyDepthRanges[depth];
            if (range.jointIndexOffset != expectedDepthOffset || !IsRangeValid(range.jointIndexOffset, range.numJointIndices, result.view.hierarchyDepthJointIndices.size()))
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidRange, "hierarchyDepthRanges", depth, range.jointIndexOffset, expectedDepthOffset);
            expectedDepthOffset += range.numJointIndices;
            for (u32 localIndex = 0; localIndex < range.numJointIndices; ++localIndex)
            {
                const u32 jointIndex = result.view.hierarchyDepthJointIndices[range.jointIndexOffset + localIndex];
                if (jointIndex >= result.view.joints.size() || depthJointSeen[jointIndex])
                    return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidIndex, "hierarchyDepthJointIndices", range.jointIndexOffset + localIndex, jointIndex, result.view.joints.size());
                if (result.view.joints[jointIndex].hierarchyDepth != depth)
                    return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "hierarchyDepthJointIndices.depth", range.jointIndexOffset + localIndex, result.view.joints[jointIndex].hierarchyDepth, depth);
                depthJointSeen[jointIndex] = true;
            }
        }

        std::vector<bool> boundJoints(result.view.joints.size(), false);
        for (u32 index = 0; index < result.view.familyBindings.size(); ++index)
        {
            const Animation::SkeletonFamilyBinding& binding = result.view.familyBindings[index];
            if (binding.skeletonJointIndex >= result.view.joints.size() || boundJoints[binding.skeletonJointIndex])
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidIndex, "familyBindings.skeletonJointIndex", index, binding.skeletonJointIndex, result.view.joints.size());
            const Animation::SkeletonJoint& joint = result.view.joints[binding.skeletonJointIndex];
            if (joint.familyJointIndex < 0 || static_cast<u32>(joint.familyJointIndex) != binding.familyJointIndex)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "familyBindings.familyJointIndex", index, binding.familyJointIndex, joint.familyJointIndex);
            if (!IsValidTransform(binding.skeletonToFamily) || !IsValidTransform(binding.familyToSkeleton))
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::NonFiniteValue, "familyBindings.transform", index);
            boundJoints[binding.skeletonJointIndex] = true;
        }
        for (u32 index = 0; index < result.view.joints.size(); ++index)
            if ((result.view.joints[index].familyJointIndex >= 0) != boundJoints[index])
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::MissingRequiredReference, "joints.familyBinding", index);

        u16 previousExecutionOrder = 0;
        for (u32 index = 0; index < result.view.propagationRules.size(); ++index)
        {
            const Animation::SkeletonPropagationRule& rule = result.view.propagationRules[index];
            if (rule.sourceJointIndex >= result.view.joints.size() || rule.targetJointIndex >= result.view.joints.size() || rule.sourceJointIndex == rule.targetJointIndex)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidIndex, "propagationRules.jointIndex", index);
            if (rule.type > Animation::SkeletonPropagationType::DistributeRotation)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::UnsupportedEnum, "propagationRules.type", index, static_cast<u8>(rule.type));
            if (rule.flags != 0)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::UnsupportedFlags, "propagationRules.flags", index, rule.flags, 0);
            if (index > 0 && rule.executionOrder < previousExecutionOrder)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "propagationRules.executionOrder", index, rule.executionOrder, previousExecutionOrder);
            if (!std::isfinite(rule.weight) || rule.weight < 0.0f || rule.weight > 1.0f || !IsValidTransform(rule.offsetTransform))
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::NonFiniteValue, "propagationRules.weightOrTransform", index);
            previousExecutionOrder = rule.executionOrder;
        }

        for (u32 index = 0; index < result.view.attachments.size(); ++index)
        {
            const Animation::SkeletonAttachment& attachment = result.view.attachments[index];
            if (attachment.semanticID == 0)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "attachments.semanticID", index);
            for (u32 previous = 0; previous < index; ++previous)
                if (result.view.attachments[previous].semanticID == attachment.semanticID)
                    return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidValue, "attachments.semanticID", index, attachment.semanticID);
            if (attachment.jointIndex >= result.view.joints.size())
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::InvalidIndex, "attachments.jointIndex", index, attachment.jointIndex, result.view.joints.size());
            if (attachment.flags != 0)
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::UnsupportedFlags, "attachments.flags", index, attachment.flags, 0);
            if (!IsValidTransform(attachment.localTransform))
                return Fail<SkeletonAssetReadResult>(DiagnosticCode::NonFiniteValue, "attachments.localTransform", index);
        }
        return result;
    }

    AnimationClipAssetReadResult AnimationAssetValidator::Validate(AnimationClipAssetReadResult result)
    {
        const Animation::AnimationClipAsset& root = result.view.root;
        if ((root.flags & ~CLIP_FLAGS) != 0)
            return Fail<AnimationClipAssetReadResult>(DiagnosticCode::UnsupportedFlags, "AnimationClipAsset.flags", Diagnostic::NO_INDEX, root.flags, CLIP_FLAGS);
        if (!IsReservedZero(root.reserved, std::size(root.reserved)))
            return Fail<AnimationClipAssetReadResult>(DiagnosticCode::ReservedValueNonZero, "AnimationClipAsset.reserved");
        if (root.sourcePoseDomainType > Animation::PoseDomainType::Skeleton)
            return Fail<AnimationClipAssetReadResult>(DiagnosticCode::UnsupportedEnum, "AnimationClipAsset.sourcePoseDomainType", Diagnostic::NO_INDEX, static_cast<u8>(root.sourcePoseDomainType));
        if (root.sourcePoseDomainAssetID == FileFormat::INVALID_ASSET_ID)
            return FailDependency<AnimationClipAssetReadResult>("AnimationClipAsset.sourcePoseDomainAssetID", root.sourcePoseDomainAssetID);
        if (root.sampleRateHz == 0 || root.sampleCount < 2 || root.durationMicroseconds == 0)
            return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidValue, "AnimationClipAsset.timing");
        const u64 expectedDuration = (static_cast<u64>(root.sampleCount - 1) * 1000000ull + root.sampleRateHz / 2u) / root.sampleRateHz;
        if (root.durationMicroseconds != expectedDuration)
            return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidValue, "AnimationClipAsset.durationMicroseconds", Diagnostic::NO_INDEX, root.durationMicroseconds, expectedDuration);

        const bool additive = (root.flags & Animation::AnimationClipFlags_Additive) != 0;
        if (root.additiveReferenceType > Animation::AdditiveReferenceType::AnimationClipSample)
            return Fail<AnimationClipAssetReadResult>(DiagnosticCode::UnsupportedEnum, "AnimationClipAsset.additiveReferenceType", Diagnostic::NO_INDEX, static_cast<u8>(root.additiveReferenceType));
        if (!additive && (root.additiveReferenceClipAssetID != FileFormat::INVALID_ASSET_ID || root.additiveReferenceTimeMicroseconds != 0 || root.additiveReferenceType != Animation::AdditiveReferenceType::SkeletonRestPose))
            return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidValue, "AnimationClipAsset.additiveReference");
        if (additive && root.additiveReferenceType == Animation::AdditiveReferenceType::SkeletonRestPose && (root.additiveReferenceClipAssetID != FileFormat::INVALID_ASSET_ID || root.additiveReferenceTimeMicroseconds != 0))
            return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidValue, "AnimationClipAsset.additiveRestPoseReference");
        if (additive && root.additiveReferenceType == Animation::AdditiveReferenceType::AnimationClipSample && root.additiveReferenceClipAssetID == FileFormat::INVALID_ASSET_ID)
            return FailDependency<AnimationClipAssetReadResult>("AnimationClipAsset.additiveReferenceClipAssetID", root.additiveReferenceClipAssetID);

        for (u32 index = 0; index < result.view.tracks.size(); ++index)
        {
            const Animation::AnimationTrack& track = result.view.tracks[index];
            for (u32 previous = 0; previous < index; ++previous)
                if (result.view.tracks[previous].sourceJointIndex == track.sourceJointIndex)
                    return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidValue, "tracks.sourceJointIndex", index, track.sourceJointIndex);
            if ((track.flags & ~TRACK_FLAGS) != 0)
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::UnsupportedFlags, "tracks.flags", index, track.flags, TRACK_FLAGS);
            const bool hasTranslations = track.numTranslations != 0;
            const bool hasRotations = track.numRotations != 0;
            const bool hasScales = track.numScales != 0;
            if ((hasTranslations && track.numTranslations != root.sampleCount) || !IsRangeValid(track.translationOffset, track.numTranslations, result.view.translationSamples.size()))
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidRange, "tracks.translations", index, track.numTranslations, root.sampleCount);
            if ((hasRotations && track.numRotations != root.sampleCount) || !IsRangeValid(track.rotationOffset, track.numRotations, result.view.rotationSamples.size()))
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidRange, "tracks.rotations", index, track.numRotations, root.sampleCount);
            if ((hasScales && track.numScales != root.sampleCount) || !IsRangeValid(track.scaleOffset, track.numScales, result.view.scaleSamples.size()))
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidRange, "tracks.scales", index, track.numScales, root.sampleCount);
            const u32 expectedFlags = (hasTranslations ? Animation::AnimationTrackFlags_HasTranslationSamples : 0) |
                                      (hasRotations ? Animation::AnimationTrackFlags_HasRotationSamples : 0) |
                                      (hasScales ? Animation::AnimationTrackFlags_HasScaleSamples : 0);
            if (track.flags != expectedFlags)
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::CountMismatch, "tracks.flags", index, track.flags, expectedFlags);
            if (track.reserved0 != 0 || track.reserved1 != 0)
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::ReservedValueNonZero, "tracks.reserved", index);
            if (!IsFinite(track.defaultTranslation) || !IsUnitQuaternion(track.defaultRotation) || !IsFinite(track.defaultScale) ||
                std::abs(track.defaultScale.x) <= std::numeric_limits<f32>::epsilon() || std::abs(track.defaultScale.y) <= std::numeric_limits<f32>::epsilon() || std::abs(track.defaultScale.z) <= std::numeric_limits<f32>::epsilon())
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::NonFiniteValue, "tracks.defaults", index);
        }
        for (u32 index = 0; index < result.view.translationSamples.size(); ++index)
            if (!IsFinite(result.view.translationSamples[index]))
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::NonFiniteValue, "translationSamples", index);
        for (u32 index = 0; index < result.view.rotationSamples.size(); ++index)
            if (!IsUnitQuaternion(result.view.rotationSamples[index]))
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::NonFiniteValue, "rotationSamples", index);
        for (u32 index = 0; index < result.view.scaleSamples.size(); ++index)
            if (!IsFinite(result.view.scaleSamples[index]) || std::abs(result.view.scaleSamples[index].x) <= std::numeric_limits<f32>::epsilon() ||
                std::abs(result.view.scaleSamples[index].y) <= std::numeric_limits<f32>::epsilon() || std::abs(result.view.scaleSamples[index].z) <= std::numeric_limits<f32>::epsilon())
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::NonFiniteValue, "scaleSamples", index);

        u32 previousTime = 0;
        for (u32 index = 0; index < result.view.synchronizationMarkers.size(); ++index)
        {
            const Animation::SynchronizationMarker& marker = result.view.synchronizationMarkers[index];
            if (marker.semanticID == 0 || marker.timeMicroseconds > root.durationMicroseconds || (index > 0 && marker.timeMicroseconds < previousTime))
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidValue, "synchronizationMarkers", index, marker.timeMicroseconds, root.durationMicroseconds);
            if (marker.flags != 0)
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::UnsupportedFlags, "synchronizationMarkers.flags", index, marker.flags, 0);
            previousTime = marker.timeMicroseconds;
        }
        previousTime = 0;
        for (u32 index = 0; index < result.view.actionWindows.size(); ++index)
        {
            const Animation::ActionWindow& window = result.view.actionWindows[index];
            if (window.semanticID == 0 || window.startTimeMicroseconds > window.endTimeMicroseconds || window.endTimeMicroseconds > root.durationMicroseconds || (index > 0 && window.startTimeMicroseconds < previousTime))
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidValue, "actionWindows", index, window.endTimeMicroseconds, root.durationMicroseconds);
            previousTime = window.startTimeMicroseconds;
        }
        previousTime = 0;
        for (u32 index = 0; index < result.view.events.size(); ++index)
        {
            const Animation::AnimationEvent& event = result.view.events[index];
            if (event.timeMicroseconds > root.durationMicroseconds || (index > 0 && event.timeMicroseconds < previousTime))
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidValue, "events.timeMicroseconds", index, event.timeMicroseconds, root.durationMicroseconds);
            if ((event.flags & ~EVENT_FLAGS) != 0)
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::UnsupportedFlags, "events.flags", index, event.flags, EVENT_FLAGS);
            if (!IsRangeValid(event.payloadOffset, event.payloadSize, result.view.eventPayloadBytes.size()))
                return Fail<AnimationClipAssetReadResult>(DiagnosticCode::InvalidRange, "events.payload", index, event.payloadOffset, event.payloadSize);
            previousTime = event.timeMicroseconds;
        }
        return result;
    }

    AnimationSetAssetReadResult AnimationAssetValidator::Validate(AnimationSetAssetReadResult result)
    {
        const Animation::AnimationSetAsset& root = result.view.root;
        if (root.declaredPoseDomainAssetID == FileFormat::INVALID_ASSET_ID)
            return FailDependency<AnimationSetAssetReadResult>("AnimationSetAsset.declaredPoseDomainAssetID", root.declaredPoseDomainAssetID);
        if (root.declaredPoseDomainType > Animation::PoseDomainType::Skeleton)
            return Fail<AnimationSetAssetReadResult>(DiagnosticCode::UnsupportedEnum, "AnimationSetAsset.declaredPoseDomainType", Diagnostic::NO_INDEX, static_cast<u8>(root.declaredPoseDomainType));
        if (!IsReservedZero(root.reserved0, std::size(root.reserved0)) || root.flags != 0)
            return Fail<AnimationSetAssetReadResult>(DiagnosticCode::ReservedValueNonZero, "AnimationSetAsset.reservedOrFlags");

        for (u32 index = 0; index < result.view.slots.size(); ++index)
        {
            const Animation::AnimationSetSlot& slot = result.view.slots[index];
            if (slot.semanticID == 0 || !IsRangeValid(slot.variantOffset, slot.numVariants, result.view.variants.size()))
                return Fail<AnimationSetAssetReadResult>(DiagnosticCode::InvalidRange, "slots.variants", index, slot.variantOffset, slot.numVariants);
            if ((slot.flags & ~Animation::AnimationSetSlotFlags_Required) != 0)
                return Fail<AnimationSetAssetReadResult>(DiagnosticCode::UnsupportedFlags, "slots.flags", index, slot.flags, Animation::AnimationSetSlotFlags_Required);
            if (slot.fallbackSlotIndex < -1 || (slot.fallbackSlotIndex >= 0 && static_cast<u32>(slot.fallbackSlotIndex) >= result.view.slots.size()) || slot.fallbackSlotIndex == static_cast<i32>(index))
                return Fail<AnimationSetAssetReadResult>(DiagnosticCode::InvalidIndex, "slots.fallbackSlotIndex", index, slot.fallbackSlotIndex, result.view.slots.size());
            if ((slot.flags & Animation::AnimationSetSlotFlags_Required) != 0 && slot.numVariants == 0)
                return Fail<AnimationSetAssetReadResult>(DiagnosticCode::EmptyRequiredRange, "slots.variants", index);
            for (u32 previous = 0; previous < index; ++previous)
                if (result.view.slots[previous].semanticID == slot.semanticID)
                    return Fail<AnimationSetAssetReadResult>(DiagnosticCode::InvalidValue, "slots.semanticID", index, slot.semanticID);
            std::vector<bool> seen(result.view.slots.size(), false);
            i32 fallback = static_cast<i32>(index);
            while (fallback >= 0)
            {
                if (seen[fallback])
                    return Fail<AnimationSetAssetReadResult>(DiagnosticCode::InvalidValue, "slots.fallbackCycle", index, fallback);
                seen[fallback] = true;
                fallback = result.view.slots[fallback].fallbackSlotIndex;
            }
            for (u32 local = 0; local < slot.numVariants; ++local)
            {
                const Animation::AnimationClipVariant& variant = result.view.variants[slot.variantOffset + local];
                if (variant.animationClipAssetID == FileFormat::INVALID_ASSET_ID)
                    return FailDependency<AnimationSetAssetReadResult>("variants.animationClipAssetID", variant.animationClipAssetID, slot.variantOffset + local);
                if (variant.selectionWeight == 0 || variant.flags != 0 || variant.stableVariationID == 0)
                    return Fail<AnimationSetAssetReadResult>(DiagnosticCode::InvalidValue, "variants.weightFlagsOrStableID", slot.variantOffset + local);
                for (u32 previous = 0; previous < local; ++previous)
                    if (result.view.variants[slot.variantOffset + previous].stableVariationID == variant.stableVariationID)
                        return Fail<AnimationSetAssetReadResult>(DiagnosticCode::InvalidValue, "variants.stableVariationID", slot.variantOffset + local, variant.stableVariationID);
            }
        }
        return result;
    }

    BoneMaskAssetReadResult AnimationAssetValidator::Validate(BoneMaskAssetReadResult result)
    {
        const Animation::BoneMaskAsset& root = result.view.root;
        if (root.poseDomainAssetID == FileFormat::INVALID_ASSET_ID)
            return FailDependency<BoneMaskAssetReadResult>("BoneMaskAsset.poseDomainAssetID", root.poseDomainAssetID);
        if (root.poseDomainType > Animation::PoseDomainType::Skeleton)
            return Fail<BoneMaskAssetReadResult>(DiagnosticCode::UnsupportedEnum, "BoneMaskAsset.poseDomainType", Diagnostic::NO_INDEX, static_cast<u8>(root.poseDomainType));
        if (!IsReservedZero(root.reserved0, std::size(root.reserved0)) || root.flags != 0)
            return Fail<BoneMaskAssetReadResult>(DiagnosticCode::ReservedValueNonZero, "BoneMaskAsset.reservedOrFlags");
        if (result.view.jointWeights.empty())
            return Fail<BoneMaskAssetReadResult>(DiagnosticCode::EmptyRequiredRange, "jointWeights");
        return result;
    }

    IKRigAssetReadResult AnimationAssetValidator::Validate(IKRigAssetReadResult result)
    {
        const Animation::IKRigAsset& root = result.view.root;
        if (root.skeletonAssetID == FileFormat::INVALID_ASSET_ID)
            return FailDependency<IKRigAssetReadResult>("IKRigAsset.skeletonAssetID", root.skeletonAssetID);
        if (root.flags != 0 || root.reserved != 0)
            return Fail<IKRigAssetReadResult>(DiagnosticCode::ReservedValueNonZero, "IKRigAsset.reservedOrFlags");
        if (!IsFinite(root.conservativeExtentExpansion) || root.conservativeExtentExpansion.x < 0.0f || root.conservativeExtentExpansion.y < 0.0f || root.conservativeExtentExpansion.z < 0.0f || !std::isfinite(root.maximumGoalDistance) || root.maximumGoalDistance < 0.0f)
            return Fail<IKRigAssetReadResult>(DiagnosticCode::NonFiniteValue, "IKRigAsset.bounds");
        if (result.view.chains.empty() || result.view.solvers.empty())
            return Fail<IKRigAssetReadResult>(DiagnosticCode::EmptyRequiredRange, "chainsOrSolvers");

        for (u32 index = 0; index < result.view.jointLimits.size(); ++index)
        {
            const Animation::IKJointLimit& limit = result.view.jointLimits[index];
            if (!IsFinite(limit.minimumRadians) || !IsFinite(limit.maximumRadians) || limit.reserved0 != 0.0f || limit.reserved1 != 0.0f || glm::any(glm::greaterThan(limit.minimumRadians, limit.maximumRadians)))
                return Fail<IKRigAssetReadResult>(DiagnosticCode::InvalidValue, "jointLimits", index);
        }

        for (u32 index = 0; index < result.view.chains.size(); ++index)
        {
            const Animation::IKChain& chain = result.view.chains[index];
            if (chain.semanticID == 0 || chain.numJointIndices < 2 || !IsRangeValid(chain.jointIndexOffset, chain.numJointIndices, result.view.chainJointIndices.size()) || !IsRangeValid(chain.jointLimitOffset, chain.numJointLimits, result.view.jointLimits.size()) || (chain.numJointLimits != 0 && chain.numJointLimits != chain.numJointIndices) || !IsFinite(chain.preferredBendDirection) || !std::isfinite(chain.maximumReach) || chain.maximumReach <= 0.0f)
                return Fail<IKRigAssetReadResult>(DiagnosticCode::InvalidRange, "chains", index);
            for (u32 previous = 0; previous < index; ++previous)
                if (result.view.chains[previous].semanticID == chain.semanticID)
                    return Fail<IKRigAssetReadResult>(DiagnosticCode::InvalidValue, "chains.semanticID", index, chain.semanticID);
            bool containsEffector = false;
            for (u32 localIndex = 0; localIndex < chain.numJointIndices; ++localIndex)
            {
                const u32 jointIndex = result.view.chainJointIndices[chain.jointIndexOffset + localIndex];
                containsEffector |= jointIndex == chain.effectorJointIndex;
                for (u32 previous = 0; previous < localIndex; ++previous)
                    if (result.view.chainJointIndices[chain.jointIndexOffset + previous] == jointIndex)
                        return Fail<IKRigAssetReadResult>(DiagnosticCode::InvalidValue, "chains.duplicateJointIndex", index, jointIndex);
            }
            if (!containsEffector || chain.effectorJointIndex != result.view.chainJointIndices[chain.jointIndexOffset + chain.numJointIndices - 1])
                return Fail<IKRigAssetReadResult>(DiagnosticCode::InvalidValue, "chains.effectorJointIndex", index, chain.effectorJointIndex);
        }

        for (u32 index = 0; index < result.view.solvers.size(); ++index)
        {
            const Animation::IKSolver& solver = result.view.solvers[index];
            if (solver.semanticID == 0 || solver.chainIndex >= result.view.chains.size() || solver.type > Animation::IKSolverType::IterativeChain || solver.goalSpace > Animation::IKGoalSpace::Joint || solver.flags != 0 || solver.reserved0 != 0 || solver.reserved1 != 0 || solver.maximumIterations == 0 || solver.maximumIterations > 64 || !std::isfinite(solver.convergenceTolerance) || solver.convergenceTolerance <= 0.0f || !std::isfinite(solver.maximumReachScale) || solver.maximumReachScale <= 0.0f || solver.maximumReachScale > 1.0f)
                return Fail<IKRigAssetReadResult>(DiagnosticCode::InvalidValue, "solvers", index);
            for (u32 previous = 0; previous < index; ++previous)
                if (result.view.solvers[previous].semanticID == solver.semanticID)
                    return Fail<IKRigAssetReadResult>(DiagnosticCode::InvalidValue, "solvers.semanticID", index, solver.semanticID);
        }
        return result;
    }

    AnimationBoundsAssetReadResult AnimationAssetValidator::Validate(AnimationBoundsAssetReadResult result)
    {
        const Animation::AnimationBoundsAsset& root = result.view.root;
        if (root.modelAssetID == FileFormat::INVALID_ASSET_ID)
            return FailDependency<AnimationBoundsAssetReadResult>("AnimationBoundsAsset.modelAssetID", root.modelAssetID);
        if (root.skeletonAssetID == FileFormat::INVALID_ASSET_ID)
            return FailDependency<AnimationBoundsAssetReadResult>("AnimationBoundsAsset.skeletonAssetID", root.skeletonAssetID);
        constexpr u32 supportedFlags = Animation::AnimationBoundsAssetFlags_HasGlobalFallback;
        if (root.reserved0 != 0 || (root.flags & ~supportedFlags) != 0 || root.reserved != 0)
            return Fail<AnimationBoundsAssetReadResult>(DiagnosticCode::ReservedValueNonZero, "AnimationBoundsAsset.reservedOrFlags");
        if (result.view.clips.empty())
            return Fail<AnimationBoundsAssetReadResult>(DiagnosticCode::EmptyRequiredRange, "clips");

        const bool hasGlobalFallback = (root.flags & Animation::AnimationBoundsAssetFlags_HasGlobalFallback) != 0;
        if (hasGlobalFallback && (result.view.clips[0].animationClipAssetID != FileFormat::INVALID_ASSET_ID || result.view.clips[0].numSegments != 0 || result.view.clips[0].segmentOffset != 0 || !IsValidBounds(result.view.clips[0].fallbackBounds)))
            return Fail<AnimationBoundsAssetReadResult>(DiagnosticCode::InvalidValue, "clips.globalFallback");

        for (u32 clipIndex = 0; clipIndex < result.view.clips.size(); ++clipIndex)
        {
            const Animation::ClipBounds& clip = result.view.clips[clipIndex];
            if (clipIndex == 0 && hasGlobalFallback)
                continue;
            if (clip.animationClipAssetID == FileFormat::INVALID_ASSET_ID)
                return FailDependency<AnimationBoundsAssetReadResult>("clips.animationClipAssetID", clip.animationClipAssetID, clipIndex);
            if (clip.numSegments == 0 || !IsRangeValid(clip.segmentOffset, clip.numSegments, result.view.segments.size()) || !IsValidBounds(clip.fallbackBounds))
                return Fail<AnimationBoundsAssetReadResult>(DiagnosticCode::InvalidRange, "clips.segmentsOrFallback", clipIndex, clip.segmentOffset, clip.numSegments);
            for (u32 previous = 0; previous < clipIndex; ++previous)
                if (result.view.clips[previous].animationClipAssetID == clip.animationClipAssetID)
                    return Fail<AnimationBoundsAssetReadResult>(DiagnosticCode::InvalidValue, "clips.animationClipAssetID", clipIndex, clip.animationClipAssetID);
            if (hasGlobalFallback && !Contains(result.view.clips[0].fallbackBounds, clip.fallbackBounds))
                return Fail<AnimationBoundsAssetReadResult>(DiagnosticCode::InvalidValue, "clips.globalFallbackContainment", clipIndex);
            u32 expectedStart = 0;
            for (u32 local = 0; local < clip.numSegments; ++local)
            {
                const u32 segmentIndex = clip.segmentOffset + local;
                const Animation::AnimatedBoundsSegment& segment = result.view.segments[segmentIndex];
                if (segment.startTimeMicroseconds != expectedStart || segment.endTimeMicroseconds <= segment.startTimeMicroseconds || !IsValidBounds(segment.bounds) || !Contains(clip.fallbackBounds, segment.bounds))
                    return Fail<AnimationBoundsAssetReadResult>(DiagnosticCode::InvalidValue, "segments.orderBoundsOrContainment", segmentIndex, segment.startTimeMicroseconds, expectedStart);
                expectedStart = segment.endTimeMicroseconds;
            }
        }
        return result;
    }

    Diagnostic AnimationAssetValidator::ValidateDomain(const AnimationClipAssetView& clip, Animation::PoseDomainType expectedType, FileFormat::AssetID expectedAssetID)
    {
        if (clip.root.sourcePoseDomainType != expectedType)
        {
            Diagnostic diagnostic = FailDiagnostic(DiagnosticCode::IncompatibleDomain, "AnimationClipAsset.sourcePoseDomainType", static_cast<u8>(clip.root.sourcePoseDomainType), static_cast<u8>(expectedType));
            diagnostic.dependencyAssetID = clip.root.sourcePoseDomainAssetID;
            return diagnostic;
        }
        if (clip.root.sourcePoseDomainAssetID != expectedAssetID)
        {
            Diagnostic diagnostic = FailDiagnostic(DiagnosticCode::IncompatibleDomain, "AnimationClipAsset.sourcePoseDomainAssetID", clip.root.sourcePoseDomainAssetID, expectedAssetID);
            diagnostic.dependencyAssetID = clip.root.sourcePoseDomainAssetID;
            return diagnostic;
        }
        return {};
    }
} // namespace AnimationLoading
