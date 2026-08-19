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
                const f64 time = binding.timeSource == FileFormat::Material::AnimationTimeSource::SharedClock ? _sharedTime : instance.stableTime;
                const FileFormat::Material::MaterialAnimationTrack& track = binding.animation->tracks[binding.trackIndex];
                const bool looping = (binding.flags & FileFormat::Material::MaterialAnimationBindingFlags_Looping) != 0 ||
                    (track.flags & FileFormat::Material::MaterialAnimationTrackFlags_Looping) != 0;
                instance.values[bindingIndex] = Sample(*binding.animation, track, time, looping);
            }

            for (u32 bindingIndex = 0; bindingIndex < instance.bindings.size(); ++bindingIndex)
            {
                const Binding& binding = instance.bindings[bindingIndex];
                const FileFormat::Material::MaterialAnimationTrack& track = binding.animation->tracks[binding.trackIndex];
                const size_t byteCount = static_cast<size_t>(track.componentCount) * sizeof(f32);
                _storage->WriteMaterialParameters(instance.materialInstance, binding.parameterOffset, {reinterpret_cast<const u8*>(&instance.values[bindingIndex]), byteCount});
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

    vec4 MaterialAnimator::Sample(const AnimationAsset& animation, const FileFormat::Material::MaterialAnimationTrack& track, f64 time, bool looping)
    {
        if (track.mode == FileFormat::Material::MaterialAnimationMode::Constant)
            return track.baseValue;
        if (track.mode == FileFormat::Material::MaterialAnimationMode::LinearRate)
            return track.baseValue + track.ratePerSecond * static_cast<f32>(time);
        if (track.numSamples == 0 || track.sampleRateHz == 0)
            return track.baseValue;

        f64 sampleTime = time;
        if (looping && animation.root.durationSeconds > 0.0f)
            sampleTime = std::fmod(std::max(sampleTime, 0.0), static_cast<f64>(animation.root.durationSeconds));
        const f64 samplePosition = std::max(sampleTime, 0.0) * static_cast<f64>(track.sampleRateHz);
        const u32 last = track.numSamples - 1u;
        const u32 left = std::min(static_cast<u32>(samplePosition), last);
        const u32 right = left < last ? left + 1u : looping ? 0u : left;
        f32 factor = left == right ? 0.0f : static_cast<f32>(samplePosition - std::floor(samplePosition));
        if (looping && left == last)
        {
            const f64 loopEnd = static_cast<f64>(animation.root.durationSeconds) * static_cast<f64>(track.sampleRateHz);
            if (loopEnd > static_cast<f64>(last))
            {
                const f64 loopFactor = (samplePosition - static_cast<f64>(last)) / (loopEnd - static_cast<f64>(last));
                factor = static_cast<f32>(std::clamp(loopFactor, 0.0, 1.0));
            }
        }
        return glm::mix(animation.samples[track.sampleOffset + left], animation.samples[track.sampleOffset + right], factor);
    }
} // namespace MaterialLoading
