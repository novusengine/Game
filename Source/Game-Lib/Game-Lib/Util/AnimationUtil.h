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
    const ::ClientDB::Definitions::AnimationData* GetAnimationDataRec(::Animation::Defines::Type type);
    bool HasAnimationSequence(const Model::ComplexModel* modelInfo, ::Animation::Defines::Type animationType);
    ::Animation::Defines::SequenceID GetFirstSequenceForAnimation(const Model::ComplexModel* modelInfo, ::Animation::Defines::Type animationID);
    ::Animation::Defines::SequenceID GetSequenceIndexForAnimation(const Model::ComplexModel* modelInfo, ::Animation::Defines::Type animationType, i8& timesToRepeat);
    i16 GetBoneIndexFromKeyBoneID(const Model::ComplexModel* modelInfo, ::Animation::Defines::Bone bone);

    bool SetBoneSequenceRaw(const Model::ComplexModel* modelInfo, ECS::Components::AnimationData& animationData, u32 boneIndex, ::Animation::Defines::Type animationType, bool propagateToChildren, ::Animation::Defines::Flags flags = ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride blendOverride = ::Animation::Defines::BlendOverride::Auto, f32 speedModifier = 1.0f);
    bool SetBoneSequence(const Model::ComplexModel* modelInfo, ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, ::Animation::Defines::Type animationType, bool propagateToChildren, ::Animation::Defines::Flags flags = ::Animation::Defines::Flags::None, ::Animation::Defines::BlendOverride blendOverride = ::Animation::Defines::BlendOverride::Auto, f32 speedModifier = 1.0f);
    bool SetBoneRotation(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, quat offset);
    bool SetBoneSequenceSpeedModRaw(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, u32 boneIndex, f32 speedModifier);
    bool SetBoneSequenceSpeedMod(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, f32 speedModifier);

    const mat4x4* GetBoneMatrixRaw(::ECS::Components::AnimationData& animationData, u16 boneIndex);
    const mat4x4* GetBoneMatrix(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone);
}