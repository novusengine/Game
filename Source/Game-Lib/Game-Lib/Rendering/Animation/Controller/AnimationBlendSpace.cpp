#include "AnimationBlendSpace.h"

#include <Base/Math/Geometry.h>
#include <Base/Math/Math.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Animation
{
    BlendSpaceResult EvaluateBlendSpace1D(std::span<const BlendSpaceSample1D> samples, f32 input, bool wrap, f32 wrapMinimum, f32 wrapMaximum)
    {
        BlendSpaceResult result;
        if (samples.empty() || !std::isfinite(input))
        {
            result.fallback = BlendSpaceFallback::Empty;
            return result;
        }
        for (u32 index = 1; index < samples.size(); ++index)
            if (!(samples[index].position > samples[index - 1].position))
            {
                result.fallback = BlendSpaceFallback::DuplicateSamples;
                return result;
            }
        if (wrap)
        {
            input = Math::Wrap(input, wrapMinimum, wrapMaximum);
            result.fallback = BlendSpaceFallback::Wrapped;
        }
        if (input <= samples.front().position || samples.size() == 1)
        {
            result.active[0] = {samples.front().clipAssetID, 1.0f, 0};
            result.activeCount = 1;
            if (!wrap && input < samples.front().position)
                result.fallback = BlendSpaceFallback::Clamped;
            return result;
        }
        if (input >= samples.back().position)
        {
            result.active[0] = {samples.back().clipAssetID, 1.0f, static_cast<u16>(samples.size() - 1)};
            result.activeCount = 1;
            if (!wrap && input > samples.back().position)
                result.fallback = BlendSpaceFallback::Clamped;
            return result;
        }
        for (u16 index = 0; index + 1 < samples.size(); ++index)
        {
            if (input > samples[index + 1].position)
                continue;
            const f32 alpha = (input - samples[index].position) / (samples[index + 1].position - samples[index].position);
            result.active[0] = {samples[index].clipAssetID, 1.0f - alpha, index};
            result.active[1] = {samples[index + 1].clipAssetID, alpha, static_cast<u16>(index + 1)};
            result.activeCount = alpha <= 0.0f || alpha >= 1.0f ? 1 : 2;
            if (alpha >= 1.0f)
                result.active[0] = result.active[1];
            return result;
        }
        result.fallback = BlendSpaceFallback::NearestSample;
        return result;
    }

    BlendSpaceResult EvaluateBlendSpace2D(std::span<const BlendSpaceSample2D> samples, std::span<const BlendSpaceTriangle> triangles, vec2 input, bool wrapX, f32 wrapMinimum, f32 wrapMaximum)
    {
        BlendSpaceResult result;
        if (samples.empty() || !std::isfinite(input.x) || !std::isfinite(input.y))
        {
            result.fallback = BlendSpaceFallback::Empty;
            return result;
        }
        if (wrapX)
        {
            input.x = Math::Wrap(input.x, wrapMinimum, wrapMaximum);
            result.fallback = BlendSpaceFallback::Wrapped;
        }
        f32 bestPenalty = std::numeric_limits<f32>::max();
        vec3 bestWeights = {};
        u32 bestTriangle = std::numeric_limits<u32>::max();
        bool sawDegenerate = false;
        for (u32 triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex)
        {
            const BlendSpaceTriangle& triangle = triangles[triangleIndex];
            if (triangle.samples[0] >= samples.size() || triangle.samples[1] >= samples.size() || triangle.samples[2] >= samples.size())
                continue;
            vec3 weights;
            if (!Geometry::TryGetBarycentricCoordinates(input, samples[triangle.samples[0]].position, samples[triangle.samples[1]].position, samples[triangle.samples[2]].position, weights))
            {
                sawDegenerate = true;
                continue;
            }
            const f32 penalty = std::max(-weights.x, 0.0f) + std::max(-weights.y, 0.0f) + std::max(-weights.z, 0.0f);
            if (penalty < bestPenalty)
            {
                bestPenalty = penalty;
                bestWeights = weights;
                bestTriangle = triangleIndex;
            }
            if (penalty <= 1.0e-5f)
                break;
        }
        if (bestTriangle == std::numeric_limits<u32>::max())
        {
            u16 nearest = 0;
            f32 nearestDistance = std::numeric_limits<f32>::max();
            for (u16 index = 0; index < samples.size(); ++index)
            {
                const f32 distance = glm::dot(samples[index].position - input, samples[index].position - input);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearest = index;
                }
            }
            result.active[0] = {samples[nearest].clipAssetID, 1.0f, nearest};
            result.activeCount = 1;
            result.fallback = sawDegenerate ? BlendSpaceFallback::DegenerateTriangle : BlendSpaceFallback::Hole;
            return result;
        }
        result.triangleIndex = bestTriangle;
        if (bestPenalty > 1.0e-5f)
        {
            bestWeights = glm::max(bestWeights, vec3(0.0f));
            const f32 total = bestWeights.x + bestWeights.y + bestWeights.z;
            if (total <= 1.0e-8f)
                bestWeights = vec3(1.0f, 0.0f, 0.0f);
            else
                bestWeights /= total;
            result.fallback = BlendSpaceFallback::Clamped;
        }
        const BlendSpaceTriangle& triangle = triangles[bestTriangle];
        for (u32 corner = 0; corner < 3; ++corner)
            if (bestWeights[corner] > 1.0e-6f)
                result.active[result.activeCount++] = {samples[triangle.samples[corner]].clipAssetID, bestWeights[corner], triangle.samples[corner]};
        return result;
    }

    f32 AdvanceSmoothedInput(f32 current, f32 target, f32 halfLifeSeconds, f32 deltaSeconds)
    {
        if (!(halfLifeSeconds > 0.0f) || !(deltaSeconds > 0.0f))
            return target;
        const f32 weight = 1.0f - std::exp2(-deltaSeconds / halfLifeSeconds);
        return current + (target - current) * weight;
    }

    SynchronizationResult SynchronizeSamples(std::span<const SynchronizationInput> samples, FileFormat::AssetID previousLeader, f32 leaderHysteresis, f32 correctionWindowSeconds, f32 minimumRate, f32 maximumRate)
    {
        SynchronizationResult result;
        result.sampleCount = std::min<u32>(static_cast<u32>(samples.size()), static_cast<u32>(result.mappedTimesMicroseconds.size()));
        if (result.sampleCount == 0)
            return result;
        u32 leader = 0;
        for (u32 index = 1; index < result.sampleCount; ++index)
            if (samples[index].influence > samples[leader].influence || (samples[index].influence == samples[leader].influence && samples[index].clipAssetID < samples[leader].clipAssetID))
                leader = index;
        for (u32 index = 0; index < result.sampleCount; ++index)
            if (samples[index].clipAssetID == previousLeader && samples[index].influence + std::max(leaderHysteresis, 0.0f) >= samples[leader].influence)
                leader = index;
        result.leaderIndex = leader;
        const SynchronizationInput& leaderInput = samples[leader];
        std::array<f32, 3> basePlaybackRates = {1.0f, 1.0f, 1.0f};
        for (u32 index = 0; index < result.sampleCount; ++index)
            result.mappedTimesMicroseconds[index] = samples[index].timeMicroseconds;

        if (leaderInput.durationMicroseconds == 0 || leaderInput.markers.size() < 2)
        {
            result.normalizedPhaseFallback = true;
        }
        else
        {
            const u64 leaderTime = leaderInput.timeMicroseconds % leaderInput.durationMicroseconds;
            u32 next = 0;
            while (next < leaderInput.markers.size() && leaderInput.markers[next].timeMicroseconds <= leaderTime)
                ++next;
            const u32 previous = next == 0 ? static_cast<u32>(leaderInput.markers.size() - 1) : next - 1;
            next %= static_cast<u32>(leaderInput.markers.size());
            const auto& previousMarker = leaderInput.markers[previous];
            const auto& nextMarker = leaderInput.markers[next];
            const u64 intervalStart = previousMarker.timeMicroseconds;
            const u64 intervalEnd = next > previous ? nextMarker.timeMicroseconds : leaderInput.durationMicroseconds + nextMarker.timeMicroseconds;
            const u64 unwrappedLeaderTime = leaderTime < intervalStart ? leaderTime + leaderInput.durationMicroseconds : leaderTime;
            if (intervalEnd <= intervalStart)
            {
                result.degenerateMarkerFallback = true;
                result.normalizedPhaseFallback = true;
            }
            else
            {
                const f64 phase = static_cast<f64>(unwrappedLeaderTime - intervalStart) / static_cast<f64>(intervalEnd - intervalStart);
                result.previousMarkerSemanticID = previousMarker.semanticID;
                result.nextMarkerSemanticID = nextMarker.semanticID;
                for (u32 index = 0; index < result.sampleCount; ++index)
                {
                    if (index == leader)
                        continue;
                    const SynchronizationInput& follower = samples[index];
                    const auto followerPrevious = std::find_if(follower.markers.begin(), follower.markers.end(), [&previousMarker](const FileFormat::Animation::SynchronizationMarker& marker) { return marker.semanticID == previousMarker.semanticID; });
                    const auto followerNext = std::find_if(follower.markers.begin(), follower.markers.end(), [&nextMarker](const FileFormat::Animation::SynchronizationMarker& marker) { return marker.semanticID == nextMarker.semanticID; });
                    if (follower.durationMicroseconds == 0 || followerPrevious == follower.markers.end() || followerNext == follower.markers.end())
                    {
                        result.normalizedPhaseFallback = true;
                        continue;
                    }
                    u64 followerEnd = followerNext->timeMicroseconds;
                    if (followerEnd <= followerPrevious->timeMicroseconds)
                        followerEnd += follower.durationMicroseconds;
                    if (followerEnd <= followerPrevious->timeMicroseconds)
                    {
                        result.degenerateMarkerFallback = true;
                        result.normalizedPhaseFallback = true;
                        continue;
                    }
                    basePlaybackRates[index] = static_cast<f32>(static_cast<f64>(followerEnd - followerPrevious->timeMicroseconds) / static_cast<f64>(intervalEnd - intervalStart));
                    result.mappedTimesMicroseconds[index] = static_cast<u64>(std::round(followerPrevious->timeMicroseconds + phase * static_cast<f64>(followerEnd - followerPrevious->timeMicroseconds))) % follower.durationMicroseconds;
                }
            }
        }
        if (result.normalizedPhaseFallback && leaderInput.durationMicroseconds != 0)
        {
            const f64 phase = static_cast<f64>(leaderInput.timeMicroseconds % leaderInput.durationMicroseconds) / static_cast<f64>(leaderInput.durationMicroseconds);
            for (u32 index = 0; index < result.sampleCount; ++index)
                if (samples[index].durationMicroseconds != 0)
                {
                    basePlaybackRates[index] = static_cast<f32>(static_cast<f64>(samples[index].durationMicroseconds) / static_cast<f64>(leaderInput.durationMicroseconds));
                    result.mappedTimesMicroseconds[index] = static_cast<u64>(std::round(phase * samples[index].durationMicroseconds)) % samples[index].durationMicroseconds;
                }
        }
        minimumRate = std::max(minimumRate, 0.0f);
        maximumRate = std::max(maximumRate, minimumRate);
        const f64 windowMicroseconds = std::max(static_cast<f64>(correctionWindowSeconds) * 1'000'000.0, 1.0);
        for (u32 index = 0; index < result.sampleCount; ++index)
        {
            if (index == leader || samples[index].durationMicroseconds == 0)
                continue;
            i64 error = static_cast<i64>(result.mappedTimesMicroseconds[index]) - static_cast<i64>(samples[index].timeMicroseconds % samples[index].durationMicroseconds);
            const i64 halfDuration = static_cast<i64>(samples[index].durationMicroseconds / 2);
            if (error > halfDuration)
                error -= static_cast<i64>(samples[index].durationMicroseconds);
            else if (error < -halfDuration)
                error += static_cast<i64>(samples[index].durationMicroseconds);
            result.playbackRates[index] = std::clamp(basePlaybackRates[index] + static_cast<f32>(static_cast<f64>(error) / windowMicroseconds), minimumRate, maximumRate);
        }
        return result;
    }
} // namespace Animation
