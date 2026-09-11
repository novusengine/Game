#include "Game-Lib/Rendering/Animation/Controller/AnimationBlendSpace.h"
#include "Game-Lib/Rendering/Animation/Controller/AnimationControllerRuntime.h"
#include "Game-Lib/Rendering/Animation/Controller/AnimationGraphDocument.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationBoundsSelector.h"
#include "Game-Lib/Rendering/Animation/Deformation/AnimationDemandPlanner.h"
#include "Game-Lib/Rendering/Animation/Deformation/AnimationDeformationKey.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"

#include <Base/Math/Geometry.h>
#include <Base/Math/Math.h>

#include <catch2/catch2.hpp>

#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

#include <set>
#include <unordered_map>

TEST_CASE("Deformation binding tables retain identity through hash collisions", "[animation][deformation]")
{
    const Animation::DeformationKey first{1, 5, 1, 0};
    const Animation::DeformationKey second{7, 5, 2, 0};
    const auto oldKey = [](const Animation::DeformationKey& key) { return key.poseSource ^ (static_cast<u64>(key.model) << 1u) ^ (key.poseGeneration * 0x9E3779B97F4A7C15ull); };
    REQUIRE(oldKey(first) == oldKey(second));
    REQUIRE(first != second);
    std::unordered_map<Animation::DeformationKey, u32, Animation::DeformationKeyHash> tables;
    tables.emplace(first, 11);
    tables.emplace(second, 22);
    REQUIRE(tables.size() == 2);
    REQUIRE(tables.at(first) == 11);
    REQUIRE(tables.at(second) == 22);
    tables.emplace(first, 33);
    REQUIRE(tables.size() == 2);
    REQUIRE(tables.at(first) == 11);
    auto nextPose = first;
    ++nextPose.poseGeneration;
    REQUIRE(nextPose != first);
    auto nextLOD = first;
    ++nextLOD.lod;
    REQUIRE(nextLOD != first);
}

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

TEST_CASE("Clip recipes and bounds follow the resolved clip clock rather than the graph duration", "[AnimationController][ClipClock]")
{
    SetFixture fixture;
    auto graph = MakeGraph();
    graph.states[0].durationMicroseconds = 60000000;
    const auto compiled = Animation::CompileAnimationGraph(graph);
    REQUIRE(compiled);
    auto clips = CLIPS;
    const bool looping = GENERATE(false, true);
    for (auto& clip : clips) clip.looping = looping;
    Animation::AnimationControllerStore store;
    const auto animator = store.Create({SKELETON, 1, compiled.graph, 77});
    REQUIRE(animator);
    for (const u64 time : {0ull, 9933332ull, 9933333ull, 9933334ull, 59999999ull, 60000000ull, 60000001ull, 100000000ull})
    {
        REQUIRE(store.Seek(animator, time));
        const auto recipe = store.RequestPose(animator, fixture.view, clips);
        REQUIRE(recipe.sampleCount == 1);
        const u64 expected = looping ? time % clips[0].durationMicroseconds : std::min(time, clips[0].durationMicroseconds);
        CHECK(recipe.samples[0].timeMicroseconds == expected);
        FileFormat::AssetID boundsClip = FileFormat::INVALID_ASSET_ID;
        u64 boundsTime = 0;
        bool tight = false;
        REQUIRE(store.GetBoundsMetadata(animator, fixture.view, clips, boundsClip, boundsTime, tight));
        CHECK(tight);
        CHECK(boundsClip == recipe.samples[0].clipAssetID);
        CHECK(boundsTime == expected);
    }
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

    Animation::EditableAnimationGraph decorated = MakeGraph();
    decorated.displayName = "Locomotion";
    decorated.states[0].displayName = "Idle";
    decorated.states[0].editorPosition = {-250.0f, 30.0f};
    const Animation::GraphCompileResult decoratedResult = Animation::CompileAnimationGraph(decorated);
    REQUIRE(decoratedResult);
    CHECK(decoratedResult.graph->controllerProgram == first.graph->controllerProgram);
    CHECK(decoratedResult.graph->contentHash == first.graph->contentHash);

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
    std::vector<vec3> firstParameters;
    std::vector<vec3> secondParameters;
    REQUIRE(store.GetParameterValues(first, firstParameters));
    REQUIRE(store.GetParameterValues(second, secondParameters));
    REQUIRE(firstParameters.size() == compiled.graph->parameters.size());
    REQUIRE(secondParameters.size() == compiled.graph->parameters.size());
    CHECK(firstParameters[0].x == Catch::Approx(1.0f));
    CHECK(secondParameters[0].x == Catch::Approx(3.0f));
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

TEST_CASE("Graph documents retain valid compilation through invalid drafts and undo", "[AnimationController][Authoring]")
{
    Animation::AnimationGraphDocument document(MakeGraph());
    REQUIRE(document.GetLastGoodGraph());
    const auto accepted = document.GetLastGoodGraph();
    auto draft = document.GetSource();
    draft.states[0].displayName = "Rest";
    document.Edit(draft);
    REQUIRE(document.IsDirty());
    REQUIRE(document.Compile());
    CHECK(document.GetLastGoodGraph()->controllerProgram == accepted->controllerProgram);
    document.MarkSaved();
    CHECK_FALSE(document.IsDirty());
    draft.entryStateGUID = 999;
    document.Edit(draft);
    CHECK_FALSE(document.Compile());
    CHECK_FALSE(document.GetCompilation().diagnostics.empty());
    CHECK(document.GetLastGoodGraph()->contentHash == accepted->contentHash);
    REQUIRE(document.Undo());
    CHECK_FALSE(document.IsDirty());
    REQUIRE(document.Compile());
    REQUIRE(document.Redo());
    CHECK_FALSE(document.Compile());
    document.Revert();
    CHECK_FALSE(document.IsDirty());
    CHECK(document.Compile());
    Animation::EditableAnimationGraph restored;
    std::string error;
    const auto text = Animation::AnimationGraphDocument::Serialize(document.GetSource());
    REQUIRE(Animation::AnimationGraphDocument::Parse(text, restored, error));
    CHECK(Animation::AnimationGraphDocument::Serialize(restored) == text);
    CHECK(Animation::CompileAnimationGraph(restored).graph->controllerProgram == document.GetLastGoodGraph()->controllerProgram);
    CHECK_FALSE(Animation::AnimationGraphDocument::Parse(std::array<u8, 1>{'{'}, restored, error));
    CHECK_FALSE(error.empty());
    CHECK(Animation::AnimationGraphDocument::Serialize(restored) == text);
    CHECK_FALSE(document.EditField("states", 0, "durationMicroseconds", "-1", error));
    CHECK_FALSE(document.EditField("states", 0, "stateVersion", "65537", error));
    CHECK_FALSE(document.EditField("states", 0, "stateVersion", "1.5", error));
    const auto count = document.GetSource().states.size();
    REQUIRE(document.DuplicateItem("states", 0, error));
    CHECK(document.GetSource().states.size() == count + 1);
    CHECK(document.GetSource().states.back().sourceGUID != document.GetSource().states.front().sourceGUID);
    CHECK_FALSE(document.Compile());
    REQUIRE(document.RemoveItem("states", static_cast<u32>(count), error));
    CHECK(document.Compile());
}

TEST_CASE("Graph binary sources reject corruption without changing the destination", "[AnimationController][Authoring][BinarySource]")
{
    auto graph = MakeGraph();
    graph.displayName = "Locomotion";
    graph.states[0].editorPosition = vec2(125.5f, -42.0f);
    graph.states[0].blendSamples = {{vec2(0.25f, 0.75f), 123, "Walk"}};
    graph.states[0].blendTriangles = {{{0, 0, 0}}};
    const auto bytes = Animation::AnimationGraphDocument::Serialize(graph);
    Animation::EditableAnimationGraph restored;
    std::string error;
    REQUIRE(Animation::AnimationGraphDocument::Parse(bytes, restored, error));
    CHECK(restored.displayName == graph.displayName);
    CHECK(restored.states[0].editorPosition == graph.states[0].editorPosition);
    CHECK(restored.states[0].blendSamples[0].displayName == "Walk");
    CHECK(Animation::AnimationGraphDocument::Serialize(restored) == bytes);
    for (size_t size = 0; size < bytes.size(); ++size)
    {
        CHECK_FALSE(Animation::AnimationGraphDocument::Parse(std::span(bytes).first(size), restored, error));
        CHECK(Animation::AnimationGraphDocument::Serialize(restored) == bytes);
    }
    for (size_t offset : {size_t{0}, size_t{4}, size_t{8}, size_t{12}, size_t{14}})
    {
        auto corrupt = bytes;
        corrupt[offset] = 255;
        CHECK_FALSE(Animation::AnimationGraphDocument::Parse(corrupt, restored, error));
    }
    auto trailing = bytes; trailing.push_back(0);
    CHECK_FALSE(Animation::AnimationGraphDocument::Parse(trailing, restored, error));
    Animation::AnimationGraphDocument document(graph);
    REQUIRE(document.EditField("states", 0, "editorPosition", "[10.5,-20]", error));
    CHECK(document.GetSource().states[0].editorPosition == vec2(10.5f, -20));
    CHECK_FALSE(document.EditField("states", 0, "editorPosition", "[1,2,3]", error));
    CHECK_FALSE(document.EditField("states", 0, "editorPosition", "[1,nan]", error));
    CHECK_FALSE(document.EditField("states", 0, "durationMicroseconds", "18446744073709551616", error));
    REQUIRE(document.EditField("states", 0, "sourceGUID", "18446744073709551615", error));
    CHECK(document.GetSource().states[0].sourceGUID == std::numeric_limits<u64>::max());
    const auto fields = document.GetFields();
    CHECK(std::any_of(fields.begin(), fields.end(), [](const auto& field) { return field.section == "parameters" && field.name == "defaultFloat3"; }));
}

TEST_CASE("Graph publication maps state GUIDs and invalidates old parameter handles", "[AnimationController][Authoring]")
{
    auto source = MakeGraph();
    const auto compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const auto animator = store.Create({SKELETON, 1, compiled.graph, 77});
    const auto speed = store.ResolveParameter(animator, Animation::HashAnimationName("speed"));
    REQUIRE(store.SetFloat(speed, 1.5f));
    store.Advance(1.0 / 60.0, false);
    Animation::AnimationControllerState before;
    REQUIRE(store.GetState(animator, before));
    std::swap(source.states[0], source.states[2]);
    std::swap(source.parameters[0], source.parameters[1]);
    Animation::AnimationGraphMigration outcome;
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome));
    CHECK(outcome.statePreserved);
    CHECK(outcome.parametersPreserved == 1);
    Animation::AnimationControllerState after;
    REQUIRE(store.GetState(animator, after));
    CHECK(after.currentState == 2);
    CHECK(after.stateTimeMicroseconds == before.stateTimeMicroseconds);
    CHECK_FALSE(store.SetFloat(speed, 4.0f));
    std::vector<vec3> values;
    REQUIRE(store.GetParameterValues(animator, values));
    CHECK(values[1].x == Catch::Approx(1.5f));
    source.states[2].durationMicroseconds += 1;
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome));
    CHECK_FALSE(outcome.statePreserved);
    REQUIRE(store.GetState(animator, after));
    CHECK(after.stateTimeMicroseconds == 0);
    store.Advance(0.1, false);
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome, false));
    CHECK_FALSE(outcome.statePreserved);
    CHECK_FALSE(outcome.transitionPreserved);
    REQUIRE(store.GetState(animator, after));
    CHECK(after.stateTimeMicroseconds == 0);
}

TEST_CASE("Graph drafts add empty collections and keep blend topology indices consistent", "[AnimationController][Authoring]")
{
    auto source = MakeGraph();
    source.states[0].blendSamples.resize(4);
    source.states[0].blendTriangles = {{{0, 1, 2}}, {{1, 2, 3}}};
    Animation::AnimationGraphDocument document(source);
    std::string error;
    REQUIRE(document.RemoveItem("states/0/blendSamples", 0, error));
    REQUIRE(document.GetSource().states[0].blendTriangles.size() == 1);
    CHECK(document.GetSource().states[0].blendTriangles[0].samples == std::array<u16, 3>{0, 1, 2});
    REQUIRE(document.DuplicateItem("layers", std::numeric_limits<u32>::max(), error));
    CHECK(document.GetSource().layers.size() == 1);
    REQUIRE(document.DuplicateItem("states/0/blendSamples", std::numeric_limits<u32>::max(), error));
    CHECK(document.GetSource().states[0].blendSamples.size() == 4);
    REQUIRE(document.Undo());
    CHECK(document.GetSource().states[0].blendSamples.size() == 3);
}

TEST_CASE("Graph publication keeps compatible transition progress", "[AnimationController][Authoring]")
{
    auto source = MakeGraph();
    Animation::AnimationControllerStore store;
    const auto animator = store.Create({SKELETON, 1, Animation::CompileAnimationGraph(source).graph, 77});
    REQUIRE(store.SetFloat(store.ResolveParameter(animator, Animation::HashAnimationName("speed")), 1.0f));
    store.Advance(1.0 / 30.0, false);
    Animation::AnimationControllerState before;
    REQUIRE(store.GetState(animator, before));
    REQUIRE(before.transitioning);
    std::reverse(source.states.begin(), source.states.end());
    Animation::AnimationGraphMigration outcome;
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome));
    CHECK(outcome.transitionPreserved);
    Animation::AnimationControllerState after;
    REQUIRE(store.GetState(animator, after));
    CHECK(after.transitionTimeMicroseconds == before.transitionTimeMicroseconds);
    source.transitions[0].threshold = 0.25f;
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome));
    CHECK_FALSE(outcome.transitionPreserved);
    REQUIRE(store.GetState(animator, after));
    CHECK_FALSE(after.transitioning);
}

TEST_CASE("Graph parameter identity survives renames but rejects incompatible versions", "[AnimationController][Authoring]")
{
    auto source = MakeGraph();
    source.parameters[0].sourceGUID = 700;
    Animation::AnimationControllerStore store;
    const auto animator = store.Create({SKELETON, 1, Animation::CompileAnimationGraph(source).graph, 77});
    REQUIRE(store.SetFloat(store.ResolveParameter(animator, Animation::HashAnimationName("speed")), 2.5f));
    source.parameters[0].name = "velocity";
    for (auto& transition : source.transitions)
        transition.parameter = "velocity";
    Animation::AnimationGraphMigration outcome;
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome));
    std::vector<vec3> values;
    REQUIRE(store.GetParameterValues(animator, values));
    CHECK(values[0].x == Catch::Approx(2.5f));
    source.parameters[0].stateVersion += 1;
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome));
    REQUIRE(store.GetParameterValues(animator, values));
    CHECK(values[0].x == 0.0f);
}

TEST_CASE("Graph publication preserves compatible actions and retains immutable owners", "[AnimationController][Authoring]")
{
    auto source = MakeGraph();
    Animation::EditableGraphActionSlot action;
    action.semanticID = 701;
    action.clipSlotSemanticID = WALK;
    action.durationMicroseconds = 1000000;
    source.actionSlots.push_back(action);
    SetFixture fixture;
    auto compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    std::weak_ptr<const Animation::CompiledAnimationGraph> retired = compiled.graph;
    Animation::AnimationControllerStore store;
    const auto animator = store.Create({SKELETON, 1, compiled.graph, 77});
    REQUIRE(store.BindAnimationData(animator, fixture.view, CLIPS));
    const auto token = store.StartAction(animator, 701);
    REQUIRE(token);
    store.Advance(1.0 / 60.0, false);
    Animation::AnimationGraphMigration outcome;
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome));
    CHECK(outcome.actionsPreserved == 1);
    CHECK(outcome.actionsInterrupted == 0);
    CHECK_FALSE(retired.expired());
    compiled.graph.reset();
    CHECK(retired.expired());
    Animation::AnimationActionStatus status;
    REQUIRE(store.GetActionStatus(token, status));
    CHECK(status == Animation::AnimationActionStatus::Playing);
    source.actionSlots[0].durationMicroseconds += 1;
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome));
    CHECK(outcome.actionsInterrupted == 1);
    REQUIRE(store.GetActionStatus(token, status));
    CHECK(status == Animation::AnimationActionStatus::Interrupted);
    REQUIRE(store.ReplaceGraph(animator, 1, Animation::CompileAnimationGraph(source).graph, &outcome));
    CHECK(outcome.actionsInterrupted == 0);
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

TEST_CASE("Compiled layers emit bounded ordered operations only while active", "[AnimationController][Layers]")
{
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.parameters = {{"upperBody", FileFormat::Animation::GraphValueType::Float}};
    source.states = {{1, IDLE, 1000000}};
    source.entryStateGUID = 1;
    source.layers = {
        {10, WALK, 5001, 1000000, Animation::AnimationLayerType::Override, "upperBody", 1.0f, 20, "Upper body"},
        {11, RUN, FileFormat::INVALID_ASSET_ID, 800000, Animation::AnimationLayerType::Additive, "", 0.25f, 10, "Breathing"}
    };
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    REQUIRE(compiled.graph->layers.size() == 2);
    CHECK(compiled.graph->layers[0].executionOrder == 10);
    CHECK(compiled.graph->layers[1].executionOrder == 20);

    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 77});
    REQUIRE(animator);
    Animation::PoseRecipe recipe = store.RequestPose(animator, fixture.view, CLIPS);
    REQUIRE(recipe.layerCount == 1);
    CHECK(recipe.layers[0].type == Animation::AnimationLayerType::Additive);
    CHECK(recipe.layers[0].weight == Catch::Approx(0.25f));
    const Animation::AnimationParameterHandle upperBody = store.ResolveParameter(animator, Animation::HashAnimationName("upperBody"));
    REQUIRE(upperBody);
    REQUIRE(store.SetFloat(upperBody, 0.5f));
    recipe = store.RequestPose(animator, fixture.view, CLIPS);
    REQUIRE(recipe.layerCount == 2);
    CHECK(recipe.layers[0].executionOrder < recipe.layers[1].executionOrder);
    CHECK(recipe.layers[1].maskAssetID == 5001);
    CHECK(recipe.layers[1].weight == Catch::Approx(0.5f));
}

TEST_CASE("Actions return generation-safe status and emit crossed events offscreen", "[AnimationController][Actions][Events]")
{
    constexpr u64 ATTACK = 0x5001;
    constexpr u32 HIT_EVENT = 0x6001;
    const std::array<FileFormat::Animation::AnimationEvent, 2> events = {{{0, 0x6000, 0, 0, 0, 0}, {100000, HIT_EVENT, 0x7001, 0, 0, 2}}};
    const std::array<u8, 2> payload = {0xAB, 0xCD};
    std::array<Animation::AnimationClipDomainRecord, 4> clips = CLIPS;
    clips[2].events = events;
    clips[2].eventPayloadBytes = payload;
    clips[2].looping = false;
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.parameters = {{"actionPhase", FileFormat::Animation::GraphValueType::Float}};
    source.states = {{1, IDLE, 1000000}};
    source.entryStateGUID = 1;
    source.actionSlots = {{ATTACK, WALK, FileFormat::INVALID_ASSET_ID, 200000, 20000, 20000, Animation::AnimationLayerType::Override, 10, true, true, "Attack"}};
    source.outputs = {{0x8001, "actionPhase"}};
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 10});
    REQUIRE(animator);
    REQUIRE(store.BindAnimationData(animator, fixture.view, clips));
    const Animation::AnimationParameterHandle actionPhase = store.ResolveParameter(animator, Animation::HashAnimationName("actionPhase"));
    REQUIRE(actionPhase);
    REQUIRE(store.SetFloat(actionPhase, 0.75f));
    const Animation::AnimationActionToken action = store.StartAction(animator, ATTACK, 99);
    REQUIRE(action);
    Animation::AnimationActionStatus status;
    REQUIRE(store.GetActionStatus(action, status));
    CHECK(status == Animation::AnimationActionStatus::Playing);
    store.Advance(1.0 / 60.0);
    REQUIRE(store.GetGraphOutputs(animator).size() == 1);
    CHECK(store.GetGraphOutputs(animator)[0].value.x == Catch::Approx(0.75f));
    REQUIRE(store.GetAuthoritativeEvents(animator).size() == 1);
    CHECK(store.GetAuthoritativeEvents(animator)[0].eventTypeID == 0x6000);
    for (u32 tick = 0; tick < 5; ++tick)
        store.Advance(1.0 / 60.0);
    REQUIRE(store.GetAuthoritativeEvents(animator).size() == 1);
    CHECK(store.GetAuthoritativeEvents(animator)[0].eventTypeID == HIT_EVENT);
    CHECK(store.GetAuthoritativeEvents(animator)[0].payload == std::vector<u8>{0xAB, 0xCD});
    for (u32 tick = 0; tick < 6; ++tick)
        store.Advance(1.0 / 60.0);
    REQUIRE(store.GetActionStatus(action, status));
    CHECK(status == Animation::AnimationActionStatus::Completed);
    CHECK(store.GetStats().poseRequests == 0);
}

TEST_CASE("Action priority interruption cancellation and locomotion continuation are explicit", "[AnimationController][Actions]")
{
    constexpr u64 LOW = 0x5101;
    constexpr u64 HIGH = 0x5102;
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.states = {{1, IDLE, 1'000'000}};
    source.entryStateGUID = 1;
    source.actionSlots = {
        {LOW, WALK, FileFormat::INVALID_ASSET_ID, 200'000, 20'000, 20'000, Animation::AnimationLayerType::Override, 1, false, true, "Low"},
        {HIGH, RUN, FileFormat::INVALID_ASSET_ID, 200'000, 20'000, 20'000, Animation::AnimationLayerType::Override, 10, true, true, "High"}
    };
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 10});
    REQUIRE(store.BindAnimationData(animator, fixture.view, CLIPS));
    const Animation::AnimationActionToken low = store.StartAction(animator, LOW);
    REQUIRE(low);
    const Animation::AnimationActionToken high = store.StartAction(animator, HIGH);
    REQUIRE(high);
    Animation::AnimationActionStatus status;
    REQUIRE(store.GetActionStatus(low, status));
    CHECK(status == Animation::AnimationActionStatus::Interrupted);
    REQUIRE(store.GetActionStatus(high, status));
    CHECK(status == Animation::AnimationActionStatus::Playing);
    const Animation::AnimationActionToken rejectedLow = store.StartAction(animator, LOW);
    REQUIRE(rejectedLow);
    REQUIRE(store.GetActionStatus(rejectedLow, status));
    CHECK(status == Animation::AnimationActionStatus::Cancelled);
    Animation::PoseRecipe recipe = store.RequestPose(animator, fixture.view, CLIPS);
    REQUIRE(recipe.sampleCount == 1);
    REQUIRE(recipe.layerCount == 1);
    CHECK((recipe.samples[0].clipAssetID == IDLE_CLIP_A || recipe.samples[0].clipAssetID == IDLE_CLIP_B));
    CHECK(recipe.layers[0].sample.clipAssetID == RUN_CLIP);
    REQUIRE(store.CancelAction(high));
    REQUIRE(store.GetActionStatus(high, status));
    CHECK(status == Animation::AnimationActionStatus::Cancelled);
}

TEST_CASE("Authoritative and cosmetic event queues have separate bounded overflow policy", "[AnimationController][Events]")
{
    constexpr u64 ACTION = 0x5201;
    std::vector<FileFormat::Animation::AnimationEvent> events;
    for (u32 index = 0; index < 70; ++index)
    {
        events.push_back({0, 0x6400 + index, 0, 0, 0, 0});
        events.push_back({0, 0x6500 + index, 0, 1, 0, 0});
    }
    std::array<Animation::AnimationClipDomainRecord, 4> clips = CLIPS;
    clips[2].events = events;
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.states = {{1, IDLE, 1'000'000}};
    source.entryStateGUID = 1;
    source.actionSlots = {{ACTION, WALK, FileFormat::INVALID_ASSET_ID, 200'000, 0, 0, Animation::AnimationLayerType::Override, 1, true, true, "Event burst"}};
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 10});
    REQUIRE(store.BindAnimationData(animator, fixture.view, clips));
    REQUIRE(store.StartAction(animator, ACTION));
    store.Advance(1.0 / 60.0);
    CHECK(store.GetAuthoritativeEvents(animator).size() == 64);
    CHECK(store.GetCosmeticEvents(animator).size() == 64);
    CHECK(store.GetStats().authoritativeEventOverflows == 6);
    CHECK(store.GetStats().cosmeticEventDrops == 6);
}

TEST_CASE("State events scan loop wraps offscreen from one deterministic leader", "[AnimationController][Events]")
{
    const std::array<FileFormat::Animation::AnimationEvent, 2> events = {{{0, 0x6100, 0, 0, 0, 0}, {25'000, 0x6101, 0, 1, 0, 0}}};
    std::array<Animation::AnimationClipDomainRecord, 4> clips = CLIPS;
    clips[0].durationMicroseconds = 50'000;
    clips[0].events = events;
    clips[0].looping = true;
    clips[1].durationMicroseconds = 50'000;
    clips[1].events = events;
    clips[1].looping = true;
    clips[1].durationMicroseconds = 50'000;
    clips[1].events = events;
    clips[1].looping = true;
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.states = {{1, IDLE, 50'000}};
    source.entryStateGUID = 1;
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 42});
    REQUIRE(animator);
    REQUIRE(store.BindAnimationData(animator, fixture.view, clips));
    store.Advance(1.0 / 60.0);
    REQUIRE(store.GetAuthoritativeEvents(animator).size() == 1);
    CHECK(store.GetAuthoritativeEvents(animator)[0].eventTypeID == 0x6100);
    CHECK(store.GetCosmeticEvents(animator).empty());
    store.Advance(1.0 / 60.0);
    REQUIRE(store.GetCosmeticEvents(animator).size() == 1);
    CHECK(store.GetCosmeticEvents(animator)[0].eventTypeID == 0x6101);
    store.Advance(1.0 / 60.0);
    REQUIRE(store.GetAuthoritativeEvents(animator).size() == 1);
    CHECK(store.GetAuthoritativeEvents(animator)[0].eventTypeID == 0x6100);
    CHECK(store.GetStats().poseRequests == 0);
}

TEST_CASE("Family-compatible concrete clips retain state layer and action events", "[AnimationController][Events][FamilyEvents]")
{
    const bool withLayer = GENERATE(false, true);
    constexpr u64 familyID = 0x9001;
    constexpr u64 actionID = 0x9002;
    const std::array<FileFormat::Animation::AnimationEvent, 1> events = {{{1000, 0x6100, 0x7100, 0, 0, 2}}};
    const std::array<u8, 2> payload = {0xAB, 0xCD};
    auto clips = CLIPS;
    for (auto& clip : clips)
    {
        clip.compatibleFamilyAssetID = familyID;
        clip.events = events;
        clip.eventPayloadBytes = payload;
    }
    SetFixture fixture;
    fixture.view.root.declaredPoseDomainType = FileFormat::Animation::PoseDomainType::RigFamily;
    fixture.view.root.declaredPoseDomainAssetID = familyID;
    Animation::EditableAnimationGraph graph;
    graph.states = {{1, IDLE, 60000000}};
    graph.entryStateGUID = 1;
    graph.actionSlots = {{actionID, WALK, FileFormat::INVALID_ASSET_ID, 200000, 0, 0, Animation::AnimationLayerType::Override, 1, true, true, "Family action"}};
    if (withLayer)
    {
        Animation::EditableGraphLayer layer;
        layer.sourceGUID = 2;
        layer.clipSlotSemanticID = RUN;
        layer.durationMicroseconds = 800000;
        layer.constantWeight = 1.0f;
        layer.emitEvents = true;
        graph.layers.push_back(layer);
    }
    const auto compiled = Animation::CompileAnimationGraph(graph);
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const auto animator = store.Create({SKELETON + 1, 1, compiled.graph, 42});
    REQUIRE(animator);
    REQUIRE(store.BindAnimationData(animator, fixture.view, clips));
    REQUIRE(store.RequestPose(animator, fixture.view, clips).sampleCount == 1);
    store.Advance(1.0 / 60.0, false);
    REQUIRE(store.GetAuthoritativeEvents(animator).size() == (withLayer ? 2 : 1));
    CHECK(store.GetAuthoritativeEvents(animator)[0].payload == std::vector<u8>{0xAB, 0xCD});
    REQUIRE(store.StartAction(animator, actionID));
    store.Advance(1.0 / 60.0, false);
    REQUIRE(store.GetAuthoritativeEvents(animator).size() == 1);
    CHECK(store.GetAuthoritativeEvents(animator)[0].payloadTypeID == 0x7100);
    for (auto& clip : clips) clip.compatibleFamilyAssetID = FileFormat::INVALID_ASSET_ID;
    REQUIRE(store.BindAnimationData(animator, fixture.view, clips));
    REQUIRE_FALSE(store.StartAction(animator, actionID));
}

TEST_CASE("Seek and reverse-time updates follow explicit no-replay policies", "[AnimationController][Events][Seek]")
{
    const std::array<FileFormat::Animation::AnimationEvent, 1> events = {{{25'000, 0x6150, 0, 0, 0, 0}}};
    std::array<Animation::AnimationClipDomainRecord, 4> clips = CLIPS;
    clips[0].durationMicroseconds = 50'000;
    clips[0].events = events;
    clips[0].looping = true;
    clips[1].durationMicroseconds = 50'000;
    clips[1].events = events;
    clips[1].looping = true;
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.states = {{1, IDLE, 50'000}};
    source.entryStateGUID = 1;
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 42});
    REQUIRE(store.BindAnimationData(animator, fixture.view, clips));
    store.Advance(2.0 / 60.0);
    REQUIRE(store.GetAuthoritativeEvents(animator).size() == 1);
    REQUIRE(store.Seek(animator, 40'000));
    CHECK(store.GetAuthoritativeEvents(animator).empty());
    Animation::AnimationControllerState before;
    REQUIRE(store.GetState(animator, before));
    store.Advance(-1.0);
    Animation::AnimationControllerState after;
    REQUIRE(store.GetState(animator, after));
    CHECK(after.stateTimeMicroseconds == before.stateTimeMicroseconds);
    CHECK(store.GetAuthoritativeEvents(animator).empty());
}

TEST_CASE("Blend spaces emit events from only their synchronization leader", "[AnimationController][BlendSpace][Events]")
{
    const std::array<FileFormat::Animation::AnimationEvent, 1> events = {{{25'000, 0x6200, 0, 0, 0, 0}}};
    std::array<Animation::AnimationClipDomainRecord, 4> clips = CLIPS;
    clips[2].events = events;
    clips[2].looping = true;
    clips[3].events = events;
    clips[3].looping = true;
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.parameters = {{"speed", FileFormat::Animation::GraphValueType::Float}};
    Animation::EditableGraphState state;
    state.sourceGUID = 1;
    state.durationMicroseconds = 1'000'000;
    state.kind = Animation::GraphStateKind::BlendSpace1D;
    state.inputX = "speed";
    state.blendSamples = {{{1.0f, 0.0f}, WALK}, {{3.0f, 0.0f}, RUN}};
    source.states = {state};
    source.entryStateGUID = 1;
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 42});
    REQUIRE(store.BindAnimationData(animator, fixture.view, clips));
    REQUIRE(store.SetFloat(store.ResolveParameter(animator, Animation::HashAnimationName("speed")), 2.0f));
    store.Advance(1.0 / 60.0);
    const Animation::PoseRecipe recipe = store.RequestPose(animator, fixture.view, clips);
    REQUIRE(recipe.sampleCount == 2);
    store.Advance(1.0 / 60.0);
    REQUIRE(store.GetAuthoritativeEvents(animator).size() == 1);
    CHECK(store.GetAuthoritativeEvents(animator)[0].eventTypeID == 0x6200);
}

TEST_CASE("Graph layers emit events only when independently declared", "[AnimationController][Layers][Events]")
{
    const std::array<FileFormat::Animation::AnimationEvent, 1> idleEvents = {{{0, 0x6300, 0, 0, 0, 0}}};
    const std::array<FileFormat::Animation::AnimationEvent, 1> layerEvents = {{{0, 0x6301, 0, 0, 0, 0}}};
    std::array<Animation::AnimationClipDomainRecord, 4> clips = CLIPS;
    clips[0].events = idleEvents;
    clips[0].looping = true;
    clips[1].events = idleEvents;
    clips[1].looping = true;
    clips[2].events = layerEvents;
    clips[2].looping = true;
    SetFixture fixture;
    Animation::EditableAnimationGraph source;
    source.states = {{1, IDLE, 1'000'000}};
    source.entryStateGUID = 1;
    Animation::EditableGraphLayer layer = {2, WALK, FileFormat::INVALID_ASSET_ID, 1'366'667, Animation::AnimationLayerType::Override, "", 1.0f, 0, "Independent events"};
    layer.emitEvents = true;
    source.layers = {layer};
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 42});
    REQUIRE(store.BindAnimationData(animator, fixture.view, clips));
    store.Advance(1.0 / 60.0);
    REQUIRE(store.GetAuthoritativeEvents(animator).size() == 2);
    CHECK(store.GetAuthoritativeEvents(animator)[0].eventTypeID == 0x6300);
    CHECK(store.GetAuthoritativeEvents(animator)[1].eventTypeID == 0x6301);
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

TEST_CASE("IK conservative expansion is included in pre-pose culling bounds", "[AnimationDeformation][AnimationBounds][IK]")
{
    FileFormat::Animation::Bounds base;
    base.center = vec3(0.0f, 1.0f, 0.0f);
    base.extents = vec3(0.25f, 0.75f, 0.2f);
    base.sphereRadius = 0.85f;
    const vec3 authoredExpansion(0.5f, 0.25f, 0.75f);
    const FileFormat::Animation::Bounds expanded = AnimationLoading::ExpandAnimationBounds(base, authoredExpansion);
    CHECK(expanded.center == base.center);
    CHECK(expanded.extents == vec3(0.75f, 1.0f, 0.95f));
    CHECK(expanded.sphereRadius == Catch::Approx(base.sphereRadius + glm::length(authoredExpansion)));
}

TEST_CASE("Graph IK operations compile into bounded target-domain pose recipes", "[AnimationController][IK]")
{
    Animation::EditableAnimationGraph source;
    Animation::EditableGraphParameter goal;
    goal.name = "leftHandGoal";
    goal.type = FileFormat::Animation::GraphValueType::Float3;
    goal.defaultFloat3 = vec3(0.25f, 1.0f, 0.5f);
    Animation::EditableGraphParameter weight;
    weight.name = "ikWeight";
    weight.type = FileFormat::Animation::GraphValueType::Float;
    weight.defaultFloat = 0.5f;
    source.parameters = {goal, weight};
    source.states = {{1, IDLE, 1'000'000}};
    source.entryStateGUID = 1;
    Animation::EditableGraphIKOperation operation;
    operation.sourceGUID = 2;
    operation.rigAssetID = 0x7001;
    operation.solverSemanticID = 0x7002;
    operation.goalAttachmentSemanticID = 0x7003;
    operation.effectorAttachmentSemanticID = 0x7004;
    operation.effectorAlignmentAxis = vec3(1.0f, 0.0f, 0.0f);
    operation.goalParameter = "leftHandGoal";
    operation.weightParameter = "ikWeight";
    operation.constantWeight = 0.8f;
    source.ikOperations = {operation};
    const Animation::GraphCompileResult compiled = Animation::CompileAnimationGraph(source);
    REQUIRE(compiled);
    REQUIRE(compiled.graph->ikOperations.size() == 1);
    CHECK(compiled.graph->ikOperations[0].goalAttachmentSemanticID == 0x7003);
    CHECK(compiled.graph->ikOperations[0].effectorAttachmentSemanticID == 0x7004);
    CHECK(compiled.graph->ikOperations[0].effectorAlignmentAxis == vec3(1.0f, 0.0f, 0.0f));

    SetFixture fixture;
    Animation::AnimationControllerStore store;
    const Animation::AnimatorHandle animator = store.Create({SKELETON, 1, compiled.graph, 42});
    REQUIRE(animator);
    const Animation::AnimationParameterHandle goalHandle = store.ResolveParameter(animator, Animation::HashAnimationName("leftHandGoal"));
    const Animation::AnimationParameterHandle weightHandle = store.ResolveParameter(animator, Animation::HashAnimationName("ikWeight"));
    REQUIRE(store.SetFloat3(goalHandle, vec3(1.0f, 2.0f, 3.0f)));
    REQUIRE(store.SetFloat(weightHandle, 0.25f));
    const Animation::PoseRecipe recipe = store.RequestPose(animator, fixture.view, CLIPS);
    REQUIRE(recipe.ikOperationCount == 1);
    CHECK(recipe.ikOperations[0].rigAssetID == 0x7001);
    CHECK(recipe.ikOperations[0].solverSemanticID == 0x7002);
    CHECK(recipe.ikOperations[0].goalAttachmentSemanticID == 0x7003);
    CHECK(recipe.ikOperations[0].effectorAttachmentSemanticID == 0x7004);
    CHECK(recipe.ikOperations[0].effectorAlignmentAxis == vec3(1.0f, 0.0f, 0.0f));
    CHECK(recipe.ikOperations[0].goalModel == vec3(1.0f, 2.0f, 3.0f));
    CHECK(recipe.ikOperations[0].weight == Catch::Approx(0.2f));
    Animation::AnimationControllerState state;
    REQUIRE(store.GetState(animator, state));
    CHECK(state.activeIKOperations == 1);

    FileFormat::AssetID boundsClip = FileFormat::INVALID_ASSET_ID;
    u64 boundsTime = 0;
    bool tightBounds = true;
    REQUIRE(store.GetBoundsMetadata(animator, fixture.view, CLIPS, boundsClip, boundsTime, tightBounds));
    CHECK_FALSE(tightBounds);
    REQUIRE(store.SetFloat(weightHandle, 0.0f));
    CHECK(store.RequestPose(animator, fixture.view, CLIPS).ikOperationCount == 0);
    REQUIRE(store.GetState(animator, state));
    CHECK(state.activeIKOperations == 0);

    Animation::EditableAnimationGraph invalid = source;
    invalid.parameters[0].type = FileFormat::Animation::GraphValueType::Float;
    CHECK_FALSE(Animation::CompileAnimationGraph(invalid));
    invalid = source;
    invalid.ikOperations.resize(5, operation);
    CHECK_FALSE(Animation::CompileAnimationGraph(invalid));
}
