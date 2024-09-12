#include "AnimationSystem.h"
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
    inline void matmul4x4(const mat4x4& __restrict m1, const mat4x4& __restrict m2, mat4x4& __restrict out)
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

    inline mat4x4 mul(const mat4x4& __restrict matrix1, const mat4x4& __restrict matrix2)
    {
        mat4x4 result;
        matmul4x4(matrix2, matrix1, result);

        return result;
    }

    mat4x4 MatrixTranslate(const vec3& v)
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

    mat4x4 MatrixRotation(const quat& quat)
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

    mat4x4 MatrixScale(const vec3& v)
    {
        mat4x4 result = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        result[0] = result[0] * v[0];
        result[1] = result[1] * v[1];
        result[2] = result[2] * v[2];

        return result;
    }

    template <typename T>
    T InterpolateKeyframe(const Model::ComplexModel::AnimationTrack<T>& track, f32 progress)
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

    AnimationSystem::AnimationSystem(ModelRenderer* modelRenderer) : _modelRenderer(modelRenderer)
    {
        BoneNames[0] = "ArmL";
        BoneNames[1] = "ArmR";
        BoneNames[2] = "ShoulderL";
        BoneNames[3] = "ShoulderR";
        BoneNames[4] = "SpineLow";
        BoneNames[5] = "Waist";
        BoneNames[6] = "Head";
        BoneNames[7] = "Jaw";
        BoneNames[8] = "IndexFingerR";
        BoneNames[9] = "MiddleFingerR";
        BoneNames[10] = "PinkyFingerR";
        BoneNames[11] = "RingFingerR";
        BoneNames[12] = "ThumbR";
        BoneNames[13] = "IndexFingerL";
        BoneNames[14] = "MiddleFingerL";
        BoneNames[15] = "PinkyFingerL";
        BoneNames[16] = "RingFingerL";
        BoneNames[17] = "ThumbL";
        BoneNames[18] = "$BTH";
        BoneNames[19] = "$CSR";
        BoneNames[20] = "$CSL";
        BoneNames[21] = "_Breath";
        BoneNames[22] = "_Name";
        BoneNames[23] = "_NameMount";
        BoneNames[24] = "$CHD";
        BoneNames[25] = "$CCH";
        BoneNames[26] = "Root";
        BoneNames[27] = "Wheel1";
        BoneNames[28] = "Wheel2";
        BoneNames[29] = "Wheel3";
        BoneNames[30] = "Wheel4";
        BoneNames[31] = "Wheel5";
        BoneNames[32] = "Wheel6";
        BoneNames[33] = "Wheel7";
        BoneNames[34] = "Wheel8";
        BoneNames[35] = "FaceAttenuation";
        BoneNames[36] = "EXP_C1_Cape1";
        BoneNames[37] = "EXP_C1_Cape2";
        BoneNames[38] = "EXP_C1_Cape3";
        BoneNames[39] = "EXP_C1_Cape4";
        BoneNames[40] = "EXP_C1_Cape5";
        BoneNames[41] = "EXP_C1_Tail1";
        BoneNames[42] = "EXP_C1_Tail2";
        BoneNames[43] = "EXP_C1_LoinBk1";
        BoneNames[44] = "EXP_C1_LoinBk2";
        BoneNames[45] = "EXP_C1_LoinBk3";
        BoneNames[48] = "EXP_C1_Spine2";
        BoneNames[49] = "EXP_C1_Neck1";
        BoneNames[50] = "EXP_C1_Neck2";
        BoneNames[51] = "EXP_C1_Pelvis1";
        BoneNames[52] = "Buckle";
        BoneNames[53] = "Chest";
        BoneNames[54] = "Main";
        BoneNames[55] = "EXP_R1_Leg1Twist1";
        BoneNames[56] = "EXP_L1_Leg1Twist1";
        BoneNames[57] = "EXP_R1_Leg2Twist1";
        BoneNames[58] = "EXP_L1_Leg2Twist1";
        BoneNames[59] = "FootL";
        BoneNames[60] = "FootR";
        BoneNames[61] = "ElbowR";
        BoneNames[62] = "ElbowL";
        BoneNames[63] = "EXP_L1_Shield1";
        BoneNames[64] = "HandR";
        BoneNames[65] = "HandL";
        BoneNames[66] = "WeaponR";
        BoneNames[67] = "WeaponL";
        BoneNames[68] = "SpellHandL";
        BoneNames[69] = "SpellHandR";
        BoneNames[70] = "EXP_R1_Leg1Twist3";
        BoneNames[71] = "EXP_L1_Leg1Twist3";
        BoneNames[72] = "EXP_R1_Arm1Twist2";
        BoneNames[73] = "EXP_L1_Arm1Twist2";
        BoneNames[74] = "EXP_R1_Arm1Twist3";
        BoneNames[75] = "EXP_L1_Arm1Twist3";
        BoneNames[76] = "EXP_R1_Arm2Twist2";
        BoneNames[77] = "EXP_L1_Arm2Twist2";
        BoneNames[78] = "EXP_R1_Arm2Twist3";
        BoneNames[79] = "EXP_L1_Arm2Twist3";
        BoneNames[80] = "ForearmR";
        BoneNames[81] = "ForearmL";
        BoneNames[82] = "EXP_R1_Arm1Twist1";
        BoneNames[83] = "EXP_L1_Arm1Twist1";
        BoneNames[84] = "EXP_R1_Arm2Twist1";
        BoneNames[85] = "EXP_L1_Arm2Twist1";
        BoneNames[86] = "EXP_R1_FingerClawA1";
        BoneNames[87] = "EXP_R1_FingerClawB1";
        BoneNames[88] = "EXP_L1_FingerClawA1";
        BoneNames[89] = "EXP_L1_FingerClawB1";
        BoneNames[190] = "_BackCloak";
        BoneNames[191] = "face_hair_00_M_JNT";
        BoneNames[192] = "face_beard_00_M_JNT";
    }

    bool AnimationSystem::IsEnabled()
    {
        return CVAR_AnimationSystemEnabled.Get() != 0;
    }

    bool AnimationSystem::AddSkeleton(ModelID modelID, Model::ComplexModel& model)
    {
        bool isEnabled = CVAR_AnimationSystemEnabled.Get();
        if (!isEnabled)
        {
            return false;
        }

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

                Type animationID = static_cast<Type>(sequence.id);
                if (animationID <= Type::Invalid || animationID >= Type::Count)
                {
                    NC_LOG_ERROR("Model {0} has sequences ({1}) with animationID ({2}). (This animationID is invalid, Min/Max Value : ({3}, {4}))", modelID, i, sequence.id, (i32)Type::Invalid + 1, (i32)Type::Count);
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
    bool AnimationSystem::AddInstance(ModelID modelID, InstanceID instanceID)
    {
        bool isEnabled = CVAR_AnimationSystemEnabled.Get();
        if (!isEnabled)
        {
            return false;
        }

        if (!HasSkeleton(modelID))
        {
            return false;
        }

        bool instanceAlreadyExists = HasInstance(instanceID);

        const AnimationSkeleton& skeleton = _storage.skeletons[modelID];
        u32 numGlobalLoops = static_cast<u32>(skeleton.globalLoops.size());
        u32 numBones = static_cast<u32>(skeleton.bones.size());
        u32 numTextureTransforms = static_cast<u32>(skeleton.textureTransforms.size());

        AnimationInstance& instance = _storage.instanceIDToData[instanceID];
        instance.modelID = modelID;
        instance.instanceID = instanceID;
        instance.boneMatrixOffset = _storage.boneMatrixIndex.fetch_add(numBones);

        if (numTextureTransforms > 0)
        {
            instance.textureTransformMatrixOffset = _storage.textureTransformMatrixIndex.fetch_add(numTextureTransforms);
        }
        
        instance.globalLoops.resize(numGlobalLoops);
        for (u32 i = 0; i < numGlobalLoops; i++)
        {
            AnimationGlobalLoopInstance& globalLoop = instance.globalLoops[i];

            globalLoop.currentTime = 0.0f;
            globalLoop.duration = static_cast<f32>(skeleton.globalLoops[i]) / 1000.0f;
        }

        instance.bones.resize(numBones);
        for (u32 i = 0; i < numBones; i++)
        {
            const AnimationSkeletonBone& skeletonBone = skeleton.bones[i];
            if (skeletonBone.info.parentBoneID == -1)
                continue;

            AnimationBoneInstance& boneInstance = instance.bones[i];
            boneInstance.parent = &instance.bones[skeletonBone.info.parentBoneID];
        }

        if (!instanceAlreadyExists)
        {
            u32 instanceIndex = _storage.instancesIndex.fetch_add(1);
            if (_storage.instanceIDs.size() <= instanceIndex)
            {
                _storage.instanceIDs.push_back(instanceID);
            }
            else
            {
                _storage.instanceIDs[instanceIndex] = instanceID;
            }
        }

        if (HasModelRenderer())
        {
            _modelRenderer->AddAnimationInstance(instanceID);
        }

        return true;
    }

    bool AnimationSystem::RemoveInstance(InstanceID instanceID)
    {
        if (!HasInstance(instanceID))
        {
            return false;
        }

        std::erase(_storage.instanceIDs, instanceID);
        _storage.instancesIndex--;

        _storage.instanceIDToData.erase(instanceID);
        _storage.dirtyInstances.erase(instanceID);

        return true;
    }

    bool AnimationSystem::GetCurrentAnimation(InstanceID instanceID, Bone bone, AnimationSequenceState* primary, AnimationSequenceState* sequence)
    {
        if (!HasInstance(instanceID))
        {
            return false;
        }

        AnimationInstance& instance = _storage.instanceIDToData[instanceID];

        i16 boneIndex = GetBoneIndexFromKeyBoneID(instance.modelID, bone);
        if (boneIndex == -1)
            return false;

        const AnimationBoneInstance& boneInstance = instance.bones[boneIndex];

        if (primary)
        {
            if (const AnimationSequenceState* sequenceState = GetSequenceStateForBone(boneInstance))
            {
                *primary = *sequenceState;
            }
            else
            {
                primary->animation = Type::Invalid;
                primary->sequenceID = InvalidSequenceID;
                primary->finishedCallback = nullptr;
                primary->flags = Flag::None;
                primary->blendTimeStart = 0.0f;
                primary->blendTimeEnd = 0.0f;
                primary->progress = 0.0f;
            }
        }
        if (sequence)
        {
            f32 timeToTransition = 0.0f;
            f32 transitionDuration = 0.0f;
            if (const AnimationSequenceState* sequenceState = GetDeferredSequenceStateForBone(boneInstance, timeToTransition, transitionDuration))
            {
                *sequence = *sequenceState;
            }
            else
            {
                sequence->animation = Type::Invalid;
                sequence->sequenceID = InvalidSequenceID;
                sequence->finishedCallback = nullptr;
                sequence->flags = Flag::None;
                sequence->blendTimeStart = 0.0f;
                sequence->blendTimeEnd = 0.0f;
                sequence->progress = 0.0f;
            }
        }

        return true;
    }

    bool AnimationSystem::IsPlaying(InstanceID instanceID, Bone bone, Type animationID)
    {
        if (animationID <= Type::Invalid || animationID >= Type::Count)
        {
            return false;
        }

        AnimationSequenceState currentAnimation = { };
        AnimationSequenceState nextAnimation = { };

        if (!GetCurrentAnimation(instanceID, bone, &currentAnimation, &nextAnimation))
        {
            return false;
        }

        return currentAnimation.animation == animationID || nextAnimation.animation == animationID;
    }

    u32 AnimationSystem::GetSequenceIDForAnimationID(ModelID modelID, Type animationID)
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

    i16 AnimationSystem::GetBoneIndexFromKeyBoneID(ModelID modelID, Bone bone)
    {
        const AnimationSkeleton& skeleton = _storage.skeletons[modelID];
        u32 numBones = static_cast<u32>(skeleton.bones.size());

        if (bone < Bone::Default || bone >= Bone::Count)
            return -1;

        if (bone == Bone::Default)
        {
            bone = Bone::Main;
            if (!skeleton.keyBoneIDToBoneIndex.contains((i16)bone))
            {
                bone = Bone::Root;
            }
        }

        i16 keyBone = (i16)bone;
        if (!skeleton.keyBoneIDToBoneIndex.contains(keyBone))
            return -1;

        i16 boneID = skeleton.keyBoneIDToBoneIndex.at(keyBone);
        if (boneID >= static_cast<i16>(numBones))
            return -1;

        return boneID;
    }

    const AnimationSequenceState* AnimationSystem::GetSequenceStateForBone(const AnimationBoneInstance& boneInstance)
    {
        if (boneInstance.currentAnimation.sequenceID != InvalidSequenceID)
            return &boneInstance.currentAnimation;

        AnimationBoneInstance* parentBoneInstance = boneInstance.parent;
        while (parentBoneInstance)
        {
            if (parentBoneInstance->currentAnimation.sequenceID != InvalidSequenceID)
                return &parentBoneInstance->currentAnimation;

            parentBoneInstance = parentBoneInstance->parent;
        }

        return nullptr;
    }
    const AnimationSequenceState* AnimationSystem::GetDeferredSequenceStateForBone(const AnimationBoneInstance& boneInstance, f32& timeToTransition, f32& transitionDuration)
    {
        if (boneInstance.nextAnimation.sequenceID != InvalidSequenceID)
        {
            timeToTransition = boneInstance.timeToTransition;
            transitionDuration = boneInstance.transitionDuration;

            return &boneInstance.nextAnimation;
        }

        AnimationBoneInstance* parentBoneInstance = boneInstance.parent;
        while (parentBoneInstance)
        {
            if (parentBoneInstance->nextAnimation.sequenceID != InvalidSequenceID)
            {
                timeToTransition = parentBoneInstance->timeToTransition;
                transitionDuration = parentBoneInstance->transitionDuration;

                return &parentBoneInstance->nextAnimation;
            }

            parentBoneInstance = parentBoneInstance->parent;
        }

        return nullptr;
    }
    
    bool AnimationSystem::SetBoneSequence(InstanceID instanceID, Bone bone, Type animationID, Flag flags, BlendOverride blendOverride, AnimationCallback onFinishedCallback)
    {
        bool isEnabled = CVAR_AnimationSystemEnabled.Get();
        if (!isEnabled)
        {
            return false;
        }

        if (!HasInstance(instanceID))
        {
            return false;
        }

        AnimationInstance& instance = _storage.instanceIDToData[instanceID];
        const AnimationSkeleton& skeleton = _storage.skeletons[instance.modelID];

        u32 numBones = static_cast<u32>(skeleton.bones.size());
        if (numBones == 0)
            return false;

        u32 sequenceID = GetSequenceIDForAnimationID(instance.modelID, animationID);
        if (sequenceID == InvalidSequenceID)
            return false;

        i16 boneIndex = 0;

        if (bone != Bone::Default)
        {
            boneIndex = GetBoneIndexFromKeyBoneID(instance.modelID, bone);
            if (boneIndex == -1)
                return false;
        }

        const Model::ComplexModel::AnimationSequence& sequenceInfo = skeleton.sequences[sequenceID];
        AnimationBoneInstance& boneInstance = instance.bones[boneIndex];

        Type currentAnimation = boneInstance.currentAnimation.animation;
        Type nextAnimation = boneInstance.nextAnimation.animation;
        
        auto callback = boneInstance.nextAnimation.finishedCallback;
        
        boneInstance.nextAnimation.animation = animationID;
        boneInstance.nextAnimation.sequenceID = sequenceID;
        boneInstance.nextAnimation.finishedCallback = onFinishedCallback;
        boneInstance.nextAnimation.blendTimeStart = static_cast<f32>(sequenceInfo.blendTimeStart) / 1000.0f;
        boneInstance.nextAnimation.blendTimeEnd = static_cast<f32>(sequenceInfo.blendTimeEnd) / 1000.0f;
        boneInstance.nextAnimation.progress = 0.0f;
        boneInstance.nextAnimation.flags = flags;
        
        if (callback)
        {
            callback(instanceID, nextAnimation, animationID);
        }
        
        bool shouldBlend = false;
        
        u16 blendTimeStartInMS = sequenceInfo.blendTimeStart;

        if (blendOverride == BlendOverride::Auto)
        {
            shouldBlend = sequenceInfo.flags.blendTransition;
        
            if (!shouldBlend && currentAnimation != Type::Invalid)
            {
                Model::ComplexModel::AnimationSequence currentSequenceInfo = skeleton.sequences[boneInstance.currentAnimation.sequenceID];
                shouldBlend |= currentSequenceInfo.flags.blendTransition || currentSequenceInfo.flags.blendTransitionIfActive;
            }
        }
        else if (blendOverride == BlendOverride::Start || blendOverride == BlendOverride::Both)
        {
            shouldBlend = true;
        }

        if (shouldBlend && blendTimeStartInMS == 0)
        {
            blendTimeStartInMS = 150;
        }
        
        f32 blendTimeStart = static_cast<f32>(blendTimeStartInMS) / 1000.0f;
        boneInstance.timeToTransition = blendTimeStart * shouldBlend;
        boneInstance.transitionDuration = boneInstance.timeToTransition;

        return true;
    }
    bool AnimationSystem::SetBoneRotation(InstanceID instanceID, Bone bone, quat& rotation)
    {
        bool isEnabled = CVAR_AnimationSystemEnabled.Get();
        if (!isEnabled)
        {
            return false;
        }

        if (!HasInstance(instanceID))
        {
            return false;
        }

        AnimationInstance& instance = _storage.instanceIDToData[instanceID];
        const AnimationSkeleton& skeleton = _storage.skeletons[instance.modelID];

        u32 numBones = static_cast<u32>(skeleton.bones.size());
        if (numBones == 0)
            return false;

        i16 boneIndex = GetBoneIndexFromKeyBoneID(instance.modelID, bone);
        if (boneIndex == -1)
            return false;

        AnimationBoneInstance& animBone = instance.bones[boneIndex];
        animBone.rotationOffset = rotation;

        return true;
    }

    void AnimationSystem::Update(f32 deltaTime)
    {
        bool isEnabled = CVAR_AnimationSystemEnabled.Get();
        if (!isEnabled)
        {
            return;
        }

        enki::TaskScheduler* taskScheduler = ServiceLocator::GetTaskScheduler();

        u32 numInstances = static_cast<u32>(_storage.instanceIDs.size());
        f32 adjustedDeltaTime = deltaTime * static_cast<f32>(CVAR_AnimationSystemTimeScale.Get());

        enki::TaskSet updateAnimationsTask(numInstances, [&, adjustedDeltaTime](enki::TaskSetPartition range, u32 threadNum)
        {
            bool hasRenderer = HasModelRenderer();

            for (u32 i = range.start; i < range.end; i++)
            {
                InstanceID instanceID = _storage.instanceIDs[i];
                AnimationInstance& instance = _storage.instanceIDToData[instanceID];
                const AnimationSkeleton& skeleton = _storage.skeletons[instance.modelID];

                bool isInstanceDirty = false;

                u32 numGlobalLoops = static_cast<u32>(instance.globalLoops.size());
                for (u32 globalLoopIndex = 0; globalLoopIndex < numGlobalLoops; globalLoopIndex++)
                {
                    AnimationGlobalLoopInstance& globalLoop = instance.globalLoops[globalLoopIndex];

                    if (globalLoop.duration == 0.0f)
                        continue;

                    globalLoop.currentTime = fmod((globalLoop.currentTime + adjustedDeltaTime), globalLoop.duration);

                    if (globalLoop.currentTime >= globalLoop.duration)
                    {
                        globalLoop.currentTime = 0.0f;
                    }
                }

                u32 numBones = static_cast<u32>(skeleton.bones.size());
                for (u32 boneIndex = 0; boneIndex < numBones; boneIndex++)
                {
                    const AnimationSkeletonBone& bone = skeleton.bones[boneIndex];
                    AnimationBoneInstance& boneInstance = instance.bones[boneIndex];
                
                    HandleBoneAnimation(skeleton, instance, bone, boneInstance, adjustedDeltaTime);
                
                    const mat4x4& originalMatrix = _storage.boneMatrices[instance.boneMatrixOffset + boneIndex];

                    mat4x4 boneMatrix = mat4x4(1.0f);

                    if (bone.info.flags.Transformed)
                    {
                        boneMatrix = GetBoneMatrix(skeleton, instance, bone, boneInstance);
                    }
                
                    // Apply parent's transformation
                    if (bone.info.parentBoneID != -1)
                    {
                        const mat4x4& parentBoneMatrix = _storage.boneMatrices[instance.boneMatrixOffset + bone.info.parentBoneID];
                        boneMatrix = mul(boneMatrix, parentBoneMatrix);
                    }
                
                    bool isDirty = originalMatrix != boneMatrix;
                    if (isDirty)
                    {
                        // Store final transformation
                        isInstanceDirty = true;
                        _storage.boneMatrices[instance.boneMatrixOffset + boneIndex] = boneMatrix;
                    }
                }

                if (numBones > 0)
                {
                    const AnimationBoneInstance& boneInstance = instance.bones[0];
                
                    u32 numTextureTransforms = static_cast<u32>(skeleton.textureTransforms.size());
                    for (u32 textureTransformIndex = 0; textureTransformIndex < numTextureTransforms; textureTransformIndex++)
                    {
                        const AnimationSkeletonTextureTransform& textureTransform = skeleton.textureTransforms[textureTransformIndex];
                        mat4x4 textureTransformMatrix = GetTextureTransformMatrix(skeleton, instance, boneInstance, textureTransform);
                
                        const mat4x4& originalMatrix = _storage.textureTransformMatrices[instance.textureTransformMatrixOffset + textureTransformIndex];
                        bool isDirty = originalMatrix != textureTransformMatrix;
                        if (isDirty)
                        {
                            isInstanceDirty = true;
                            _storage.textureTransformMatrices[instance.textureTransformMatrixOffset + textureTransformIndex] = textureTransformMatrix;
                        }
                    }
                }

                if (hasRenderer && isInstanceDirty)
                {
                    _storage.dirtyInstancesMutex.lock();
                    if (!_storage.dirtyInstances.contains(instanceID))
                    {
                        _storage.dirtyInstances.insert(instanceID);
                        _storage.dirtyInstancesQueue.push(instanceID);
                    }
                    _storage.dirtyInstancesMutex.unlock();
                }
            }
        });

        taskScheduler->AddTaskSetToPipe(&updateAnimationsTask);
        taskScheduler->WaitforTask(&updateAnimationsTask);

        bool hasRenderer = HasModelRenderer();
        if (hasRenderer)
        {
            u32 throttle = CVAR_AnimationSystemThrottle.Get();

            u32 numDirty = static_cast<u32>(_storage.dirtyInstances.size());
            u32 numInstancesToUpdate = glm::min(numDirty, throttle);
            u32 numUpdatedInstances = 0;

            while (numUpdatedInstances < numInstancesToUpdate && !_storage.dirtyInstancesQueue.empty())
            {
                InstanceID instanceID = _storage.dirtyInstancesQueue.front();

                if (!_storage.dirtyInstances.contains(instanceID))
                {
                    _storage.dirtyInstancesQueue.pop();
                    continue;
                }

                AnimationInstance& instance = _storage.instanceIDToData[instanceID];
                const AnimationSkeleton& skeleton = _storage.skeletons[instance.modelID];

                u32 numBones = static_cast<u32>(skeleton.bones.size());
                if (numBones > 0)
                {
                    _modelRenderer->SetBoneMatricesAsDirty(instanceID, 0, numBones, &_storage.boneMatrices[instance.boneMatrixOffset]);
                }

                u32 numTextureTransforms = static_cast<u32>(skeleton.textureTransforms.size());
                if (numTextureTransforms > 0)
                {
                    _modelRenderer->SetTextureTransformMatricesAsDirty(instanceID, 0, numTextureTransforms, &_storage.textureTransformMatrices[instance.textureTransformMatrixOffset]);
                }

                _storage.dirtyInstancesQueue.pop();
                _storage.dirtyInstances.erase(instanceID);

                numUpdatedInstances++;
            }
        }
    }

    void AnimationSystem::Reserve(u32 numSkeletons, u32 numInstances, u32 numBones, u32 numTextureTransforms)
    {
        bool isEnabled = CVAR_AnimationSystemEnabled.Get();
        if (!isEnabled)
        {
            return;
        }

        u32 currentNumSkeletons = static_cast<u32>(_storage.skeletons.size());
        _storage.skeletons.reserve(currentNumSkeletons + numSkeletons);

        u32 currentNumInstances = static_cast<u32>(_storage.instanceIDs.size());
        _storage.instanceIDs.resize(currentNumInstances + numInstances);
        _storage.instanceIDToData.reserve(currentNumInstances + numInstances);
        _storage.dirtyInstances.reserve(currentNumInstances + numInstances);

        u32 currentNumBones = static_cast<u32>(_storage.boneMatrices.size());
        _storage.boneMatrices.resize(currentNumBones + numBones);

        for (u32 i = 0; i < numBones; i++)
        {
            _storage.boneMatrices[currentNumBones + i] = mat4x4(1.0f);
        }

        u32 currentNumTextureTransforms = static_cast<u32>(_storage.textureTransformMatrices.size());
        _storage.textureTransformMatrices.resize(currentNumTextureTransforms + numTextureTransforms);

        for (u32 i = 0; i < numTextureTransforms; i++)
        {
            _storage.textureTransformMatrices[currentNumTextureTransforms + i] = mat4x4(1.0f);
        }
    }
    void AnimationSystem::FitToBuffersAfterLoad()
    {
        bool isEnabled = CVAR_AnimationSystemEnabled.Get();
        if (!isEnabled)
        {
            return;
        }

        u32 numInstances = _storage.instancesIndex.load();

        _storage.instanceIDs.resize(numInstances);
        _storage.dirtyInstances.reserve(numInstances);
    }
    void AnimationSystem::Clear()
    {
        _storage.skeletons.clear();

        _storage.instancesIndex.store(0);
        _storage.instanceIDs.clear();
        _storage.instanceIDToData.clear();

        _storage.dirtyInstances.clear();

        _storage.boneMatrixIndex.store(0);
        _storage.boneMatrices.clear();

        _storage.textureTransformMatrixIndex.store(0);
        _storage.textureTransformMatrices.clear();
    }

    void AnimationSystem::HandleCurrentAnimation(const AnimationSkeleton& skeleton, const AnimationInstance& instance, AnimationBoneInstance& boneInstance, f32 deltaTime)
    {
        Type animationID = boneInstance.currentAnimation.animation;
        u32 sequenceID = boneInstance.currentAnimation.sequenceID;
        const Model::ComplexModel::AnimationSequence& sequenceInfo = skeleton.sequences[sequenceID];

        f32 sequenceDuration = static_cast<f32>(sequenceInfo.duration / 1000.0f);
        u32 progress = static_cast<u32>(boneInstance.currentAnimation.progress * 1000.0f);
        bool isInTransitionPhaseBefore = boneInstance.currentAnimation.progress >= sequenceDuration - boneInstance.transitionDuration;
        bool canLoop = HasAnimationFlag(boneInstance.currentAnimation.flags, Flag::Loop);

        if (progress >= sequenceInfo.duration && !canLoop)
        {
            if (!HasAnimationFlag(boneInstance.currentAnimation.flags, Flag::Frozen))
            {
                if (boneInstance.nextAnimation.animation == Type::Invalid)
                {
                    auto callback = boneInstance.currentAnimation.finishedCallback;

                    if (!HasAnimationFlag(boneInstance.currentAnimation.flags, Flag::Freeze))
                    {
                        boneInstance.currentAnimation.animation = Type::Invalid;
                        boneInstance.currentAnimation.sequenceID = InvalidSequenceID;
                        boneInstance.currentAnimation.finishedCallback = nullptr;

                        boneInstance.currentAnimation.flags = Flag::None;
                        boneInstance.currentAnimation.blendTimeStart = 0.0f;
                        boneInstance.currentAnimation.blendTimeEnd = 0.0f;
                        boneInstance.currentAnimation.progress = 0.0f;
                    }
                    else
                    {
                        boneInstance.currentAnimation.finishedCallback = nullptr;
                        SetAnimationFlag(boneInstance.currentAnimation.flags, Flag::Frozen);
                    }

                    if (callback)
                    {
                        callback(instance.instanceID, animationID, Type::Invalid);
                    }
                }
            }
        }
        else
        {
            boneInstance.currentAnimation.progress += deltaTime;

            if (canLoop)
            {
                bool isInTransitionPhaseAfter = boneInstance.currentAnimation.progress >= sequenceDuration - boneInstance.transitionDuration;
                if (!isInTransitionPhaseBefore && isInTransitionPhaseAfter)
                {
                    if (boneInstance.nextAnimation.animation == Type::Invalid)
                    {
                        u16 nextSequenceID = sequenceInfo.nextVariationID;

                        if (nextSequenceID == InvalidSequenceID)
                            nextSequenceID = GetSequenceIDForAnimationID(instance.modelID, animationID);

                        Model::ComplexModel::AnimationSequence nextSequenceInfo = skeleton.sequences[nextSequenceID];

                        boneInstance.nextAnimation = boneInstance.currentAnimation;
                        boneInstance.nextAnimation.sequenceID = nextSequenceID;
                        boneInstance.nextAnimation.progress = 0.0f;

                        bool blendTransition = nextSequenceInfo.flags.blendTransition;
                        f32 blendTimeStart = static_cast<f32>(sequenceInfo.blendTimeStart) / 1000.0f;
                        boneInstance.timeToTransition = blendTimeStart * blendTransition;
                        boneInstance.transitionDuration = boneInstance.timeToTransition;
                    }
                }
            }
        }
    }
    mat4x4 AnimationSystem::GetBoneMatrix(const AnimationSkeleton& skeleton, const AnimationInstance& instance, const AnimationSkeletonBone& bone, const AnimationBoneInstance& boneInstance)
    {
        vec3 translationValue = vec3(0.0f, 0.0f, 0.0f);
        quat rotationValue = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scaleValue = vec3(1.0f, 1.0f, 1.0f);

        const AnimationBoneInstance* translationBoneInstance = nullptr;
        const AnimationBoneInstance* rotationBoneInstance = nullptr;
        const AnimationBoneInstance* scaleBoneInstance = nullptr;

        bool isTranslationGlobalLoop = bone.info.translation.globalLoopIndex != -1;
        bool isRotationGlobalLoop = bone.info.rotation.globalLoopIndex != -1;
        bool isScaleGlobalLoop = bone.info.scale.globalLoopIndex != -1;

        // Primary Sequence
        {
            const AnimationBoneInstance& rootBoneInstance = instance.bones[0];

            // Handle Translation
            {
                translationBoneInstance = &boneInstance;

                if (isTranslationGlobalLoop || translationBoneInstance->currentAnimation.animation == Type::Invalid && translationBoneInstance->nextAnimation.animation == Type::Invalid)
                {
                    translationBoneInstance = &rootBoneInstance;
                }

                u32 sequenceID = translationBoneInstance->currentAnimation.sequenceID;
                if (isTranslationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < bone.info.translation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.translation.tracks[sequenceID];

                    f32 progress = translationBoneInstance->currentAnimation.progress;
                    f32 maxDuration = 0.0f;

                    if (bone.info.translation.globalLoopIndex != -1)
                    {
                        const AnimationGlobalLoopInstance& globalLoop = instance.globalLoops[bone.info.translation.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        translationValue = InterpolateKeyframe(track, progress);
                }
            }

            // Handle Rotation
            {
                rotationBoneInstance = &boneInstance;

                if (isRotationGlobalLoop || rotationBoneInstance->currentAnimation.animation == Type::Invalid && rotationBoneInstance->nextAnimation.animation == Type::Invalid)
                {
                    rotationBoneInstance = &rootBoneInstance;
                }

                u32 sequenceID = rotationBoneInstance->currentAnimation.sequenceID;
                if (isRotationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < bone.info.rotation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<quat>& track = bone.info.rotation.tracks[sequenceID];

                    f32 progress = rotationBoneInstance->currentAnimation.progress;

                    if (bone.info.rotation.globalLoopIndex != -1)
                    {
                        const AnimationGlobalLoopInstance& globalLoop = instance.globalLoops[bone.info.rotation.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                    {
                        rotationValue = InterpolateKeyframe(track, progress);
                    }
                }
            }

            // Handle Scale
            {
                scaleBoneInstance = &boneInstance;

                if (isScaleGlobalLoop || scaleBoneInstance->currentAnimation.animation == Type::Invalid && scaleBoneInstance->nextAnimation.animation == Type::Invalid)
                {
                    scaleBoneInstance = &rootBoneInstance;
                }

                u32 sequenceID = scaleBoneInstance->currentAnimation.sequenceID;
                if (isScaleGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < bone.info.scale.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.scale.tracks[sequenceID];

                    f32 progress = scaleBoneInstance->currentAnimation.progress;

                    if (bone.info.scale.globalLoopIndex != -1)
                    {
                        const AnimationGlobalLoopInstance& globalLoop = instance.globalLoops[bone.info.scale.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        scaleValue = InterpolateKeyframe(track, progress);
                }
            }
        }

        // Transition Sequence
        {
            if (translationBoneInstance && !isTranslationGlobalLoop)
            {
                f32 timeToTransition = translationBoneInstance->timeToTransition;
                f32 transitionDuration = translationBoneInstance->transitionDuration;
                f32 transitionProgress = timeToTransition ? glm::clamp(1.0f - (timeToTransition / transitionDuration), 0.0f, 1.0f) : 1.0f;

                if (transitionDuration > 0.0f)
                {
                    u32 sequenceID = translationBoneInstance->nextAnimation.sequenceID;
                    if (sequenceID < bone.info.translation.tracks.size())
                    {
                        const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.translation.tracks[sequenceID];

                        if (track.values.size() > 0 && track.timestamps.size() > 0)
                        {
                            f32 progress = translationBoneInstance->nextAnimation.progress;

                            vec3 translation = InterpolateKeyframe(track, progress);
                            translationValue = glm::mix(translationValue, translation, transitionProgress);
                        }
                    }
                }
            }

            if (rotationBoneInstance && !isRotationGlobalLoop)
            {
                f32 timeToTransition = rotationBoneInstance->timeToTransition;
                f32 transitionDuration = rotationBoneInstance->transitionDuration;
                f32 transitionProgress = timeToTransition ? glm::clamp(1.0f - (timeToTransition / transitionDuration), 0.0f, 1.0f) : 1.0f;

                if (transitionDuration > 0.0f)
                {
                    u32 sequenceID = rotationBoneInstance->nextAnimation.sequenceID;
                    if (sequenceID < bone.info.rotation.tracks.size())
                    {
                        const Model::ComplexModel::AnimationTrack<quat>& track = bone.info.rotation.tracks[sequenceID];

                        if (track.values.size() > 0 && track.timestamps.size() > 0)
                        {
                            f32 progress = rotationBoneInstance->nextAnimation.progress;

                            quat rotation = InterpolateKeyframe(track, progress);
                            rotationValue = glm::slerp(rotationValue, rotation, transitionProgress);
                        }
                    }
                }
            }

            if (scaleBoneInstance && !isScaleGlobalLoop)
            {
                f32 timeToTransition = scaleBoneInstance->timeToTransition;
                f32 transitionDuration = scaleBoneInstance->transitionDuration;
                f32 transitionProgress = timeToTransition ? glm::clamp(1.0f - (timeToTransition / transitionDuration), 0.0f, 1.0f) : 1.0f;

                if (transitionDuration > 0.0f)
                {
                    u32 sequenceID = scaleBoneInstance->nextAnimation.sequenceID;
                    if (sequenceID < bone.info.scale.tracks.size())
                    {
                        const Model::ComplexModel::AnimationTrack<vec3>& track = bone.info.scale.tracks[sequenceID];

                        if (track.values.size() > 0 && track.timestamps.size() > 0)
                        {
                            f32 progress = scaleBoneInstance->nextAnimation.progress;

                            vec3 scale = InterpolateKeyframe(track, progress);
                            scaleValue = glm::mix(scaleValue, scale, transitionProgress);
                        }
                    }
                }
            }
        }

        rotationValue = glm::normalize(boneInstance.rotationOffset * glm::normalize(rotationValue));

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

    void AnimationSystem::HandleBoneAnimation(const AnimationSkeleton& skeleton, const AnimationInstance& instance, const AnimationSkeletonBone& bone, AnimationBoneInstance& boneInstance, f32 deltaTime)
    {
        if (boneInstance.currentAnimation.animation != Type::Invalid)
        {
            HandleCurrentAnimation(skeleton, instance, boneInstance, deltaTime);
        }

        // Check if we need to transition
        if (boneInstance.nextAnimation.animation != Type::Invalid)
        {
            if (boneInstance.timeToTransition <= 0.0f || boneInstance.currentAnimation.animation == Type::Invalid)
            {
                Type currentAnimationID = boneInstance.currentAnimation.animation;
                auto callback = boneInstance.currentAnimation.finishedCallback;

                boneInstance.timeToTransition = 0.0f;
                boneInstance.transitionDuration = 0.0f;
                boneInstance.currentAnimation = boneInstance.nextAnimation;

                // Reset transition state
                boneInstance.nextAnimation.animation = Type::Invalid;
                boneInstance.nextAnimation.sequenceID = InvalidSequenceID;
                boneInstance.nextAnimation.finishedCallback = nullptr;

                boneInstance.nextAnimation.flags = Flag::None;
                boneInstance.nextAnimation.blendTimeStart = 0.0f;
                boneInstance.nextAnimation.blendTimeEnd = 0.0f;
                boneInstance.nextAnimation.progress = 0.0f;

                if (callback)
                {
                    callback(instance.instanceID, currentAnimationID, boneInstance.currentAnimation.animation);
                    callback = nullptr;
                }

                HandleCurrentAnimation(skeleton, instance, boneInstance, deltaTime);
            }
            else
            {
                // Progress the next animation

                Type animationID = boneInstance.nextAnimation.animation;
                u32 sequenceID = GetSequenceIDForAnimationID(instance.modelID, animationID);
                Model::ComplexModel::AnimationSequence sequenceInfo = skeleton.sequences[sequenceID];

                u32 progress = static_cast<u32>(boneInstance.nextAnimation.progress * 1000.0f);
                if (progress < sequenceInfo.duration)
                {
                    f32 sequenceDuration = static_cast<f32>(sequenceInfo.duration / 1000.0f);
                    f32 maxDiff = sequenceDuration - boneInstance.nextAnimation.progress;

                    boneInstance.nextAnimation.progress += glm::clamp(deltaTime, 0.0f, maxDiff);
                }

                boneInstance.timeToTransition -= glm::clamp(deltaTime, 0.0f, boneInstance.timeToTransition);
            }
        }
    }
    mat4x4 AnimationSystem::GetTextureTransformMatrix(const AnimationSkeleton& skeleton, const AnimationInstance& instance, const AnimationBoneInstance& boneInstance, const AnimationSkeletonTextureTransform& textureTransform)
    {
        mat4x4 matrix = mat4x4(1.0f);
        vec3 translationValue = vec3(0.0f, 0.0f, 0.0f);
        quat rotationValue = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scaleValue = vec3(1.0f, 1.0f, 1.0f);

        const AnimationBoneInstance* translationBoneInstance = nullptr;
        const AnimationBoneInstance* rotationBoneInstance = nullptr;
        const AnimationBoneInstance* scaleBoneInstance = nullptr;

        bool isTranslationGlobalLoop = textureTransform.info.translation.globalLoopIndex != -1;
        bool isRotationGlobalLoop = textureTransform.info.rotation.globalLoopIndex != -1;
        bool isScaleGlobalLoop = textureTransform.info.scale.globalLoopIndex != -1;

        // Primary Sequence
        {
            const AnimationBoneInstance& rootBoneInstance = instance.bones[0];

            // Handle Translation
            {
                translationBoneInstance = &boneInstance;

                if (isTranslationGlobalLoop || translationBoneInstance->currentAnimation.animation == Type::Invalid && translationBoneInstance->nextAnimation.animation == Type::Invalid)
                {
                    translationBoneInstance = &rootBoneInstance;
                }

                u32 sequenceID = translationBoneInstance->currentAnimation.sequenceID;
                if (isTranslationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < textureTransform.info.translation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = textureTransform.info.translation.tracks[sequenceID];

                    f32 progress = translationBoneInstance->currentAnimation.progress;

                    if (isTranslationGlobalLoop)
                    {
                        const AnimationGlobalLoopInstance& globalLoop = instance.globalLoops[textureTransform.info.translation.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        translationValue = InterpolateKeyframe(track, progress);
                }
            }

            // Handle Rotation
            {
                rotationBoneInstance = &boneInstance;

                if (isRotationGlobalLoop || rotationBoneInstance->currentAnimation.animation == Type::Invalid && rotationBoneInstance->nextAnimation.animation == Type::Invalid)
                {
                    rotationBoneInstance = &rootBoneInstance;
                }

                u32 sequenceID = rotationBoneInstance->currentAnimation.sequenceID;
                if (isRotationGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < textureTransform.info.rotation.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<quat>& track = textureTransform.info.rotation.tracks[sequenceID];

                    f32 progress = rotationBoneInstance->currentAnimation.progress;

                    if (isRotationGlobalLoop)
                    {
                        const AnimationGlobalLoopInstance& globalLoop = instance.globalLoops[textureTransform.info.rotation.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        rotationValue = InterpolateKeyframe(track, progress);
                }
            }

            // Handle Scale
            {
                scaleBoneInstance = &boneInstance;

                if (isScaleGlobalLoop || scaleBoneInstance->currentAnimation.animation == Type::Invalid && scaleBoneInstance->nextAnimation.animation == Type::Invalid)
                {
                    scaleBoneInstance = &rootBoneInstance;
                }

                u32 sequenceID = scaleBoneInstance->currentAnimation.sequenceID;
                if (isScaleGlobalLoop)
                {
                    sequenceID = 0;
                }

                if (sequenceID < textureTransform.info.scale.tracks.size())
                {
                    const Model::ComplexModel::AnimationTrack<vec3>& track = textureTransform.info.scale.tracks[sequenceID];

                    f32 progress = scaleBoneInstance->currentAnimation.progress;

                    if (isScaleGlobalLoop)
                    {
                        const AnimationGlobalLoopInstance& globalLoop = instance.globalLoops[textureTransform.info.scale.globalLoopIndex];
                        progress = globalLoop.currentTime;
                    }

                    if (track.values.size() > 0 && track.timestamps.size() > 0)
                        scaleValue = InterpolateKeyframe(track, progress);
                }
            }
        }

        // Transition Sequence
        {
            if (translationBoneInstance && !isTranslationGlobalLoop)
            {
                f32 timeToTransition = translationBoneInstance->timeToTransition;
                f32 transitionDuration = translationBoneInstance->transitionDuration;
                f32 transitionProgress = timeToTransition ? glm::clamp(1.0f - (timeToTransition / transitionDuration), 0.0f, 1.0f) : 1.0f;

                if (transitionDuration > 0.0f)
                {
                    u32 sequenceID = translationBoneInstance->nextAnimation.sequenceID;
                    if (sequenceID < textureTransform.info.translation.tracks.size())
                    {
                        const Model::ComplexModel::AnimationTrack<vec3>& track = textureTransform.info.translation.tracks[sequenceID];

                        if (track.values.size() > 0 && track.timestamps.size() > 0)
                        {
                            f32 progress = translationBoneInstance->nextAnimation.progress;

                            vec3 translation = InterpolateKeyframe(track, progress);
                            translationValue = glm::mix(translationValue, translation, transitionProgress);
                        }
                    }
                }
            }

            if (rotationBoneInstance && !isRotationGlobalLoop)
            {
                f32 timeToTransition = rotationBoneInstance->timeToTransition;
                f32 transitionDuration = rotationBoneInstance->transitionDuration;
                f32 transitionProgress = timeToTransition ? glm::clamp(1.0f - (timeToTransition / transitionDuration), 0.0f, 1.0f) : 1.0f;

                if (transitionDuration > 0.0f)
                {
                    u32 sequenceID = rotationBoneInstance->nextAnimation.sequenceID;
                    if (sequenceID < textureTransform.info.rotation.tracks.size())
                    {
                        const Model::ComplexModel::AnimationTrack<quat>& track = textureTransform.info.rotation.tracks[sequenceID];

                        if (track.values.size() > 0 && track.timestamps.size() > 0)
                        {
                            f32 progress = rotationBoneInstance->nextAnimation.progress;

                            quat rotation = InterpolateKeyframe(track, progress);
                            rotationValue = glm::mix(rotationValue, rotation, transitionProgress);
                        }
                    }
                }
            }

            if (scaleBoneInstance && !isScaleGlobalLoop)
            {
                f32 timeToTransition = scaleBoneInstance->timeToTransition;
                f32 transitionDuration = scaleBoneInstance->transitionDuration;
                f32 transitionProgress = timeToTransition ? glm::clamp(1.0f - (timeToTransition / transitionDuration), 0.0f, 1.0f) : 1.0f;

                if (transitionDuration > 0.0f)
                {
                    u32 sequenceID = scaleBoneInstance->nextAnimation.sequenceID;
                    if (sequenceID < textureTransform.info.scale.tracks.size())
                    {
                        const Model::ComplexModel::AnimationTrack<vec3>& track = textureTransform.info.scale.tracks[sequenceID];

                        if (track.values.size() > 0 && track.timestamps.size() > 0)
                        {
                            f32 progress = scaleBoneInstance->nextAnimation.progress;

                            vec3 scale = InterpolateKeyframe(track, progress);
                            scaleValue = glm::mix(scaleValue, scale, transitionProgress);
                        }
                    }
                }
            }
        }

        rotationValue = glm::normalize(boneInstance.rotationOffset * rotationValue);

        const vec3 pivot = vec3(0.5f, 0.5f, 0.5f);
        matrix = mul(glm::translate(mat4x4(1.0f), pivot), matrix);

        mat4x4 translationMatrix = glm::translate(mat4x4(1.0f), translationValue);
        mat4x4 rotationMatrix = glm::toMat4(glm::normalize(rotationValue));
        mat4x4 scaleMatrix = glm::scale(mat4x4(1.0f), scaleValue);

        matrix = mul(translationMatrix, matrix);
        matrix = mul(rotationMatrix, matrix);
        matrix = mul(scaleMatrix, matrix);

        matrix = mul(glm::translate(mat4x4(1.0f), -pivot), matrix);

        return matrix;
    }
}