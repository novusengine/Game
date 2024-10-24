#pragma once
#include "AnimationDefines.h"

#include <Base/Types.h>

#include <FileFormat/Novus/Model/ComplexModel.h>

#include <entt/entt.hpp>
#include <robinhood/robinhood.h>

#include <limits>

namespace Model
{
    struct ComplexModel;
}
class ModelRenderer;

namespace Animation
{
    struct AnimationSkeletonBone
    {
    public:
        Model::ComplexModel::Bone info;
    };
    struct AnimationSkeletonTextureTransform
    {
    public:
        Model::ComplexModel::TextureTransform info;
    };
    struct AnimationSkeleton
    {
    public:
        static constexpr AnimationModelID InvalidID = std::numeric_limits<AnimationModelID>().max();

        u32 modelID = InvalidID;

        std::vector<u32> globalLoops;
        std::vector<Model::ComplexModel::AnimationSequence> sequences;
        std::vector<AnimationSkeletonBone> bones;
        std::vector<AnimationSkeletonTextureTransform> textureTransforms;

        robin_hood::unordered_map<AnimationType, u16> animationIDToFirstSequenceID;
        robin_hood::unordered_map<u16, i16> keyBoneIDToBoneIndex;
    };

    struct AnimationStorage
    {
    public:
        robin_hood::unordered_map<AnimationModelID, AnimationSkeleton> skeletons;
    };

    class AnimationSystem
    {
    public:
        AnimationSystem();
        
        bool IsEnabled();

        void Reserve(u32 numSkeletons);
        void Clear(entt::registry& registry);

        bool HasSkeleton(AnimationModelID modelID) { return _storage.skeletons.contains(modelID); }
        bool AddSkeleton(AnimationModelID modelID, Model::ComplexModel& model);
        
        i16 GetBoneIndexFromKeyBoneID(AnimationModelID modelID, AnimationBone bone);
        u16 GetSequenceIDForAnimationID(AnimationModelID modelID, AnimationType animationID);

        AnimationStorage& GetStorage() { return _storage; }

    private:
        AnimationStorage _storage;
    };
}