#pragma once

#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"

#include <FileFormat/Novus/Model/Material.h>

#include <robinhood/robinhood.h>

#include <memory>
#include <span>
#include <vector>

namespace PACT { class PactStorage; }

namespace MaterialLoading
{
    class MaterialStorage;

    // Owns CPU-side material-animation assets and writes sampled values into mutable GPU-backed MaterialInstance parameters.
    // This keeps texture and parameter animation independent from skeletal pose evaluation.
    class MaterialAnimator
    {
      public:
        MaterialAnimator(PACT::PactStorage* pactStorage, MaterialStorage* storage);

        void Register(RenderAssets::MaterialInstanceHandle materialInstance, std::span<const FileFormat::Material::MaterialAnimationBinding> bindings);
        void Update(f32 deltaTime);

      private:
        struct AnimationAsset
        {
            FileFormat::Material::MaterialAnimationAsset root;
            std::vector<FileFormat::Material::MaterialAnimationTrack> tracks;
            std::vector<vec4> samples;
        };

        struct Binding
        {
            std::shared_ptr<const AnimationAsset> animation;
            u32 parameterOffset = 0;
            u16 trackIndex = 0;
            FileFormat::Material::AnimationTimeSource timeSource = FileFormat::Material::AnimationTimeSource::SharedClock;
            u8 flags = FileFormat::Material::MaterialAnimationBindingFlags_None;
        };

        struct AnimatedInstance
        {
            RenderAssets::MaterialInstanceHandle materialInstance;
            std::vector<Binding> bindings;
            std::vector<vec4> values;
            f32 stableTime = 0.0f;
        };

        std::shared_ptr<const AnimationAsset> Load(FileFormat::AssetID assetID);
        static vec4 Sample(const AnimationAsset& animation, const FileFormat::Material::MaterialAnimationTrack& track, f32 time, bool looping);

        PACT::PactStorage* _pactStorage = nullptr;
        MaterialStorage* _storage = nullptr;
        robin_hood::unordered_map<FileFormat::AssetID, std::shared_ptr<const AnimationAsset>> _animations;
        std::vector<AnimatedInstance> _instances;
        f32 _sharedTime = 0.0f;
    };
} // namespace MaterialLoading
