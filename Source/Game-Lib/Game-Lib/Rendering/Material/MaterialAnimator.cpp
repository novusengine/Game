#include "MaterialAnimator.h"

#include "MaterialAssetReader.h"
#include "MaterialStorage.h"

#include <Base/Util/DebugHandler.h>

#include <Filesystem/PactStorage.h>

#include <algorithm>
#include <cmath>

namespace MaterialLoading
{
    MaterialAnimator::MaterialAnimator(PACT::PactStorage* pactStorage, MaterialStorage* storage)
        : _pactStorage(pactStorage), _storage(storage)
    {
    }

    void MaterialAnimator::Register(RenderAssets::MaterialInstanceHandle materialInstance, std::span<const FileFormat::Material::MaterialAnimationBinding> bindings)
    {
        if (bindings.empty())
            return;
        AnimatedInstance instance;
        instance.materialInstance = materialInstance;
        instance.bindings.reserve(bindings.size());
        for (const FileFormat::Material::MaterialAnimationBinding& source : bindings)
        {
            if (source.timeSource == FileFormat::Material::AnimationTimeSource::AnimationController)
                continue;
            std::shared_ptr<const AnimationAsset> animation = Load(source.materialAnimationAssetID);
            if (!animation || source.trackIndex >= animation->tracks.size())
            {
                NC_LOG_ERROR("MATERIAL_ANIMATION binding_failed asset={} track={}", source.materialAnimationAssetID, source.trackIndex);
                continue;
            }
            instance.bindings.push_back({std::move(animation), source.parameterByteOffset, source.trackIndex, source.timeSource, source.flags});
        }
        std::sort(instance.bindings.begin(), instance.bindings.end(), [](const Binding& left, const Binding& right)
        {
            return left.parameterOffset < right.parameterOffset;
        });
        instance.values.resize(instance.bindings.size());
        if (!instance.bindings.empty())
            _instances.push_back(std::move(instance));
    }

    void MaterialAnimator::Update(f32 deltaTime)
    {
        _sharedTime += deltaTime;
        for (AnimatedInstance& instance : _instances)
        {
            instance.stableTime += deltaTime;
            for (u32 bindingIndex = 0; bindingIndex < instance.bindings.size(); ++bindingIndex)
            {
                const Binding& binding = instance.bindings[bindingIndex];
                const f32 time = binding.timeSource == FileFormat::Material::AnimationTimeSource::SharedClock ? _sharedTime : instance.stableTime;
                const FileFormat::Material::MaterialAnimationTrack& track = binding.animation->tracks[binding.trackIndex];
                const bool looping = (binding.flags & FileFormat::Material::MaterialAnimationBindingFlags_Looping) != 0 ||
                    (track.flags & FileFormat::Material::MaterialAnimationTrackFlags_Looping) != 0;
                instance.values[bindingIndex] = Sample(*binding.animation, track, time, looping);
            }

            for (u32 first = 0; first < instance.bindings.size();)
            {
                u32 end = first + 1;
                while (end < instance.bindings.size() && instance.bindings[end].parameterOffset == instance.bindings[end - 1].parameterOffset + sizeof(vec4))
                    ++end;
                const std::span<const vec4> values(instance.values.data() + first, end - first);
                _storage->WriteMaterialParameters(instance.materialInstance, instance.bindings[first].parameterOffset,
                    {reinterpret_cast<const u8*>(values.data()), values.size_bytes()});
                first = end;
            }
        }
    }

    std::shared_ptr<const MaterialAnimator::AnimationAsset> MaterialAnimator::Load(FileFormat::AssetID assetID)
    {
        const auto existing = _animations.find(assetID);
        if (existing != _animations.end())
            return existing->second;
        PACT::PactFileHandle file;
        if (_pactStorage->ReadFile(assetID, file) != PACT::PactReadResult::Success)
            return {};
        const std::span<const u8> payload(reinterpret_cast<const u8*>(file.GetData()), file.GetSize());
        const MaterialAssetReadResult<MaterialAnimationAssetView> result = MaterialAssetReader::ReadMaterialAnimation(payload);
        if (!result)
            return {};
        auto animation = std::make_shared<AnimationAsset>();
        animation->root = result.view.root;
        animation->tracks.assign(result.view.tracks.begin(), result.view.tracks.end());
        animation->samples.assign(result.view.samples.begin(), result.view.samples.end());
        _animations.emplace(assetID, animation);
        return animation;
    }

    vec4 MaterialAnimator::Sample(const AnimationAsset& animation, const FileFormat::Material::MaterialAnimationTrack& track, f32 time, bool looping)
    {
        if (track.mode == FileFormat::Material::MaterialAnimationMode::Constant)
            return track.baseValue;
        if (track.mode == FileFormat::Material::MaterialAnimationMode::LinearRate)
            return track.baseValue + track.ratePerSecond * time;
        if (track.numSamples == 0 || track.sampleRateHz == 0)
            return track.baseValue;

        f32 sampleTime = time;
        if (looping && animation.root.durationSeconds > 0.0f)
            sampleTime = std::fmod(std::max(sampleTime, 0.0f), animation.root.durationSeconds);
        const f32 samplePosition = std::max(sampleTime, 0.0f) * track.sampleRateHz;
        const u32 left = std::min(static_cast<u32>(samplePosition), track.numSamples - 1u);
        const u32 right = left + 1u < track.numSamples ? left + 1u : looping ? 0u : left;
        const f32 factor = left == right ? 0.0f : samplePosition - std::floor(samplePosition);
        return glm::mix(animation.samples[track.sampleOffset + left], animation.samples[track.sampleOffset + right], factor);
    }
} // namespace MaterialLoading
