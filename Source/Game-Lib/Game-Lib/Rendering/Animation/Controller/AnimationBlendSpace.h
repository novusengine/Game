#pragma once
#include "Game-Lib/Rendering/Animation/Controller/AnimationSetResolver.h"

#include <array>
#include <limits>
#include <span>

namespace Animation
{
    struct BlendSpaceSample1D
    {
        f32 position = 0.0f;
        FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
    };

    struct BlendSpaceSample2D
    {
        vec2 position = {};
        FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
    };

    struct BlendSpaceTriangle
    {
        std::array<u16, 3> samples = {};
    };

    enum class BlendSpaceFallback : u8
    {
        None,
        Clamped,
        Wrapped,
        NearestSample,
        Empty,
        DuplicateSamples,
        DegenerateTriangle,
        Hole
    };

    struct BlendSpaceWeight
    {
        FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
        f32 weight = 0.0f;
        u16 sampleIndex = 0;
    };

    struct BlendSpaceResult
    {
        std::array<BlendSpaceWeight, 3> active = {};
        u32 activeCount = 0;
        u32 triangleIndex = std::numeric_limits<u32>::max();
        BlendSpaceFallback fallback = BlendSpaceFallback::None;
    };

    // Evaluates 1D and triangulated 2D blend spaces into bounded clip weights.
    // Explicit fallback results expose clamping, authored holes, and degenerate layouts.
    BlendSpaceResult EvaluateBlendSpace1D(std::span<const BlendSpaceSample1D> samples, f32 input, bool wrap = false, f32 wrapMinimum = 0.0f, f32 wrapMaximum = 0.0f);
    BlendSpaceResult EvaluateBlendSpace2D(std::span<const BlendSpaceSample2D> samples, std::span<const BlendSpaceTriangle> triangles, vec2 input, bool wrapX = false, f32 wrapMinimum = 0.0f, f32 wrapMaximum = 0.0f);
    f32 AdvanceSmoothedInput(f32 current, f32 target, f32 halfLifeSeconds, f32 deltaSeconds);

    struct SynchronizationInput
    {
        FileFormat::AssetID clipAssetID = FileFormat::INVALID_ASSET_ID;
        f32 influence = 0.0f;
        u64 timeMicroseconds = 0;
        u64 durationMicroseconds = 0;
        std::span<const FileFormat::Animation::SynchronizationMarker> markers;
    };

    struct SynchronizationResult
    {
        u32 leaderIndex = std::numeric_limits<u32>::max();
        std::array<u64, 3> mappedTimesMicroseconds = {};
        std::array<f32, 3> playbackRates = {1.0f, 1.0f, 1.0f};
        u32 sampleCount = 0;
        u64 previousMarkerSemanticID = 0;
        u64 nextMarkerSemanticID = 0;
        bool normalizedPhaseFallback = false;
        bool degenerateMarkerFallback = false;
    };

    // Keeps contributing clips aligned as the active blend-space samples change.
    SynchronizationResult SynchronizeSamples(std::span<const SynchronizationInput> samples, FileFormat::AssetID previousLeader, f32 leaderHysteresis, f32 correctionWindowSeconds, f32 minimumRate = 0.8f, f32 maximumRate = 1.2f);
} // namespace Animation
