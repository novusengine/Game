#include "AnimationPose.h"

#include <Base/Math/Math.h>

#include <cmath>

namespace
{
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
