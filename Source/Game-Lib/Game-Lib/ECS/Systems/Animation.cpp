#include "Animation.h"

#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/AttachmentData.h"
#include "Game-Lib/ECS/Components/Camera.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Singletons/ActiveCamera.h"
#include "Game-Lib/ECS/Singletons/AnimationSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Gameplay/Animation/Defines.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelLoader.h"
#include "Game-Lib/Rendering/Model/ModelRenderer.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/AttachmentUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <entt/entt.hpp>

#include <glm/gtx/matrix_decompose.hpp>

AutoCVar_Int CVAR_AnimationSimulationEnabled(CVarCategory::Client | CVarCategory::Rendering, "animationSimulationEnabled", "Enables the Animation Simulation", 1, CVarFlags::EditCheckbox);
#define ANIMATION_SIMULATION_MT 1

namespace ECS::Systems
{
    /*static __forceinline void matmul4x4(const mat4x4& __restrict m1, const mat4x4& __restrict m2, mat4x4& __restrict out)
    {
        __m128  vx = _mm_load_ps(&m1[0][0]);
        __m128  vy = _mm_load_ps(&m1[1][0]);
        __m128  vz = _mm_load_ps(&m1[2][0]);
        __m128  vw = _mm_load_ps(&m1[3][0]);

        for (i32 i = 0; i < 4; i++)
        {
            __m128 col = _mm_load_ps(&m2[i][0]);

            __m128 x = _mm_permute_ps(col, _MM_SHUFFLE(0, 0, 0, 0));
            __m128 y = _mm_permute_ps(col, _MM_SHUFFLE(1, 1, 1, 1));
            __m128 z = _mm_permute_ps(col, _MM_SHUFFLE(2, 2, 2, 2));
            __m128 w = _mm_permute_ps(col, _MM_SHUFFLE(3, 3, 3, 3));

            __m128 resA = _mm_fmadd_ps(x, vx, _mm_mul_ps(y, vy));
            __m128 resB = _mm_fmadd_ps(z, vz, _mm_mul_ps(w, vw));

            _mm_store_ps(&out[i][0], _mm_add_ps(resA, resB));
        }
    }

    static __forceinline mat4x4 mul(const mat4x4& __restrict matrix1, const mat4x4& __restrict matrix2)
    {
        mat4x4 result;
        matmul4x4(matrix2, matrix1, result);

        return result;
    }*/

    static glm::mat4 mul(const glm::mat4& matrix1, const glm::mat4& matrix2)
    {
        return matrix2 * matrix1;
    }

    static mat4x4 MatrixTranslate(const vec3& v)
    {
        mat4x4 result =
        {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            v[0], v[1], v[2], 1
        };

        return result;
    }

    static mat4x4 MatrixRotation(const quat& quat)
    {
        f32 x2 = quat.x * quat.x;
        f32 y2 = quat.y * quat.y;
        f32 z2 = quat.z * quat.z;
        f32 sx = quat.w * quat.x;
        f32 sy = quat.w * quat.y;
        f32 sz = quat.w * quat.z;
        f32 xz = quat.x * quat.z;
        f32 yz = quat.y * quat.z;
        f32 xy = quat.x * quat.y;

        return mat4x4(1.0f - 2.0f * (y2 + z2), 2.0f * (xy + sz), 2.0f * (xz - sy), 0.0f,
            2.0f * (xy - sz), 1.0f - 2.0f * (x2 + z2), 2.0f * (sx + yz), 0.0f,
            2.0f * (sy + xz), 2.0f * (yz - sx), 1.0f - 2.0f * (x2 + y2), 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    static mat4x4 MatrixScale(const vec3& v)
    {
        mat4x4 result = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        result[0] = result[0] * v[0];
        result[1] = result[1] * v[1];
        result[2] = result[2] * v[2];

        return result;
    }

    template <typename T>
    static T InterpolateKeyframe(const Model::ComplexModel::AnimationTrack<T>& track, f32 progress)
    {
        u32 numTimeStamps = static_cast<u32>(track.timestamps.size());
        if (numTimeStamps == 1)
        {
            return track.values[0];
        }

        f32 lastTimestamp = static_cast<f32>(track.timestamps[numTimeStamps - 1] / 1000.0f);
        if (progress >= lastTimestamp)
            return track.values[numTimeStamps - 1];

        f32 timestamp1 = static_cast<f32>(track.timestamps[0] / 1000.0f);
        f32 timestamp2 = static_cast<f32>(track.timestamps[1] / 1000.0f);

        T value1 = track.values[0];
        T value2 = track.values[1];

        for (u32 i = 1; i < numTimeStamps; i++)
        {
            f32 timestamp = static_cast<f32>(track.timestamps[i] / 1000.0f);

            if (progress <= timestamp)
            {
                timestamp1 = static_cast<f32>(track.timestamps[i - 1] / 1000.0f);
                timestamp2 = static_cast<f32>(track.timestamps[i] / 1000.0f);

                value1 = track.values[i - 1];
                value2 = track.values[i];
                break;
            }
        }

        f32 currentProgress = progress - timestamp1;
        f32 currentFrameDuration = timestamp2 - timestamp1;

        if (currentFrameDuration == 0.0f)
            return value1;

        f32 t = currentProgress / currentFrameDuration;

        if constexpr (std::is_same_v<T, quat>)
        {
            return glm::slerp(value1, value2, t);
        }
        else
        {
            return glm::mix(value1, value2, t);
        }
    }

    static mat4x4 GetBoneMatrix(const std::vector<::Animation::Defines::GlobalLoop>& globalLoops, const ::Animation::Defines::State& animationState, const Model::ComplexModel::Bone& bone, const quat& rotationOffset)
    {
        vec3 translationValue = vec3(0.0f, 0.0f, 0.0f);
        quat rotationValue = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scaleValue = vec3(1.0f, 1.0f, 1.0f);

        bool isTranslationGlobalLoop = bone.translation.globalLoopIndex != -1;
        bool isRotationGlobalLoop = bone.rotation.globalLoopIndex != -1;
        bool isScaleGlobalLoop = bone.scale.globalLoopIndex != -1;

        // Primary Sequence
        {
            // Handle Translation
            {
                u32 sequenceID = animationState.currentSequenceIndex;
                if (isTranslationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < bone.translation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = bone.translation.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (isTranslationGlobalLoop)
                    {
                        const ::Animation::Defines::GlobalLoop& globalLoop = globalLoops[bone.translation.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        translationValue = InterpolateKeyframe(track, progress);
                }
            }

            // Handle Rotation
            {
                u32 sequenceID = animationState.currentSequenceIndex;
                if (isRotationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < bone.rotation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<quat>& track = bone.rotation.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (bone.rotation.globalLoopIndex != -1)
                    {
                        const ::Animation::Defines::GlobalLoop& globalLoop = globalLoops[bone.rotation.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        rotationValue = InterpolateKeyframe(track, progress);
                }
            }
        
            // Handle Scale
            {
                u32 sequenceID = animationState.currentSequenceIndex;
                if (isScaleGlobalLoop)
                {
                    sequenceID = 0;
                }
        
                if (sequenceID < bone.scale.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = bone.scale.tracks[sequenceID];
                    f32 progress = animationState.progress;
        
                    if (bone.scale.globalLoopIndex != -1)
                    {
                        const ::Animation::Defines::GlobalLoop& globalLoop = globalLoops[bone.scale.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }
        
                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        scaleValue = InterpolateKeyframe(track, progress);
                }
            }
        }
        
        // Transition Sequence
        bool isInTransition = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Transitioning);
        bool hasTransitionTime = animationState.timeToTransitionMS > 0;
        u32 nextSequenceID = animationState.nextSequenceIndex;
        if (isInTransition && hasTransitionTime && nextSequenceID != ::Animation::Defines::InvalidSequenceID)
        {
            if (!isTranslationGlobalLoop)
            {
                f32 transitionTime = static_cast<f32>(animationState.timeToTransitionMS) / 1000.0f;
                f32 transitionProgressTime = animationState.transitionTime;
                f32 transitionProgress = transitionTime ? transitionProgressTime / transitionTime : 1.0f;
        
                if (nextSequenceID < bone.translation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = bone.translation.tracks[nextSequenceID];
    
                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                    {
                        vec3 translation = InterpolateKeyframe(track, 0.0f);
                        translationValue = glm::mix(translationValue, translation, transitionProgress);
                    }
                }
            }
        
            if (!isRotationGlobalLoop)
            {
                f32 transitionTime = static_cast<f32>(animationState.timeToTransitionMS) / 1000.0f;
                f32 transitionProgressTime = animationState.transitionTime;
                f32 transitionProgress = transitionTime ? transitionProgressTime / transitionTime : 1.0f;

                if (nextSequenceID < bone.rotation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<quat>& track = bone.rotation.tracks[nextSequenceID];

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                    {
                        quat rotation = InterpolateKeyframe(track, 0.0f);
                        rotationValue = glm::slerp(rotationValue, rotation, transitionProgress);
                    }
                }
            }

            if (!isScaleGlobalLoop)
            {
                f32 transitionTime = static_cast<f32>(animationState.timeToTransitionMS) / 1000.0f;
                f32 transitionProgressTime = animationState.transitionTime;
                f32 transitionProgress = transitionTime ? transitionProgressTime / transitionTime : 1.0f;

                if (nextSequenceID < bone.scale.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = bone.scale.tracks[nextSequenceID];

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                    {
                        vec3 scale = InterpolateKeyframe(track, 0.0f);
                        scaleValue = glm::mix(translationValue, scaleValue, transitionProgress);
                    }
                }
            }
        }

        rotationValue = glm::normalize(rotationOffset * glm::normalize(rotationValue));

        const vec3& pivot = bone.pivot;
        mat4x4 boneMatrix = glm::translate(mat4x4(1.0f), pivot);

        mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), translationValue);
        mat4x4 rotationMatrix = glm::toMat4(rotationValue);
        mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scaleValue);

        boneMatrix = mul(translationMatrix, boneMatrix);
        boneMatrix = mul(rotationMatrix, boneMatrix);
        boneMatrix = mul(scaleMatrix, boneMatrix);

        boneMatrix = mul(glm::translate(mat4x4(1.0f), -pivot), boneMatrix);

        return boneMatrix;
    }
   
    static mat4x4 GetAttachmentMatrix(const Model::ComplexModel::Attachment& attachment)
    {
        mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), attachment.position);
        mat4x4 rotationMatrix = glm::toMat4(quat(1.0f, 0.0f, 0.0f, 0.0f));
        mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), vec3(1.0f));

        mat4x4 attachmentMatrix = mat4x4(1.0f);
        attachmentMatrix = mul(translationMatrix, attachmentMatrix);
        attachmentMatrix = mul(rotationMatrix, attachmentMatrix);
        attachmentMatrix = mul(scaleMatrix, attachmentMatrix);

        return attachmentMatrix;
    }

    static mat4x4 GetTextureTransformMatrix(const std::vector<::Animation::Defines::GlobalLoop>& globalLoops, const ::Animation::Defines::State& animationState, const Model::ComplexModel::TextureTransform& textureTransform)
    {
        vec3 translationValue = vec3(0.0f, 0.0f, 0.0f);
        quat rotationValue = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scaleValue = vec3(1.0f, 1.0f, 1.0f);

        bool isTranslationGlobalLoop = textureTransform.translation.globalLoopIndex != -1;
        bool isRotationGlobalLoop = textureTransform.rotation.globalLoopIndex != -1;
        bool isScaleGlobalLoop = textureTransform.scale.globalLoopIndex != -1;

        // Primary Sequence
        {
            // Handle Translation
            {
                u32 sequenceID = animationState.currentSequenceIndex;
                if (isTranslationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < textureTransform.translation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = textureTransform.translation.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (isTranslationGlobalLoop)
                    {
                        const ::Animation::Defines::GlobalLoop& globalLoop = globalLoops[textureTransform.translation.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        translationValue = InterpolateKeyframe(track, progress);
                }
            }

            // Handle Rotation
            {
                u32 sequenceID = animationState.currentSequenceIndex;
                if (isRotationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < textureTransform.rotation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<quat>& track = textureTransform.rotation.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (textureTransform.rotation.globalLoopIndex != -1)
                    {
                        const ::Animation::Defines::GlobalLoop& globalLoop = globalLoops[textureTransform.rotation.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        rotationValue = InterpolateKeyframe(track, progress);
                }
            }

            // Handle Scale
            {
                u32 sequenceID = animationState.currentSequenceIndex;
                if (isScaleGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < textureTransform.scale.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = textureTransform.scale.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (textureTransform.scale.globalLoopIndex != -1)
                    {
                        const ::Animation::Defines::GlobalLoop& globalLoop = globalLoops[textureTransform.scale.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        scaleValue = InterpolateKeyframe(track, progress);
                }
            }
        }

        //
        //// Transition Sequence
        //{
        //    if (translationBoneInstance && !isTranslationGlobalLoop)
        //    {
        //        f32 timeToTransition = translationBoneInstance->timeToTransition;
        //        f32 transitionDuration = translationBoneInstance->transitionDuration;
        //        f32 transitionProgress = timeToTransition ? glm::clamp(1.0f - (timeToTransition / transitionDuration), 0.0f, 1.0f) : 1.0f;
        //
        //        if (transitionDuration > 0.0f)
        //        {
        //            u32 sequenceID = translationBoneInstance->nextAnimation.sequenceID;
        //            if (sequenceID < bone.translation.tracks.size())
        //            {
        //                const Model::ComplexModel::AnimationTrack<vec3>& track = bone.translation.tracks[sequenceID];
        //
        //                if (track.values.size() > 0 && track.timestamps.size() > 0)
        //                {
        //                    f32 progress = translationBoneInstance->nextAnimation.progress;
        //
        //                    vec3 translation = InterpolateKeyframe(track, progress);
        //                    translationValue = glm::mix(translationValue, translation, transitionProgress);
        //                }
        //            }
        //        }
        //    }
        //
        //    if (rotationBoneInstance && !isRotationGlobalLoop)
        //    {
        //        f32 timeToTransition = rotationBoneInstance->timeToTransition;
        //        f32 transitionDuration = rotationBoneInstance->transitionDuration;
        //        f32 transitionProgress = timeToTransition ? glm::clamp(1.0f - (timeToTransition / transitionDuration), 0.0f, 1.0f) : 1.0f;
        //
        //        if (transitionDuration > 0.0f)
        //        {
        //            u32 sequenceID = rotationBoneInstance->nextAnimation.sequenceID;
        //            if (sequenceID < bone.rotation.tracks.size())
        //            {
        //                const Model::ComplexModel::AnimationTrack<quat>& track = bone.rotation.tracks[sequenceID];
        //
        //                if (track.values.size() > 0 && track.timestamps.size() > 0)
        //                {
        //                    f32 progress = rotationBoneInstance->nextAnimation.progress;
        //
        //                    quat rotation = InterpolateKeyframe(track, progress);
        //                    rotationValue = glm::slerp(rotationValue, rotation, transitionProgress);
        //                }
        //            }
        //        }
        //    }
        //
        //    if (scaleBoneInstance && !isScaleGlobalLoop)
        //    {
        //        f32 timeToTransition = scaleBoneInstance->timeToTransition;
        //        f32 transitionDuration = scaleBoneInstance->transitionDuration;
        //        f32 transitionProgress = timeToTransition ? glm::clamp(1.0f - (timeToTransition / transitionDuration), 0.0f, 1.0f) : 1.0f;
        //
        //        if (transitionDuration > 0.0f)
        //        {
        //            u32 sequenceID = scaleBoneInstance->nextAnimation.sequenceID;
        //            if (sequenceID < bone.scale.tracks.size())
        //            {
        //                const Model::ComplexModel::AnimationTrack<vec3>& track = bone.scale.tracks[sequenceID];
        //
        //                if (track.values.size() > 0 && track.timestamps.size() > 0)
        //                {
        //                    f32 progress = scaleBoneInstance->nextAnimation.progress;
        //
        //                    vec3 scale = InterpolateKeyframe(track, progress);
        //                    scaleValue = glm::mix(scaleValue, scale, transitionProgress);
        //                }
        //            }
        //        }
        //    }
        //}

        //rotationValue = glm::normalize(boneInstance.rotationOffset * glm::normalize(rotationValue));

        static const vec3 pivot = vec3(0.5f, 0.5f, 0.5f);

        mat4x4 matrix = mul(glm::translate(mat4x4(1.0f), pivot), mat4x4(1.0f));

        mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), translationValue);
        mat4x4 rotationMatrix = glm::toMat4(glm::normalize(rotationValue));
        mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scaleValue);

        matrix = mul(translationMatrix, matrix);
        matrix = mul(rotationMatrix, matrix);
        matrix = mul(scaleMatrix, matrix);

        matrix = mul(glm::translate(mat4x4(1.0f), -pivot), matrix);

        return matrix;
    }

    void SetupDynamicAnimationInstance(entt::registry& registry, entt::entity entity, const Components::Model& model, const Model::ComplexModel* modelInfo)
    {
        auto& animationData = registry.get_or_emplace<Components::AnimationData>(entity);
        u32 numGlobalLoops = static_cast<u32>(modelInfo->globalLoops.size());
        u32 numBones = static_cast<u32>(modelInfo->bones.size());
        u32 numAttachments = static_cast<u32>(modelInfo->attachments.size());
        u32 numTextureTransforms = static_cast<u32>(modelInfo->textureTransforms.size());

        animationData.modelHash = model.modelHash;

        animationData.globalLoops.clear();
        animationData.globalLoops.resize(numGlobalLoops);
        for (u32 i = 0; i < numGlobalLoops; i++)
        {
            ::Animation::Defines::GlobalLoop& globalLoop = animationData.globalLoops[i];

            globalLoop.currentTime = 0.0f;
            globalLoop.duration = static_cast<f32>(modelInfo->globalLoops[i]) / 1000.0f;
        }

        animationData.boneInstances.clear();
        animationData.boneInstances.resize(numBones);

        animationData.animationStates.reserve(8);
        animationData.animationStates.clear();

        animationData.boneTransforms.resize(numBones);
        for (u32 i = 0; i < numBones; i++)
        {
            animationData.boneTransforms[i] = mat4x4(1.0f);
        }

        animationData.textureTransforms.resize(numTextureTransforms);
        for (u32 i = 0; i < numTextureTransforms; i++)
        {
            animationData.textureTransforms[i] = mat4x4(1.0f);
        }

        animationData.proceduralRotationOffsets.reserve(8);
        animationData.proceduralRotationOffsets.clear();

        bool hasAnyTransformedBones = false;

        for (u32 i = 0; i < numBones; i++)
        {
            ::Animation::Defines::BoneInstance& boneInstance = animationData.boneInstances[i];
            const Model::ComplexModel::Bone& skeletonBone = modelInfo->bones[i];

            // Copy First 7 flags from Skeleton Bone Flags Struct
            u8 skeletonFlags = *reinterpret_cast<const u8*>(&skeletonBone.flags) & 0x7F;

            boneInstance.stateIndex = 0;
            boneInstance.flags |= static_cast<::Animation::Defines::BoneFlags>(skeletonFlags);

            if (skeletonBone.flags.Transformed)
            {
                boneInstance.flags |= ::Animation::Defines::BoneFlags::Transformed;
                hasAnyTransformedBones = true;
            }
        }

        // Insert a default animation state
        animationData.animationStates.emplace_back();

        if (hasAnyTransformedBones)
        {
            i16 defaultBoneIndex = Util::Animation::GetBoneIndexFromKeyBoneID(modelInfo, ::Animation::Defines::Bone::Default);

            if (defaultBoneIndex == ::Animation::Defines::InvalidBoneID)
                defaultBoneIndex = 0;

            if (!Util::Animation::SetBoneSequenceRaw(registry, modelInfo, animationData, defaultBoneIndex, ::Animation::Defines::Type::Stand, false))
            {
                Util::Animation::SetBoneSequenceRaw(registry, modelInfo, animationData, defaultBoneIndex, ::Animation::Defines::Type::Closed, false);
            }
        }
    }

    void SetupStaticAnimationInstance(entt::registry& registry, entt::entity entity, Singletons::AnimationSingleton& animationSingleton, const Components::Model& model, const Model::ComplexModel* modelInfo, ModelRenderer* modelRenderer)
    {
        entt::entity dynamicEntity = entt::null;
        if (!animationSingleton.staticModelIDToEntity.contains(model.modelID))
        {
            dynamicEntity = registry.create();
            auto& animationStaticInstance = registry.get_or_emplace<Components::AnimationStaticInstance>(dynamicEntity);
            auto& dynamicModel = registry.emplace<Components::Model>(dynamicEntity);
            dynamicModel.modelID = model.modelID;
            dynamicModel.instanceID = std::numeric_limits<u32>().max();
            dynamicModel.modelHash = model.modelHash;

            if (modelRenderer)
            {
                bool result = modelRenderer->AddUninstancedAnimationData(dynamicModel.modelID, animationStaticInstance.boneMatrixOffset, animationStaticInstance.textureMatrixOffset);
                NC_ASSERT(result, "Failed to add uninstanced animation data.");
            }

            SetupDynamicAnimationInstance(registry, dynamicEntity, model, modelInfo);
            animationSingleton.staticModelIDToEntity[model.modelID] = dynamicEntity;
        }
        else
        {
            dynamicEntity = animationSingleton.staticModelIDToEntity[model.modelID];
        }

        if (modelRenderer)
        {
            auto& animationStaticInstance = registry.get<Components::AnimationStaticInstance>(dynamicEntity);
            bool result = modelRenderer->SetInstanceAnimationData(model.instanceID, animationStaticInstance.boneMatrixOffset, animationStaticInstance.textureMatrixOffset);
            NC_ASSERT(result, "Failed to set instance animation data.");
        }
    }

    void Animation::Init(entt::registry& registry)
    {
        registry.ctx().emplace<Singletons::AnimationSingleton>();
        std::srand(static_cast<u32>(std::time(NULL)));
    }

    void Animation::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("ECS::Animation");

        {
            ZoneScopedN("ECS::Animation::HandleAnimationDataInit");
            HandleAnimationDataInit(registry, deltaTime);
        }

        if (!CVAR_AnimationSimulationEnabled.Get())
            return;

        {
            ZoneScopedN("ECS::Animation::HandleSimulation");
            HandleSimulation(registry, deltaTime);
        }
    }

    void Animation::HandleAnimationDataInit(entt::registry& registry, f32 deltaTime)
    {
        ModelRenderer* modelRenderer = ServiceLocator::GetGameRenderer()->GetModelRenderer();
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        auto& animationSingleton = registry.ctx().get<Singletons::AnimationSingleton>();
        auto initView = registry.view<Components::Model, Components::AnimationInitData>();
        initView.each([&](entt::entity entity, Components::Model& model, Components::AnimationInitData& animationInitData)
        {
            const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
            if (!modelInfo)
                return;

            if (modelInfo->modelHeader.numSequences == 0 && modelInfo->modelHeader.numBones == 0 && modelInfo->modelHeader.numTextureTransforms == 0 && modelInfo->modelHeader.numAttachments == 0)
                return;

            if (animationInitData.flags.isDynamic)
            {
                SetupDynamicAnimationInstance(registry, entity, model, modelInfo);
            }
            else
            {
                SetupStaticAnimationInstance(registry, entity, animationSingleton, model, modelInfo, modelRenderer);
            }
        });

        registry.clear<Components::AnimationInitData>();
    }

    void Animation::HandleSimulation(entt::registry& registry, f32 deltaTime)
    {
        enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();
        ModelRenderer* modelRenderer = ServiceLocator::GetGameRenderer()->GetModelRenderer();
        ModelLoader* modelLoader = ServiceLocator::GetGameRenderer()->GetModelLoader();

        auto simulationView = registry.view<Components::Model, Components::AnimationData>();

#if ANIMATION_SIMULATION_MT
        auto* viewHandle = simulationView.handle();
        u32 numEntitiesToHandle = static_cast<u32>(viewHandle->size());
        if (numEntitiesToHandle == 0)
            return;

        const auto& begin = viewHandle->begin();
        moodycamel::ConcurrentQueue<entt::entity> dirtyEntities(numEntitiesToHandle);
        enki::TaskSet simulateEntitiesTask(numEntitiesToHandle, [&registry, &simulationView, &begin, &modelLoader, &dirtyEntities, deltaTime](enki::TaskSetPartition range, uint32_t threadNum)
        {
            for (u32 i = range.start; i < range.end; i++)
            {
                const auto& val = begin.data() + i;
                entt::entity entity = *val;

                if (!simulationView.contains(entity))
                    continue;

                auto components = simulationView.get(entity);

                const auto& model = std::get<0>(components);
                auto& animationData = std::get<1>(components);

                if (animationData.modelHash != model.modelHash)
                    continue;

                const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
                if (!modelInfo)
                    continue;

                u32 numGlobalLoops = static_cast<u32>(animationData.globalLoops.size());
                for (u32 i = 0; i < numGlobalLoops; i++)
                {
                    ::Animation::Defines::GlobalLoop& globalLoop = animationData.globalLoops[i];

                    if (globalLoop.duration == 0.0f)
                        continue;

                    globalLoop.currentTime += deltaTime;
                    globalLoop.currentTime = fmod(globalLoop.currentTime, globalLoop.duration);
                }

                u64 animationStateDirtyBitMask = 0;

                u32 numAnimationStates = static_cast<u32>(animationData.animationStates.size());
                for (u32 i = 0; i < numAnimationStates; i++)
                {
                    ::Animation::Defines::State& animationState = animationData.animationStates[i];

                    bool canBePlayed = animationState.currentSequenceIndex != ::Animation::Defines::InvalidSequenceID;
                    bool isPaused = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Paused);
                    if (!canBePlayed || isPaused)
                        continue;

                    const Model::ComplexModel::AnimationSequence* animationSequence = &modelInfo->sequences[animationState.currentSequenceIndex];
                    ::Animation::Defines::Type animationType = static_cast<::Animation::Defines::Type>(animationSequence->id);
                    f32 duration = static_cast<f32>(animationSequence->duration) / 1000.0f;

                    bool playInReverse = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::PlayReversed);
                    bool holdAtEnd = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::HoldAtEnd);
                    if (playInReverse)
                    {
                        animationState.progress = glm::max(animationState.progress - deltaTime, 0.0f);
                    }
                    else
                    {
                        animationState.progress = glm::min(animationState.progress + deltaTime, duration);
                    }

                    bool hasNextSequence = animationState.nextSequenceIndex != ::Animation::Defines::InvalidSequenceID;
                    bool hasVariation = animationSequence->subID != 0 || animationSequence->nextVariationID != -1;
                    bool canSelectNextSequence = (hasVariation && !holdAtEnd) || (!hasVariation && !holdAtEnd);
                    bool isRepeating = animationState.timesToRepeat > 0;

                    // Check if we need to select the next sequence
                    if (!hasNextSequence && canSelectNextSequence)
                    {
                        if (hasVariation || isRepeating)
                        {
                            if (isRepeating)
                            {
                                animationState.nextSequenceIndex = animationState.currentSequenceIndex;
                                animationState.timesToRepeat--;
                            }
                            else
                            {
                                animationState.nextSequenceIndex = Util::Animation::GetSequenceIndexForAnimation(modelInfo, animationType, animationState.timesToRepeat);
                            }
                        }
                        else
                        {
                            animationState.nextSequenceIndex = animationState.currentSequenceIndex;
                        }

                        animationState.nextAnimation = animationState.currentAnimation;
                        animationState.nextFlags = animationState.currentFlags & ~(::Animation::Defines::Flags::ForceTransition);
                        hasNextSequence = true;
                    }

                    bool isInTransition = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Transitioning);
                    if (!isInTransition && hasNextSequence)
                    {
                        const Model::ComplexModel::AnimationSequence* nextAnimationSequence = &modelInfo->sequences[animationState.nextSequenceIndex];
                        bool needsToBlendTransition = animationState.currentSequenceIndex != animationState.nextSequenceIndex && (animationState.timeToTransitionMS > 0 || animationSequence->flags.blendTransition || animationSequence->flags.blendTransitionIfActive || nextAnimationSequence->flags.blendTransition);

                        bool canForceTransition = ::Animation::Defines::HasFlag(animationState.nextFlags, ::Animation::Defines::Flags::ForceTransition);
                        bool canTransition = canForceTransition;

                        if (!canForceTransition)
                        {
                            if (needsToBlendTransition)
                            {
                                if (needsToBlendTransition)
                                {
                                    f32 timeToTransition = static_cast<f32>(nextAnimationSequence->blendTimeStart) / 1000.0f;
                                    f32 timeLeft = duration;
                                    if (playInReverse)
                                    {
                                        timeLeft = animationState.progress;
                                    }
                                    else
                                    {
                                        timeLeft = duration - animationState.progress;
                                    }

                                    isInTransition = timeLeft <= timeToTransition;
                                    animationState.timeToTransitionMS = static_cast<u8>(nextAnimationSequence->blendTimeStart);
                                }
                            }
                            else
                            {
                                if (playInReverse)
                                {
                                    canTransition = animationState.progress <= 0.0f;
                                }
                                else
                                {
                                    canTransition = animationState.progress >= duration;
                                }

                                animationState.timeToTransitionMS = 0;
                            }
                        }

                        if (canTransition)
                        {
                            animationState.currentFlags |= ::Animation::Defines::Flags::Transitioning;
                            isInTransition = true;
                        }
                    }

                    if (isInTransition)
                    {
                        bool isFinished = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Finished);
                        bool canForceTransition = ::Animation::Defines::HasFlag(animationState.nextFlags, ::Animation::Defines::Flags::ForceTransition);

                        if (!canForceTransition)
                        {
                            if (playInReverse)
                            {
                                animationState.progress = 0;
                            }
                            else
                            {
                                animationState.progress = duration;
                            }
                        }

                        if (animationState.timeToTransitionMS > 0)
                        {
                            f32 timeToTransition = static_cast<f32>(animationState.timeToTransitionMS) / 1000.0f;
                            animationState.transitionTime = glm::min(animationState.transitionTime + deltaTime, timeToTransition);

                            isFinished = animationState.transitionTime >= timeToTransition;
                            if (isFinished)
                            {
                                animationState.currentFlags |= ::Animation::Defines::Flags::Finished;
                            }
                        }
                        else
                        {
                            animationState.currentFlags |= ::Animation::Defines::Flags::Finished;
                            isFinished = true;
                        }

                        u64 animationStateDirtyBit = 1ULL << i;
                        animationStateDirtyBitMask |= animationStateDirtyBit;

                        if (isFinished)
                        {
                            animationState.currentAnimation = animationState.nextAnimation;
                            animationState.nextAnimation = ::Animation::Defines::Type::Invalid;
                            animationState.currentSequenceIndex = animationState.nextSequenceIndex;
                            animationState.nextSequenceIndex = ::Animation::Defines::InvalidSequenceID;
                            animationState.currentFlags = animationState.nextFlags & ~(::Animation::Defines::Flags::ForceTransition);
                            animationState.nextFlags = ::Animation::Defines::Flags::None;
                            animationState.timeToTransitionMS = 0;

                            animationState.progress = 0.0f;
                            animationState.transitionTime = 0.0f;
                            animationState.finishedCallback = nullptr;
                        }
                    }
                    else
                    {
                        bool holdAtEnd = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::HoldAtEnd) || !hasNextSequence;
                        bool isFinished = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Finished);
                        if (!isFinished)
                        {
                            if (playInReverse)
                            {
                                if (animationState.progress <= 0.0f)
                                {
                                    animationState.currentFlags |= ::Animation::Defines::Flags::Finished;
                                    isFinished = true;
                                }
                            }
                            else
                            {
                                if (animationState.progress >= duration)
                                {
                                    animationState.currentFlags |= ::Animation::Defines::Flags::Finished;
                                    isFinished = true;
                                }
                            }

                            u64 animationStateDirtyBit = 1ULL << i;
                            animationStateDirtyBitMask |= animationStateDirtyBit;
                        }

                        if (isFinished && !holdAtEnd)
                        {
                            animationState.currentAnimation = animationState.nextAnimation;
                            animationState.nextAnimation = ::Animation::Defines::Type::Invalid;
                            animationState.currentSequenceIndex = animationState.nextSequenceIndex;
                            animationState.nextSequenceIndex = ::Animation::Defines::InvalidSequenceID;
                            animationState.currentFlags = animationState.nextFlags & ~(::Animation::Defines::Flags::ForceTransition);
                            animationState.nextFlags = ::Animation::Defines::Flags::None;

                            animationState.timeToTransitionMS = 0;

                            bool playReversed = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::PlayReversed);
                            if (playReversed)
                            {
                                animationSequence = &modelInfo->sequences[animationState.currentSequenceIndex];
                                animationState.progress = static_cast<f32>(animationSequence->duration) / 1000.0f;
                            }
                            else
                            {
                                animationState.progress = 0.0f;
                            }

                            animationState.transitionTime = 0.0f;
                            animationState.finishedCallback = nullptr;
                        }
                    }
                }

                u32 numBoneInstances = static_cast<u32>(animationData.boneInstances.size());
                if (animationStateDirtyBitMask)
                {
                    if (numBoneInstances > 0)
                    {
                        for (u32 i = 0; i < numBoneInstances; i++)
                        {
                            ::Animation::Defines::BoneInstance& boneInstance = animationData.boneInstances[i];
                            const Model::ComplexModel::Bone& bone = modelInfo->bones[i];

                            if (boneInstance.stateIndex == ::Animation::Defines::InvalidStateID)
                                continue;

                            mat4x4 boneMatrix = mat4x4(1.0f);

                            bool isTransformed = ::Animation::Defines::HasBoneFlag(boneInstance.flags, ::Animation::Defines::BoneFlags::Transformed);
                            bool hasParent = bone.parentBoneID != -1;

                            if (isTransformed || hasParent)
                            {
                                ::Animation::Defines::State& animationState = animationData.animationStates[boneInstance.stateIndex];

                                if (isTransformed)
                                {
                                    quat rotationOffset = quat(1.0f, 0.0f, 0.0f, 0.0f);
                                    if (boneInstance.proceduralRotationOffsetIndex != ::Animation::Defines::InvalidProcedualBoneID)
                                    {
                                        rotationOffset = animationData.proceduralRotationOffsets[boneInstance.proceduralRotationOffsetIndex];
                                    }

                                    boneMatrix = GetBoneMatrix(animationData.globalLoops, animationState, bone, rotationOffset);
                                }

                                if (hasParent)
                                {
                                    const mat4x4& parentBoneMatrix = animationData.boneTransforms[bone.parentBoneID];
                                    boneMatrix = mul(boneMatrix, parentBoneMatrix);
                                }

                                animationData.boneTransforms[i] = boneMatrix;
                            }
                        }

                        ::Animation::Defines::BoneInstance& rootBoneInstance = animationData.boneInstances[0];

                        u32 numTextureTransforms = static_cast<u32>(animationData.textureTransforms.size());
                        u64 rootBoneMask = 1ull << rootBoneInstance.stateIndex;
                        bool isRootBoneAnimationStateDirty = rootBoneInstance.stateIndex != ::Animation::Defines::InvalidStateID && (animationStateDirtyBitMask & (rootBoneMask)) == rootBoneMask;
                        if (isRootBoneAnimationStateDirty && numTextureTransforms > 0)
                        {
                            ::Animation::Defines::State& animationState = animationData.animationStates[rootBoneInstance.stateIndex];

                            for (u32 textureTransformIndex = 0; textureTransformIndex < numTextureTransforms; textureTransformIndex++)
                            {
                                const Model::ComplexModel::TextureTransform& textureTransform = modelInfo->textureTransforms[textureTransformIndex];
                                mat4x4 textureTransformMatrix = GetTextureTransformMatrix(animationData.globalLoops, animationState, textureTransform);

                                animationData.textureTransforms[textureTransformIndex] = textureTransformMatrix;
                            }
                        }

                        ModelRenderer* modelRenderer = ServiceLocator::GetGameRenderer()->GetModelRenderer();
                        if (modelRenderer)
                        {
                            dirtyEntities.enqueue(entity);
                        }
                    }

                    if (auto* attachmentData = registry.try_get<Components::AttachmentData>(entity))
                    {
                        entt::registry::context& ctx = registry.ctx();
                        auto& transformSystem = ctx.get<TransformSystem>();
                        auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
                        auto& camera = registry.get<Components::Camera>(activeCamera.entity);

                        u32 numBones = static_cast<u32>(modelInfo->bones.size());
                        for (auto& pair : attachmentData->attachmentToInstance)
                        {
                            u16 attachmentIndex = ::Attachment::Defines::InvalidAttachmentIndex;
                            if (!::Util::Attachment::CanUseAttachment(modelInfo, *attachmentData, pair.first, attachmentIndex))
                                continue;

                            const Model::ComplexModel::Attachment& skeletonAttachment = modelInfo->attachments[attachmentIndex];

                            u32 boneIndex = 0;
                            if (skeletonAttachment.bone < numBones)
                                boneIndex = skeletonAttachment.bone;

                            const mat4x4& parentBoneMatrix = animationData.boneTransforms[boneIndex];
                            mat4x4 attachmentMatrix = GetAttachmentMatrix(skeletonAttachment);
                            attachmentMatrix = mul(attachmentMatrix, parentBoneMatrix);

                            pair.second.matrix = attachmentMatrix;

                            vec3 scale;
                            quat rotation;
                            vec3 translation;
                            vec3 skew;
                            vec4 perspective;
                            if (!glm::decompose(attachmentMatrix, scale, rotation, translation, skew, perspective))
                                continue;

                            transformSystem.SetLocalTransform(pair.second.entity, translation, rotation, scale);
                        }
                    }
                }
            }
        });

        taskScheduler->AddTaskSetToPipe(&simulateEntitiesTask);
        taskScheduler->WaitforTask(&simulateEntitiesTask);

        entt::entity dirtyEntity = entt::null;

        while (dirtyEntities.try_dequeue(dirtyEntity))
        {
            const auto& model = registry.get<Components::Model>(dirtyEntity);
            const auto& animationData = registry.get<Components::AnimationData>(dirtyEntity);

            u32 numBoneTransforms = static_cast<u32>(animationData.boneTransforms.size());
            u32 numTextureTransforms = static_cast<u32>(animationData.textureTransforms.size());

            if (model.instanceID == std::numeric_limits<u32>().max())
            {
                auto& animationStaticInstance = registry.get<Components::AnimationStaticInstance>(dirtyEntity);

                if (numBoneTransforms > 0)
                {
                    modelRenderer->SetUninstancedBoneMatricesAsDirty(model.modelID, animationStaticInstance.boneMatrixOffset, 0, numBoneTransforms, animationData.boneTransforms.data());
                }

                if (numTextureTransforms > 0)
                {
                    modelRenderer->SetUninstancedTextureTransformMatricesAsDirty(model.modelID, animationStaticInstance.textureMatrixOffset, 0, numTextureTransforms, animationData.textureTransforms.data());
                }
            }
            else
            {
                if (numBoneTransforms > 0)
                {
                    modelRenderer->SetBoneMatricesAsDirty(model.instanceID, 0, numBoneTransforms, animationData.boneTransforms.data());
                }

                if (numTextureTransforms > 0)
                {
                    modelRenderer->SetTextureTransformMatricesAsDirty(model.instanceID, 0, numTextureTransforms, animationData.textureTransforms.data());
                }
            }
        }
#else
        simulationView.each([&](entt::entity entity, Components::Model& model, Components::AnimationData& animationData)
        {
            if (animationData.modelHash != model.modelHash)
                return;

            const auto* modelInfo = modelLoader->GetModelInfo(model.modelHash);
            if (!modelInfo)
                return;

            u32 numGlobalLoops = static_cast<u32>(animationData.globalLoops.size());
            for (u32 i = 0; i < numGlobalLoops; i++)
            {
                ::Animation::Defines::GlobalLoop& globalLoop = animationData.globalLoops[i];

                if (globalLoop.duration == 0.0f)
                    continue;

                globalLoop.currentTime += deltaTime;
                globalLoop.currentTime = fmod(globalLoop.currentTime, globalLoop.duration);
            }

            u64 animationStateDirtyBitMask = 0;

            u32 numAnimationStates = static_cast<u32>(animationData.animationStates.size());
            for (u32 i = 0; i < numAnimationStates; i++)
            {
                ::Animation::Defines::State& animationState = animationData.animationStates[i];

                bool canBePlayed = animationState.currentSequenceIndex != ::Animation::Defines::InvalidSequenceID;
                bool isPaused = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Paused);
                if (!canBePlayed || isPaused)
                    continue;

                const Model::ComplexModel::AnimationSequence* animationSequence = &modelInfo->sequences[animationState.currentSequenceIndex];
                ::Animation::Defines::Type animationType = static_cast<::Animation::Defines::Type>(animationSequence->id);
                f32 duration = static_cast<f32>(animationSequence->duration) / 1000.0f;

                bool playInReverse = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::PlayReversed);
                bool holdAtEnd = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::HoldAtEnd);
                if (playInReverse)
                {
                    animationState.progress = glm::max(animationState.progress - deltaTime, 0.0f);
                }
                else
                {
                    animationState.progress = glm::min(animationState.progress + deltaTime, duration);
                }

                bool hasNextSequence = animationState.nextSequenceIndex != ::Animation::Defines::InvalidSequenceID;
                bool hasVariation = animationSequence->subID != 0 || animationSequence->nextVariationID != -1;
                bool canSelectNextSequence = (hasVariation && !holdAtEnd) || (!hasVariation && !holdAtEnd);
                bool isRepeating = animationState.timesToRepeat > 0;

                // Check if we need to select the next sequence
                if (!hasNextSequence && canSelectNextSequence)
                {
                    if (hasVariation || isRepeating)
                    {
                        if (isRepeating)
                        {
                            animationState.nextSequenceIndex = animationState.currentSequenceIndex;
                            animationState.timesToRepeat--;
                        }
                        else
                        {
                            animationState.nextSequenceIndex = Util::Animation::GetSequenceIndexForAnimation(modelInfo, animationType, animationState.timesToRepeat);
                        }
                    }
                    else
                    {
                        animationState.nextSequenceIndex = animationState.currentSequenceIndex;
                    }

                    animationState.nextAnimation = animationState.currentAnimation;
                    animationState.nextFlags = animationState.currentFlags & ~(::Animation::Defines::Flags::ForceTransition);
                    hasNextSequence = true;
                }

                bool isInTransition = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Transitioning);
                if (!isInTransition && hasNextSequence)
                {
                    const Model::ComplexModel::AnimationSequence* nextAnimationSequence = &modelInfo->sequences[animationState.nextSequenceIndex];
                    bool needsToBlendTransition = animationState.currentSequenceIndex != animationState.nextSequenceIndex && (animationState.timeToTransitionMS > 0 || animationSequence->flags.blendTransition || animationSequence->flags.blendTransitionIfActive || nextAnimationSequence->flags.blendTransition);

                    bool canForceTransition = ::Animation::Defines::HasFlag(animationState.nextFlags, ::Animation::Defines::Flags::ForceTransition);
                    bool canTransition = canForceTransition;

                    if (!canForceTransition)
                    {
                        if (needsToBlendTransition)
                        {
                            if (needsToBlendTransition)
                            {
                                f32 timeToTransition = static_cast<f32>(nextAnimationSequence->blendTimeStart) / 1000.0f;
                                f32 timeLeft = duration;
                                if (playInReverse)
                                {
                                    timeLeft = animationState.progress;
                                }
                                else
                                {
                                    timeLeft = duration - animationState.progress;
                                }

                                isInTransition = timeLeft <= timeToTransition;
                                animationState.timeToTransitionMS = static_cast<u8>(nextAnimationSequence->blendTimeStart);
                            }
                        }
                        else
                        {
                            if (playInReverse)
                            {
                                canTransition = animationState.progress <= 0.0f;
                            }
                            else
                            {
                                canTransition = animationState.progress >= duration;
                            }

                            animationState.timeToTransitionMS = 0;
                        }
                    }

                    if (canTransition)
                    {
                        animationState.currentFlags |= ::Animation::Defines::Flags::Transitioning;
                        isInTransition = true;
                    }
                }

                if (isInTransition)
                {
                    bool isFinished = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Finished);
                    bool canForceTransition = ::Animation::Defines::HasFlag(animationState.nextFlags, ::Animation::Defines::Flags::ForceTransition);

                    if (!canForceTransition)
                    {
                        if (playInReverse)
                        {
                            animationState.progress = 0;
                        }
                        else
                        {
                            animationState.progress = duration;
                        }
                    }

                    if (animationState.timeToTransitionMS > 0)
                    {
                        f32 timeToTransition = static_cast<f32>(animationState.timeToTransitionMS) / 1000.0f;
                        animationState.transitionTime = glm::min(animationState.transitionTime + deltaTime, timeToTransition);

                        isFinished = animationState.transitionTime >= timeToTransition;
                        if (isFinished)
                        {
                            animationState.currentFlags |= ::Animation::Defines::Flags::Finished;
                        }
                    }
                    else
                    {
                        animationState.currentFlags |= ::Animation::Defines::Flags::Finished;
                        isFinished = true;
                    }

                    u64 animationStateDirtyBit = 1ULL << i;
                    animationStateDirtyBitMask |= animationStateDirtyBit;

                    if (isFinished)
                    {
                        animationState.currentAnimation = animationState.nextAnimation;
                        animationState.nextAnimation = ::Animation::Defines::Type::Invalid;
                        animationState.currentSequenceIndex = animationState.nextSequenceIndex;
                        animationState.nextSequenceIndex = ::Animation::Defines::InvalidSequenceID;
                        animationState.currentFlags = animationState.nextFlags & ~(::Animation::Defines::Flags::ForceTransition);
                        animationState.nextFlags = ::Animation::Defines::Flags::None;
                        animationState.timeToTransitionMS = 0;

                        animationState.progress = 0.0f;
                        animationState.transitionTime = 0.0f;
                        animationState.finishedCallback = nullptr;
                    }
                }
                else
                {
                    bool holdAtEnd = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::HoldAtEnd) || !hasNextSequence;
                    bool isFinished = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::Finished);
                    if (!isFinished)
                    {
                        if (playInReverse)
                        {
                            if (animationState.progress <= 0.0f)
                            {
                                animationState.currentFlags |= ::Animation::Defines::Flags::Finished;
                                isFinished = true;
                            }
                        }
                        else
                        {
                            if (animationState.progress >= duration)
                            {
                                animationState.currentFlags |= ::Animation::Defines::Flags::Finished;
                                isFinished = true;
                            }
                        }

                        u64 animationStateDirtyBit = 1ULL << i;
                        animationStateDirtyBitMask |= animationStateDirtyBit;
                    }

                    if (isFinished && !holdAtEnd)
                    {
                        animationState.currentAnimation = animationState.nextAnimation;
                        animationState.nextAnimation = ::Animation::Defines::Type::Invalid;
                        animationState.currentSequenceIndex = animationState.nextSequenceIndex;
                        animationState.nextSequenceIndex = ::Animation::Defines::InvalidSequenceID;
                        animationState.currentFlags = animationState.nextFlags & ~(::Animation::Defines::Flags::ForceTransition);
                        animationState.nextFlags = ::Animation::Defines::Flags::None;

                        animationState.timeToTransitionMS = 0;

                        bool playReversed = ::Animation::Defines::HasFlag(animationState.currentFlags, ::Animation::Defines::Flags::PlayReversed);
                        if (playReversed)
                        {
                            animationSequence = &modelInfo->sequences[animationState.currentSequenceIndex];
                            animationState.progress = static_cast<f32>(animationSequence->duration) / 1000.0f;
                        }
                        else
                        {
                            animationState.progress = 0.0f;
                        }

                        animationState.transitionTime = 0.0f;
                        animationState.finishedCallback = nullptr;
                    }
                }
            }

            u32 numBoneInstances = static_cast<u32>(animationData.boneInstances.size());
            if (animationStateDirtyBitMask)
            {
                if (numBoneInstances > 0)
                {
                    for (u32 i = 0; i < numBoneInstances; i++)
                    {
                        ::Animation::Defines::BoneInstance& boneInstance = animationData.boneInstances[i];
                        const Model::ComplexModel::Bone& bone = modelInfo->bones[i];

                        if (boneInstance.stateIndex == ::Animation::Defines::InvalidStateID)
                            continue;

                        mat4x4 boneMatrix = mat4x4(1.0f);

                        bool isTransformed = ::Animation::Defines::HasBoneFlag(boneInstance.flags, ::Animation::Defines::BoneFlags::Transformed);
                        bool hasParent = bone.parentBoneID != -1;

                        if (isTransformed || hasParent)
                        {
                            ::Animation::Defines::State& animationState = animationData.animationStates[boneInstance.stateIndex];

                            if (isTransformed)
                            {
                                quat rotationOffset = quat(1.0f, 0.0f, 0.0f, 0.0f);
                                if (boneInstance.proceduralRotationOffsetIndex != ::Animation::Defines::InvalidProcedualBoneID)
                                {
                                    rotationOffset = animationData.proceduralRotationOffsets[boneInstance.proceduralRotationOffsetIndex];
                                }

                                boneMatrix = GetBoneMatrix(animationData.globalLoops, animationState, bone, rotationOffset);
                            }

                            if (hasParent)
                            {
                                const mat4x4& parentBoneMatrix = animationData.boneTransforms[bone.parentBoneID];
                                boneMatrix = mul(boneMatrix, parentBoneMatrix);
                            }

                            animationData.boneTransforms[i] = boneMatrix;
                        }
                    }

                    ::Animation::Defines::BoneInstance& rootBoneInstance = animationData.boneInstances[0];

                    u32 numTextureTransforms = static_cast<u32>(animationData.textureTransforms.size());
                    u64 rootBoneMask = 1ull << rootBoneInstance.stateIndex;
                    bool isRootBoneAnimationStateDirty = rootBoneInstance.stateIndex != ::Animation::Defines::InvalidStateID && (animationStateDirtyBitMask & (rootBoneMask)) == rootBoneMask;
                    if (isRootBoneAnimationStateDirty && numTextureTransforms > 0)
                    {
                        ::Animation::Defines::State& animationState = animationData.animationStates[rootBoneInstance.stateIndex];

                        for (u32 textureTransformIndex = 0; textureTransformIndex < numTextureTransforms; textureTransformIndex++)
                        {
                            const Model::ComplexModel::TextureTransform& textureTransform = modelInfo->textureTransforms[textureTransformIndex];
                            mat4x4 textureTransformMatrix = GetTextureTransformMatrix(animationData.globalLoops, animationState, textureTransform);

                            animationData.textureTransforms[textureTransformIndex] = textureTransformMatrix;
                        }
                    }

                    ModelRenderer* modelRenderer = ServiceLocator::GetGameRenderer()->GetModelRenderer();
                    if (modelRenderer)
                    {
                        if (model.instanceID == std::numeric_limits<u32>().max())
                        {
                            auto& animationStaticInstance = registry.get<Components::AnimationStaticInstance>(entity);

                            if (numBoneInstances > 0)
                            {
                                modelRenderer->SetUninstancedBoneMatricesAsDirty(model.modelID, animationStaticInstance.boneMatrixOffset, 0, numBoneInstances, animationData.boneTransforms.data());
                            }

                            if (numTextureTransforms > 0)
                            {
                                modelRenderer->SetUninstancedTextureTransformMatricesAsDirty(model.modelID, animationStaticInstance.textureMatrixOffset, 0, numTextureTransforms, animationData.textureTransforms.data());
                            }
                        }
                        else
                        {
                            if (numBoneInstances > 0)
                            {
                                modelRenderer->SetBoneMatricesAsDirty(model.instanceID, 0, numBoneInstances, animationData.boneTransforms.data());
                            }

                            if (numTextureTransforms > 0)
                            {
                                modelRenderer->SetTextureTransformMatricesAsDirty(model.instanceID, 0, numTextureTransforms, animationData.textureTransforms.data());
                            }
                        }
                    }
                }

                if (auto* attachmentData = registry.try_get<Components::AttachmentData>(entity))
                {
                    entt::registry::context& ctx = registry.ctx();
                    auto& transformSystem = ctx.get<TransformSystem>();
                    auto& activeCamera = ctx.get<Singletons::ActiveCamera>();
                    auto& camera = registry.get<Components::Camera>(activeCamera.entity);

                    u32 numBones = static_cast<u32>(modelInfo->bones.size());
                    for (auto& pair : attachmentData->attachmentToInstance)
                    {
                        u16 attachmentIndex = ::Attachment::Defines::InvalidAttachmentIndex;
                        if (!::Util::Attachment::CanUseAttachment(modelInfo, *attachmentData, pair.first, attachmentIndex))
                            continue;

                        const Model::ComplexModel::Attachment& skeletonAttachment = modelInfo->attachments[attachmentIndex];

                        u32 boneIndex = 0;
                        if (skeletonAttachment.bone < numBones)
                            boneIndex = skeletonAttachment.bone;

                        const mat4x4& parentBoneMatrix = animationData.boneTransforms[boneIndex];
                        mat4x4 attachmentMatrix = GetAttachmentMatrix(skeletonAttachment);
                        attachmentMatrix = mul(attachmentMatrix, parentBoneMatrix);

                        pair.second.matrix = attachmentMatrix;

                        vec3 scale;
                        quat rotation;
                        vec3 translation;
                        vec3 skew;
                        vec4 perspective;
                        if (!glm::decompose(attachmentMatrix, scale, rotation, translation, skew, perspective))
                            continue;

                        transformSystem.SetLocalTransform(pair.second.entity, translation, rotation, scale);
                    }
                }
            }
        });
#endif
    }
}