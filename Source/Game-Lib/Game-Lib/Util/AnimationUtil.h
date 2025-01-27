#pragma once
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/Gameplay/Animation/Defines.h"

#include <Base/Types.h>

#include <entt/fwd.hpp>

namespace ECS::Components
{
    struct AnimationData;
    struct AttachmentData;
}

namespace ClientDB::Definitions
{
    struct AnimationData;
}

namespace Model
{
    struct ComplexModel;
};

namespace Util::Animation
{
    const ::ClientDB::Definitions::AnimationData* GetAnimationDataRec(entt::registry& registry, ::Animation::Defines::Type type);
    bool HasAnimationSequence(entt::registry& registry, const Model::ComplexModel* modelInfo, ::Animation::Defines::Type animationType);
    ::Animation::Defines::SequenceID GetFirstSequenceForAnimation(const Model::ComplexModel* modelInfo, ::Animation::Defines::Type animationID);
    ::Animation::Defines::SequenceID GetSequenceIndexForAnimation(const Model::ComplexModel* modelInfo, ::Animation::Defines::Type animationType, i8& timesToRepeat);
    i16 GetBoneIndexFromKeyBoneID(const Model::ComplexModel* modelInfo, ::Animation::Defines::Bone bone);

    bool SetBoneSequenceRaw(entt::registry& registry, const Model::ComplexModel* modelInfo, ECS::Components::AnimationData& animationData, u32 boneIndex, ::Animation::Defines::Type animationType, bool propagateToChildren, ::Animation::Defines::Flags flags = ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride blendOverride = ::Animation::Defines::BlendOverride::Auto);
    bool SetBoneSequence(entt::registry& registry, const Model::ComplexModel* modelInfo, ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, ::Animation::Defines::Type animationType, bool propagateToChildren, ::Animation::Defines::Flags flags = ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride blendOverride = ::Animation::Defines::BlendOverride::Auto);
    bool SetBoneRotation(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, quat offset);
    const mat4x4* GetBoneMatrixRaw(::ECS::Components::AnimationData& animationData, u16 boneIndex);
    const mat4x4* GetBoneMatrix(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone);
}