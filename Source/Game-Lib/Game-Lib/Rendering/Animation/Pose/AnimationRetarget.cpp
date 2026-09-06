#include "AnimationRetarget.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"
#include "AnimationIK.h"
#include "AnimationPose.h"

#include <Base/Util/StringUtils.h>
#include <algorithm>
#include <glm/gtx/matrix_decompose.hpp>

namespace
{
    using Transform = FileFormat::Animation::LocalTransform;

    Transform Convert(const Transform& pose, const Transform& rest, const Transform& canonical, const Transform& correction, bool additive)
    {
        Transform result;
        const quat delta = additive ? pose.rotation : glm::inverse(rest.rotation) * pose.rotation;
        const quat rotated = correction.rotation * delta * glm::inverse(correction.rotation);
        result.rotation = glm::normalize(additive ? rotated : canonical.rotation * rotated);
        const vec3 translation = additive ? pose.translation : pose.translation - rest.translation;
        const vec3 corrected = canonical.rotation * (correction.rotation * (glm::inverse(rest.rotation) * translation) * correction.scale);
        result.translation = additive ? corrected : canonical.translation + corrected;
        result.scale = additive ? pose.scale : canonical.scale * (pose.scale / rest.scale);
        return result;
    }
}

namespace Animation
{
    bool FamilyPoseBridge::Initialize(const AnimationLoading::RetargetProfileAssetView& profile, const AnimationLoading::RigFamilyAssetView& source, const AnimationLoading::RigFamilyAssetView& target)
    {
        *this = {};
        if (profile.root.flags != 0 || profile.root.reserved != 0 || profile.jointMappings.empty() || !AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::RigFamilyAssetReadResult{source, {}}) || !AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::RigFamilyAssetReadResult{target, {}}))
            return false;
        std::vector<bool> written(target.joints.size(), false);
        for (const auto& mapping : profile.jointMappings)
        {
            if (mapping.sourceJointIndex < 0 || mapping.targetJointIndex < 0 || mapping.sourceJointIndex >= source.joints.size() || mapping.targetJointIndex >= target.joints.size() || written[mapping.targetJointIndex] || mapping.semanticID != target.joints[mapping.targetJointIndex].semanticID)
                return false;
            const auto& transform = mapping.sourceToTarget;
            if (!std::isfinite(glm::length(transform.translation)) || glm::length(transform.translation) > 0.00001f || !std::isfinite(glm::length(transform.rotation)) || std::abs(glm::length(transform.rotation) - 1.0f) > 0.0001f || !std::isfinite(glm::length(transform.scale)) || glm::any(glm::lessThanEqual(transform.scale, vec3(0))))
                return false;
            written[mapping.targetJointIndex] = true;
        }
        for (u32 index = 0; index < target.joints.size(); ++index)
            if (!written[index] && (target.joints[index].flags & FileFormat::Animation::RigFamilyJointFlags_Required) != 0)
                return false;
        _mappings.assign(profile.jointMappings.begin(), profile.jointMappings.end());
        for (const auto& joint : source.joints) _source.push_back(joint.canonicalTransform);
        for (const auto& joint : target.joints) _target.push_back(joint.canonicalTransform);
        return true;
    }

    bool FamilyPoseBridge::ConvertPose(std::span<const Transform> source, std::vector<Transform>& target, bool additive) const
    {
        if (source.size() != _source.size() || _target.empty())
            return false;
        target = additive ? std::vector<Transform>(_target.size()) : _target;
        for (const auto& mapping : _mappings)
            target[mapping.targetJointIndex] = Convert(source[mapping.sourceJointIndex], _source[mapping.sourceJointIndex], _target[mapping.targetJointIndex], mapping.sourceToTarget, additive);
        return true;
    }

    bool FamilyPoseBinding::Initialize(const AnimationLoading::SkeletonAssetView& skeleton, const AnimationLoading::RigFamilyAssetView& family)
    {
        _signature = 0;
        _bindings.clear();
        _rest.clear();
        _canonical.clear();
        _parents.clear();
        _propagation.clear();
        _chains.clear();
        _familyJoints.clear();
        if (AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(skeleton, family))
            return false;
        _bindings.assign(skeleton.familyBindings.begin(), skeleton.familyBindings.end());
        for (const auto& joint : skeleton.joints)
        {
            _rest.push_back(joint.restTransform);
            _parents.push_back(joint.parentJointIndex);
        }
        _propagation.assign(skeleton.propagationRules.begin(), skeleton.propagationRules.end());
        for (const auto& joint : family.joints)
        {
            _canonical.push_back(joint.canonicalTransform);
            FileFormat::Animation::SkeletonJoint value;
            value.parentJointIndex = joint.parentJointIndex;
            value.restTransform = joint.canonicalTransform;
            _familyJoints.push_back(value);
        }
        _joints.assign(skeleton.joints.begin(), skeleton.joints.end());
        const auto familyRest = EvaluatePose(_familyJoints, _canonical);
        const auto targetRest = EvaluatePose(_joints, _rest);
        if (!familyRest || !targetRest)
            return false;
        for (const auto& authored : family.chains)
        {
            if (_chains.size() == 4)
                break;
            if (authored.numJointIndices != 3)
                continue;
            Chain chain;
            bool mapped = true;
            f32 familyLength = 0;
            for (u32 index = 0; index < 3; ++index)
            {
                const u32 familyIndex = family.chainJointIndices[authored.jointIndexOffset + index];
                const auto binding = std::find_if(_bindings.begin(), _bindings.end(), [familyIndex](const auto& value) { return value.familyJointIndex == familyIndex; });
                if (binding == _bindings.end()) { mapped = false; break; }
                chain.target[index] = binding->skeletonJointIndex;
                chain.familyEffector = familyIndex;
                if (index > 0)
                {
                    const u32 previousFamily = family.chainJointIndices[authored.jointIndexOffset + index - 1];
                    familyLength += glm::length(vec3(familyRest.modelTransforms[familyIndex][3]) - vec3(familyRest.modelTransforms[previousFamily][3]));
                    chain.reach += glm::length(vec3(targetRest.modelTransforms[chain.target[index]][3]) - vec3(targetRest.modelTransforms[chain.target[index - 1]][3]));
                }
            }
            if (!mapped || familyLength <= 0 || chain.reach <= 0)
                continue;
            chain.familyRestEffector = vec3(familyRest.modelTransforms[chain.familyEffector][3]);
            chain.targetRestEffector = vec3(targetRest.modelTransforms[chain.target[2]][3]);
            chain.proportion = chain.reach / familyLength;
            _chains.push_back(chain);
        }
        _signature = XXHash64::hash(skeleton.joints.data(), skeleton.joints.size_bytes(), skeleton.root.rigFamilyAssetID);
        _signature = XXHash64::hash(family.joints.data(), family.joints.size_bytes(), _signature);
        _signature = XXHash64::hash(family.chains.data(), family.chains.size_bytes(), _signature);
        _signature = XXHash64::hash(family.chainJointIndices.data(), family.chainJointIndices.size_bytes(), _signature);
        _signature = XXHash64::hash(_bindings.data(), _bindings.size() * sizeof(_bindings[0]), _signature);
        _signature = XXHash64::hash(_propagation.data(), _propagation.size() * sizeof(FileFormat::Animation::SkeletonPropagationRule), _signature);
        return true;
    }

    bool FamilyPoseBinding::ToFamily(std::span<const Transform> source, std::span<Transform> familyPose, bool additive) const
    {
        if (_rest.empty() || source.size() != _rest.size() || familyPose.size() != _canonical.size())
            return false;
        for (u32 index = 0; index < familyPose.size(); ++index)
            familyPose[index] = additive ? Transform{} : _canonical[index];
        for (const auto& binding : _bindings)
            familyPose[binding.familyJointIndex] = Convert(source[binding.skeletonJointIndex], _rest[binding.skeletonJointIndex], _canonical[binding.familyJointIndex], binding.skeletonToFamily, additive);
        return true;
    }

    bool FamilyPoseBinding::FromFamily(std::span<const Transform> familyPose, std::span<Transform> target, bool additive, bool correctChains) const
    {
        if (_rest.empty() || target.size() != _rest.size() || familyPose.size() != _canonical.size())
            return false;
        if (additive && !_propagation.empty())
        {
            // Helper offsets describe absolute poses, not additive deltas.
            std::vector<Transform> absolute(_canonical.size()), reference(target.size()), propagated(target.size());
            for (u32 index = 0; index < absolute.size(); ++index)
            {
                absolute[index].translation = _canonical[index].translation + familyPose[index].translation;
                absolute[index].rotation = glm::normalize(_canonical[index].rotation * familyPose[index].rotation);
                absolute[index].scale = _canonical[index].scale * familyPose[index].scale;
            }
            if (!FromFamily(_canonical, reference) || !FromFamily(absolute, propagated))
                return false;
            for (u32 index = 0; index < target.size(); ++index)
            {
                target[index].translation = propagated[index].translation - reference[index].translation;
                target[index].rotation = glm::normalize(glm::inverse(reference[index].rotation) * propagated[index].rotation);
                target[index].scale = propagated[index].scale / reference[index].scale;
            }
            return true;
        }
        for (u32 index = 0; index < target.size(); ++index)
            target[index] = additive ? Transform{} : _rest[index];
        for (const auto& binding : _bindings)
            target[binding.skeletonJointIndex] = Convert(familyPose[binding.familyJointIndex], _canonical[binding.familyJointIndex], _rest[binding.skeletonJointIndex], binding.familyToSkeleton, additive);
        if (correctChains && !additive && !_chains.empty())
        {
            const auto canonicalModel = EvaluatePose(_familyJoints, familyPose);
            if (!canonicalModel)
                return false;
            for (const auto& chain : _chains)
            {
                const auto targetModel = EvaluatePose(_joints, target);
                if (!targetModel)
                    return false;
                const vec3 goal = chain.targetRestEffector + (vec3(canonicalModel.modelTransforms[chain.familyEffector][3]) - chain.familyRestEffector) * chain.proportion;
                const vec3 pole = vec3(targetModel.modelTransforms[chain.target[1]][3]);
                if (!SolveTwoBoneIK(_joints, chain.target[0], chain.target[1], chain.target[2], goal, pole, chain.reach, 1.0f, target))
                    return false;
            }
        }
        for (const auto& rule : _propagation)
        {
            const u32 source = rule.sourceJointIndex;
            const u32 destination = rule.targetJointIndex;
            Transform value = target[source];
            if (rule.type == FileFormat::Animation::SkeletonPropagationType::DistributeRotation)
            {
                value = additive ? Transform{} : _rest[destination];
                const quat delta = additive ? target[source].rotation : glm::inverse(_rest[source].rotation) * target[source].rotation;
                value.rotation = glm::normalize(value.rotation * glm::slerp(quat(1, 0, 0, 0), delta, rule.weight) * rule.offsetTransform.rotation);
            }
            else
            {
                auto matrix = [](const Transform& transform) { return glm::translate(mat4x4(1), transform.translation) * glm::mat4_cast(transform.rotation) * glm::scale(mat4x4(1), transform.scale); };
                mat4x4 transformed = matrix(value);
                if (rule.type == FileFormat::Animation::SkeletonPropagationType::CopyModelTransform)
                {
                    for (i32 parent = _parents[source]; parent >= 0; parent = _parents[parent])
                        transformed = matrix(target[parent]) * transformed;
                    mat4x4 parentModel(1);
                    for (i32 parent = _parents[destination]; parent >= 0; parent = _parents[parent])
                        parentModel = matrix(target[parent]) * parentModel;
                    transformed = glm::inverse(parentModel) * transformed;
                }
                transformed *= matrix(rule.offsetTransform);
                vec3 skew;
                vec4 perspective;
                if (!glm::decompose(transformed, value.scale, value.rotation, value.translation, skew, perspective))
                    return false;
                value.rotation = glm::normalize(value.rotation);
                value.translation = glm::mix(target[destination].translation, value.translation, rule.weight);
                value.rotation = glm::normalize(glm::slerp(target[destination].rotation, value.rotation, rule.weight));
                value.scale = glm::mix(target[destination].scale, value.scale, rule.weight);
            }
            target[destination] = value;
        }
        return true;
    }
}
