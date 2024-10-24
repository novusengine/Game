#pragma once
#include "Game-Lib/Animation/AnimationDefines.h"

#include <Base/Types.h>

namespace ECS
{
    namespace Components
    {
        struct AnimationInitData
        {
            struct Flags
            {
                u8 shouldInitialize : 1 = 0;
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
            std::vector<Animation::AnimationGlobalLoop> globalLoops;
            std::vector<Animation::AnimationBoneInstance> boneInstances;
            std::vector<Animation::AnimationState> animationStates;

            std::vector<mat4x4> boneTransforms;
            std::vector<mat4x4> textureTransforms;
            std::vector<mat4x4> proceduralBoneTransforms;
        };
    }
}