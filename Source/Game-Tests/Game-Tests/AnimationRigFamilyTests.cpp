#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetDocument.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationRetarget.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationClipSampler.h"
#include "Game-Lib/Rendering/Animation/Controller/ControllerPosePlayer.h"
#include "Game-Lib/Rendering/Animation/Controller/AnimationSemantic.h"

#include <Base/Memory/Bytebuffer.h>
#include <catch2/catch2.hpp>

#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace
{
    namespace F = FileFormat::Animation;

    struct FamilyFixture
    {
        std::vector<F::RigFamilyJoint> joints = {{11, -1, F::RigFamilyJointFlags_Required}, {22, 0, F::RigFamilyJointFlags_Required}};
        std::vector<F::SkeletonJoint> concrete = {F::SkeletonJoint{}, F::SkeletonJoint{}};
        std::vector<F::SkeletonFamilyBinding> bindings = {{0, 0}, {1, 1}};
        AnimationLoading::RigFamilyAssetView Family() { AnimationLoading::RigFamilyAssetView v; v.joints = joints; return v; }
        AnimationLoading::SkeletonAssetView Skeleton() { AnimationLoading::SkeletonAssetView v; v.joints = concrete; v.familyBindings = bindings; return v; }
        FamilyFixture()
        {
            concrete[0].semanticID = 91;
            concrete[0].familyJointIndex = 0;
            concrete[1].semanticID = 92;
            concrete[1].parentJointIndex = 0;
            concrete[1].familyJointIndex = 1;
        }
    };
}

TEST_CASE("RigFamily exceptional bridges require explicit complete mappings", "[rigfamily]")
{
    FamilyFixture fixture;
    AnimationLoading::RetargetProfileAssetView profile;
    std::vector<F::RetargetJointMapping> mappings = {{11, 0, 0}, {22, 1, 1}};
    profile.jointMappings = mappings;
    Animation::FamilyPoseBridge bridge;
    REQUIRE(bridge.Initialize(profile, fixture.Family(), fixture.Family()));
    std::vector<F::LocalTransform> source(2), result;
    source[1].rotation = glm::angleAxis(0.3f, vec3(0, 1, 0));
    REQUIRE(bridge.ConvertPose(source, result));
    REQUIRE(std::abs(glm::dot(source[1].rotation, result[1].rotation)) > 0.99999f);
    mappings[1].targetJointIndex = 0;
    REQUIRE_FALSE(bridge.Initialize(profile, fixture.Family(), fixture.Family()));
}

TEST_CASE("RigFamily controller converts demanded domains and preserves native bypass", "[rigfamily][fixture]")
{
    const char* directory = std::getenv("NOVUS_RIG_FAMILY_FIXTURES");
    if (!directory) { WARN("Set NOVUS_RIG_FAMILY_FIXTURES for controller conformance"); return; }
    auto bytes = [&](const char* filename)
    {
        std::ifstream stream(std::filesystem::path(directory) / filename, std::ios::binary);
        REQUIRE(stream.good());
        return std::vector<u8>(std::istreambuf_iterator<char>(stream), {});
    };
    auto id = [](std::string_view path) { return XXHash64::hash(path.data(), path.size(), 0); };
    AnimationLoading::AnimationAssetRegistry assets;
    const auto family = assets.PublishRigFamily(id("animation/bipedal_humanoid.rigfamily"), bytes("bipedal_humanoid.rigfamily"));
    const auto reference = assets.PublishSkeleton(id("animation/retarget/reference.skeleton"), bytes("reference.skeleton"));
    const auto frames = assets.PublishSkeleton(id("animation/retarget/frames.skeleton"), bytes("frames.skeleton"));
    const auto target = assets.PublishSkeleton(id("animation/retarget/proportions.skeleton"), bytes("proportions.skeleton"));
    const auto idle = assets.PublishAnimationClip(id("animation/retarget/idle.animationclip"), bytes("idle.animationclip"));
    const auto alternate = assets.PublishAnimationClip(id("animation/retarget/frames_idle.animationclip"), bytes("frames_idle.animationclip"));
    const auto canonical = assets.PublishAnimationClip(id("animation/retarget/family_idle.animationclip"), bytes("family_idle.animationclip"));
    const auto set = assets.PublishAnimationSet(id("animation/retarget/family.animationset"), bytes("family.animationset"));
    REQUIRE(family); REQUIRE(reference); REQUIRE(frames); REQUIRE(target); REQUIRE(idle); REQUIRE(alternate); REQUIRE(canonical); REQUIRE(set);
    Animation::EditableAnimationGraph graph;
    graph.parameters = {{"speed", F::GraphValueType::Float}};
    graph.states = {{1, Animation::HashAnimationSlot("Idle"), 60000000}, {2, Animation::HashAnimationSlot("WalkForward"), 60000000}};
    graph.entryStateGUID = 1;
    graph.transitions = {{1, 2, "speed", Animation::GraphComparison::Greater, 0.5f, 1000000}};
    Animation::EditableGraphLayer layer;
    layer.sourceGUID = 3;
    layer.clipSlotSemanticID = Animation::HashAnimationSlot("Idle");
    layer.durationMicroseconds = 60000000;
    layer.constantWeight = 0.25f;
    layer.composeInSourceDomain = true;
    graph.layers = {layer};
    const auto compiled = Animation::CompileAnimationGraph(graph);
    REQUIRE(compiled);
    Animation::PoseSourceStore poses;
    Animation::ControllerPosePlayer player(&poses, &assets);
    const auto handle = player.Create(target, set, *assets.Get(target), *assets.Get(set), compiled.graph, 1);
    REQUIRE(handle);
    const auto pose = player.GetPoseSource(handle);
    player.Advance(0.05, false);
    REQUIRE(player.RequestPose(pose));
    REQUIRE(player.GetStats().familyConversions == 1);
    REQUIRE(player.RequestPose(pose));
    REQUIRE(player.GetStats().familyConversions == 1);
    REQUIRE(player.SetFloat(player.ResolveParameter(handle, Animation::HashAnimationName("speed")), 1));
    player.Advance(0.1, false);
    REQUIRE(player.RequestPose(pose));
    REQUIRE(player.GetStats().familyConversions == 3);
    // An inactive source-local layer must not become a second target-local layer.
    const auto prior = player.GetStats().familyConversions;
    player.Advance(2.0, false);
    REQUIRE(player.RequestPose(pose));
    REQUIRE(player.GetStats().familyConversions == prior + 1);
    REQUIRE(player.Destroy(handle));
    const auto maskID = id("animation/retarget/upper.bonemask");
    REQUIRE(assets.PublishBoneMask(maskID, bytes("upper.bonemask")));
    auto layeredGraph = graph;
    layeredGraph.states.resize(1);
    layeredGraph.transitions.clear();
    layeredGraph.layers[0].composeInSourceDomain = false;
    layeredGraph.layers[0].clipSlotSemanticID = Animation::HashAnimationSlot("WalkForward");
    layeredGraph.layers[0].maskAssetID = maskID;
    const auto layeredCompiled = Animation::CompileAnimationGraph(layeredGraph);
    REQUIRE(layeredCompiled);
    const auto layered = player.Create(target, set, *assets.Get(target), *assets.Get(set), layeredCompiled.graph, 1);
    REQUIRE(layered);
    const auto beforeLayer = player.GetStats().familyConversions;
    player.Advance(0.05, false);
    REQUIRE(player.RequestPose(player.GetPoseSource(layered)));
    REQUIRE(player.GetStats().familyConversions == beforeLayer + 2);
    REQUIRE(player.Destroy(layered));
    const auto nativeSet = assets.PublishAnimationSet(id("animation/retarget/native.animationset"), bytes("native.animationset"));
    const auto native = player.Create(reference, nativeSet, *assets.Get(reference), *assets.Get(nativeSet), compiled.graph, 1);
    REQUIRE(native);
    const auto before = player.GetStats().familyConversions;
    player.Advance(0.05, false);
    REQUIRE(player.RequestPose(player.GetPoseSource(native)));
    REQUIRE(player.GetStats().familyConversions == before);
    REQUIRE(player.Destroy(native));
    auto alienFamilyBytes = bytes("bipedal_humanoid.rigfamily");
    const auto familyView = AnimationLoading::AnimationAssetReader::ReadRigFamily(alienFamilyBytes);
    REQUIRE(familyView);
    F::RetargetProfileAsset profileAsset;
    profileAsset.sourceRigFamilyAssetID = id("animation/bipedal_humanoid.rigfamily");
    profileAsset.targetRigFamilyAssetID = id("animation/unrelated.rigfamily");
    F::RetargetProfileData profileData;
    for (u32 index = 0; index < familyView.view.joints.size(); ++index)
    {
        const u64 semantic = familyView.view.joints[index].semanticID ^ 0x1000000000000000ull;
        std::memcpy(alienFamilyBytes.data() + familyView.view.root.jointsOffset + index * sizeof(F::RigFamilyJoint), &semantic, sizeof(semantic));
        profileData.jointMappings.push_back({semantic, static_cast<i32>(index), static_cast<i32>(index)});
    }
    REQUIRE(assets.PublishRigFamily(profileAsset.targetRigFamilyAssetID, alienFamilyBytes));
    auto alienSkeletonBytes = bytes("proportions.skeleton");
    std::memcpy(alienSkeletonBytes.data() + offsetof(F::SkeletonAsset, rigFamilyAssetID), &profileAsset.targetRigFamilyAssetID, sizeof(FileFormat::AssetID));
    const auto alien = assets.PublishSkeleton(id("animation/unrelated.skeleton"), alienSkeletonBytes);
    const auto bridgeSet = assets.PublishAnimationSet(id("animation/bridge.animationset"), bytes("family.animationset"));
    REQUIRE(alien); REQUIRE(bridgeSet);
    REQUIRE_FALSE(player.Create(alien, bridgeSet, *assets.Get(alien), *assets.Get(bridgeSet), compiled.graph, 1));
    auto buffer = Bytebuffer::BorrowRuntime(profileAsset.GetSerializedSize(profileData));
    REQUIRE(profileAsset.Save(buffer, profileData));
    const auto profileID = id("animation/explicit.retargetprofile");
    REQUIRE(assets.PublishRetargetProfile(profileID, std::span<const u8>(buffer->GetDataPointer(), buffer->writtenData)));
    const auto bridged = player.Create(alien, bridgeSet, *assets.Get(alien), *assets.Get(bridgeSet), compiled.graph, 1, profileID);
    REQUIRE(bridged);
    player.Advance(0.05, false);
    REQUIRE(player.RequestPose(player.GetPoseSource(bridged)));
    REQUIRE(assets.PublishRigFamily(profileAsset.sourceRigFamilyAssetID, bytes("bipedal_humanoid.rigfamily")));
    // Bound playback keeps its immutable generation until an accepted replacement is published.
    REQUIRE(player.RequestPose(player.GetPoseSource(bridged)));
    std::string error;
    REQUIRE(player.QueueAsset(Animation::AnimationDocumentType::Skeleton, id("animation/unrelated.skeleton"), alienSkeletonBytes, error));
    player.Advance(0.05, false);
    CHECK(player.GetAssetPublicationStatus().starts_with("Published"));
    REQUIRE(player.RequestPose(player.GetPoseSource(bridged)));
    const auto poseBefore = poses.Get(player.GetPoseSource(bridged));
    REQUIRE(poseBefore);
    const auto retainedSkeleton = poseBefore->skeleton;
    auto incompatible = alienSkeletonBytes;
    const auto decoded = AnimationLoading::AnimationAssetReader::ReadSkeleton(incompatible);
    REQUIRE(decoded);
    const u64 renamed = 987654321;
    std::memcpy(incompatible.data() + decoded.view.root.jointsOffset, &renamed, sizeof(renamed));
    REQUIRE(player.QueueAsset(Animation::AnimationDocumentType::Skeleton, id("animation/unrelated.skeleton"), incompatible, error));
    player.Advance(0.05, false);
    CHECK(player.GetAssetPublicationStatus().starts_with("Rejected"));
    CHECK(poses.Get(player.GetPoseSource(bridged))->skeleton == retainedSkeleton);
    REQUIRE(player.RequestPose(player.GetPoseSource(bridged)));
    REQUIRE(player.Destroy(bridged));
}

TEST_CASE("RigFamily validates logical ancestry and canonical frames", "[animation][rigfamily]")
{
    FamilyFixture fixture;
    REQUIRE(AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::RigFamilyAssetReadResult{fixture.Family(), {}}));
    REQUIRE_FALSE(AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(fixture.Skeleton(), fixture.Family()));
    SECTION("Missing required joint") { fixture.bindings.pop_back(); REQUIRE(AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(fixture.Skeleton(), fixture.Family())); }
    SECTION("Duplicate binding") { fixture.bindings[1].familyJointIndex = 0; REQUIRE(AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(fixture.Skeleton(), fixture.Family())); }
    SECTION("Reparented inherited joint") { fixture.concrete[1].parentJointIndex = -1; REQUIRE(AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(fixture.Skeleton(), fixture.Family())); }
    SECTION("Noninverse calibration") { fixture.bindings[1].skeletonToFamily.scale = vec3(2); REQUIRE(AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(fixture.Skeleton(), fixture.Family())); }
    SECTION("Duplicate semantic") { fixture.joints[1].semanticID = 11; REQUIRE_FALSE(AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::RigFamilyAssetReadResult{fixture.Family(), {}})); }
    SECTION("Bad canonical quaternion") { fixture.joints[1].canonicalTransform.rotation = quat(0, 0, 0, 0); REQUIRE_FALSE(AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::RigFamilyAssetReadResult{fixture.Family(), {}})); }
    SECTION("Propagation cycle")
    {
        fixture.concrete.resize(4);
        std::vector<F::SkeletonPropagationRule> rules = {{2, 3, F::SkeletonPropagationType::CopyLocalTransform, 0, 0}, {3, 2, F::SkeletonPropagationType::CopyLocalTransform, 0, 1}};
        auto skeleton = fixture.Skeleton();
        skeleton.propagationRules = rules;
        REQUIRE(AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(skeleton, fixture.Family()));
    }
    SECTION("Model propagation cannot depend on its target's descendants")
    {
        fixture.concrete.resize(4);
        fixture.concrete[2].parentJointIndex = 1;
        fixture.concrete[3].parentJointIndex = 2;
        std::vector<F::SkeletonPropagationRule> rules = {{3, 2, F::SkeletonPropagationType::CopyModelTransform}};
        auto skeleton = fixture.Skeleton();
        skeleton.propagationRules = rules;
        REQUIRE(AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(skeleton, fixture.Family()));
    }
}

TEST_CASE("WoW corpus skeletons satisfy the neutral family contract", "[animation][rigfamily][wow][fixture]")
{
    const char* directory = std::getenv("NOVUS_WOW_ANIMATION_FIXTURES");
    if (!directory) { WARN("Set NOVUS_WOW_ANIMATION_FIXTURES for converter conformance"); return; }
    auto read = [](const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        REQUIRE(stream.good());
        return std::vector<u8>(std::istreambuf_iterator<char>(stream), {});
    };
    const auto familyBytes = read(std::filesystem::path(directory) / "animation/bipedal_humanoid.rigfamily");
    const auto family = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadRigFamily(familyBytes));
    REQUIRE(family);
    for (const std::string name : {"humanmale", "humanfemale", "orcmale", "orcfemale", "dwarfmale", "dwarffemale", "nightelfmale", "nightelffemale", "scourgemale", "scourgefemale", "taurenmale", "taurenfemale", "gnomemale", "gnomefemale", "trollmale", "trollfemale"})
    {
        INFO(name);
        const auto bytes = read(std::filesystem::path(directory) / "animations/wow" / name / (name + ".skeleton"));
        const auto skeleton = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadSkeleton(bytes));
        INFO(AssetLoading::Describe(skeleton.diagnostic));
        REQUIRE(skeleton);
        REQUIRE_FALSE(AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(skeleton.view, family.view));
        Animation::FamilyPoseBinding binding;
        REQUIRE(binding.Initialize(skeleton.view, family.view));
        std::vector<F::LocalTransform> rest, canonical(family.view.joints.size()), recovered(skeleton.view.joints.size());
        for (const auto& joint : skeleton.view.joints) rest.push_back(joint.restTransform);
        REQUIRE(binding.ToFamily(rest, canonical));
        REQUIRE(binding.FromFamily(canonical, recovered));
        if (name == "trollfemale")
        {
            const auto sourceBytes = read(std::filesystem::path(directory) / "animations/wow/humanmale/humanmale.skeleton");
            const auto source = AnimationLoading::AnimationAssetReader::ReadSkeleton(sourceBytes);
            const auto motionBytes = read(std::filesystem::path(directory) / "animations/wow/humanmale/dance-0.animationclip");
            const auto motion = AnimationLoading::AnimationAssetReader::ReadAnimationClip(motionBytes);
            REQUIRE(source);
            REQUIRE(motion);
            Animation::FamilyPoseBinding sourceBinding;
            REQUIRE(sourceBinding.Initialize(source.view, family.view));
            std::vector<F::LocalTransform> sampled(source.view.joints.size()), retargeted(skeleton.view.joints.size());
            REQUIRE(Animation::SampleClipLocalPose(motion.view, source.view, 0.1, false, sampled) == Animation::ClipSampleError::None);
            REQUIRE(sourceBinding.ToFamily(sampled, canonical));
            REQUIRE(binding.FromFamily(canonical, retargeted));
            // Coincident spine pivots must retain unit calibration, not amplify
            // a small source translation into a metre-scale torso displacement.
            REQUIRE(glm::length(retargeted[1].translation - rest[1].translation) < 0.1f);
        }
        if (name == "taurenmale" || name == "taurenfemale")
        {
            const auto sourceBytes = read(std::filesystem::path(directory) / "animations/wow/humanmale/humanmale.skeleton");
            const auto source = AnimationLoading::AnimationAssetReader::ReadSkeleton(sourceBytes);
            const auto clipBytes = read(std::filesystem::path(directory) / "animations/wow/humanmale/idle-0.animationclip");
            const auto clip = AnimationLoading::AnimationAssetReader::ReadAnimationClip(clipBytes);
            REQUIRE(source);
            REQUIRE(clip);
            Animation::FamilyPoseBinding sourceBinding;
            REQUIRE(sourceBinding.Initialize(source.view, family.view));
            std::vector<F::LocalTransform> sampled(source.view.joints.size()), retargeted(skeleton.view.joints.size());
            auto findJoint = [&](const std::string& semantic)
            {
                for (u32 index = 0; index < skeleton.view.joints.size(); ++index)
                    if (skeleton.view.joints[index].semanticID == Animation::HashAnimationSemantic("humanoid", semantic)) return index;
                FAIL("Missing lower-limb semantic");
                return 0u;
            };
            for (const f32 time : {0.0f, 0.3f, 0.6f, 0.9f})
            {
                REQUIRE(Animation::SampleClipLocalPose(clip.view, source.view, time, false, sampled) == Animation::ClipSampleError::None);
                REQUIRE(sourceBinding.ToFamily(sampled, canonical));
                REQUIRE(binding.FromFamily(canonical, retargeted));
                const auto pose = Animation::EvaluatePose(skeleton.view.joints, retargeted);
                REQUIRE(pose);
                const vec3 headForward = glm::normalize(vec3(pose.modelTransforms[findJoint("head")] * vec4(0, 0, -1, 0)));
                REQUIRE(glm::abs(headForward.y) < 0.3f);
                REQUIRE(headForward.z < -0.9f);
                for (const std::string side : {"left", "right"})
                {
                    const vec3 knee = vec3(pose.modelTransforms[findJoint(side + "leg")][3]);
                    const vec3 hock = vec3(pose.modelTransforms[findJoint(side + "foot")][3]);
                    const vec3 hoof = vec3(pose.modelTransforms[findJoint(side + "toebase")][3]);
                    // Human idle must not straighten the bent hock or tip the hoof upward.
                    REQUIRE(hock.z - knee.z > 0.1f);
                    REQUIRE(hoof.y < hock.y - 0.05f);
                }
            }
        }
        for (const char* slot : {"idle", "walk", "run", "dance"})
        {
            INFO(slot);
            const auto clipBytes = read(std::filesystem::path(directory) / "animations/wow" / name / (std::string(slot) + "-0.animationclip"));
            const auto clip = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationClip(clipBytes));
            INFO(AssetLoading::Describe(clip.diagnostic));
            REQUIRE(clip);
            REQUIRE(clip.view.root.sourcePoseDomainType == F::PoseDomainType::Skeleton);
            REQUIRE(clip.view.tracks.size() == skeleton.view.joints.size());
        }
        for (u32 index = 0; index < rest.size(); ++index)
        {
            REQUIRE(glm::length(rest[index].translation - recovered[index].translation) < 0.00001f);
            REQUIRE(std::abs(glm::dot(rest[index].rotation, recovered[index].rotation)) > 0.99999f);
        }
    }
}

TEST_CASE("RigFamily binding preserves a distinct concrete reference posture", "[rigfamily]")
{
    FamilyFixture fixture;
    fixture.bindings[1].familyReferenceRotation = glm::angleAxis(0.7f, vec3(0,0,1));
    Animation::FamilyPoseBinding binding;
    REQUIRE(binding.Initialize(fixture.Skeleton(), fixture.Family()));
    std::vector<F::LocalTransform> source(2), family(2), recovered(2);
    REQUIRE(binding.ToFamily(source, family));
    REQUIRE(std::abs(glm::dot(family[1].rotation, fixture.bindings[1].familyReferenceRotation)) > 0.99999f);
    REQUIRE(binding.FromFamily(family, recovered));
    REQUIRE(std::abs(recovered[1].rotation.w) > 0.99999f);
    source[1].rotation = glm::angleAxis(-0.4f, vec3(1,0,0));
    source[1].translation = vec3(0.1f,0.2f,0.3f);
    REQUIRE(binding.ToFamily(source, family));
    REQUIRE(binding.FromFamily(family, recovered));
    REQUIRE(std::abs(glm::dot(source[1].rotation, recovered[1].rotation)) > 0.99999f);
    REQUIRE(glm::length(source[1].translation-recovered[1].translation) < 0.00001f);
    fixture.bindings[1].familyReferenceRotation *= 2.0f;
    REQUIRE_FALSE(binding.Initialize(fixture.Skeleton(), fixture.Family()));
}

TEST_CASE("NBS RigFamily corpus validates in Game", "[animation][rigfamily][fixture]")
{
    const char* directory = std::getenv("NOVUS_RIG_FAMILY_FIXTURES");
    if (!directory) { WARN("Set NOVUS_RIG_FAMILY_FIXTURES for Blender corpus conformance"); return; }
    auto read = [](const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        REQUIRE(stream.good());
        return std::vector<u8>(std::istreambuf_iterator<char>(stream), {});
    };
    const auto familyBytes = read(std::filesystem::path(directory) / "bipedal_humanoid.rigfamily");
    const auto family = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadRigFamily(familyBytes));
    REQUIRE(family);
    const auto sourceBytes = read(std::filesystem::path(directory) / "reference.skeleton");
    const auto source = AnimationLoading::AnimationAssetReader::ReadSkeleton(sourceBytes);
    const auto clipBytes = read(std::filesystem::path(directory) / "idle.animationclip");
    const auto clip = AnimationLoading::AnimationAssetReader::ReadAnimationClip(clipBytes);
    REQUIRE(source);
    REQUIRE(clip);
    Animation::FamilyPoseBinding sourceBinding;
    REQUIRE(sourceBinding.Initialize(source.view, family.view));
    for (const char* name : {"reference", "proportions", "frames"})
    {
        INFO(name);
        const auto bytes = read(std::filesystem::path(directory) / (std::string(name) + ".skeleton"));
        const auto skeleton = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadSkeleton(bytes));
        REQUIRE(skeleton);
        REQUIRE_FALSE(AnimationLoading::AnimationAssetValidator::ValidateFamilyBinding(skeleton.view, family.view));
        Animation::FamilyPoseBinding binding;
        REQUIRE(binding.Initialize(skeleton.view, family.view));
        std::vector<F::LocalTransform> rest, canonical(family.view.joints.size()), recovered(skeleton.view.joints.size());
        for (const auto& joint : skeleton.view.joints) rest.push_back(joint.restTransform);
        REQUIRE(binding.ToFamily(rest, canonical));
        REQUIRE(binding.FromFamily(canonical, recovered));
        for (u32 index = 0; index < rest.size(); ++index)
        {
            REQUIRE(glm::length(rest[index].translation - recovered[index].translation) < 0.00001f);
            REQUIRE(std::abs(glm::dot(rest[index].rotation, recovered[index].rotation)) > 0.99999f);
        }
        for (auto& joint : rest)
        {
            joint.rotation = glm::normalize(joint.rotation * glm::angleAxis(0.3f, glm::normalize(vec3(1, 2, 3))));
            joint.translation += vec3(0.01f, 0.02f, 0.03f);
        }
        REQUIRE(binding.ToFamily(rest, canonical));
        REQUIRE(binding.FromFamily(canonical, recovered));
        for (const auto& mapped : skeleton.view.familyBindings)
        {
            const u32 index = mapped.skeletonJointIndex;
            REQUIRE(glm::length(rest[index].translation - recovered[index].translation) < 0.00001f);
            REQUIRE(std::abs(glm::dot(rest[index].rotation, recovered[index].rotation)) > 0.99999f);
        }
        for (const char* clipName : {"idle", "walk", "run"})
        {
            INFO(clipName);
            const auto motionBytes = read(std::filesystem::path(directory) / (std::string(clipName) + ".animationclip"));
            const auto motion = AnimationLoading::AnimationAssetReader::ReadAnimationClip(motionBytes);
            REQUIRE(motion);
            const auto goldenBytes = read(std::filesystem::path(directory) / (std::string(name) + "_" + clipName + ".animationclip"));
            const auto golden = AnimationLoading::AnimationAssetReader::ReadAnimationClip(goldenBytes);
            REQUIRE(golden);
            std::vector<F::LocalTransform> sampled(source.view.joints.size()), expected(skeleton.view.joints.size());
            f32 maximumPositionError = 0, maximumRotationError = 0;
            u32 maximumErrorJoint = 0, maximumErrorFrame = 0;
            for (u32 frame = 0; frame < motion.view.root.sampleCount; ++frame)
            {
                const f64 time = static_cast<f64>(motion.view.root.durationMicroseconds) * frame / (motion.view.root.sampleCount - 1) / 1000000.0;
                REQUIRE(Animation::SampleClipLocalPose(motion.view, source.view, time, false, sampled) == Animation::ClipSampleError::None);
                REQUIRE(sourceBinding.ToFamily(sampled, canonical));
                REQUIRE(binding.FromFamily(canonical, recovered));
                REQUIRE(Animation::SampleClipLocalPose(golden.view, skeleton.view, time, false, expected) == Animation::ClipSampleError::None);
                const auto expectedModel = Animation::EvaluatePose(skeleton.view.joints, expected);
                const auto actualModel = Animation::EvaluatePose(skeleton.view.joints, recovered);
                REQUIRE(expectedModel);
                REQUIRE(actualModel);
                for (u32 joint = 0; joint < expected.size(); ++joint)
                {
                    const f32 error = glm::length(vec3(expectedModel.modelTransforms[joint][3]) - vec3(actualModel.modelTransforms[joint][3]));
                    if (error > maximumPositionError) { maximumPositionError = error; maximumErrorJoint = joint; maximumErrorFrame = frame; }
                    maximumRotationError = std::max(maximumRotationError, 1.0f - std::abs(glm::dot(expected[joint].rotation, recovered[joint].rotation)));
                }
            }
            INFO("Maximum model-space position error: " << maximumPositionError << ", quaternion dot error: " << maximumRotationError);
            INFO("Worst position at joint " << maximumErrorJoint << " frame " << maximumErrorFrame);
            REQUIRE(maximumPositionError < 0.00002f);
            REQUIRE(maximumRotationError < 0.000002f);
        }
        const auto fkPose = Animation::EvaluatePose(skeleton.view.joints, recovered);
        REQUIRE(binding.FromFamily(canonical, recovered, false, true));
        const auto correctedPose = Animation::EvaluatePose(skeleton.view.joints, recovered);
        REQUIRE(correctedPose);
        std::vector<F::SkeletonJoint> familyJoints;
        std::vector<F::LocalTransform> familyRest, targetRest;
        for (const auto& joint : family.view.joints)
        {
            F::SkeletonJoint value;
            value.parentJointIndex = joint.parentJointIndex;
            value.restTransform = joint.canonicalTransform;
            familyJoints.push_back(value);
            familyRest.push_back(joint.canonicalTransform);
        }
        for (const auto& joint : skeleton.view.joints) targetRest.push_back(joint.restTransform);
        const auto restFamilyModel = Animation::EvaluatePose(familyJoints, familyRest);
        const auto restTargetModel = Animation::EvaluatePose(skeleton.view.joints, targetRest);
        const auto animatedFamilyModel = Animation::EvaluatePose(familyJoints, canonical);
        for (const auto& chain : family.view.chains)
        {
            std::array<u32, 3> concrete = {}, semantic = {};
            f32 familyLength = 0, targetLength = 0;
            for (u32 index = 0; index < 3; ++index)
            {
                semantic[index] = family.view.chainJointIndices[chain.jointIndexOffset + index];
                for (const auto& mapped : skeleton.view.familyBindings)
                    if (mapped.familyJointIndex == semantic[index]) concrete[index] = mapped.skeletonJointIndex;
                if (index > 0)
                {
                    familyLength += glm::length(vec3(restFamilyModel.modelTransforms[semantic[index]][3]) - vec3(restFamilyModel.modelTransforms[semantic[index - 1]][3]));
                    targetLength += glm::length(vec3(restTargetModel.modelTransforms[concrete[index]][3]) - vec3(restTargetModel.modelTransforms[concrete[index - 1]][3]));
                }
            }
            const vec3 goal = vec3(restTargetModel.modelTransforms[concrete[2]][3]) + (vec3(animatedFamilyModel.modelTransforms[semantic[2]][3]) - vec3(restFamilyModel.modelTransforms[semantic[2]][3])) * (targetLength / familyLength);
            REQUIRE(glm::length(vec3(correctedPose.modelTransforms[concrete[2]][3]) - goal) <= glm::length(vec3(fkPose.modelTransforms[concrete[2]][3]) - goal) + 0.0001f);
        }
        std::vector<F::LocalTransform> identity(family.view.joints.size());
        REQUIRE(binding.FromFamily(identity, recovered, true));
        for (const auto& joint : recovered)
        {
            REQUIRE(glm::length(joint.translation) < 0.00001f);
            REQUIRE(std::abs(joint.rotation.w) > 0.99999f);
            REQUIRE(glm::length(joint.scale - vec3(1)) < 0.00001f);
        }
    }
}
