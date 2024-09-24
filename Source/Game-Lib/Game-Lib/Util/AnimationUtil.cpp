#include "AnimationUtil.h"
#include "Game-Lib/Animation/AnimationSystem.h"
#include "Game-Lib/ECS/Singletons/ClientDBCollection.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <FileFormat/Novus/ClientDB/Definitions.h>

#include <entt/entt.hpp>

namespace Util::Animation
{
    const ::ClientDB::Definitions::AnimationData* GetAnimationDataRec(entt::registry& registry, ::Animation::AnimationType type)
    {
        auto& clientDBCollection = registry.ctx().get<ECS::Singletons::ClientDBCollection>();
        auto* animationDatas = clientDBCollection.Get<ClientDB::Definitions::AnimationData>(ECS::Singletons::ClientDBHash::AnimationData);

        if (!animationDatas)
            return nullptr;

        u32 typeIndex = static_cast<u32>(type);
        return animationDatas->GetRow(typeIndex);
    }

    bool SetBoneSequence(entt::registry& registry, const ECS::Components::Model& model, ECS::Components::AnimationData& animationData, ::Animation::AnimationBone bone, ::Animation::AnimationType animationType)
    {
        /*
            if (animationType == AnimationType::Invalid)
            // Unset the current animation for the bone and fallback to parent chain if applicable
        */

        const ::ClientDB::Definitions::AnimationData* animationDataRec = GetAnimationDataRec(registry, animationType);
        if (!animationDataRec)
            return false;

        ::Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
        i16 boneIndex = animationSystem->GetBoneIndexFromKeyBoneID(model.modelID, bone);
        if (boneIndex == ::Animation::InvalidBoneID)
            return false;

        ::Animation::AnimationSkeleton& skeleton = animationSystem->GetStorage().skeletons[model.modelID];
        ::Animation::AnimationType animType = animationType;
        ::Animation::AnimationSequenceID sequenceID = animationSystem->GetSequenceIDForAnimationID(model.modelID, animationType);
        ClientDB::Definitions::AnimationData::Flags animationDataFlags = animationDataRec->flags[0];
        bool isUsingFallback = false;

        if (sequenceID == ::Animation::InvalidSequenceID && animationDataRec->fallback != 0)
        {
            auto fallbackAnimationType = static_cast<::Animation::AnimationType>(animationDataRec->fallback);
            animType = fallbackAnimationType;
            sequenceID = animationSystem->GetSequenceIDForAnimationID(model.modelID, fallbackAnimationType);
            isUsingFallback = true;
        }

        if (sequenceID == ::Animation::InvalidSequenceID)
            return false;

        bool updateBoneInstances = false;
        u32 animationStateIndex = ::Animation::InvalidStateID;
        ::Animation::AnimationBoneInstance& animationBoneInstance = animationData.boneInstances[boneIndex];

        {
            bool foundExistingIndex = false;

            if (animationBoneInstance.animationStateIndex != ::Animation::InvalidStateID)
            {
                ::Animation::AnimationState& animationState = animationData.animationStates[animationBoneInstance.animationStateIndex];
                if (animationState.boneIndex == boneIndex)
                {
                    animationStateIndex = animationBoneInstance.animationStateIndex;
                    foundExistingIndex = true;
                }
            }

            if (!foundExistingIndex)
            {
                u32 numAnimationStates = static_cast<u32>(animationData.animationStates.size());
                for (u32 i = 0; i < numAnimationStates; i++)
                {
                    ::Animation::AnimationState& animationState = animationData.animationStates[i];
                    if (animationState.boneIndex == boneIndex)
                    {
                        animationStateIndex = i;
                        foundExistingIndex = true;
                        updateBoneInstances = true;
                        break;
                    }
                }

                if (!foundExistingIndex)
                {
                    animationStateIndex = numAnimationStates;

                    ::Animation::AnimationState& animationState = animationData.animationStates.emplace_back();
                    animationState.boneIndex = boneIndex;

                    updateBoneInstances = true;
                }
            }

            animationBoneInstance.animationStateIndex = animationStateIndex;
            animationBoneInstance.flags = animationBoneInstance.flags | ::Animation::AnimationBoneFlags::Transformed;
        }

        auto animationFlags = ::Animation::AnimationFlags::None;
        bool playReversed = (isUsingFallback && animationDataFlags.FallbackPlaysReverse);
        bool holdAtEnd = (isUsingFallback && animationDataFlags.FallbackHoldsEnd) || playReversed;

        if (holdAtEnd)
        {
            animationFlags |= ::Animation::AnimationFlags::HoldAtEnd;
        }

        if (playReversed)
        {
            animationFlags |= ::Animation::AnimationFlags::PlayReversed;
        }

        ::Animation::AnimationState& animationState = animationData.animationStates[animationBoneInstance.animationStateIndex];
        auto& animationSequence = skeleton.sequences[sequenceID];

        if (::Animation::HasAnimationFlag(animationState.currentFlags, ::Animation::AnimationFlags::Finished))
        {
            animationState.currentAnimation = animationType;
            animationState.currentSequenceIndex = Util::Animation::GetSequenceIndexForAnimation(skeleton, animType, animationState.timesToRepeat);
            animationState.nextSequenceIndex = ::Animation::InvalidSequenceID;
            animationState.currentFlags = animationFlags;
            animationState.nextFlags = ::Animation::AnimationFlags::None;
            animationState.timeToTransitionMS = 0;

            if (playReversed)
            {
                u32 durationInMS = animationSequence.duration;
                animationState.progress = static_cast<f32>(durationInMS) / 1000.0f;
            }
            else
            {
                animationState.progress = 0.0f;
            }

            animationState.transitionTime = 0.0f;
            animationState.finishedCallback = nullptr;
        }
        else
        {
            if (animationState.currentAnimation == animationType || animationState.nextAnimation == animationType)
                return false;

            animationState.nextAnimation = animationType;
            animationState.nextSequenceIndex = Util::Animation::GetSequenceIndexForAnimation(skeleton, animType, animationState.timesToRepeat);
            animationState.nextFlags = animationFlags | ::Animation::AnimationFlags::ForceTransition;
        }

        if (updateBoneInstances)
        {
            u32 numBoneInstances = static_cast<u32>(animationData.boneInstances.size());
            for (u32 i = 1; i < numBoneInstances; i++)
            {
                const ::Animation::AnimationSkeletonBone& bone = skeleton.bones[i];
                ::Animation::AnimationBoneInstance& boneInstance = animationData.boneInstances[i];
        
                if (bone.info.parentBoneID == -1)
                {
                    ::Animation::AnimationBoneInstance& parentBoneInstance = animationData.boneInstances[0];
                    boneInstance.animationStateIndex = parentBoneInstance.animationStateIndex;
        
                    continue;
                }
        
                ::Animation::AnimationBoneInstance& parentBoneInstance = animationData.boneInstances[bone.info.parentBoneID];
        
                bool hasTransformedFlag = ::Animation::HasAnimationBoneFlag(boneInstance.flags, ::Animation::AnimationBoneFlags::Transformed);
                bool missingAnimationState = boneInstance.animationStateIndex == ::Animation::InvalidStateID;

                if (!hasTransformedFlag || (hasTransformedFlag && missingAnimationState))
                {
                    boneInstance.animationStateIndex = parentBoneInstance.animationStateIndex;
                }
            }
        }

        return true;
    }

    ::Animation::AnimationSequenceID GetSequenceIndexForAnimation(const::Animation::AnimationSkeleton& skeleton, ::Animation::AnimationType animationType, i8& timesToRepeat)
    {
        ::Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();

        i32 probability = static_cast<i32>((static_cast<f32>(rand()) / static_cast<f32>(RAND_MAX)) * static_cast<f32>(0x7FFF));
        i32 currentProbability = 0;

        u32 nextSequenceID = ::Animation::InvalidSequenceID;
        i32 variationID = animationSystem->GetSequenceIDForAnimationID(skeleton.modelID, animationType);

        do
        {
            const Model::ComplexModel::AnimationSequence& currentVariation = skeleton.sequences[variationID];

            currentProbability += currentVariation.frequency;
            nextSequenceID = variationID;
            variationID = currentVariation.nextVariationID;

        } while (currentProbability < probability && variationID != -1);

        NC_ASSERT(nextSequenceID != ::Animation::InvalidSequenceID, "nextSequenceID is invalid.");

        const Model::ComplexModel::AnimationSequence& nextVariationSequence = skeleton.sequences[nextSequenceID];
        u32 minRepetitions = nextVariationSequence.repetitionRange.x;
        u32 maxRepetitions = nextVariationSequence.repetitionRange.y;

        timesToRepeat = static_cast<i8>(minRepetitions + ((maxRepetitions - minRepetitions) * (static_cast<f32>(rand()) / static_cast<f32>(RAND_MAX)))) - 1;

        return nextSequenceID;
    }
}
