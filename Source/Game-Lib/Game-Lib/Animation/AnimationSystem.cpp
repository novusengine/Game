#include "AnimationSystem.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Singletons/AnimationSingleton.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/Model/ModelRenderer.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <FileFormat/Shared.h>

AutoCVar_Int CVAR_AnimationSystemEnabled(CVarCategory::Client | CVarCategory::Rendering, "animationEnabled", "Enables the Animation System", 0, CVarFlags::EditCheckbox);
AutoCVar_Float CVAR_AnimationSystemTimeScale(CVarCategory::Client | CVarCategory::Rendering, "animationTimeScale", "Controls the global speed of all animations", 1.0f);
AutoCVar_Int CVAR_AnimationSystemThrottle(CVarCategory::Client | CVarCategory::Rendering, "animationThrottle", "Sets the number of dirty instances that can be updated every frame", 1024);

namespace Animation
{
    AnimationSystem::AnimationSystem() { }

    bool AnimationSystem::IsEnabled()
    {
        return CVAR_AnimationSystemEnabled.Get() != 0;
    }

    void AnimationSystem::Reserve(u32 numSkeletons)
    {
        u32 currentNumSkeletons = static_cast<u32>(_storage.skeletons.size());
        _storage.skeletons.reserve(currentNumSkeletons + numSkeletons);
    }

    void AnimationSystem::Clear(entt::registry& registry)
    {
        _storage.skeletons.clear();

        auto view = registry.view<ECS::Components::Model>();
        view.each([&](ECS::Components::Model& model)
        {
            model.modelID = std::numeric_limits<u32>().max();
            model.instanceID = std::numeric_limits<u32>().max();
        });

        registry.clear<ECS::Components::AnimationData>();
        registry.clear<ECS::Components::AnimationInitData>();
        registry.clear<ECS::Components::AnimationStaticInstance>();

        auto& animationSingleton = registry.ctx().get<ECS::Singletons::AnimationSingleton>();

        for (auto& pair : animationSingleton.staticModelIDToEntity)
        {
            entt::entity entity = pair.second;
            if (registry.valid(entity))
            {
                registry.destroy(entity);
            }
        }

        animationSingleton.staticModelIDToEntity.clear();
    }

    bool AnimationSystem::AddSkeleton(AnimationModelID modelID, Model::ComplexModel& model)
    {
        if (HasSkeleton(modelID))
        {
            return false;
        }

        u32 numGlobalLoops = static_cast<u32>(model.globalLoops.size());
        u32 numSequences = static_cast<u32>(model.sequences.size());
        u32 numBones = static_cast<u32>(model.bones.size());
        u32 numTextureTransforms = static_cast<u32>(model.textureTransforms.size());

        bool hasAnimations = (numSequences > 0 && numBones > 0);
        if (!hasAnimations)
        {
            return false;
        }

        bool anyBoneContainsTrack = false;

        for (const Model::ComplexModel::Bone& bone : model.bones)
        {
            u32 numTranslationTracks = static_cast<u32>(bone.translation.tracks.size());
            for (u32 i = 0; i < numTranslationTracks; i++)
            {
                auto& track = bone.translation.tracks[i];
                anyBoneContainsTrack |= !track.timestamps.empty() && !track.values.empty();
            }

            u32 numRotationTracks = static_cast<u32>(bone.rotation.tracks.size());
            for (u32 i = 0; i < numRotationTracks; i++)
            {
                auto& track = bone.rotation.tracks[i];
                anyBoneContainsTrack |= !track.timestamps.empty() && !track.values.empty();
            }

            u32 numScaleTracks = static_cast<u32>(bone.scale.tracks.size());
            for (u32 i = 0; i < numScaleTracks; i++)
            {
                auto& track = bone.scale.tracks[i];
                anyBoneContainsTrack |= !track.timestamps.empty() && !track.values.empty();
            }
        }

        if (!anyBoneContainsTrack && numTextureTransforms == 0)
        {
            return false;
        }

        AnimationSkeleton& skeleton = _storage.skeletons[modelID];
        skeleton.modelID = modelID;

        if (numGlobalLoops)
        {
            skeleton.globalLoops.resize(numGlobalLoops);
            memcpy(skeleton.globalLoops.data(), model.globalLoops.data(), numGlobalLoops * sizeof(u32));
        }

        if (numSequences)
        {
            skeleton.sequences.resize(numSequences);
            skeleton.animationIDToFirstSequenceID.reserve(numSequences);

            memcpy(skeleton.sequences.data(), model.sequences.data(), numSequences * sizeof(Model::ComplexModel::AnimationSequence));

            for (u32 i = 0; i < numSequences; i++)
            {
                const Model::ComplexModel::AnimationSequence& sequence = skeleton.sequences[i];

                // Ignore sequences that are aliases or always playing
                if (sequence.flags.isAlias)
                    continue;

                if (sequence.subID != 0)
                    continue;

                AnimationType animationID = static_cast<AnimationType>(sequence.id);
                if (animationID <= AnimationType::Invalid || animationID >= AnimationType::Count)
                {
                    NC_LOG_ERROR("Model {0} has sequences ({1}) with animationID ({2}). (This animationID is invalid, Min/Max Value : ({3}, {4}))", modelID, i, sequence.id, (i32)AnimationType::Invalid + 1, (i32)AnimationType::Count);
                    continue;
                }

                if (skeleton.animationIDToFirstSequenceID.contains(animationID))
                {
                    u32 existingSequenceID = skeleton.animationIDToFirstSequenceID[animationID];
                    NC_LOG_ERROR("Model {0} has two sequences ({1}, {2}) both referencing animationID ({3}) and SubID 0. (SubID is unique for each animationID)", modelID, existingSequenceID, i, (i32)animationID);
                }

                skeleton.animationIDToFirstSequenceID[animationID] = i;
            }
        }

        if (numBones)
        {
            skeleton.bones.resize(numBones);

            for (u32 i = 0; i < numBones; i++)
            {
                const Model::ComplexModel::Bone& bone = model.bones[i];
                AnimationSkeletonBone& animBone = skeleton.bones[i];

                animBone.info = bone;
            }

            skeleton.keyBoneIDToBoneIndex = model.keyBoneIDToBoneIndex;
        }

        if (numTextureTransforms)
        {
            skeleton.textureTransforms.resize(numTextureTransforms);

            for (u32 i = 0; i < numTextureTransforms; i++)
            {
                const Model::ComplexModel::TextureTransform& textureTransform = model.textureTransforms[i];
                AnimationSkeletonTextureTransform& animTextureTransform = skeleton.textureTransforms[i];

                animTextureTransform.info = textureTransform;
            }
        }

        return true;
    }

    u16 AnimationSystem::GetSequenceIDForAnimationID(AnimationModelID modelID, AnimationType animationID)
    {
        const AnimationSkeleton& skeleton = _storage.skeletons[modelID];

        if (!skeleton.animationIDToFirstSequenceID.contains(animationID))
        {
            return InvalidSequenceID;
        }

        u16 sequenceID = skeleton.animationIDToFirstSequenceID.at(animationID);
        if (sequenceID >= skeleton.sequences.size())
        {
            return InvalidSequenceID;
        }

        return sequenceID;
    }

    i16 AnimationSystem::GetBoneIndexFromKeyBoneID(AnimationModelID modelID, AnimationBone bone)
    {
        const AnimationSkeleton& skeleton = _storage.skeletons[modelID];
        u32 numBones = static_cast<u32>(skeleton.bones.size());
        if (numBones == 0)
            return -1;

        if (bone < AnimationBone::Default || bone >= AnimationBone::Count)
            return -1;

        if (bone == AnimationBone::Default)
        {
            static std::array<AnimationBone, 5> defaultBones = { AnimationBone::Default, AnimationBone::Main, AnimationBone::Root, AnimationBone::Waist, AnimationBone::Head };

            bool foundMapping = false;
            for (AnimationBone defaultBone : defaultBones)
            {
                if (skeleton.keyBoneIDToBoneIndex.contains((i16)defaultBone))
                {
                    bone = defaultBone;
                    foundMapping = true;
                    break;
                }
            }

            if (!foundMapping)
                return 0;
        }

        i16 keyBone = (i16)bone;
        if (!skeleton.keyBoneIDToBoneIndex.contains(keyBone))
            return -1;

        i16 boneID = skeleton.keyBoneIDToBoneIndex.at(keyBone);
        if (boneID >= static_cast<i16>(numBones))
            return -1;

        return boneID;
    }
}