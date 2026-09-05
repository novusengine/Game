#include "AnimationPose.h"

#include <Base/Math/Math.h>

#include <algorithm>
#include <cmath>

namespace
{
    bool IsFinite(const vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool IsFinite(const quat& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    }

    bool IsFinite(const mat4a& transform)
    {
        for (u32 column = 0; column < 4; ++column)
            for (u32 row = 0; row < 3; ++row)
                if (!std::isfinite(transform[column][row]))
                    return false;
        return true;
    }
}

namespace Animation
{
    bool ResolveBoneMask(const AnimationLoading::BoneMaskAssetView& mask, FileFormat::Animation::PoseDomainType poseDomainType, FileFormat::AssetID poseDomainAssetID, u32 jointCount, std::vector<f32>& outputWeights)
    {
        if (mask.root.poseDomainType != poseDomainType || mask.root.poseDomainAssetID != poseDomainAssetID || mask.jointWeights.size() != jointCount)
            return false;
        outputWeights.resize(jointCount);
        for (u32 jointIndex = 0; jointIndex < jointCount; ++jointIndex)
            outputWeights[jointIndex] = static_cast<f32>(mask.jointWeights[jointIndex]) / 255.0f;
        return true;
    }

    bool BlendOverrideLocalPoses(std::span<const FileFormat::Animation::LocalTransform> basePose, std::span<const FileFormat::Animation::LocalTransform> layerPose, f32 layerWeight, std::span<const f32> maskWeights, std::span<FileFormat::Animation::LocalTransform> output)
    {
        if (basePose.size() != layerPose.size() || output.size() != basePose.size() || (!maskWeights.empty() && maskWeights.size() != basePose.size()) || !std::isfinite(layerWeight))
            return false;
        layerWeight = std::clamp(layerWeight, 0.0f, 1.0f);
        for (u32 jointIndex = 0; jointIndex < output.size(); ++jointIndex)
        {
            const f32 weight = layerWeight * (maskWeights.empty() ? 1.0f : std::clamp(maskWeights[jointIndex], 0.0f, 1.0f));
            output[jointIndex].translation = glm::mix(basePose[jointIndex].translation, layerPose[jointIndex].translation, weight);
            quat layerRotation = layerPose[jointIndex].rotation;
            if (glm::dot(basePose[jointIndex].rotation, layerRotation) < 0.0f)
                layerRotation = -layerRotation;
            output[jointIndex].rotation = glm::normalize(glm::slerp(basePose[jointIndex].rotation, layerRotation, weight));
            output[jointIndex].scale = glm::mix(basePose[jointIndex].scale, layerPose[jointIndex].scale, weight);
        }
        return true;
    }

    bool BuildAdditiveLocalPose(std::span<const FileFormat::Animation::LocalTransform> sourcePose, std::span<const FileFormat::Animation::LocalTransform> referencePose, std::span<FileFormat::Animation::LocalTransform> output)
    {
        if (sourcePose.size() != referencePose.size() || output.size() != sourcePose.size())
            return false;
        constexpr f32 minimumScale = 1.0e-6f;
        for (u32 jointIndex = 0; jointIndex < output.size(); ++jointIndex)
        {
            const FileFormat::Animation::LocalTransform& source = sourcePose[jointIndex];
            const FileFormat::Animation::LocalTransform& reference = referencePose[jointIndex];
            if (!IsFinite(source.translation) || !IsFinite(source.rotation) || !IsFinite(source.scale) || !IsFinite(reference.translation) || !IsFinite(reference.rotation) || !IsFinite(reference.scale) || glm::any(glm::lessThan(glm::abs(reference.scale), vec3(minimumScale))))
                return false;
            output[jointIndex].translation = source.translation - reference.translation;
            output[jointIndex].rotation = glm::normalize(glm::inverse(reference.rotation) * source.rotation);
            output[jointIndex].scale = source.scale / reference.scale;
        }
        return true;
    }

    bool ApplyAdditiveLocalPose(std::span<const FileFormat::Animation::LocalTransform> basePose, std::span<const FileFormat::Animation::LocalTransform> additivePose, f32 layerWeight, std::span<const f32> maskWeights, std::span<FileFormat::Animation::LocalTransform> output)
    {
        if (basePose.size() != additivePose.size() || output.size() != basePose.size() || (!maskWeights.empty() && maskWeights.size() != basePose.size()) || !std::isfinite(layerWeight))
            return false;
        layerWeight = std::clamp(layerWeight, 0.0f, 1.0f);
        const quat identity(1.0f, 0.0f, 0.0f, 0.0f);
        for (u32 jointIndex = 0; jointIndex < output.size(); ++jointIndex)
        {
            const f32 weight = layerWeight * (maskWeights.empty() ? 1.0f : std::clamp(maskWeights[jointIndex], 0.0f, 1.0f));
            quat deltaRotation = additivePose[jointIndex].rotation;
            if (deltaRotation.w < 0.0f)
                deltaRotation = -deltaRotation;
            output[jointIndex].translation = basePose[jointIndex].translation + additivePose[jointIndex].translation * weight;
            output[jointIndex].rotation = glm::normalize(basePose[jointIndex].rotation * glm::slerp(identity, deltaRotation, weight));
            output[jointIndex].scale = basePose[jointIndex].scale * glm::mix(vec3(1.0f), additivePose[jointIndex].scale, weight);
        }
        return true;
    }

    bool BlendLocalPoses(std::span<const FileFormat::Animation::LocalTransform> outgoing, std::span<const FileFormat::Animation::LocalTransform> incoming, f32 weight, std::span<FileFormat::Animation::LocalTransform> output)
    {
        if (outgoing.size() != incoming.size() || output.size() != outgoing.size() || !std::isfinite(weight))
            return false;
        weight = std::clamp(weight, 0.0f, 1.0f);
        for (u32 jointIndex = 0; jointIndex < output.size(); ++jointIndex)
        {
            output[jointIndex].translation = glm::mix(outgoing[jointIndex].translation, incoming[jointIndex].translation, weight);
            quat incomingRotation = incoming[jointIndex].rotation;
            if (glm::dot(outgoing[jointIndex].rotation, incomingRotation) < 0.0f)
                incomingRotation = -incomingRotation;
            output[jointIndex].rotation = glm::normalize(glm::slerp(outgoing[jointIndex].rotation, incomingRotation, weight));
            output[jointIndex].scale = glm::mix(outgoing[jointIndex].scale, incoming[jointIndex].scale, weight);
            output[jointIndex].reserved0 = 0;
            output[jointIndex].reserved1 = 0;
        }
        return true;
    }

    mat4a ComposeLocalTransform(const FileFormat::Animation::LocalTransform& transform)
    {
        return Math::AffineMatrix::TransformMatrix(transform.translation, transform.rotation, transform.scale);
    }

    AffineFloat3x4 PackAffineFloat3x4(const mat4a& transform)
    {
        AffineFloat3x4 packed;
        packed.row0 = vec4(transform[0][0], transform[1][0], transform[2][0], transform[3][0]);
        packed.row1 = vec4(transform[0][1], transform[1][1], transform[2][1], transform[3][1]);
        packed.row2 = vec4(transform[0][2], transform[1][2], transform[2][2], transform[3][2]);
        return packed;
    }

    mat4a UnpackAffineFloat3x4(const AffineFloat3x4& transform)
    {
        return mat4a(transform.row0.x, transform.row1.x, transform.row2.x,
                     transform.row0.y, transform.row1.y, transform.row2.y,
                     transform.row0.z, transform.row1.z, transform.row2.z,
                     transform.row0.w, transform.row1.w, transform.row2.w);
    }

    vec3 TransformPoint(const AffineFloat3x4& transform, const vec3& point)
    {
        const vec4 homogeneous(point, 1.0f);
        return vec3(glm::dot(transform.row0, homogeneous), glm::dot(transform.row1, homogeneous), glm::dot(transform.row2, homogeneous));
    }

    vec3 TransformVector(const AffineFloat3x4& transform, const vec3& vector)
    {
        const vec4 homogeneous(vector, 0.0f);
        return vec3(glm::dot(transform.row0, homogeneous), glm::dot(transform.row1, homogeneous), glm::dot(transform.row2, homogeneous));
    }

    bool UpdateModelTransforms(std::span<const FileFormat::Animation::SkeletonJoint> joints, std::span<const FileFormat::Animation::LocalTransform> localPose, u32 firstJointIndex, std::span<mat4a> modelTransforms)
    {
        if (joints.size() != localPose.size() || modelTransforms.size() != joints.size() || firstJointIndex > joints.size())
            return false;
        for (u32 jointIndex = firstJointIndex; jointIndex < joints.size(); ++jointIndex)
        {
            const FileFormat::Animation::SkeletonJoint& joint = joints[jointIndex];
            const mat4a localTransform = ComposeLocalTransform(localPose[jointIndex]);
            if (!IsFinite(localTransform) || joint.parentJointIndex < -1 || joint.parentJointIndex >= static_cast<i32>(jointIndex))
                return false;
            modelTransforms[jointIndex] = joint.parentJointIndex < 0 ? localTransform : Math::AffineMatrix::MatrixMul(modelTransforms[joint.parentJointIndex], localTransform);
            if (!IsFinite(modelTransforms[jointIndex]))
                return false;
        }
        return true;
    }

    PoseEvaluationResult EvaluatePose(std::span<const FileFormat::Animation::SkeletonJoint> joints, std::span<const FileFormat::Animation::LocalTransform> localPose)
    {
        PoseEvaluationResult result;
        if (joints.size() != localPose.size())
        {
            result.error = PoseEvaluationError::JointCountMismatch;
            return result;
        }

        result.modelTransforms.resize(joints.size());
        result.palette.resize(joints.size());
        u32 rootCount = 0;
        for (u32 jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
        {
            const FileFormat::Animation::SkeletonJoint& joint = joints[jointIndex];
            const mat4a localTransform = ComposeLocalTransform(localPose[jointIndex]);
            if (!IsFinite(localTransform) || !IsFinite(joint.inverseBindTransform))
            {
                result.error = PoseEvaluationError::NonFiniteTransform;
                result.errorJointIndex = jointIndex;
                return result;
            }

            if (joint.parentJointIndex == -1)
            {
                ++rootCount;
                result.modelTransforms[jointIndex] = localTransform;
            }
            else
            {
                if (joint.parentJointIndex < 0 || static_cast<u32>(joint.parentJointIndex) >= jointIndex)
                {
                    result.error = PoseEvaluationError::InvalidParent;
                    result.errorJointIndex = jointIndex;
                    return result;
                }
                result.modelTransforms[jointIndex] = Math::AffineMatrix::MatrixMul(result.modelTransforms[joint.parentJointIndex], localTransform);
            }

            const mat4a skinningTransform = Math::AffineMatrix::MatrixMul(result.modelTransforms[jointIndex], joint.inverseBindTransform);
            if (!IsFinite(skinningTransform))
            {
                result.error = PoseEvaluationError::NonFiniteTransform;
                result.errorJointIndex = jointIndex;
                return result;
            }
            result.palette[jointIndex] = PackAffineFloat3x4(skinningTransform);
        }

        if (rootCount == 0)
            result.error = PoseEvaluationError::NoRoot;
        else if (rootCount != 1)
            result.error = PoseEvaluationError::MultipleRoots;
        return result;
    }

    PoseEvaluationResult EvaluateRestPose(const AnimationLoading::SkeletonAssetView& skeleton)
    {
        std::vector<FileFormat::Animation::LocalTransform> localPose;
        localPose.reserve(skeleton.joints.size());
        for (const FileFormat::Animation::SkeletonJoint& joint : skeleton.joints)
            localPose.push_back(joint.restTransform);
        return EvaluatePose(skeleton.joints, localPose);
    }
} // namespace Animation
