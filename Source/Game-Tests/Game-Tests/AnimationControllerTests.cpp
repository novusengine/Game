#include "Game-Lib/Rendering/Animation/Controller/AnimationBlendSpace.h"
#include "Game-Lib/Rendering/Animation/Controller/AnimationControllerRuntime.h"
#include "Game-Lib/Rendering/Animation/Deformation/AnimationDemandPlanner.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"

#include <Base/Math/Geometry.h>
#include <Base/Math/Math.h>

#include <catch2/catch2.hpp>

#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

#include <set>

namespace
{
    constexpr u64 IDLE = 0x1001;
    constexpr u64 WALK = 0x1002;
    constexpr u64 RUN = 0x1003;
    constexpr FileFormat::AssetID SKELETON = 0x2001;
    constexpr FileFormat::AssetID IDLE_CLIP_A = 0x3001;
    constexpr FileFormat::AssetID IDLE_CLIP_B = 0x3002;
    constexpr FileFormat::AssetID WALK_CLIP = 0x3003;
    constexpr FileFormat::AssetID RUN_CLIP = 0x3004;
    constexpr u64 LEFT_CONTACT = 0x4001;
    constexpr u64 RIGHT_CONTACT = 0x4002;
    constexpr f64 CONTACT_PHASE_TOLERANCE = 0.01;

    Animation::EditableAnimationGraph MakeGraph()
    {
        Animation::EditableAnimationGraph graph;
        graph.parameters = {{"speed", FileFormat::Animation::GraphValueType::Float}, {"hit", FileFormat::Animation::GraphValueType::Bool, 0.0f, 0, false, true}};
        graph.states = {{1, IDLE, 1'000'000}, {2, WALK, 1'000'000}, {3, RUN, 800'000}};
        graph.entryStateGUID = 1;
        graph.transitions = {
            {1, 2, "speed", Animation::GraphComparison::Greater, 0.1f, 300'000, Animation::BlendCurve::SmoothStep},
            {2, 1, "speed", Animation::GraphComparison::LessEqual, 0.1f, 250'000, Animation::BlendCurve::SmoothStep},
            {2, 3, "speed", Animation::GraphComparison::Greater, 2.0f, 200'000, Animation::BlendCurve::Linear},
            {3, 2, "speed", Animation::GraphComparison::LessEqual, 2.0f, 200'000, Animation::BlendCurve::Linear},
        };
        return graph;
    }

    struct SetFixture
    {
        FileFormat::Animation::AnimationSetAsset root;
        std::array<FileFormat::Animation::AnimationSetSlot, 3> slots = {};
        std::array<FileFormat::Animation::AnimationClipVariant, 4> variants = {};
        AnimationLoading::AnimationSetAssetView view;

        SetFixture()
        {
            root.declaredPoseDomainAssetID = SKELETON;
            root.declaredPoseDomainType = FileFormat::Animation::PoseDomainType::Skeleton;
            slots[0] = {IDLE, 0, 2, -1, FileFormat::Animation::AnimationSetSlotFlags_Required};
            slots[1] = {WALK, 2, 1, 0, FileFormat::Animation::AnimationSetSlotFlags_Required};
            slots[2] = {RUN, 3, 1, 1, FileFormat::Animation::AnimationSetSlotFlags_None};
            variants[0] = {IDLE_CLIP_A, 1, 0, 101};
            variants[1] = {IDLE_CLIP_B, 3, 0, 102};
            variants[2] = {WALK_CLIP, 1, 0, 201};
            variants[3] = {RUN_CLIP, 1, 0, 301};
            view.root = root;
            view.slots = slots;
            view.variants = variants;
        }
    };

    const std::array<FileFormat::Animation::SynchronizationMarker, 2> WALK_MARKERS = {{{LEFT_CONTACT, 212'500, 0}, {RIGHT_CONTACT, 887'500, 0}}};
    const std::array<FileFormat::Animation::SynchronizationMarker, 2> RUN_MARKERS = {{{LEFT_CONTACT, 191'667, 0}, {RIGHT_CONTACT, 554'167, 0}}};
    const std::array<Animation::AnimationClipDomainRecord, 4> CLIPS = {{
        {IDLE_CLIP_A, FileFormat::Animation::PoseDomainType::Skeleton, SKELETON, 9'933'333},
        {IDLE_CLIP_B, FileFormat::Animation::PoseDomainType::Skeleton, SKELETON, 9'933'333},
        {WALK_CLIP, FileFormat::Animation::PoseDomainType::Skeleton, SKELETON, 1'366'667, WALK_MARKERS},
        {RUN_CLIP, FileFormat::Animation::PoseDomainType::Skeleton, SKELETON, 700'000, RUN_MARKERS},
    }};

    u64 CircularDistance(u64 first, u64 second, u64 duration)
    {
        const u64 direct = first > second ? first - second : second - first;
        return std::min(direct, duration - direct);
    }

    u64 FindSampleTime(const Animation::PoseRecipe& recipe, FileFormat::AssetID clipAssetID)
    {
        const auto sample = std::find_if(recipe.samples.begin(), recipe.samples.begin() + recipe.sampleCount, [clipAssetID](const Animation::PoseRecipeSample& value) { return value.clipAssetID == clipAssetID; });
        REQUIRE(sample != recipe.samples.begin() + recipe.sampleCount);
        return sample->timeMicroseconds;
    }

    f64 FindContactIntervalPhase(u64 timeMicroseconds, u64 durationMicroseconds, std::span<const FileFormat::Animation::SynchronizationMarker> markers)
    {
        REQUIRE(durationMicroseconds > 0);
        REQUIRE(markers.size() >= 2);
        const u64 time = timeMicroseconds % durationMicroseconds;
        u32 next = 0;
        while (next < markers.size() && markers[next].timeMicroseconds <= time)
            ++next;
        const u32 previous = next == 0 ? static_cast<u32>(markers.size() - 1) : next - 1;
        next %= static_cast<u32>(markers.size());
        const u64 intervalStart = markers[previous].timeMicroseconds;
        const u64 intervalEnd = next > previous ? markers[next].timeMicroseconds : durationMicroseconds + markers[next].timeMicroseconds;
        const u64 unwrappedTime = time < intervalStart ? time + durationMicroseconds : time;
        return static_cast<f64>(unwrappedTime - intervalStart) / static_cast<f64>(intervalEnd - intervalStart);
    }
}

TEST_CASE("AnimationSet resolves semantic slots and deterministic weighted variants", "[AnimationController][AnimationSet]")
{
    SetFixture fixture;
    const Animation::ResolvedAnimationSlot first = Animation::ResolveAnimationSlot(fixture.view, CLIPS, IDLE, 42);
    const Animation::ResolvedAnimationSlot repeated = Animation::ResolveAnimationSlot(fixture.view, CLIPS, IDLE, 42);
    REQUIRE(first);
    CHECK(first.clipAssetID == repeated.clipAssetID);
    CHECK(first.stableVariationID == repeated.stableVariationID);
    CHECK(Animation::ResolveAnimationSlot(fixture.view, CLIPS, IDLE, 999, first.stableVariationID).stableVariationID == first.stableVariationID);

    std::set<u32> observed;
    for (u64 seed = 0; seed < 128; ++seed)
        observed.insert(Animation::ResolveAnimationSlot(fixture.view, CLIPS, IDLE, seed).stableVariationID);
    CHECK(observed == std::set<u32>{101, 102});

    std::array<FileFormat::Animation::AnimationSetSlot, 1> fallbackSlot = {{{0x9999, 0, 0, 1, 0}}};
    AnimationLoading::AnimationSetAssetView invalid = fixture.view;
    invalid.slots = fallbackSlot;
    CHECK(Animation::ResolveAnimationSlot(invalid, CLIPS, 0x9999, 0).resolution == Animation::AnimationSlotResolution::InvalidSet);
}

TEST_CASE("Graph compiler is deterministic compact and rejects invalid replacement input", "[AnimationController][AnimationGraph]")
{
    const Animation::GraphCompileResult first = Animation::CompileAnimationGraph(MakeGraph());
    const Animation::GraphCompileResult second = Animation::CompileAnimationGraph(MakeGraph());
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.graph->controllerProgram == second.graph->controllerProgram);
    CHECK(first.graph->contentHash == second.graph->contentHash);
    CHECK(first.graph->controllerProgram.size() < 512);
    CHECK(first.graph->states.size() == 3);
    CHECK(first.graph->transitions.size() == 4);

    Animation::EditableAnimationGraph invalid = MakeGraph();
    invalid.states[2].sourceGUID = invalid.states[1].sourceGUID;
    const Animation::GraphCompileResult failure = Animation::CompileAnimationGraph(invalid);
    CHECK_FALSE(failure);
    CHECK_FALSE(failure.diagnostics.empty());

    invalid = MakeGraph();
    invalid.version++;
    const Animation::GraphCompileResult versionFailure = Animation::CompileAnimationGraph(invalid);
    CHECK_FALSE(versionFailure);
    REQUIRE(versionFailure.diagnostics.size() == 1);
    CHECK(versionFailure.diagnostics[0].field == "version");
}

TEST_CASE("CPU controllers advance deterministically offscreen without pose work", "[AnimationController][Determinism]")
{
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(MakeGraph());
    REQUIRE(compiled);
    Animation::AnimationControllerStore sixtyHz;
    Animation::AnimationControllerStore thirtyHz;
    std::vector<Animation::AnimatorHandle> firstHandles;
    std::vector<Animation::AnimatorHandle> secondHandles;
    for (u32 index = 0; index < 2000; ++index)
    {
        firstHandles.push_back(sixtyHz.Create({SKELETON, 1, compiled.graph, index}));
        secondHandles.push_back(thirtyHz.Create({SKELETON, 1, compiled.graph, index}));
    }
    for (u32 frame = 0; frame < 60; ++frame)
        sixtyHz.Advance(1.0 / 60.0);
    for (u32 frame = 0; frame < 30; ++frame)
        thirtyHz.Advance(1.0 / 30.0);
    Animation::AnimationControllerState a;
    Animation::AnimationControllerState b;
    REQUIRE(sixtyHz.GetState(firstHandles[123], a));
    REQUIRE(thirtyHz.GetState(secondHandles[123], b));
    CHECK(a.currentState == b.currentState);
    CHECK(a.stateTimeMicroseconds == b.stateTimeMicroseconds);
    CHECK(sixtyHz.GetStats().poseRequests == 0);
    CHECK(thirtyHz.GetStats().poseRequests == 0);
    CHECK(sixtyHz.GetStats().advances == thirtyHz.GetStats().advances);
}

TEST_CASE("Native controller batches use the same typed generation-safe parameter path", "[AnimationController][BatchInputs]")
{
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(MakeGraph());
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle first = store.Create({SKELETON, 1, compiled.graph, 1});
    const Animation::AnimatorHandle second = store.Create({SKELETON, 1, compiled.graph, 2});
    const Animation::AnimationParameterHandle firstSpeed = store.ResolveParameter(first, Animation::HashAnimationName("speed"));
    const Animation::AnimationParameterHandle secondSpeed = store.ResolveParameter(second, Animation::HashAnimationName("speed"));
    REQUIRE(firstSpeed);
    REQUIRE(secondSpeed);
    std::array<Animation::AnimationControllerBatchInput, 2> inputs = {{{first, firstSpeed, {}, {}, 1.0f}, {second, secondSpeed, {}, {}, 3.0f}}};
    CHECK(store.ApplyBatchInputs(inputs) == 2);
    store.Advance(1.0 / 60.0);
    Animation::AnimationControllerState firstState;
    Animation::AnimationControllerState secondState;
    REQUIRE(store.GetState(first, firstState));
    REQUIRE(store.GetState(second, secondState));
    CHECK(firstState.transitionTarget == 1);
    CHECK(secondState.transitionTarget == 1);

    REQUIRE(store.Destroy(first));
    CHECK(store.ApplyBatchInputs(std::span<const Animation::AnimationControllerBatchInput>(inputs.data(), 1)) == 0);
    CHECK(store.GetStats().staleHandleRejects > 0);
}

TEST_CASE("Typed handles transitions and interruption snapshots remain generation safe and bounded", "[AnimationController][Crossfade]")
{
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(MakeGraph());
    REQUIRE(compiled);
    SetFixture fixture;
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 77});
    REQUIRE(animator);
    const Animation::AnimationParameterHandle speed = store.ResolveParameter(animator, Animation::HashAnimationName("speed"));
    const Animation::AnimationParameterHandle hit = store.ResolveParameter(animator, Animation::HashAnimationName("hit"));
    REQUIRE(speed);
    REQUIRE(hit);
    CHECK_FALSE(store.SetInt(speed, 4));
    CHECK(store.SetFloat(speed, 1.0f));
    CHECK(store.Trigger(hit));
    CHECK(store.QueueCommand(animator, 500, 1));
    store.Advance(1.0 / 60.0);
    CHECK(store.GetOutputs(animator).size() == 1);
    store.Advance(1.0 / 60.0);
    Animation::PoseRecipe recipe = store.RequestPose(animator, fixture.view, CLIPS);
    REQUIRE(recipe.sampleCount == 2);
    CHECK(recipe.samples[0].weight + recipe.samples[1].weight == Catch::Approx(1.0f));
    CHECK(recipe.transitionWeight > 0.0f);
    CHECK(recipe.transitionWeight < 1.0f);

    CHECK(store.SetFloat(speed, 3.0f));
    store.Advance(1.0 / 60.0);
    std::array<FileFormat::Animation::LocalTransform, 2> continuity = {};
    continuity[0].translation = vec3(1.0f);
    recipe = store.RequestPose(animator, fixture.view, CLIPS, continuity);
    CHECK(recipe.usesSnapshot);
    CHECK(recipe.sampleCount == 0);
    CHECK(store.GetStats().interruptions == 1);
    CHECK(store.GetStats().snapshots == 1);

    const Animation::GraphCompileResult replacement = Animation::CompileAnimationGraph(MakeGraph());
    REQUIRE(replacement);
    CHECK(store.ReplaceGraph(animator, 2, replacement.graph));
    CHECK_FALSE(store.SetFloat(speed, 0.0f));
    CHECK(store.Destroy(animator));
    const Animation::AnimatorHandle reused = store.Create({SKELETON, 2, compiled.graph, 77});
    CHECK(reused.GetRawValue() != animator.GetRawValue());
    Animation::AnimationControllerState staleState;
    CHECK_FALSE(store.GetState(animator, staleState));
}

TEST_CASE("Local TRS crossfade matches endpoint and interior golden values", "[AnimationController][Crossfade]")
{
    std::array<FileFormat::Animation::LocalTransform, 1> outgoing = {};
    std::array<FileFormat::Animation::LocalTransform, 1> incoming = {};
    std::array<FileFormat::Animation::LocalTransform, 1> output = {};
    outgoing[0].translation = vec3(0.0f);
    outgoing[0].rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    outgoing[0].scale = vec3(1.0f);
    incoming[0].translation = vec3(10.0f, 2.0f, -4.0f);
    incoming[0].rotation = glm::angleAxis(glm::half_pi<f32>(), vec3(0.0f, 1.0f, 0.0f));
    incoming[0].scale = vec3(2.0f);
    REQUIRE(Animation::BlendLocalPoses(outgoing, incoming, 0.25f, output));
    CHECK(output[0].translation.x == Catch::Approx(2.5f));
    CHECK(output[0].translation.y == Catch::Approx(0.5f));
    CHECK(output[0].scale.x == Catch::Approx(1.25f));
    CHECK(std::abs(glm::dot(output[0].rotation, glm::angleAxis(glm::half_pi<f32>() * 0.25f, vec3(0.0f, 1.0f, 0.0f)))) == Catch::Approx(1.0f));
    REQUIRE(Animation::BlendLocalPoses(outgoing, incoming, 0.0f, output));
    CHECK(output[0].translation == outgoing[0].translation);
    REQUIRE(Animation::BlendLocalPoses(outgoing, incoming, 1.0f, output));
    CHECK(output[0].translation == incoming[0].translation);
}

TEST_CASE("Compiled blend spaces produce bounded continuous deterministic weights", "[AnimationController][BlendSpace]")
{
    CHECK(Math::Wrap(-0.25f, 0.0f, 1.0f) == Catch::Approx(0.75f));
    CHECK(Math::Wrap(1.25f, 0.0f, 1.0f) == Catch::Approx(0.25f));
    vec3 barycentricCoordinates;
    REQUIRE(Geometry::TryGetBarycentricCoordinates(vec2(0.25f, 0.25f), vec2(0.0f, 0.0f), vec2(1.0f, 0.0f), vec2(0.0f, 1.0f), barycentricCoordinates));
    CHECK(barycentricCoordinates == vec3(0.5f, 0.25f, 0.25f));
    CHECK_FALSE(Geometry::TryGetBarycentricCoordinates(vec2(0.0f), vec2(0.0f), vec2(1.0f), vec2(2.0f), barycentricCoordinates));

    const std::array<Animation::BlendSpaceSample1D, 3> oneDimension = {{{-1.0f, 1}, {0.0f, 2}, {1.0f, 3}}};
    Animation::BlendSpaceResult result = Animation::EvaluateBlendSpace1D(oneDimension, 0.25f);
    REQUIRE(result.activeCount == 2);
    CHECK(result.active[0].weight == Catch::Approx(0.75f));
    CHECK(result.active[1].weight == Catch::Approx(0.25f));
    CHECK(Animation::EvaluateBlendSpace1D(oneDimension, 5.0f).fallback == Animation::BlendSpaceFallback::Clamped);

    const std::array<Animation::BlendSpaceSample2D, 4> twoDimension = {{{vec2(-1.0f, 0.0f), 1}, {vec2(1.0f, 0.0f), 2}, {vec2(0.0f, 1.0f), 3}, {vec2(0.0f, -1.0f), 4}}};
    const std::array<Animation::BlendSpaceTriangle, 2> triangles = {{{{0, 1, 2}}, {{0, 3, 1}}}};
    result = Animation::EvaluateBlendSpace2D(twoDimension, triangles, vec2(0.0f, 0.5f));
    CHECK(result.activeCount <= 3);
    f32 total = 0.0f;
    for (u32 index = 0; index < result.activeCount; ++index)
        total += result.active[index].weight;
    CHECK(total == Catch::Approx(1.0f));
    CHECK(Animation::EvaluateBlendSpace2D(twoDimension, triangles, vec2(5.0f, 5.0f)).fallback == Animation::BlendSpaceFallback::Clamped);
    CHECK(Animation::AdvanceSmoothedInput(0.0f, 1.0f, 0.1f, 0.1f) == Catch::Approx(0.5f));
}

TEST_CASE("Synchronization markers select a stable leader and clamp follower correction", "[AnimationController][Synchronization]")
{
    const std::array<FileFormat::Animation::SynchronizationMarker, 2> leaderMarkers = {{{11, 0, 0}, {12, 500'000, 0}}};
    const std::array<FileFormat::Animation::SynchronizationMarker, 2> followerMarkers = {{{11, 0, 0}, {12, 400'000, 0}}};
    const std::array<Animation::SynchronizationInput, 2> inputs = {{
        {100, 0.55f, 250'000, 1'000'000, leaderMarkers},
        {200, 0.45f, 100'000, 800'000, followerMarkers},
    }};
    Animation::SynchronizationResult result = Animation::SynchronizeSamples(inputs, 200, 0.15f, 0.2f);
    CHECK(result.leaderIndex == 1);
    result = Animation::SynchronizeSamples(inputs, FileFormat::INVALID_ASSET_ID, 0.05f, 0.2f);
    CHECK(result.leaderIndex == 0);
    CHECK(result.previousMarkerSemanticID == 11);
    CHECK(result.nextMarkerSemanticID == 12);
    CHECK(result.mappedTimesMicroseconds[1] == 200'000);
    CHECK(result.playbackRates[1] >= 0.8f);
    CHECK(result.playbackRates[1] <= 1.2f);

    std::array<Animation::SynchronizationInput, 2> aligned = inputs;
    aligned[1].timeMicroseconds = 200'000;
    result = Animation::SynchronizeSamples(aligned, 100, 0.05f, 0.2f);
    CHECK(result.playbackRates[1] == Catch::Approx(0.8f));

    std::array<Animation::SynchronizationInput, 2> missing = inputs;
    missing[1].markers = {};
    result = Animation::SynchronizeSamples(missing, 0, 0.0f, 0.2f);
    CHECK(result.normalizedPhaseFallback);
    CHECK(result.playbackRates[1] <= 1.2f);
}

TEST_CASE("Compiled 1D and 2D blend-space states emit bounded synchronized pose recipes offscreen", "[AnimationController][BlendSpace][AnimationGraph]")
{
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.parameters = {{"speed", FileFormat::Animation::GraphValueType::Float}, {"direction", FileFormat::Animation::GraphValueType::Float}};
    Animation::EditableGraphState state;
    state.sourceGUID = 10;
    state.durationMicroseconds = 1'000'000;
    state.kind = Animation::GraphStateKind::BlendSpace1D;
    state.inputX = "speed";
    state.smoothingHalfLifeSeconds = 0.1f;
    state.blendSamples = {{{0.0f, 0.0f}, IDLE}, {{1.0f, 0.0f}, WALK}, {{3.0f, 0.0f}, RUN}};
    source.states = {state};
    source.entryStateGUID = state.sourceGUID;
    const Animation::GraphCompileResult compiled1D = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled1D);
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled1D.graph, 9});
    const Animation::AnimationParameterHandle speed = store.ResolveParameter(animator, Animation::HashAnimationName("speed"));
    REQUIRE(store.SetFloat(speed, 2.0f));
    for (u32 tick = 0; tick < 12; ++tick)
        store.Advance(1.0 / 60.0);
    const u64 poseRequestsBefore = store.GetStats().poseRequests;
    Animation::AnimationControllerState controllerState;
    REQUIRE(store.GetState(animator, controllerState));
    CHECK(controllerState.stateTimeMicroseconds > 0);
    CHECK(store.GetStats().poseRequests == poseRequestsBefore);
    const Animation::PoseRecipe recipe1D = store.RequestPose(animator, fixture.view, CLIPS);
    REQUIRE(recipe1D.sampleCount == 2);
    CHECK(recipe1D.samples[0].weight + recipe1D.samples[1].weight == Catch::Approx(1.0f));
    REQUIRE(store.GetState(animator, controllerState));
    CHECK(controllerState.activeBlendWeights[0] + controllerState.activeBlendWeights[1] == Catch::Approx(1.0f));
    CHECK(controllerState.synchronizationPlaybackRates[0] >= 0.8f);
    CHECK(controllerState.synchronizationPlaybackRates[0] <= 1.2f);

    state.kind = Animation::GraphStateKind::BlendSpace2D;
    state.inputY = "direction";
    state.blendSamples = {{{-1.0f, 0.0f}, WALK}, {{1.0f, 0.0f}, RUN}, {{0.0f, 1.0f}, IDLE}};
    state.blendTriangles = {{{{0, 1, 2}}}};
    source.states = {state};
    const Animation::GraphCompileResult compiled2D = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled2D);
    Animation::AnimationControllerStore twoDimensions;
    const Animation::AnimatorHandle twoDimensionalAnimator = twoDimensions.Create({SKELETON, 1, compiled2D.graph, 9});
    REQUIRE(twoDimensions.SetFloat(twoDimensions.ResolveParameter(twoDimensionalAnimator, Animation::HashAnimationName("speed")), 0.0f));
    REQUIRE(twoDimensions.SetFloat(twoDimensions.ResolveParameter(twoDimensionalAnimator, Animation::HashAnimationName("direction")), 0.5f));
    for (u32 tick = 0; tick < 60; ++tick)
        twoDimensions.Advance(1.0 / 60.0);
    const Animation::PoseRecipe recipe2D = twoDimensions.RequestPose(twoDimensionalAnimator, fixture.view, CLIPS);
    CHECK(recipe2D.sampleCount <= 3);
    f32 totalWeight = 0.0f;
    for (u32 index = 0; index < recipe2D.sampleCount; ++index)
        totalWeight += recipe2D.samples[index].weight;
    CHECK(totalWeight == Catch::Approx(1.0f));
}

TEST_CASE("Clip and blend-space states compile transitions within the six-sample pose recipe", "[AnimationController][BlendSpace][AnimationGraph][Transition]")
{
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.parameters = {{"speed", FileFormat::Animation::GraphValueType::Float}};
    Animation::EditableGraphState locomotion;
    locomotion.sourceGUID = 11;
    locomotion.durationMicroseconds = 60'000'000;
    locomotion.kind = Animation::GraphStateKind::BlendSpace1D;
    locomotion.inputX = "speed";
    locomotion.blendSamples = {{{1.0f, 0.0f}, WALK}, {{3.0f, 0.0f}, RUN}};
    source.states = {{10, IDLE, 60'000'000}, locomotion};
    source.entryStateGUID = 10;
    source.transitions = {{10, 11, "speed", Animation::GraphComparison::Greater, 0.1f, 250'000, Animation::BlendCurve::SmoothStep}, {11, 10, "speed", Animation::GraphComparison::LessEqual, 0.1f, 250'000, Animation::BlendCurve::SmoothStep}};
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);

    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 9});
    const Animation::AnimationParameterHandle speed = store.ResolveParameter(animator, Animation::HashAnimationName("speed"));
    REQUIRE(store.SetFloat(speed, 2.0f));
    store.Advance(1.0 / 60.0);
    store.Advance(1.0 / 60.0);
    const Animation::PoseRecipe entering = store.RequestPose(animator, fixture.view, CLIPS);
    REQUIRE(entering.sampleCount == 3);
    CHECK(entering.sampleCount <= entering.samples.size());

    for (u32 tick = 0; tick < 20; ++tick)
        store.Advance(1.0 / 60.0);
    REQUIRE(store.SetFloat(speed, 0.0f));
    store.Advance(1.0 / 60.0);
    store.Advance(1.0 / 60.0);
    const Animation::PoseRecipe leaving = store.RequestPose(animator, fixture.view, CLIPS);
    REQUIRE(leaving.sampleCount == 2);
    CHECK(leaving.sampleCount <= leaving.samples.size());
}

TEST_CASE("Blend-space clocks preserve pose time through leader changes and advance synchronization offscreen", "[AnimationController][BlendSpace][Synchronization][Continuity]")
{
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.parameters = {{"speed", FileFormat::Animation::GraphValueType::Float}};
    Animation::EditableGraphState state;
    state.sourceGUID = 10;
    state.durationMicroseconds = 60'000'000;
    state.kind = Animation::GraphStateKind::BlendSpace1D;
    state.inputX = "speed";
    state.blendSamples = {{{0.0f, 0.0f}, IDLE}, {{1.0f, 0.0f}, WALK}, {{3.0f, 0.0f}, RUN}};
    source.states = {state};
    source.entryStateGUID = state.sourceGUID;
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);

    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 9});
    const Animation::AnimationParameterHandle speed = store.ResolveParameter(animator, Animation::HashAnimationName("speed"));
    REQUIRE(speed);
    REQUIRE(store.SetFloat(speed, 0.4f));
    Animation::PoseRecipe recipe;
    for (u32 tick = 0; tick < 60; ++tick)
    {
        store.Advance(1.0 / 60.0);
        recipe = store.RequestPose(animator, fixture.view, CLIPS);
    }
    const u64 walkBeforeLeaderChange = FindSampleTime(recipe, WALK_CLIP);
    REQUIRE(store.SetFloat(speed, 0.8f));
    store.Advance(1.0 / 60.0);
    recipe = store.RequestPose(animator, fixture.view, CLIPS);
    const u64 walkAfterLeaderChange = FindSampleTime(recipe, WALK_CLIP);
    CHECK(CircularDistance(walkBeforeLeaderChange, walkAfterLeaderChange, 1'366'667) < 25'000);
    Animation::AnimationControllerState controllerState;
    REQUIRE(store.GetState(animator, controllerState));
    CHECK(controllerState.synchronizationLeader == WALK_CLIP);

    REQUIRE(store.SetFloat(speed, 2.0f));
    store.Advance(1.0 / 60.0);
    recipe = store.RequestPose(animator, fixture.view, CLIPS);
    for (u32 tick = 0; tick < 120; ++tick)
        store.Advance(1.0 / 60.0);
    recipe = store.RequestPose(animator, fixture.view, CLIPS);
    for (u32 tick = 0; tick < 30; ++tick)
    {
        store.Advance(1.0 / 60.0);
        recipe = store.RequestPose(animator, fixture.view, CLIPS);
    }
    const f64 walkContactPhase = FindContactIntervalPhase(FindSampleTime(recipe, WALK_CLIP), 1'366'667, WALK_MARKERS);
    const f64 runContactPhase = FindContactIntervalPhase(FindSampleTime(recipe, RUN_CLIP), 700'000, RUN_MARKERS);
    CHECK(std::abs(walkContactPhase - runContactPhase) < CONTACT_PHASE_TOLERANCE);

    const u64 runBeforeLeaderChange = FindSampleTime(recipe, RUN_CLIP);
    REQUIRE(store.SetFloat(speed, 2.6f));
    store.Advance(1.0 / 60.0);
    recipe = store.RequestPose(animator, fixture.view, CLIPS);
    const u64 runAfterLeaderChange = FindSampleTime(recipe, RUN_CLIP);
    CHECK(CircularDistance(runBeforeLeaderChange, runAfterLeaderChange, 700'000) < 25'000);
    REQUIRE(store.GetState(animator, controllerState));
    CHECK(controllerState.synchronizationLeader == RUN_CLIP);
}

TEST_CASE("Two-dimensional blend-space leader changes preserve the promoted clip timeline", "[AnimationController][BlendSpace][Synchronization][Continuity]")
{
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.parameters = {{"lateral", FileFormat::Animation::GraphValueType::Float}, {"forward", FileFormat::Animation::GraphValueType::Float}};
    Animation::EditableGraphState state;
    state.sourceGUID = 20;
    state.durationMicroseconds = 60'000'000;
    state.kind = Animation::GraphStateKind::BlendSpace2D;
    state.inputX = "lateral";
    state.inputY = "forward";
    state.blendSamples = {{{-1.0f, 0.0f}, WALK}, {{1.0f, 0.0f}, RUN}, {{0.0f, 1.0f}, IDLE}};
    state.blendTriangles = {{{{0, 1, 2}}}};
    source.states = {state};
    source.entryStateGUID = state.sourceGUID;
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);

    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 9});
    const Animation::AnimationParameterHandle lateral = store.ResolveParameter(animator, Animation::HashAnimationName("lateral"));
    const Animation::AnimationParameterHandle forward = store.ResolveParameter(animator, Animation::HashAnimationName("forward"));
    REQUIRE(lateral);
    REQUIRE(forward);
    REQUIRE(store.SetFloat(lateral, -0.4f));
    REQUIRE(store.SetFloat(forward, 0.0f));
    Animation::PoseRecipe recipe;
    for (u32 tick = 0; tick < 60; ++tick)
    {
        store.Advance(1.0 / 60.0);
        recipe = store.RequestPose(animator, fixture.view, CLIPS);
    }
    const u64 runBeforeLeaderChange = FindSampleTime(recipe, RUN_CLIP);
    REQUIRE(store.SetFloat(lateral, 0.4f));
    store.Advance(1.0 / 60.0);
    recipe = store.RequestPose(animator, fixture.view, CLIPS);
    const u64 runAfterLeaderChange = FindSampleTime(recipe, RUN_CLIP);
    CHECK(CircularDistance(runBeforeLeaderChange, runAfterLeaderChange, 700'000) < 25'000);
    Animation::AnimationControllerState controllerState;
    REQUIRE(store.GetState(animator, controllerState));
    CHECK(controllerState.synchronizationLeader == RUN_CLIP);
}

TEST_CASE("Animation demand frustum uses transformed conservative bounds before pose evaluation", "[AnimationDeformation][AnimationBounds][ViewDemand]")
{
    Camera camera = {};
    camera.frustum[0] = vec4(1.0f, 0.0f, 0.0f, -1.0f);
    camera.frustum[1] = vec4(-1.0f, 0.0f, 0.0f, -1.0f);
    camera.frustum[2] = vec4(0.0f, 1.0f, 0.0f, -1.0f);
    camera.frustum[3] = vec4(0.0f, -1.0f, 0.0f, -1.0f);
    camera.frustum[4] = vec4(0.0f, 0.0f, 1.0f, -1.0f);
    camera.frustum[5] = vec4(0.0f, 0.0f, -1.0f, -1.0f);
    FileFormat::Animation::Bounds local;
    local.center = vec3(0.0f);
    local.extents = vec3(0.25f);
    local.sphereRadius = 0.5f;
    CHECK(Animation::IntersectsFrustum(Animation::TransformAnimationBounds(local, mat4x4(1.0f)), camera));
    mat4x4 outside(1.0f);
    outside[3] = vec4(5.0f, 0.0f, 0.0f, 1.0f);
    CHECK_FALSE(Animation::IntersectsFrustum(Animation::TransformAnimationBounds(local, outside), camera));
    mat4x4 edge(1.0f);
    edge[0][0] = 4.0f;
    edge[3] = vec4(1.9f, 0.0f, 0.0f, 1.0f);
    CHECK(Animation::IntersectsFrustum(Animation::TransformAnimationBounds(local, edge), camera));
}
