#include "AnimationUtil.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Util/ServiceLocator.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

namespace Util::Animation
{
    const ::ClientDB::Definitions::AnimationData* GetAnimationDataRec(entt::registry& registry, ::Animation::Defines::Type type)
    {
        auto& clientDBCollection = registry.ctx().get<ECS::Singletons::ClientDBCollection>();
        auto* animationDatas = clientDBCollection.Get(ECS::Singletons::ClientDBHash::AnimationData);

        if (!animationDatas)
            return nullptr;

        u32 typeIndex = static_cast<u32>(type);
        return &animationDatas->Get<ClientDB::Definitions::AnimationData>(typeIndex);
    }

    bool HasAnimationSequence(entt::registry& registry, const Model::ComplexModel* modelInfo, ::Animation::Defines::Type animationType)
    {
        const ::ClientDB::Definitions::AnimationData* animationDataRec = GetAnimationDataRec(registry, animationType);
        if (!animationDataRec)
            return false;

        ::Animation::Defines::SequenceID sequenceID = GetFirstSequenceForAnimation(modelInfo, animationType);

        bool canUseFallback = animationDataRec->fallback != 0;
        if (sequenceID == ::Animation::Defines::InvalidSequenceID && canUseFallback)
        {
            auto fallbackAnimationType = static_cast<::Animation::Defines::Type>(animationDataRec->fallback);
            sequenceID = GetFirstSequenceForAnimation(modelInfo, fallbackAnimationType);
        }

        bool result = sequenceID != ::Animation::Defines::InvalidSequenceID;
        return result;
    }

    ::Animation::Defines::SequenceID GetFirstSequenceForAnimation(const Model::ComplexModel* modelInfo, ::Animation::Defines::Type animationID)
    {
        if (!modelInfo->animationIDToFirstSequenceID.contains((i16)animationID))
        {
            return ::Animation::Defines::InvalidSequenceID;
        }

        u32 numSequences = static_cast<u16>(modelInfo->sequences.size());
        u16 sequenceID = modelInfo->animationIDToFirstSequenceID.at((i16)animationID);
        if (sequenceID >= numSequences)
        {
            return ::Animation::Defines::InvalidSequenceID;
        }

        return sequenceID;
    }

    ::Animation::Defines::SequenceID GetSequenceIndexForAnimation(const Model::ComplexModel* modelInfo, ::Animation::Defines::Type animationType, i8& timesToRepeat)
    {
        i32 probability = static_cast<i32>((static_cast<f32>(rand()) / static_cast<f32>(RAND_MAX)) * static_cast<f32>(0x7FFF));
        i32 currentProbability = 0;

        u32 nextSequenceID = ::Animation::Defines::InvalidSequenceID;
        i32 variationID = GetFirstSequenceForAnimation(modelInfo, animationType);

        do
        {
            const Model::ComplexModel::AnimationSequence& currentVariation = modelInfo->sequences[variationID];

            currentProbability += currentVariation.frequency;
            nextSequenceID = variationID;
            variationID = currentVariation.nextVariationID;

        } while (currentProbability < probability && variationID != -1);

        NC_ASSERT(nextSequenceID != ::Animation::Defines::InvalidSequenceID, "nextSequenceID is invalid.");

        const Model::ComplexModel::AnimationSequence& nextVariationSequence = modelInfo->sequences[nextSequenceID];
        u32 minRepetitions = nextVariationSequence.repetitionRange.x;
        u32 maxRepetitions = nextVariationSequence.repetitionRange.y;

        timesToRepeat = static_cast<i8>(minRepetitions + ((maxRepetitions - minRepetitions) * (static_cast<f32>(rand()) / static_cast<f32>(RAND_MAX)))) - 1;

        return nextSequenceID;
    }

    i16 GetBoneIndexFromKeyBoneID(const Model::ComplexModel* modelInfo, ::Animation::Defines::Bone bone)
    {
        u32 numBones = static_cast<u32>(modelInfo->bones.size());
        if (numBones == 0)
            return ::Animation::Defines::InvalidBoneID;

        if (bone < ::Animation::Defines::Bone::Default || bone >= ::Animation::Defines::Bone::Count)
            return ::Animation::Defines::InvalidBoneID;

        if (bone == ::Animation::Defines::Bone::Default)
        {
            static std::array<::Animation::Defines::Bone, 5> defaultBones = { ::Animation::Defines::Bone::Default, ::Animation::Defines::Bone::Main, ::Animation::Defines::Bone::Root, ::Animation::Defines::Bone::Waist, ::Animation::Defines::Bone::Head };

            bool foundMapping = false;
            for (::Animation::Defines::Bone defaultBone : defaultBones)
            {
                if (modelInfo->keyBoneIDToBoneIndex.contains((i16)defaultBone))
                {
                    i16 boneIndex = modelInfo->keyBoneIDToBoneIndex.at((i16)defaultBone);
                    if (boneIndex == ::Animation::Defines::InvalidBoneID)
                        continue;

                    bone = defaultBone;
                    foundMapping = true;
                    break;
                }
            }

            if (!foundMapping)
                return ::Animation::Defines::InvalidBoneID;
        }

        if (!modelInfo->keyBoneIDToBoneIndex.contains((i16)bone))
            return ::Animation::Defines::InvalidBoneID;

        i16 boneIndex = modelInfo->keyBoneIDToBoneIndex.at((i16)bone);
        if (boneIndex >= static_cast<i16>(numBones))
            return ::Animation::Defines::InvalidBoneID;

        return boneIndex;
    }

    void SetChildrenAnimationStateIndex(const Model::ComplexModel* modelInfo, ECS::Components::AnimationData& animationData, u32 stateIndex, u16 boneIndex)
    {
        if (!modelInfo->boneIndexToChildren.contains(boneIndex))
            return;

        const auto& childrenList = modelInfo->boneIndexToChildren.at(boneIndex);
        for (u16 childBoneIndex : childrenList)
        {
            ::Animation::Defines::BoneInstance& animationBoneInstance = animationData.boneInstances[childBoneIndex];
            animationBoneInstance.stateIndex = stateIndex;

            SetChildrenAnimationStateIndex(modelInfo, animationData, stateIndex, childBoneIndex);
        }
    }

    bool SetBoneSequenceRaw(entt::registry& registry, const Model::ComplexModel* modelInfo, ECS::Components::AnimationData& animationData, u32 boneIndex, ::Animation::Defines::Type animationType, bool propagateToChildren, ::Animation::Defines::Flags flags, ::Animation::Defines::BlendOverride blendOverride)
    {
        u32 numBoneInstances = static_cast<u32>(animationData.boneInstances.size());
        if (boneIndex >= numBoneInstances)
            return false;

        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        if (animationType == ::Animation::Defines::Type::Invalid)
        {
            ::Animation::Defines::BoneInstance& animationBoneInstance = animationData.boneInstances[boneIndex];
            animationBoneInstance.stateIndex = 0;

            if (propagateToChildren)
            {
                SetChildrenAnimationStateIndex(modelInfo, animationData, 0, boneIndex);
            }

            return true;
        }

        const ::ClientDB::Definitions::AnimationData* animationDataRec = GetAnimationDataRec(registry, animationType);
        if (!animationDataRec)
            return false;

        ::Animation::Defines::Type animType = animationType;
        ::Animation::Defines::SequenceID sequenceID = GetFirstSequenceForAnimation(modelInfo, animationType);
        bool isUsingFallback = false;

        if (sequenceID == ::Animation::Defines::InvalidSequenceID && animationDataRec->fallback != 0)
        {
            auto fallbackAnimationType = static_cast<::Animation::Defines::Type>(animationDataRec->fallback);
            animType = fallbackAnimationType;
            sequenceID = GetFirstSequenceForAnimation(modelInfo, fallbackAnimationType);
            isUsingFallback = true;
        }

        if (sequenceID == ::Animation::Defines::InvalidSequenceID)
            return false;

        ClientDB::Definitions::AnimationData::Flags animationDataFlags = animationDataRec->flags[0];
        bool splitBody = animationDataFlags.IsSplitBodyBehavior;

        i16 defaultBoneIndex = GetBoneIndexFromKeyBoneID(modelInfo, ::Animation::Defines::Bone::Default);
        bool isPlayedOnDefault = boneIndex == defaultBoneIndex || defaultBoneIndex == ::Animation::Defines::InvalidBoneID;
        u32 stateIndex = ::Animation::Defines::InvalidStateID;
        ::Animation::Defines::BoneInstance& animationBoneInstance = animationData.boneInstances[boneIndex];

        {
            if (!isPlayedOnDefault)
            {
                bool foundExistingIndex = false;

                if (animationBoneInstance.stateIndex != ::Animation::Defines::InvalidStateID)
                {
                    ::Animation::Defines::State& animationState = animationData.animationStates[animationBoneInstance.stateIndex];
                    if (animationState.currentAnimation == animType)
                    {
                        stateIndex = animationBoneInstance.stateIndex;
                        foundExistingIndex = true;
                    }
                }

                if (!foundExistingIndex)
                {
                    u32 numAnimationStates = static_cast<u32>(animationData.animationStates.size());
                    for (u32 i = 0; i < numAnimationStates; i++)
                    {
                        ::Animation::Defines::State& animationState = animationData.animationStates[i];
                        if (animationState.currentAnimation == animType)
                        {
                            stateIndex = i;
                            foundExistingIndex = true;
                            break;
                        }
                    }

                    if (!foundExistingIndex)
                    {
                        stateIndex = numAnimationStates;
                        animationData.animationStates.emplace_back();
                    }
                }
            }
            else
            {
                stateIndex = 0;
            }

            animationBoneInstance.stateIndex = stateIndex;
            animationBoneInstance.flags = animationBoneInstance.flags | ::Animation::Defines::BoneFlags::Transformed;
        }

        auto animationFlags = ::Animation::Defines::Flags::None;
        bool playReversed = (isUsingFallback && animationDataFlags.FallbackPlaysReverse || ::Animation::Defines::HasFlag(flags, ::Animation::Defines::Flags::PlayReversed));
        bool holdAtEnd = (isUsingFallback && animationDataFlags.FallbackHoldsEnd) || playReversed || ::Animation::Defines::HasFlag(flags, ::Animation::Defines::Flags::HoldAtEnd);

        if (holdAtEnd)
        {
            animationFlags |= ::Animation::Defines::Flags::HoldAtEnd;
        }

        if (playReversed)
        {
            animationFlags |= ::Animation::Defines::Flags::PlayReversed;
        }

        ::Animation::Defines::State& animationState = animationData.animationStates[animationBoneInstance.stateIndex];
        auto& animationSequence = modelInfo->sequences[sequenceID];

        bool shouldBlend = false;
        bool canBlend = animationState.currentSequenceIndex != ::Animation::Defines::InvalidSequenceID;
        u16 blendTimeStartInMS = animationSequence.blendTimeStart;

        if (canBlend)
        {
            if (blendOverride == ::Animation::Defines::BlendOverride::Auto)
            {
                shouldBlend = animationSequence.flags.blendTransition;

                if (!shouldBlend)
                {
                    const Model::ComplexModel::AnimationSequence& currentSequenceInfo = modelInfo->sequences[animationState.currentSequenceIndex];
                    shouldBlend |= currentSequenceInfo.flags.blendTransition || currentSequenceInfo.flags.blendTransitionIfActive;
                }
            }
            else if (blendOverride == ::Animation::Defines::BlendOverride::Start || blendOverride == ::Animation::Defines::BlendOverride::Both)
            {
                shouldBlend = true;
            }

            if (shouldBlend)
            {
                if (blendTimeStartInMS == 0)
                    blendTimeStartInMS = 150;
            }
        }

        if (shouldBlend)
        {
            animationState.nextAnimation = animationType;
            animationState.nextSequenceIndex = Util::Animation::GetSequenceIndexForAnimation(modelInfo, animType, animationState.timesToRepeat);
            animationState.nextFlags = animationFlags | ::Animation::Defines::Flags::ForceTransition;
            animationState.timeToTransitionMS = blendTimeStartInMS;
            animationState.transitionTime = 0.0f;
        }
        else
        {
            animationState.currentAnimation = animationType;
            animationState.currentSequenceIndex = Util::Animation::GetSequenceIndexForAnimation(modelInfo, animType, animationState.timesToRepeat);
            animationState.nextAnimation = ::Animation::Defines::Type::Invalid;
            animationState.nextSequenceIndex = ::Animation::Defines::InvalidSequenceID;
            animationState.currentFlags = animationFlags;
            animationState.nextFlags = ::Animation::Defines::Flags::None;
            animationState.timeToTransitionMS = 0;
            animationState.transitionTime = 0.0f;

            if (playReversed)
            {
                u32 durationInMS = animationSequence.duration;
                animationState.progress = static_cast<f32>(durationInMS) / 1000.0f;
            }
            else
            {
                animationState.progress = 0.0f;
            }

            animationState.finishedCallback = nullptr;
        }

        if (propagateToChildren)
        {
            SetChildrenAnimationStateIndex(modelInfo, animationData, stateIndex, boneIndex);
        }

        return true;
    }

    bool SetBoneSequence(entt::registry& registry, const Model::ComplexModel* modelInfo, ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, ::Animation::Defines::Type animationType, bool propagateToChildren, ::Animation::Defines::Flags flags, ::Animation::Defines::BlendOverride blendOverride)
    {
        i16 boneIndex = GetBoneIndexFromKeyBoneID(modelInfo, bone);
        if (boneIndex == ::Animation::Defines::InvalidBoneID)
            return false;

        return SetBoneSequenceRaw(registry, modelInfo, animationData, boneIndex, animationType, propagateToChildren, flags);
    }

    bool SetBoneRotation(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone, quat offset)
    {
        i16 boneIndex = GetBoneIndexFromKeyBoneID(modelInfo, bone);
        if (boneIndex == ::Animation::Defines::InvalidBoneID)
            return false;

        if (animationData.proceduralRotationOffsets.size() >= ::Animation::Defines::InvalidProcedualBoneID - 1)
            return false;

        ::Animation::Defines::BoneInstance& boneInstance = animationData.boneInstances[boneIndex];
        if (boneInstance.proceduralRotationOffsetIndex == ::Animation::Defines::InvalidProcedualBoneID)
        {
            animationData.proceduralRotationOffsets.push_back(offset);
            boneInstance.proceduralRotationOffsetIndex = static_cast<u8>(animationData.proceduralRotationOffsets.size()) - 1u;
        }
        else
        {
            animationData.proceduralRotationOffsets[boneInstance.proceduralRotationOffsetIndex] = offset;
        }

        return true;
    }

    const mat4x4* GetBoneMatrixRaw(::ECS::Components::AnimationData& animationData, u16 boneIndex)
    {
        return &animationData.boneTransforms[boneIndex];
    }

    const mat4x4* GetBoneMatrix(const Model::ComplexModel* modelInfo, ::ECS::Components::AnimationData& animationData, ::Animation::Defines::Bone bone)
    {
        u32 numBoneMatrices = static_cast<u32>(animationData.boneTransforms.size());
        u16 boneIndex = GetBoneIndexFromKeyBoneID(modelInfo, bone);

        if (boneIndex == ::Animation::Defines::InvalidBoneID || boneIndex >= numBoneMatrices)
            return nullptr;

        return GetBoneMatrixRaw(animationData, boneIndex);
    }
}
