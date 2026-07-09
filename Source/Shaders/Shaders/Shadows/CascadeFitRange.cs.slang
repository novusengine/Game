#include "Include/Camera.inc.slang"
#include "Shadows/SDSM.inc.slang"

// First fit dispatch: turns the reduced depth min/max into the working cascade range
// (expand-instant, windowed-jump shrink, outward quantization) and the split distances.
// CascadeXYReduce assigns pixels to cascades from splitDepth, CascadeFitCameras builds
// the cameras from splitDist. Both forms are stored, not recomputed, to keep bit parity.

[[vk::push_constant]] SDSMConstants _constants;

[[vk::binding(0, PER_PASS)]] RWStructuredBuffer<SDSMData> _sdsmData;
[[vk::binding(1, PER_PASS)]] StructuredBuffer<uint> _depthMinMax; // [0] = min depth bits (farthest), [1] = max depth bits (nearest)
[[vk::binding(2, PER_PASS)]] StructuredBuffer<Camera> _srcCameras; // Element 0 = main camera

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    Camera mainCamera = _srcCameras[0];

    float nearClip = mainCamera.nearFar.x;
    float farClip = mainCamera.nearFar.y;
    float clipRange = farClip - nearClip;

    // Working range for the cascade distribution
    float rangeMin = max(nearClip, 1.0f);
    float rangeMax = min(farClip, _constants.shadowMaxDistance);

    SDSMState state = _sdsmData[0].state;

    if (_constants.useDepthBounds && _depthMinMax[1] != 0)
    {
        // Reversed Z linearization, depth 1 = near plane, depth 0 = far plane
        float minDepth = asfloat(_depthMinMax[1]); // Max depth bits = nearest sample
        float maxDepth = asfloat(_depthMinMax[0]); // Min depth bits = farthest sample

        float rawMin = (nearClip * farClip) / (nearClip + minDepth * (farClip - nearClip));
        float rawMax = (nearClip * farClip) / (nearClip + maxDepth * (farClip - nearClip));

        rawMin = clamp(rawMin, rangeMin, _constants.shadowMaxDistance - 1.0f);
        rawMax = clamp(rawMax, rawMin + 1.0f, _constants.shadowMaxDistance);

        // Hysteresis: expand instantly, shrink in a single jump after the range has been smaller
        // for a whole window. A continuous shrink rescales the cascades every step and the texel
        // grid realignment reads as shimmer, one jump per window is one realignment
        if (state.smoothedMax <= state.smoothedMin) // Uninitialized state
        {
            state.smoothedMin = rawMin;
            state.smoothedMax = rawMax;
            state.windowLowestMin = rawMin;
            state.windowHighestMax = rawMax;
            state.windowTimer = 0.0f;
        }
        else
        {
            state.smoothedMin = min(state.smoothedMin, rawMin);
            state.smoothedMax = max(state.smoothedMax, rawMax);

            state.windowLowestMin = min(state.windowLowestMin, rawMin);
            state.windowHighestMax = max(state.windowHighestMax, rawMax);
            state.windowTimer += _constants.deltaTime;

            if (state.windowTimer >= _constants.shrinkDelay)
            {
                // Shrink to the widest bounds the window actually needed
                state.smoothedMin = max(state.smoothedMin, state.windowLowestMin);
                state.smoothedMax = min(state.smoothedMax, state.windowHighestMax);

                state.windowLowestMin = rawMin;
                state.windowHighestMax = rawMax;
                state.windowTimer = 0.0f;
            }
        }

        // Quantize outward AFTER smoothing so the working bounds are piecewise-constant
        // and the stable texel snapping stays effective
        float step = max(_constants.quantizeStep, 0.01f);
        rangeMin = max(floor(state.smoothedMin / step) * step, max(nearClip, 1.0f));
        rangeMax = min(ceil(state.smoothedMax / step) * step, _constants.shadowMaxDistance);
        rangeMax = max(rangeMax, rangeMin + 1.0f);
    }
    else
    {
        // Parity mode / no valid depth: keep the state seeded so enabling SDSM starts sane
        state.smoothedMin = rangeMin;
        state.smoothedMax = rangeMax;
        state.windowLowestMin = rangeMin;
        state.windowHighestMax = rangeMax;
        state.windowTimer = 0.0f;
    }

    state.usedMin = rangeMin;
    state.usedMax = rangeMax;
    _sdsmData[0].state = state;

    // Split distances, same log/uniform lambda mix as the legacy CPU path.
    // The split fractions stay relative to the FULL camera clip range so the frustum
    // corner slicing and the eyePosition.w = distance - near semantics hold
    {
        float ratio = rangeMax / rangeMin;
        float range = rangeMax - rangeMin;
        for (uint i = 0; i < _constants.numCascades; i++)
        {
            float p = (i + 1) / float(_constants.numCascades);
            float logSplit = rangeMin * pow(ratio, p);
            float uniformSplit = rangeMin + range * p;
            float d = _constants.cascadeSplitLambda * (logSplit - uniformSplit) + uniformSplit;

            _sdsmData[0].splitDist[i] = 1.0f - ((d - nearClip) / clipRange);
            _sdsmData[0].splitDepth[i] = d - nearClip;
        }
    }
}
