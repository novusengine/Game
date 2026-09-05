#include "AnimationIK.h"

#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    quat ModelRotation(const mat4a& transform)
    {
        mat3x3 rotation(transform);
        for (u32 axis = 0; axis < 3; ++axis)
            rotation[axis] = glm::normalize(rotation[axis]);
        return glm::normalize(glm::quat_cast(rotation));
    }

    quat LocalizeDelta(std::span<const FileFormat::Animation::SkeletonJoint> joints, std::span<const mat4a> modelTransforms, u32 jointIndex, const quat& modelDelta)
    {
        const i32 parent = joints[jointIndex].parentJointIndex;
        if (parent < 0)
            return modelDelta;
        const quat parentRotation = ModelRotation(modelTransforms[parent]);
        return glm::inverse(parentRotation) * modelDelta * parentRotation;
    }

    bool Finite(const vec3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
}

namespace Animation
{
    IKSolveResult SolveTwoBoneIK(std::span<const FileFormat::Animation::SkeletonJoint> joints, u32 rootJoint, u32 middleJoint, u32 effectorJoint, const vec3& goalModel, const vec3& poleModel, f32 maximumReach, f32 weight, std::span<FileFormat::Animation::LocalTransform> localPose, IKPoseWorkspace* workspace, const vec3& effectorLocalPoint, const vec3& effectorLocalAxis, const vec3& goalModelAxis)
    {
        IKSolveResult result;
        if (localPose.size() != joints.size() || rootJoint >= joints.size() || middleJoint >= joints.size() || effectorJoint >= joints.size() || joints[middleJoint].parentJointIndex != static_cast<i32>(rootJoint) || joints[effectorJoint].parentJointIndex != static_cast<i32>(middleJoint))
            return result;
        if (!Finite(goalModel) || !Finite(poleModel) || !std::isfinite(maximumReach) || maximumReach <= 0.0f || !std::isfinite(weight))
        {
            result.status = IKSolveStatus::InvalidGoal;
            return result;
        }
        weight = std::clamp(weight, 0.0f, 1.0f);
        IKPoseWorkspace localWorkspace;
        IKPoseWorkspace& poseWorkspace = workspace ? *workspace : localWorkspace;
        if (poseWorkspace.modelTransforms.size() != joints.size())
        {
            poseWorkspace.modelTransforms.resize(joints.size());
            poseWorkspace.current = false;
        }
        if (!poseWorkspace.current && !UpdateModelTransforms(joints, localPose, 0, poseWorkspace.modelTransforms))
            return result;
        poseWorkspace.current = true;
        const vec3 rootPosition = vec3(poseWorkspace.modelTransforms[rootJoint][3]);
        const vec3 middlePosition = vec3(poseWorkspace.modelTransforms[middleJoint][3]);
        const mat4x4 effectorTransform = mat4x4(poseWorkspace.modelTransforms[effectorJoint]);
        const vec3 wristPosition = vec3(effectorTransform[3]);
        const vec3 socketPosition = vec3(effectorTransform * vec4(effectorLocalPoint, 1.0f));
        const bool alignEffector = glm::length2(effectorLocalAxis) > 1.0e-8f && glm::length2(goalModelAxis) > 1.0e-8f;
        quat desiredEffectorRotation = ModelRotation(effectorTransform);
        vec3 desiredSocketOffset = socketPosition - wristPosition;
        if (alignEffector)
        {
            const vec3 currentAxis = glm::normalize(desiredEffectorRotation * glm::normalize(effectorLocalAxis));
            const quat alignmentDelta = glm::slerp(quat(1.0f, 0.0f, 0.0f, 0.0f), glm::rotation(currentAxis, glm::normalize(goalModelAxis)), weight);
            desiredEffectorRotation = glm::normalize(alignmentDelta * desiredEffectorRotation);
            desiredSocketOffset = alignmentDelta * desiredSocketOffset;
        }
        const vec3 desiredWristPosition = alignEffector ? goalModel - desiredSocketOffset : goalModel;
        const vec3 effectorPosition = alignEffector ? wristPosition : socketPosition;
        const f32 firstLength = glm::length(middlePosition - rootPosition);
        const f32 secondLength = glm::length(effectorPosition - middlePosition);
        const f32 wristLength = glm::length(wristPosition - middlePosition);
        if (firstLength <= 1.0e-6f || secondLength <= 1.0e-6f)
        {
            result.status = IKSolveStatus::DegenerateChain;
            return result;
        }
        vec3 goalVector = desiredWristPosition - rootPosition;
        const f32 authoredReach = std::min(maximumReach + std::max(0.0f, secondLength - wristLength), firstLength + secondLength);
        const f32 originalDistance = glm::length(goalVector);
        if (originalDistance <= 1.0e-6f)
            goalVector = glm::normalize(effectorPosition - rootPosition) * 1.0e-6f;
        const f32 distance = std::clamp(glm::length(goalVector), std::abs(firstLength - secondLength) + 1.0e-6f, authoredReach);
        const vec3 direction = glm::normalize(goalVector);
        vec3 bend = middlePosition - rootPosition - direction * glm::dot(middlePosition - rootPosition, direction);
        if (glm::length2(bend) <= 1.0e-8f)
            bend = poleModel - rootPosition - direction * glm::dot(poleModel - rootPosition, direction);
        if (glm::length2(bend) <= 1.0e-8f)
            bend = glm::cross(direction, std::abs(direction.y) < 0.9f ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f));
        bend = glm::normalize(bend);
        const f32 cosine = std::clamp((firstLength * firstLength + distance * distance - secondLength * secondLength) / (2.0f * firstLength * distance), -1.0f, 1.0f);
        const f32 sine = std::sqrt(std::max(0.0f, 1.0f - cosine * cosine));
        const vec3 desiredMiddle = rootPosition + direction * (cosine * firstLength) + bend * (sine * firstLength);
        const quat rootDelta = glm::slerp(quat(1.0f, 0.0f, 0.0f, 0.0f), glm::rotation(glm::normalize(middlePosition - rootPosition), glm::normalize(desiredMiddle - rootPosition)), weight);
        localPose[rootJoint].rotation = glm::normalize(LocalizeDelta(joints, poseWorkspace.modelTransforms, rootJoint, rootDelta) * localPose[rootJoint].rotation);

        if (!UpdateModelTransforms(joints, localPose, rootJoint, poseWorkspace.modelTransforms))
        {
            poseWorkspace.current = false;
            return result;
        }
        const vec3 updatedMiddle = vec3(poseWorkspace.modelTransforms[middleJoint][3]);
        const vec3 updatedEffector = vec3(mat4x4(poseWorkspace.modelTransforms[effectorJoint]) * vec4(effectorLocalPoint, 1.0f));
        const vec3 desiredEffector = rootPosition + direction * distance;
        const quat middleDelta = glm::slerp(quat(1.0f, 0.0f, 0.0f, 0.0f), glm::rotation(glm::normalize(updatedEffector - updatedMiddle), glm::normalize(desiredEffector - updatedMiddle)), weight);
        localPose[middleJoint].rotation = glm::normalize(LocalizeDelta(joints, poseWorkspace.modelTransforms, middleJoint, middleDelta) * localPose[middleJoint].rotation);
        if (!UpdateModelTransforms(joints, localPose, middleJoint, poseWorkspace.modelTransforms))
        {
            poseWorkspace.current = false;
            result.finalError = std::numeric_limits<f32>::infinity();
            return result;
        }
        if (alignEffector)
        {
            const quat currentEffectorRotation = ModelRotation(poseWorkspace.modelTransforms[effectorJoint]);
            const quat effectorDelta = desiredEffectorRotation * glm::inverse(currentEffectorRotation);
            localPose[effectorJoint].rotation = glm::normalize(LocalizeDelta(joints, poseWorkspace.modelTransforms, effectorJoint, effectorDelta) * localPose[effectorJoint].rotation);
            if (!UpdateModelTransforms(joints, localPose, effectorJoint, poseWorkspace.modelTransforms))
            {
                poseWorkspace.current = false;
                result.finalError = std::numeric_limits<f32>::infinity();
                return result;
            }
        }
        result.finalError = glm::length(vec3(mat4x4(poseWorkspace.modelTransforms[effectorJoint]) * vec4(effectorLocalPoint, 1.0f)) - goalModel);
        result.affectedJointCount = 3;
        result.status = originalDistance > authoredReach ? IKSolveStatus::Clamped : IKSolveStatus::Solved;
        return result;
    }

    IKSolveResult SolveIterativeChainIK(std::span<const FileFormat::Animation::SkeletonJoint> joints, std::span<const u32> chainJoints, u32 effectorJoint, const vec3& goalModel, f32 maximumReach, u32 maximumIterations, f32 convergenceTolerance, f32 weight, std::span<FileFormat::Animation::LocalTransform> localPose, IKPoseWorkspace* workspace, const vec3& effectorLocalPoint, const vec3& effectorLocalAxis, const vec3& goalModelAxis)
    {
        IKSolveResult result;
        if (localPose.size() != joints.size() || chainJoints.size() < 2 || chainJoints.back() != effectorJoint || effectorJoint >= joints.size())
            return result;
        for (u32 index = 0; index < chainJoints.size(); ++index)
        {
            if (chainJoints[index] >= joints.size() || (index != 0 && joints[chainJoints[index]].parentJointIndex != static_cast<i32>(chainJoints[index - 1])))
                return result;
        }
        if (!Finite(goalModel) || !std::isfinite(maximumReach) || maximumReach <= 0.0f || maximumIterations == 0 || !std::isfinite(convergenceTolerance) || convergenceTolerance <= 0.0f || !std::isfinite(weight))
        {
            result.status = IKSolveStatus::InvalidGoal;
            return result;
        }
        weight = std::clamp(weight, 0.0f, 1.0f);
        IKPoseWorkspace localWorkspace;
        IKPoseWorkspace& poseWorkspace = workspace ? *workspace : localWorkspace;
        if (poseWorkspace.modelTransforms.size() != joints.size())
        {
            poseWorkspace.modelTransforms.resize(joints.size());
            poseWorkspace.current = false;
        }
        if (!poseWorkspace.current && !UpdateModelTransforms(joints, localPose, 0, poseWorkspace.modelTransforms))
            return result;
        poseWorkspace.current = true;
        const vec3 rootPosition = vec3(poseWorkspace.modelTransforms[chainJoints.front()][3]);
        const bool alignEffector = glm::length2(effectorLocalAxis) > 1.0e-8f && glm::length2(goalModelAxis) > 1.0e-8f;
        vec3 desiredWristPosition = goalModel;
        result.finalError = std::numeric_limits<f32>::infinity();
        for (u32 iteration = 0; iteration < maximumIterations; ++iteration)
        {
            const mat4x4 initialEffectorTransform = mat4x4(poseWorkspace.modelTransforms[effectorJoint]);
            const vec3 initialWristPosition(initialEffectorTransform[3]);
            const vec3 initialSocketPosition(initialEffectorTransform * vec4(effectorLocalPoint, 1.0f));
            quat desiredEffectorRotation = ModelRotation(initialEffectorTransform);
            vec3 desiredSocketOffset = initialSocketPosition - initialWristPosition;
            if (alignEffector)
            {
                const vec3 currentAxis = glm::normalize(desiredEffectorRotation * glm::normalize(effectorLocalAxis));
                const quat alignmentDelta = glm::slerp(quat(1.0f, 0.0f, 0.0f, 0.0f), glm::rotation(currentAxis, glm::normalize(goalModelAxis)), weight);
                desiredEffectorRotation = glm::normalize(alignmentDelta * desiredEffectorRotation);
                desiredSocketOffset = alignmentDelta * desiredSocketOffset;
            }
            desiredWristPosition = goalModel - desiredSocketOffset;
            for (u32 chainOffset = static_cast<u32>(chainJoints.size() - 1); chainOffset-- > 0;)
            {
                const u32 jointIndex = chainJoints[chainOffset];
                const vec3 jointPosition = vec3(poseWorkspace.modelTransforms[jointIndex][3]);
                const vec3 wristPosition = vec3(poseWorkspace.modelTransforms[effectorJoint][3]);
                if (glm::length2(wristPosition - jointPosition) <= 1.0e-8f || glm::length2(desiredWristPosition - jointPosition) <= 1.0e-8f)
                    continue;
                const quat modelDelta = glm::slerp(quat(1.0f, 0.0f, 0.0f, 0.0f), glm::rotation(glm::normalize(wristPosition - jointPosition), glm::normalize(desiredWristPosition - jointPosition)), weight);
                localPose[jointIndex].rotation = glm::normalize(LocalizeDelta(joints, poseWorkspace.modelTransforms, jointIndex, modelDelta) * localPose[jointIndex].rotation);
                if (!UpdateModelTransforms(joints, localPose, jointIndex, poseWorkspace.modelTransforms))
                {
                    poseWorkspace.current = false;
                    return result;
                }
            }
            if (alignEffector)
            {
                const quat currentEffectorRotation = ModelRotation(poseWorkspace.modelTransforms[effectorJoint]);
                const quat effectorDelta = desiredEffectorRotation * glm::inverse(currentEffectorRotation);
                localPose[effectorJoint].rotation = glm::normalize(LocalizeDelta(joints, poseWorkspace.modelTransforms, effectorJoint, effectorDelta) * localPose[effectorJoint].rotation);
                if (!UpdateModelTransforms(joints, localPose, effectorJoint, poseWorkspace.modelTransforms))
                {
                    poseWorkspace.current = false;
                    return result;
                }
            }
            result.finalError = glm::length(vec3(mat4x4(poseWorkspace.modelTransforms[effectorJoint]) * vec4(effectorLocalPoint, 1.0f)) - goalModel);
            if (result.finalError <= convergenceTolerance)
                break;
        }
        result.affectedJointCount = static_cast<u32>(chainJoints.size());
        result.status = glm::length(desiredWristPosition - rootPosition) > maximumReach ? IKSolveStatus::Clamped : IKSolveStatus::Solved;
        return result;
    }

    IKSolveResult SolveAimIK(std::span<const FileFormat::Animation::SkeletonJoint> joints, u32 jointIndex, const vec3& localAxis, const vec3& goalModel, f32 weight, std::span<FileFormat::Animation::LocalTransform> localPose, IKPoseWorkspace* workspace)
    {
        IKSolveResult result;
        if (localPose.size() != joints.size() || jointIndex >= joints.size() || !Finite(localAxis) || glm::length2(localAxis) <= 1.0e-8f || !Finite(goalModel) || !std::isfinite(weight))
            return result;
        IKPoseWorkspace localWorkspace;
        IKPoseWorkspace& poseWorkspace = workspace ? *workspace : localWorkspace;
        if (poseWorkspace.modelTransforms.size() != joints.size())
        {
            poseWorkspace.modelTransforms.resize(joints.size());
            poseWorkspace.current = false;
        }
        if (!poseWorkspace.current && !UpdateModelTransforms(joints, localPose, 0, poseWorkspace.modelTransforms))
            return result;
        poseWorkspace.current = true;
        const vec3 position = vec3(poseWorkspace.modelTransforms[jointIndex][3]);
        if (glm::length2(goalModel - position) <= 1.0e-8f)
        {
            result.status = IKSolveStatus::InvalidGoal;
            return result;
        }
        const quat modelRotation = ModelRotation(poseWorkspace.modelTransforms[jointIndex]);
        const vec3 currentAxis = modelRotation * glm::normalize(localAxis);
        const quat delta = glm::slerp(quat(1.0f, 0.0f, 0.0f, 0.0f), glm::rotation(currentAxis, glm::normalize(goalModel - position)), std::clamp(weight, 0.0f, 1.0f));
        localPose[jointIndex].rotation = glm::normalize(LocalizeDelta(joints, poseWorkspace.modelTransforms, jointIndex, delta) * localPose[jointIndex].rotation);
        if (!UpdateModelTransforms(joints, localPose, jointIndex, poseWorkspace.modelTransforms))
        {
            poseWorkspace.current = false;
            return result;
        }
        const vec3 aimedAxis = ModelRotation(poseWorkspace.modelTransforms[jointIndex]) * glm::normalize(localAxis);
        result.finalError = glm::acos(std::clamp(glm::dot(aimedAxis, glm::normalize(goalModel - position)), -1.0f, 1.0f));
        result.affectedJointCount = 1;
        result.status = IKSolveStatus::Solved;
        return result;
    }

    bool ApplyIKJointLimits(std::span<const u32> jointIndices, std::span<const FileFormat::Animation::IKJointLimit> limits, std::span<FileFormat::Animation::LocalTransform> localPose, bool& clamped)
    {
        clamped = false;
        if (limits.empty())
            return true;
        if (jointIndices.size() != limits.size())
            return false;
        for (u32 localIndex = 0; localIndex < jointIndices.size(); ++localIndex)
        {
            const u32 jointIndex = jointIndices[localIndex];
            if (jointIndex >= localPose.size())
                return false;
            const vec3 angles = glm::eulerAngles(localPose[jointIndex].rotation);
            const vec3 limited = glm::clamp(angles, limits[localIndex].minimumRadians, limits[localIndex].maximumRadians);
            if (glm::any(glm::greaterThan(glm::abs(angles - limited), vec3(1.0e-6f))))
            {
                localPose[jointIndex].rotation = glm::normalize(quat(limited));
                clamped = true;
            }
        }
        return true;
    }
} // namespace Animation
