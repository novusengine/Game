#include "Animations.h"

#include "Game-Lib/Animation/AnimationSystem.h"
#include "Game-Lib/ECS/Components/AABB.h"
#include "Game-Lib/ECS/Components/AnimationData.h"
#include "Game-Lib/ECS/Components/Model.h"
#include "Game-Lib/ECS/Components/Tags.h"
#include "Game-Lib/ECS/Singletons/AnimationSingleton.h"
#include "Game-Lib/ECS/Util/Transforms.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Model/ModelRenderer.h"
#include "Game-Lib/Util/AnimationUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>

#include <glm/gtx/matrix_decompose.hpp>

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

    static mat4x4 GetBoneMatrix(const std::vector<Animation::AnimationGlobalLoop>& globalLoops, const Animation::AnimationState& animationState, const Animation::AnimationSkeletonBone& bone)
    {
        vec3 translationValue = vec3(0.0f, 0.0f, 0.0f);
        quat rotationValue = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scaleValue = vec3(1.0f, 1.0f, 1.0f);

        bool isTranslationGlobalLoop = bone.info.translation.globalLoopIndex != -1;
        bool isRotationGlobalLoop = bone.info.rotation.globalLoopIndex != -1;
        bool isScaleGlobalLoop = bone.info.scale.globalLoopIndex != -1;

        // Primary Sequence
        {
            // Handle Translation
            {
                u32 sequenceID = animationState.currentSequenceIndex;
                if (isTranslationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < bone.info.translation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.translation.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (isTranslationGlobalLoop)
                    {
                        const Animation::AnimationGlobalLoop& globalLoop = globalLoops[bone.info.translation.globalLoopIndex];
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

                if (sequenceID < bone.info.rotation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<quat>& track = bone.info.rotation.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (bone.info.rotation.globalLoopIndex != -1)
                    {
                        const Animation::AnimationGlobalLoop& globalLoop = globalLoops[bone.info.rotation.globalLoopIndex];
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
        
                if (sequenceID < bone.info.scale.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.scale.tracks[sequenceID];
                    f32 progress = animationState.progress;
        
                    if (bone.info.scale.globalLoopIndex != -1)
                    {
                        const Animation::AnimationGlobalLoop& globalLoop = globalLoops[bone.info.scale.globalLoopIndex];
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
        //            if (sequenceID < bone.info.translation.tracks.size())
        //            {
        //                const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.translation.tracks[sequenceID];
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
        //            if (sequenceID < bone.info.rotation.tracks.size())
        //            {
        //                const Model::ComplexModel::AnimationTrack<quat>& track = bone.info.rotation.tracks[sequenceID];
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
        //            if (sequenceID < bone.info.scale.tracks.size())
        //            {
        //                const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.scale.tracks[sequenceID];
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
        //
        //rotationValue = glm::normalize(boneInstance.rotationOffset * glm::normalize(rotationValue));

        const vec3& pivot = bone.info.pivot;
        mat4x4 boneMatrix = mul(glm::translate(mat4x4(1.0f), pivot), mat4x4(1.0f));

        mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), translationValue);
        mat4x4 rotationMatrix = glm::toMat4(rotationValue);
        mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scaleValue);

        boneMatrix = mul(translationMatrix, boneMatrix);
        boneMatrix = mul(rotationMatrix, boneMatrix);
        boneMatrix = mul(scaleMatrix, boneMatrix);

        boneMatrix = mul(glm::translate(mat4x4(1.0f), -pivot), boneMatrix);

        return boneMatrix;
    }
   
    static mat4x4 GetTextureTransformMatrix(const std::vector<Animation::AnimationGlobalLoop>& globalLoops, const Animation::AnimationState& animationState, const Animation::AnimationSkeletonTextureTransform& textureTransform)
    {
        vec3 translationValue = vec3(0.0f, 0.0f, 0.0f);
        quat rotationValue = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scaleValue = vec3(1.0f, 1.0f, 1.0f);

        bool isTranslationGlobalLoop = textureTransform.info.translation.globalLoopIndex != -1;
        bool isRotationGlobalLoop = textureTransform.info.rotation.globalLoopIndex != -1;
        bool isScaleGlobalLoop = textureTransform.info.scale.globalLoopIndex != -1;

        // Primary Sequence
        {
            // Handle Translation
            {
                u32 sequenceID = animationState.currentSequenceIndex;
                if (isTranslationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < textureTransform.info.translation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = textureTransform.info.translation.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (isTranslationGlobalLoop)
                    {
                        const Animation::AnimationGlobalLoop& globalLoop = globalLoops[textureTransform.info.translation.globalLoopIndex];
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

                if (sequenceID < textureTransform.info.rotation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<quat>& track = textureTransform.info.rotation.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (textureTransform.info.rotation.globalLoopIndex != -1)
                    {
                        const Animation::AnimationGlobalLoop& globalLoop = globalLoops[textureTransform.info.rotation.globalLoopIndex];
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

                if (sequenceID < textureTransform.info.scale.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = textureTransform.info.scale.tracks[sequenceID];
                    f32 progress = animationState.progress;

                    if (textureTransform.info.scale.globalLoopIndex != -1)
                    {
                        const Animation::AnimationGlobalLoop& globalLoop = globalLoops[textureTransform.info.scale.globalLoopIndex];
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
        //            if (sequenceID < bone.info.translation.tracks.size())
        //            {
        //                const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.translation.tracks[sequenceID];
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
        //            if (sequenceID < bone.info.rotation.tracks.size())
        //            {
        //                const Model::ComplexModel::AnimationTrack<quat>& track = bone.info.rotation.tracks[sequenceID];
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
        //            if (sequenceID < bone.info.scale.tracks.size())
        //            {
        //                const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.scale.tracks[sequenceID];
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
        //
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

    void SetupDynamicAnimationInstance(entt::registry& registry, entt::entity entity, const Components::Model& model, const Animation::AnimationSkeleton& skeleton)
    {
        auto& animationData = registry.get_or_emplace<Components::AnimationData>(entity);
        u32 numGlobalLoops = static_cast<u32>(skeleton.globalLoops.size());
        u32 numBones = static_cast<u32>(skeleton.bones.size());
        u32 numTextureTransforms = static_cast<u32>(skeleton.textureTransforms.size());

        animationData.globalLoops.clear();
        animationData.globalLoops.resize(numGlobalLoops);
        for (u32 i = 0; i < numGlobalLoops; i++)
        {
            Animation::AnimationGlobalLoop& globalLoop = animationData.globalLoops[i];

            globalLoop.currentTime = 0.0f;
            globalLoop.duration = static_cast<f32>(skeleton.globalLoops[i]) / 1000.0f;
        }

        animationData.boneInstances.clear();
        animationData.boneInstances.resize(numBones);

        animationData.animationStates.reserve(8);
        animationData.animationStates.clear();

        animationData.boneTransforms.resize(numBones);
        for (u32 i = 0; i < numBones; i++)
        {
            animationData.boneTransforms[i] = mat4a(1.0f);
        }

        animationData.textureTransforms.resize(numTextureTransforms);
        for (u32 i = 0; i < numTextureTransforms; i++)
        {
            animationData.textureTransforms[i] = mat4a(1.0f);
        }

        animationData.proceduralBoneTransforms.reserve(8);
        animationData.proceduralBoneTransforms.clear();

        bool hasAnyTransformedBones = false;

        for (u32 i = 0; i < numBones; i++)
        {
            Animation::AnimationBoneInstance& boneInstance = animationData.boneInstances[i];
            const Animation::AnimationSkeletonBone& skeletonBone = skeleton.bones[i];

            // Copy First 7 flags from Skeleton Bone Flags Struct
            u8 skeletonFlags = *reinterpret_cast<const u8*>(&skeletonBone.info.flags) & 0x7F;
            boneInstance.flags |= static_cast<Animation::AnimationBoneFlags>(skeletonFlags);

            if (skeletonBone.info.flags.Transformed)
            {
                boneInstance.flags |= Animation::AnimationBoneFlags::Transformed;
                hasAnyTransformedBones = true;
            }
        }

        if (hasAnyTransformedBones)
        {
            if (!Util::Animation::SetBoneSequence(registry, model, animationData, Animation::AnimationBone::Default, Animation::AnimationType::Stand))
            {
                Util::Animation::SetBoneSequence(registry, model, animationData, Animation::AnimationBone::Default, Animation::AnimationType::Closed);
            }
        }
        else
        {
            volatile i32 test = 0;
        }
    }
    void SetupStaticAnimationInstance(entt::registry& registry, entt::entity entity, Singletons::AnimationSingleton& animationSingleton, const Components::Model& model, const Animation::AnimationSkeleton& skeleton, ModelRenderer* modelRenderer)
    {
        entt::entity dynamicEntity = entt::null;
        if (!animationSingleton.staticModelIDToEntity.contains(model.modelID))
        {
            dynamicEntity = registry.create();
            auto& animationStaticInstance = registry.get_or_emplace<Components::AnimationStaticInstance>(dynamicEntity);
            auto& dynamicModel = registry.emplace<Components::Model>(dynamicEntity);
            dynamicModel.modelID = model.modelID;
            dynamicModel.instanceID = std::numeric_limits<u32>().max();

            if (modelRenderer)
            {
                bool result = modelRenderer->AddUninstancedAnimationData(dynamicModel.modelID, animationStaticInstance.boneMatrixOffset, animationStaticInstance.textureMatrixOffset);
                NC_ASSERT(result, "Failed to add uninstanced animation data.");
            }

            SetupDynamicAnimationInstance(registry, dynamicEntity, model, skeleton);
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

    void Animations::Init(entt::registry& registry)
    {
        registry.ctx().emplace<Singletons::AnimationSingleton>();
        std::srand(static_cast<u32>(std::time(NULL)));
    }

    void Animations::UpdateSimulation(entt::registry& registry, f32 deltaTime)
    {
        Animation::AnimationSystem* animationSystem = ServiceLocator::GetAnimationSystem();
        Animation::AnimationStorage& animationStorage = animationSystem->GetStorage();

        ModelRenderer* modelRenderer = ServiceLocator::GetGameRenderer()->GetModelRenderer();
        auto& animationSingleton = registry.ctx().get<Singletons::AnimationSingleton>();

        auto initView = registry.view<Components::Model, Components::AnimationInitData>();
        initView.each([&](entt::entity entity, Components::Model& model, Components::AnimationInitData& animationInitData)
        {
            if (!animationInitData.flags.shouldInitialize)
                return;

            Animation::AnimationSkeleton& skeleton = animationStorage.skeletons[model.modelID];

            if (animationInitData.flags.isDynamic)
            {
                SetupDynamicAnimationInstance(registry, entity, model, skeleton);
            }
            else
            {
                SetupStaticAnimationInstance(registry, entity, animationSingleton, model, skeleton, modelRenderer);
            }
        });

        registry.clear<Components::AnimationInitData>();

        if (!animationSystem->IsEnabled())
            return;

        auto simulationView = registry.view<Components::Model, Components::AnimationData>();
        simulationView.each([&](entt::entity entity, Components::Model& model, Components::AnimationData& animationData)
        {
            u32 modelID = model.modelID;
            Animation::AnimationSkeleton& skeleton = animationStorage.skeletons[modelID];
            NC_ASSERT(skeleton.modelID != Animation::AnimationSkeleton::InvalidID, "ModelID is invalid.");

            u32 numGlobalLoops = static_cast<u32>(animationData.globalLoops.size());
            for (u32 i = 0; i < numGlobalLoops; i++)
            {
                Animation::AnimationGlobalLoop& globalLoop = animationData.globalLoops[i];

                if (globalLoop.duration == 0.0f)
                    continue;

                globalLoop.currentTime += deltaTime;
                globalLoop.currentTime = fmod(globalLoop.currentTime, globalLoop.duration);
            }

            u64 animationStateDirtyBitMask = 0;

            u32 numAnimationStates = static_cast<u32>(animationData.animationStates.size());
            for (u32 i = 0; i < numAnimationStates; i++)
            {
                Animation::AnimationState& animationState = animationData.animationStates[i];

                bool canBePlayed = animationState.currentSequenceIndex != Animation::InvalidSequenceID;
                bool isPaused = Animation::HasAnimationFlag(animationState.currentFlags, Animation::AnimationFlags::Paused);
                if (!canBePlayed || isPaused)
                    continue;

                Model::ComplexModel::AnimationSequence& animationSequence = skeleton.sequences[animationState.currentSequenceIndex];
                Animation::AnimationType animationType = static_cast<Animation::AnimationType>(animationSequence.id);
                f32 duration = static_cast<f32>(animationSequence.duration) / 1000.0f;

                bool playInReverse = Animation::HasAnimationFlag(animationState.currentFlags, Animation::AnimationFlags::PlayReversed);
                bool holdAtEnd = Animation::HasAnimationFlag(animationState.currentFlags, Animation::AnimationFlags::HoldAtEnd);
                if (playInReverse)
                {
                    animationState.progress = glm::max(animationState.progress - deltaTime, 0.0f);
                }
                else
                {
                    animationState.progress = glm::min(animationState.progress + deltaTime, duration);
                }

                bool hasNextSequence = animationState.nextSequenceIndex != Animation::InvalidSequenceID;
                bool hasVariation = animationSequence.subID != 0 || animationSequence.nextVariationID != -1;
                bool canSelectNextSequence = hasVariation || !holdAtEnd;
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
                            animationState.nextSequenceIndex = Util::Animation::GetSequenceIndexForAnimation(skeleton, animationType, animationState.timesToRepeat);
                        }
                    }
                    else
                    {
                        animationState.nextSequenceIndex = animationState.currentSequenceIndex;
                    }

                    animationState.nextAnimation = animationState.currentAnimation;
                    animationState.nextFlags = animationState.currentFlags & ~(Animation::AnimationFlags::ForceTransition);
                    hasNextSequence = true;
                }

                bool isInTransition = Animation::HasAnimationFlag(animationState.currentFlags, Animation::AnimationFlags::Transitioning);
                if (!isInTransition && hasNextSequence)
                {
                    Model::ComplexModel::AnimationSequence* nextAnimationSequence = &skeleton.sequences[animationState.nextSequenceIndex];
                    bool needsToBlendTransition = animationState.currentSequenceIndex != animationState.nextSequenceIndex && (animationSequence.flags.blendTransition || animationSequence.flags.blendTransitionIfActive || nextAnimationSequence->flags.blendTransition);

                    bool canForceTransition = Animation::HasAnimationFlag(animationState.nextFlags, Animation::AnimationFlags::ForceTransition);
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
                        animationState.currentFlags |= Animation::AnimationFlags::Transitioning;
                        isInTransition = true;
                    }
                }

                if (isInTransition)
                {
                    bool isFinished = Animation::HasAnimationFlag(animationState.currentFlags, Animation::AnimationFlags::Finished);

                    if (playInReverse)
                    {
                        animationState.progress = 0;
                    }
                    else
                    {
                        animationState.progress = duration;
                    }

                    if (!isFinished)
                    {
                        if (animationState.timeToTransitionMS > 0)
                        {
                            f32 timeToTransition = static_cast<f32>(animationState.timeToTransitionMS) / 1000.0f;
                            animationState.transitionTime = glm::min(animationState.transitionTime + deltaTime, timeToTransition);

                            if (animationState.transitionTime >= timeToTransition)
                            {
                                animationState.currentFlags |= Animation::AnimationFlags::Finished;
                                isFinished = true;
                            }
                        }
                        else
                        {
                            animationState.currentFlags |= Animation::AnimationFlags::Finished;
                            isFinished = true;
                        }

                        u64 animationStateDirtyBit = 1ULL << i;
                        animationStateDirtyBitMask |= animationStateDirtyBit;
                    }

                    if (!holdAtEnd && isFinished)
                    {
                        animationState.currentAnimation = animationState.nextAnimation;
                        animationState.nextAnimation = Animation::AnimationType::Invalid;
                        animationState.currentSequenceIndex = animationState.nextSequenceIndex;
                        animationState.nextSequenceIndex = Animation::InvalidSequenceID;
                        animationState.currentFlags = animationState.nextFlags & ~(Animation::AnimationFlags::ForceTransition);
                        animationState.nextFlags = Animation::AnimationFlags::None;
                        animationState.timeToTransitionMS = 0;

                        animationState.progress = 0.0f;
                        animationState.transitionTime = 0.0f;
                        animationState.finishedCallback = nullptr;
                    }
                }
                else
                {
                    bool holdAtEnd = Animation::HasAnimationFlag(animationState.currentFlags, Animation::AnimationFlags::HoldAtEnd) || !hasNextSequence;
                    bool isFinished = Animation::HasAnimationFlag(animationState.currentFlags, Animation::AnimationFlags::Finished);
                    if (!isFinished)
                    {
                        if (playInReverse)
                        {
                            if (animationState.progress <= 0.0f)
                            {
                                animationState.currentFlags |= Animation::AnimationFlags::Finished;
                                isFinished = true;
                            }
                        }
                        else
                        {
                            if (animationState.progress >= duration)
                            {
                                animationState.currentFlags |= Animation::AnimationFlags::Finished;
                                isFinished = true;
                            }
                        }

                        u64 animationStateDirtyBit = 1ULL << i;
                        animationStateDirtyBitMask |= animationStateDirtyBit;
                    }

                    if (isFinished && !holdAtEnd)
                    {
                        animationState.currentAnimation = animationState.nextAnimation;
                        animationState.nextAnimation = Animation::AnimationType::Invalid;
                        animationState.currentSequenceIndex = animationState.nextSequenceIndex;
                        animationState.nextSequenceIndex = Animation::InvalidSequenceID;
                        animationState.currentFlags = animationState.nextFlags & ~(Animation::AnimationFlags::ForceTransition);
                        animationState.nextFlags = Animation::AnimationFlags::None;

                        animationState.timeToTransitionMS = 0;

                        bool playReversed = Animation::HasAnimationFlag(animationState.currentFlags, Animation::AnimationFlags::PlayReversed);
                        if (playReversed)
                        {
                            animationSequence = skeleton.sequences[animationState.currentSequenceIndex];
                            animationState.progress = static_cast<f32>(animationSequence.duration) / 1000.0f;
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
                ::Animation::AnimationStateID animationStateIndex = Animation::InvalidStateID;

                if (numBoneInstances > 0)
                {
                    for (u32 i = 0; i < numBoneInstances; i++)
                    {
                        Animation::AnimationBoneInstance& boneInstance = animationData.boneInstances[i];
                        const Animation::AnimationSkeletonBone& bone = skeleton.bones[i];

                        if (boneInstance.animationStateIndex == Animation::InvalidStateID)
                            continue;

                        mat4x4 boneMatrix = mat4x4(1.0f);

                        bool isTransformed = Animation::HasAnimationBoneFlag(boneInstance.flags, Animation::AnimationBoneFlags::Transformed);
                        bool hasParent = bone.info.parentBoneID != -1;

                        if (isTransformed || hasParent)
                        {
                            Animation::AnimationState& animationState = animationData.animationStates[boneInstance.animationStateIndex];

                            if (isTransformed)
                            {
                                boneMatrix = GetBoneMatrix(animationData.globalLoops, animationState, bone);
                            }

                            if (hasParent)
                            {
                                const mat4x4& parentBoneMatrix = animationData.boneTransforms[bone.info.parentBoneID];
                                boneMatrix = mul(boneMatrix, parentBoneMatrix);
                            }

                            animationData.boneTransforms[i] = boneMatrix;
                        }
                    }

                    Animation::AnimationBoneInstance& rootBoneInstance = animationData.boneInstances[0];

                    u32 numTextureTransforms = static_cast<u32>(animationData.textureTransforms.size());
                    u64 rootBoneMask = 1ull << rootBoneInstance.animationStateIndex;
                    bool isRootBoneAnimationStateDirty = rootBoneInstance.animationStateIndex != Animation::InvalidStateID && (animationStateDirtyBitMask & (rootBoneMask)) == rootBoneMask;
                    if (isRootBoneAnimationStateDirty && numTextureTransforms > 0)
                    {
                        Animation::AnimationState& animationState = animationData.animationStates[rootBoneInstance.animationStateIndex];

                        for (u32 textureTransformIndex = 0; textureTransformIndex < numTextureTransforms; textureTransformIndex++)
                        {
                            const Animation::AnimationSkeletonTextureTransform& textureTransform = skeleton.textureTransforms[textureTransformIndex];
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
            }
        });
    }
}