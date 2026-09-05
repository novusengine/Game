#pragma once
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"

#include <span>

namespace Animation
{
    enum class IKSolveStatus : u8
    {
        Solved,
        Clamped,
        InvalidChain,
        DegenerateChain,
        InvalidGoal
    };

    struct IKSolveResult
    {
        IKSolveStatus status = IKSolveStatus::InvalidChain;
        f32 finalError = 0.0f;
        u32 affectedJointCount = 0;
        explicit operator bool() const { return status == IKSolveStatus::Solved || status == IKSolveStatus::Clamped; }
    };

    struct IKPoseWorkspace
    {
        std::vector<mat4a> modelTransforms;
        bool current = false;
    };

    // Applies bounded target-skeleton IK operations to a composed local pose.
    // Solvers consume cooked joint indices and never discover chains at runtime.
    IKSolveResult SolveTwoBoneIK(std::span<const FileFormat::Animation::SkeletonJoint> joints, u32 rootJoint, u32 middleJoint, u32 effectorJoint, const vec3& goalModel, const vec3& poleModel, f32 maximumReach, f32 weight, std::span<FileFormat::Animation::LocalTransform> localPose, IKPoseWorkspace* workspace = nullptr, const vec3& effectorLocalPoint = {}, const vec3& effectorLocalAxis = {}, const vec3& goalModelAxis = {});
    IKSolveResult SolveIterativeChainIK(std::span<const FileFormat::Animation::SkeletonJoint> joints, std::span<const u32> chainJoints, u32 effectorJoint, const vec3& goalModel, f32 maximumReach, u32 maximumIterations, f32 convergenceTolerance, f32 weight, std::span<FileFormat::Animation::LocalTransform> localPose, IKPoseWorkspace* workspace = nullptr, const vec3& effectorLocalPoint = {}, const vec3& effectorLocalAxis = {}, const vec3& goalModelAxis = {});
    IKSolveResult SolveAimIK(std::span<const FileFormat::Animation::SkeletonJoint> joints, u32 jointIndex, const vec3& localAxis, const vec3& goalModel, f32 weight, std::span<FileFormat::Animation::LocalTransform> localPose, IKPoseWorkspace* workspace = nullptr);
    bool ApplyIKJointLimits(std::span<const u32> jointIndices, std::span<const FileFormat::Animation::IKJointLimit> limits, std::span<FileFormat::Animation::LocalTransform> localPose, bool& clamped);
} // namespace Animation
