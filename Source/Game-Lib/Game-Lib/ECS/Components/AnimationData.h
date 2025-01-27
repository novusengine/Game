#pragma once
#include "Game-Lib/Gameplay/Animation/Defines.h"

#include <Base/Types.h>

namespace ECS
{
    namespace Components
    {
        struct AnimationInitData
        {
            struct Flags
            {
                u8 isDynamic : 1 = 0;
            };

            Flags flags;
        };

        struct AnimationStaticInstance
        {
        public:
            u32 boneMatrixOffset = std::numeric_limits<u32>().max();
            u32 textureMatrixOffset = std::numeric_limits<u32>().max();
        };

        struct AnimationData
        {
        public:
            u32 modelHash = std::numeric_limits<u32>().max();

            std::vector<::Animation::Defines::GlobalLoop> globalLoops;
            std::vector<::Animation::Defines::BoneInstance> boneInstances;
            std::vector<::Animation::Defines::State> animationStates;

            std::vector<mat4x4> boneTransforms;
            std::vector<mat4x4> textureTransforms;
            std::vector<quat> proceduralRotationOffsets;
        };


        // ANIM UNIT SYSTEM
        struct AnimationBoneSet
        {
        public:
            u32 id;

            u8 numBones;
            ::Animation::Defines::Bone bone;
        };

        struct AnimationPlayConfig
        {
        public:
            struct Flags
            {
                u32 IgnoreFallback : 1;
                u32 PlayOnAllChildren : 1;
                u32 SkipIfPartiallySuppressedByPriority : 1;
                u32 SkipIfEntirelySuppressedByPriority : 1;
                u32 PlayFromStartWhenNotSuppressed : 1;

            };

            Flags flags;
        };

        struct AnimationPlayInfo
        {
        public:
            ::Animation::Defines::Type type;
            u16 boneSetID;
            u16 configID;

            u8 index;
            u8 priority;
            i8 forcedVariation;
            u8 blendTimeIn;
            u8 blendTimeOut;

            f32 speed;
        };
    }
}