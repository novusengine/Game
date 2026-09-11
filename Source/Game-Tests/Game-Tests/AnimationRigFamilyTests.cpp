#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetDocument.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationRetarget.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationClipSampler.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationIK.h"
#include "Game-Lib/Rendering/Animation/Controller/ControllerPosePlayer.h"
#include "Game-Lib/Rendering/Animation/Controller/AnimationSemantic.h"

#include <Base/Memory/Bytebuffer.h>
#include <FileFormat/Warcraft/M2/M2.h>
#include <FileFormat/Warcraft/Parsers/M2Parser.h>
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

TEST_CASE("M2 corpus skeletons satisfy the neutral family contract", "[animation][rigfamily][m2][fixture]")
{
    const char* directory = std::getenv("NOVUS_M2_ANIMATION_FIXTURES");
    if (!directory) { WARN("Set NOVUS_M2_ANIMATION_FIXTURES for converter conformance"); return; }
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
        const auto bytes = read(std::filesystem::path(directory) / "animations/m2" / name / (name + ".skeleton"));
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
            const auto sourceBytes = read(std::filesystem::path(directory) / "animations/m2/humanmale/humanmale.skeleton");
            const auto source = AnimationLoading::AnimationAssetReader::ReadSkeleton(sourceBytes);
            const auto motionBytes = read(std::filesystem::path(directory) / "animations/m2/humanmale/dance-0.animationclip");
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
            const auto sourceBytes = read(std::filesystem::path(directory) / "animations/m2/humanmale/humanmale.skeleton");
            const auto source = AnimationLoading::AnimationAssetReader::ReadSkeleton(sourceBytes);
            const auto clipBytes = read(std::filesystem::path(directory) / "animations/m2/humanmale/idle-0.animationclip");
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
            const auto clipBytes = read(std::filesystem::path(directory) / "animations/m2" / name / (std::string(slot) + "-0.animationclip"));
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

TEST_CASE("M2 independent clips retain exact source clock periods", "[animation][m2][fixture]")
{
    const char* directory = std::getenv("NOVUS_M2_ANIMATION_FIXTURES");
    if (!directory) { WARN("Set NOVUS_M2_ANIMATION_FIXTURES for converter conformance"); return; }
    const std::pair<const char*, u32> clocks[] = {{"humanmale", 6633000}, {"humanfemale", 6667000}, {"orcmale", 7100000}, {"orcfemale", 6967000}, {"dwarfmale", 5933000}, {"dwarffemale", 10967000}, {"nightelfmale", 6700000}, {"nightelffemale", 6733000}, {"scourgemale", 6667000}, {"scourgefemale", 6667000}, {"taurenmale", 7100000}, {"taurenfemale", 6767000}, {"gnomemale", 9400000}, {"gnomefemale", 11767000}, {"trollmale", 8700000}, {"trollfemale", 8700000}};
    for (const auto& [name, period] : clocks)
    {
        INFO(name);
        std::ifstream stream(std::filesystem::path(directory) / "animations/m2" / name / "ambient-face.animationclip", std::ios::binary);
        REQUIRE(stream.good());
        const std::vector<u8> bytes(std::istreambuf_iterator<char>(stream), {});
        const auto clip = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationClip(bytes));
        REQUIRE(clip);
        REQUIRE(clip.view.root.durationMicroseconds == period);
        REQUIRE((clip.view.root.flags & F::AnimationClipFlags_Additive) != 0);
        for (const u32 loop : {1u, 17u, 1000u})
        {
            const f64 time = (static_cast<u64>(loop) * period + 1000) / 1000000.0;
            const auto location = Animation::MapClipTime(clip.view.root, time, true);
            REQUIRE(std::abs(location.mappedTimeSeconds - 0.001) < 0.000001);
        }
    }
}

TEST_CASE("Humanoid stance correction preserves foot ordering across proportions", "[animation][m2][fixture][stance]")
{
    const char* directory = std::getenv("NOVUS_M2_ANIMATION_FIXTURES");
    if (!directory) { WARN("Set NOVUS_M2_ANIMATION_FIXTURES for stance conformance"); return; }
    auto read = [&](const std::string& path)
    {
        std::ifstream stream(std::filesystem::path(directory) / path, std::ios::binary);
        REQUIRE(stream.good());
        return std::vector<u8>(std::istreambuf_iterator<char>(stream), {});
    };
    const auto familyBytes = read("animation/bipedal_humanoid.rigfamily");
    const auto family = AnimationLoading::AnimationAssetReader::ReadRigFamily(familyBytes);
    REQUIRE(family);
    const auto sourceBytes = read("animations/m2/gnomefemale/gnomefemale.skeleton");
    const auto source = AnimationLoading::AnimationAssetReader::ReadSkeleton(sourceBytes);
    REQUIRE(source);
    const u32 variation = GENERATE(0u, 1u, 2u, 3u);
    INFO("Gnome Female dance variation " << variation);
    const auto clipBytes = read("animations/m2/gnomefemale/dance-" + std::to_string(variation) + ".animationclip");
    const auto clip = AnimationLoading::AnimationAssetReader::ReadAnimationClip(clipBytes);
    REQUIRE(clip);
    const auto idleBytes = read("animations/m2/gnomefemale/idle-0.animationclip");
    const auto idle = AnimationLoading::AnimationAssetReader::ReadAnimationClip(idleBytes);
    REQUIRE(idle);
    Animation::FamilyPoseBinding sourceBinding;
    REQUIRE(sourceBinding.Initialize(source.view, family.view));
    auto joint = [&](const auto& skeleton, const char* name)
    {
        const u64 semantic = Animation::HashAnimationSemantic("humanoid", name);
        for (const auto& sourceJoint : source.view.joints)
            if (sourceJoint.semanticID == semantic)
                for (const auto& binding : skeleton.familyBindings)
                    if (binding.familyJointIndex == sourceJoint.familyJointIndex) return binding.skeletonJointIndex;
        FAIL("Missing stance joint");
        return 0u;
    };
    auto ordering = [&](const auto& skeleton, const auto& pose)
    {
        const auto model = Animation::EvaluatePose(skeleton.joints, pose);
        REQUIRE(model);
        std::vector<F::LocalTransform> rest;
        for (const auto& item : skeleton.joints) rest.push_back(item.restTransform);
        const auto restModel = Animation::EvaluatePose(skeleton.joints, rest);
        REQUIRE(restModel);
        const u32 hips = joint(skeleton, "hips");
        const mat4x4 pelvis = mat4x4(mat3x3(restModel.modelTransforms[hips])) * glm::inverse(mat4x4(model.modelTransforms[hips]));
        return (pelvis * vec4(vec3(model.modelTransforms[joint(skeleton, "leftfoot")][3]), 1)).x - (pelvis * vec4(vec3(model.modelTransforms[joint(skeleton, "rightfoot")][3]), 1)).x;
    };
    for (const std::string name : {"humanmale", "humanfemale", "orcmale", "orcfemale", "dwarfmale", "dwarffemale", "nightelfmale", "nightelffemale", "scourgemale", "scourgefemale", "trollmale", "trollfemale", "taurenmale", "taurenfemale", "gnomemale", "reference", "proportions", "frames"})
    {
        INFO(name);
        const bool blender = name == "reference" || name == "proportions" || name == "frames";
        const char* blenderDirectory = std::getenv("NOVUS_RIG_FAMILY_FIXTURES");
        REQUIRE((!blender || blenderDirectory != nullptr));
        const auto targetBytes = read(blender ? (std::filesystem::path(blenderDirectory) / (name + ".skeleton")).string() : "animations/m2/" + name + "/" + name + ".skeleton");
        const auto target = AnimationLoading::AnimationAssetReader::ReadSkeleton(targetBytes);
        REQUIRE(target);
        Animation::FamilyPoseBinding targetBinding;
        REQUIRE(targetBinding.Initialize(target.view, family.view));
        std::vector<F::LocalTransform> sourceRestPose, targetRestPose;
        for (const auto& item : source.view.joints) sourceRestPose.push_back(item.restTransform);
        for (const auto& item : target.view.joints) targetRestPose.push_back(item.restTransform);
        const auto sourceRestModel = Animation::EvaluatePose(source.view.joints, sourceRestPose);
        const auto targetRestModel = Animation::EvaluatePose(target.view.joints, targetRestPose);
        REQUIRE(sourceRestModel);
        REQUIRE(targetRestModel);
        vec3 footScale[2];
        for (u32 foot = 0; foot < 2; ++foot)
        {
            const char* hip = foot == 0 ? "leftupleg" : "rightupleg";
            const char* knee = foot == 0 ? "leftleg" : "rightleg";
            const char* ankle = foot == 0 ? "leftfoot" : "rightfoot";
            auto reach = [&](const auto& skeleton, const auto& model)
            {
                return glm::length(vec3(model.modelTransforms[joint(skeleton, hip)][3] - model.modelTransforms[joint(skeleton, knee)][3])) + glm::length(vec3(model.modelTransforms[joint(skeleton, knee)][3] - model.modelTransforms[joint(skeleton, ankle)][3]));
            };
            footScale[foot] = vec3(reach(target.view, targetRestModel) / reach(source.view, sourceRestModel));
            footScale[foot].x = targetRestModel.modelTransforms[joint(target.view, ankle)][3].x / sourceRestModel.modelTransforms[joint(source.view, ankle)][3].x;
        }
        vec3 sourceMin[2] = {vec3(FLT_MAX), vec3(FLT_MAX)}, sourceMax[2] = {vec3(-FLT_MAX), vec3(-FLT_MAX)};
        vec3 targetMin[2] = {vec3(FLT_MAX), vec3(FLT_MAX)}, targetMax[2] = {vec3(-FLT_MAX), vec3(-FLT_MAX)};
        for (u32 sample = 0; sample < 97; ++sample)
        {
            const f64 time = static_cast<f64>(clip.view.root.durationMicroseconds) * sample / 96 / 1000000.0;
            INFO(time);
            std::vector<F::LocalTransform> sampled(source.view.joints.size()), canonical(family.view.joints.size()), corrected(target.view.joints.size());
            REQUIRE(Animation::SampleClipLocalPose(clip.view, source.view, time, true, sampled) == Animation::ClipSampleError::None);
            REQUIRE(sourceBinding.ToFamily(sampled, canonical));
            REQUIRE(targetBinding.FromFamily(canonical, corrected));
            const auto before = corrected;
            REQUIRE(targetBinding.CorrectStance(sourceBinding, sampled, corrected));
            const auto sourceGroundPose = Animation::EvaluatePose(source.view.joints, sampled);
            const auto targetGroundPose = Animation::EvaluatePose(target.view.joints, corrected);
            REQUIRE(sourceGroundPose);
            REQUIRE(targetGroundPose);
            const auto beforeGroundPose = Animation::EvaluatePose(target.view.joints, before);
            REQUIRE(beforeGroundPose);
            const u32 pelvisIndex = joint(target.view, "hips");
            const vec3 pelvisOffset = vec3(targetGroundPose.modelTransforms[pelvisIndex][3] - beforeGroundPose.modelTransforms[pelvisIndex][3]);
            REQUIRE(std::abs(pelvisOffset.x) < 0.000001f);
            REQUIRE(std::abs(pelvisOffset.z) < 0.000001f);
            for (u32 foot = 0; foot < 2; ++foot)
            {
                const char* role = foot == 0 ? "leftfoot" : "rightfoot";
                const vec3 sourcePosition(sourceGroundPose.modelTransforms[joint(source.view, role)][3]);
                const vec3 targetPosition(targetGroundPose.modelTransforms[joint(target.view, role)][3]);
                sourceMin[foot] = glm::min(sourceMin[foot], sourcePosition);
                sourceMax[foot] = glm::max(sourceMax[foot], sourcePosition);
                targetMin[foot] = glm::min(targetMin[foot], targetPosition);
                targetMax[foot] = glm::max(targetMax[foot], targetPosition);
                if (name == "humanmale" || name == "humanfemale")
                {
                    const vec3 sourceRest(sourceRestModel.modelTransforms[joint(source.view, role)][3]);
                    const vec3 targetRest(targetRestModel.modelTransforms[joint(target.view, role)][3]);
                    const vec3 expected = targetRest + (sourcePosition - sourceRest) * footScale[foot];
                    REQUIRE(std::abs(targetPosition.x - expected.x) < 0.001f);
                    REQUIRE(std::abs(targetPosition.z - expected.z) < 0.001f);
                }
            }
            const f32 sourceOrder = ordering(source.view, sampled);
            const f32 targetOrder = ordering(target.view, corrected);
            if (sourceOrder > 0.03f) REQUIRE(targetOrder > 0.0f);
            if (sourceOrder < -0.03f) REQUIRE(targetOrder < 0.0f);
            for (const char* name : {"hips", "spine", "head", "leftarm", "rightarm"})
            {
                const u32 index = joint(target.view, name);
                if (std::string_view(name) != "hips") REQUIRE(glm::length(corrected[index].translation - before[index].translation) < 0.000001f);
                REQUIRE(std::abs(glm::dot(corrected[index].rotation, before[index].rotation)) > 0.99999f);
            }
            const auto stanceOnly = corrected;
            REQUIRE(targetBinding.FromFamily(canonical, corrected, false, true, &sourceBinding, sampled));
            const auto controllerPose = Animation::EvaluatePose(target.view.joints, corrected);
            const auto stancePose = Animation::EvaluatePose(target.view.joints, stanceOnly);
            REQUIRE(controllerPose);
            REQUIRE(stancePose);
            for (const char* name : {"leftupleg", "rightupleg", "leftleg", "rightleg", "leftfoot", "rightfoot"})
            {
                const u32 index = joint(target.view, name);
                for (u32 column = 0; column < 4; ++column)
                    REQUIRE(glm::length(controllerPose.modelTransforms[index][column] - stancePose.modelTransforms[index][column]) < 0.00001f);
            }
            if (sample == 0)
            {
                auto crossed = sampled;
                const auto uncrossed = Animation::EvaluatePose(source.view.joints, sampled);
                REQUIRE(uncrossed);
                for (const bool left : {false, true})
                {
                    const u32 hip = joint(source.view, left ? "leftupleg" : "rightupleg");
                    const u32 knee = joint(source.view, left ? "leftleg" : "rightleg");
                    const u32 foot = joint(source.view, left ? "leftfoot" : "rightfoot");
                    const u32 opposite = joint(source.view, left ? "rightfoot" : "leftfoot");
                    const vec3 goal = vec3(uncrossed.modelTransforms[opposite][3]);
                    const vec3 pole = vec3(uncrossed.modelTransforms[knee][3]);
                    REQUIRE(Animation::SolveTwoBoneIK(source.view.joints, hip, knee, foot, goal, pole, 10.0f, 1.0f, crossed));
                }
                REQUIRE(ordering(source.view, crossed) < -0.03f);
                REQUIRE(sourceBinding.ToFamily(crossed, canonical));
                REQUIRE(targetBinding.FromFamily(canonical, corrected, false, true, &sourceBinding, crossed));
                REQUIRE(ordering(target.view, corrected) < 0.0f);
            }
            if (sample % 12 == 0)
            {
                std::vector<F::LocalTransform> idlePose(source.view.joints.size());
                REQUIRE(Animation::SampleClipLocalPose(idle.view, source.view, time, true, idlePose) == Animation::ClipSampleError::None);
                for (const f32 weight : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
                {
                    INFO("Idle/dance transition weight " << weight);
                    auto blended = sampled;
                    for (u32 index = 0; index < blended.size(); ++index)
                    {
                        blended[index].translation = glm::mix(idlePose[index].translation, sampled[index].translation, weight);
                        blended[index].rotation = glm::normalize(glm::slerp(idlePose[index].rotation, sampled[index].rotation, weight));
                        blended[index].scale = glm::mix(idlePose[index].scale, sampled[index].scale, weight);
                    }
                    REQUIRE(sourceBinding.ToFamily(blended, canonical));
                    REQUIRE(targetBinding.FromFamily(canonical, corrected, false, true, &sourceBinding, blended));
                    const f32 sourceOrder = ordering(source.view, blended);
                    const f32 targetOrder = ordering(target.view, corrected);
                    if (sourceOrder > 0.03f) REQUIRE(targetOrder > 0.0f);
                    if (sourceOrder < -0.03f) REQUIRE(targetOrder < 0.0f);
                }
            }
        }
        if (name == "humanmale" || name == "humanfemale")
        {
            for (u32 foot = 0; foot < 2; ++foot)
            {
                const vec3 sourceTravel = sourceMax[foot] - sourceMin[foot];
                const vec3 targetTravel = targetMax[foot] - targetMin[foot];
                INFO("Foot " << foot << " source travel " << sourceTravel.x << ", " << sourceTravel.z << " target travel " << targetTravel.x << ", " << targetTravel.z);
                // Preserve planted intervals and intentional steps, without adding hip sway.
                REQUIRE(targetTravel.x < sourceTravel.x * std::abs(footScale[foot].x) + 0.002f);
                REQUIRE(targetTravel.z < sourceTravel.z * footScale[foot].z + 0.002f);
            }
        }
    }
}

TEST_CASE("M2 family layers retain target sockets and support concrete IK", "[animation][m2][fixture][retarget-integration]")
{
    const char* directory = std::getenv("NOVUS_M2_ANIMATION_FIXTURES");
    if (!directory) { WARN("Set NOVUS_M2_ANIMATION_FIXTURES for retarget integration"); return; }
    auto read = [&](const std::string& name)
    {
        std::ifstream file(std::filesystem::path(directory) / name, std::ios::binary);
        REQUIRE(file.good());
        return std::vector<u8>(std::istreambuf_iterator<char>(file), {});
    };
    auto id = [](const std::string& path) { return XXHash64::hash(path.data(), path.size(), 0); };
    AnimationLoading::AnimationAssetRegistry assets;
    const auto familyID = id("animation/bipedal_humanoid.rigfamily");
    const auto familyHandle = assets.PublishRigFamily(familyID, read("animation/bipedal_humanoid.rigfamily"));
    const auto sourceHandle = assets.PublishSkeleton(id("animations/m2/humanmale/humanmale.skeleton"), read("animations/m2/humanmale/humanmale.skeleton"));
    const auto danceID = id("animations/m2/humanmale/dance-0.animationclip");
    const auto idleID = id("animations/m2/humanmale/idle-0.animationclip");
    const auto danceHandle = assets.PublishAnimationClip(danceID, read("animations/m2/humanmale/dance-0.animationclip"));
    const auto idleHandle = assets.PublishAnimationClip(idleID, read("animations/m2/humanmale/idle-0.animationclip"));
    REQUIRE(familyHandle); REQUIRE(sourceHandle); REQUIRE(danceHandle); REQUIRE(idleHandle);
    const auto family = *assets.Get(familyHandle);
    const auto source = *assets.Get(sourceHandle);
    const auto dance = *assets.Get(danceHandle);
    const auto idle = *assets.Get(idleHandle);
    F::AnimationSetAsset setRoot;
    setRoot.declaredPoseDomainAssetID = familyID;
    setRoot.declaredPoseDomainType = F::PoseDomainType::RigFamily;
    F::AnimationSetData setData;
    setData.slots = {{Animation::HashAnimationSlot("Dance"), 0, 1, -1, F::AnimationSetSlotFlags_Required}, {Animation::HashAnimationSlot("Idle"), 1, 1, -1, F::AnimationSetSlotFlags_Required}};
    setData.variants = {{danceID, 1, 0, 1}, {idleID, 1, 0, 2}};
    auto setBuffer = Bytebuffer::BorrowRuntime(setRoot.GetSerializedSize(setData));
    REQUIRE(setRoot.Save(setBuffer, setData));
    const auto set = assets.PublishAnimationSet(801, std::span<const u8>(setBuffer->GetDataPointer(), setBuffer->writtenData));
    REQUIRE(set);
    F::BoneMaskAsset maskRoot;
    maskRoot.poseDomainAssetID = familyID;
    maskRoot.poseDomainType = F::PoseDomainType::RigFamily;
    F::BoneMaskData maskData;
    maskData.jointWeights.resize(family.joints.size());
    for (u32 index = 0; index < family.joints.size(); ++index)
    {
        const i32 parent = family.joints[index].parentJointIndex;
        maskData.jointWeights[index] = family.joints[index].semanticID == Animation::HashAnimationSemantic("humanoid", "rightarm") || (parent >= 0 && maskData.jointWeights[parent]) ? 255 : 0;
    }
    auto maskBuffer = Bytebuffer::BorrowRuntime(maskRoot.GetSerializedSize(maskData));
    REQUIRE(maskRoot.Save(maskBuffer, maskData));
    REQUIRE(assets.PublishBoneMask(802, std::span<const u8>(maskBuffer->GetDataPointer(), maskBuffer->writtenData)));
    Animation::EditableAnimationGraph graph;
    graph.entryStateGUID = 1;
    graph.states = {{1, Animation::HashAnimationSlot("Dance"), 60000000}};
    Animation::EditableGraphLayer layer;
    layer.sourceGUID = 2;
    layer.clipSlotSemanticID = Animation::HashAnimationSlot("Idle");
    layer.durationMicroseconds = idle.root.durationMicroseconds;
    layer.maskAssetID = 802;
    layer.constantWeight = 0.4f;
    layer.composeInSourceDomain = true;
    graph.layers = {layer};
    const auto compiled = Animation::CompileAnimationGraph(graph);
    REQUIRE(compiled);
    Animation::FamilyPoseBinding sourceBinding;
    REQUIRE(sourceBinding.Initialize(source, family));
    for (const std::string name : {"humanfemale", "orcmale", "orcfemale", "dwarfmale", "trollmale", "trollfemale", "taurenmale", "taurenfemale", "gnomefemale"})
    {
        INFO(name);
        const auto targetHandle = assets.PublishSkeleton(id("animations/m2/" + name + "/" + name + ".skeleton"), read("animations/m2/" + name + "/" + name + ".skeleton"));
        REQUIRE(targetHandle);
        const auto target = *assets.Get(targetHandle);
        Animation::FamilyPoseBinding targetBinding;
        REQUIRE(targetBinding.Initialize(target, family));
        Animation::PoseSourceStore poses;
        Animation::ControllerPosePlayer player(&poses, &assets);
        const auto controller = player.Create(targetHandle, set, target, *assets.Get(set), compiled.graph, 1);
        REQUIRE(controller);
        const auto poseHandle = player.GetPoseSource(controller);
        const auto socket = poses.CreateAttachment(poseHandle, target, Animation::HashAnimationSemantic("skeleton-attachment", "right_hand"), false, vec3(0.1f));
        REQUIRE(socket);
        for (u64 frame = 1; frame <= 9; ++frame)
        {
            player.Advance(0.1, false);
            Animation::PoseRecipe recipe;
            REQUIRE(player.CaptureRecipe(controller, recipe));
            REQUIRE(recipe.sampleCount == 1);
            REQUIRE(recipe.layerCount == 1);
            std::vector<F::LocalTransform> sampled(source.joints.size()), canonical(family.joints.size()), base(target.joints.size()), overlay(target.joints.size()), expected(target.joints.size());
            REQUIRE(Animation::SampleClipLocalPose(dance, source, recipe.samples[0].timeMicroseconds / 1000000.0, true, sampled) == Animation::ClipSampleError::None);
            REQUIRE(sourceBinding.ToFamily(sampled, canonical));
            REQUIRE(targetBinding.FromFamily(canonical, base));
            REQUIRE(Animation::SampleClipLocalPose(idle, source, recipe.layers[0].sample.timeMicroseconds / 1000000.0, true, sampled) == Animation::ClipSampleError::None);
            REQUIRE(sourceBinding.ToFamily(sampled, canonical));
            REQUIRE(targetBinding.FromFamily(canonical, overlay));
            std::vector<f32> weights(target.joints.size());
            for (const auto& binding : target.familyBindings) weights[binding.skeletonJointIndex] = maskData.jointWeights[binding.familyJointIndex] / 255.0f;
            REQUIRE(Animation::BlendOverrideLocalPoses(base, overlay, 0.4f, weights, expected));
            const auto* actual = poses.Get(poseHandle);
            REQUIRE(actual);
            CHECK_FALSE(actual->supportsTightAnimationBounds);
            for (u32 joint = 0; joint < expected.size(); ++joint)
            {
                REQUIRE(glm::length(actual->localPose[joint].translation - expected[joint].translation) < 0.00001f);
                REQUIRE(std::abs(glm::dot(actual->localPose[joint].rotation, expected[joint].rotation)) > 0.99999f);
            }
            poses.BeginDemandFrame(frame);
            REQUIRE(poses.MarkDemand(poseHandle, frame));
            poses.ResolveAttachments(frame);
            const auto* attached = poses.GetAttachment(socket);
            REQUIRE(attached);
            REQUIRE(attached->valid);
            const auto definition = std::find_if(target.attachments.begin(), target.attachments.end(), [&](const auto& value) { return value.semanticID == attached->semanticID; });
            REQUIRE(definition != target.attachments.end());
            const auto expectedSocket = mat4x4(actual->modelTransforms[definition->jointIndex]) * mat4x4(Animation::ComposeLocalTransform(definition->localTransform));
            REQUIRE(glm::length(vec3(expectedSocket[3]) - vec3(attached->currentTransform[3])) < 0.00001f);
            poses.CommitDemandFrame();
            auto find = [&](const char* role)
            {
                const u64 semantic = Animation::HashAnimationSemantic("humanoid", role);
                for (u32 index = 0; index < target.joints.size(); ++index) if (target.joints[index].semanticID == semantic) return index;
                FAIL("Missing concrete IK joint"); return 0u;
            };
            const u32 shoulder = find("rightarm"), elbow = find("rightforearm"), hand = find("righthand");
            auto corrected = actual->localPose;
            const auto& model = actual->modelTransforms;
            const vec3 handPoint(model[hand][3]), elbowPoint(model[elbow][3]), shoulderPoint(model[shoulder][3]);
            const f32 reach = glm::length(handPoint-elbowPoint) + glm::length(elbowPoint-shoulderPoint);
            const vec3 goal = glm::mix(handPoint, shoulderPoint, 0.02f);
            const auto solved = Animation::SolveTwoBoneIK(target.joints, shoulder, elbow, hand, goal, elbowPoint, reach, 1.0f, corrected);
            REQUIRE(solved);
            REQUIRE(solved.finalError < 0.0001f);
            const auto result = Animation::EvaluatePose(target.joints, corrected);
            REQUIRE(result);
            REQUIRE(glm::length(vec3(result.modelTransforms[hand][3]) - goal) < 0.0001f);
        }
        REQUIRE(player.GetStats().failures == 0);
        REQUIRE(player.Destroy(controller));
    }
}

TEST_CASE("M2 contact events retain family roles on differently named targets offscreen", "[animation][m2][fixture][family-contact-events]")
{
    const char* directory = std::getenv("NOVUS_M2_ANIMATION_FIXTURES");
    const char* blenderDirectory = std::getenv("NOVUS_RIG_FAMILY_FIXTURES");
    if (!directory || !blenderDirectory) { WARN("Set M2 and Blender fixture directories for contact conformance"); return; }
    auto read = [&](const std::filesystem::path& path)
    {
        std::ifstream stream(std::filesystem::path(directory) / path, std::ios::binary);
        REQUIRE(stream.good());
        return std::vector<u8>(std::istreambuf_iterator<char>(stream), {});
    };
    const auto familyBytes = read("animation/bipedal_humanoid.rigfamily");
    const auto family = AnimationLoading::AnimationAssetReader::ReadRigFamily(familyBytes);
    const auto clipBytes = read("animations/m2/humanmale/walk-0.animationclip");
    const auto clip = AnimationLoading::AnimationAssetReader::ReadAnimationClip(clipBytes);
    REQUIRE(family); REQUIRE(clip); REQUIRE_FALSE(clip.view.events.empty());
    constexpr u64 familyID = 9101, clipID = 9102;
    AnimationLoading::AnimationSetAssetView set;
    set.root.declaredPoseDomainType = F::PoseDomainType::RigFamily;
    set.root.declaredPoseDomainAssetID = familyID;
    const std::array<F::AnimationSetSlot, 1> slots = {{{Animation::HashAnimationSlot("Walk"), 0, 1, -1, F::AnimationSetSlotFlags_Required}}};
    const std::array<F::AnimationClipVariant, 1> variants = {{{clipID, 1, 0, 1}}};
    set.slots = slots; set.variants = variants;
    const std::array<Animation::AnimationClipDomainRecord, 1> clips = {{{clipID, F::PoseDomainType::Skeleton, clip.view.root.sourcePoseDomainAssetID, clip.view.root.durationMicroseconds, clip.view.synchronizationMarkers, clip.view.events, clip.view.eventPayloadBytes, true, familyID}}};
    Animation::EditableAnimationGraph graph;
    graph.entryStateGUID = 1;
    graph.states = {{1, Animation::HashAnimationSlot("Walk"), 60000000}};
    const auto compiled = Animation::CompileAnimationGraph(graph);
    REQUIRE(compiled);
    for (const std::string name : {"humanmale", "humanfemale", "orcmale", "orcfemale", "dwarfmale", "dwarffemale", "nightelfmale", "nightelffemale", "scourgemale", "scourgefemale", "taurenmale", "taurenfemale", "gnomemale", "gnomefemale", "trollmale", "trollfemale", "reference", "frames", "proportions"})
    {
        INFO(name);
        const bool blender = name == "reference" || name == "frames" || name == "proportions";
        const auto bytes = read(blender ? std::filesystem::path(blenderDirectory) / (name + ".skeleton") : std::filesystem::path("animations/m2/" + name + "/" + name + ".skeleton"));
        const auto target = AnimationLoading::AnimationAssetReader::ReadSkeleton(bytes);
        REQUIRE(target);
        Animation::AnimationControllerStore store;
        const auto animator = store.Create({9201, 1, compiled.graph, 42});
        REQUIRE(animator); REQUIRE(store.BindAnimationData(animator, set, clips));
        u32 contacts = 0;
        for (u32 frame = 0; frame < 120; ++frame)
        {
            store.Advance(1.0 / 60.0, false);
            for (const auto& event : store.GetAuthoritativeEvents(animator))
            {
                REQUIRE(event.eventTypeID == static_cast<u32>(Animation::HashAnimationSemantic("animation-event", "footstep")));
                REQUIRE(event.payloadTypeID == static_cast<u32>(Animation::HashAnimationSemantic("animation-payload", "joint-semantic.v1")));
                REQUIRE(event.payload.size() == sizeof(u64));
                u64 semantic;
                std::memcpy(&semantic, event.payload.data(), sizeof(semantic));
                const auto role = std::find_if(family.view.joints.begin(), family.view.joints.end(), [semantic](const auto& joint) { return joint.semanticID == semantic; });
                REQUIRE(role != family.view.joints.end());
                const u32 familyIndex = static_cast<u32>(role - family.view.joints.begin());
                REQUIRE(std::any_of(target.view.familyBindings.begin(), target.view.familyBindings.end(), [familyIndex](const auto& binding) { return binding.familyJointIndex == familyIndex; }));
                ++contacts;
            }
        }
        REQUIRE(contacts > 0);
        REQUIRE(store.GetStats().poseRequests == 0);
    }
}

TEST_CASE("M2 parser resolves nested material attachment and event track offsets", "[animation][m2][fixture][source-offsets]")
{
    const char* directory = std::getenv("NOVUS_M2_ANIMATION_FIXTURES");
    if (!directory) { WARN("Set NOVUS_M2_ANIMATION_FIXTURES for source offset conformance"); return; }
    const auto sources = std::filesystem::path(directory).parent_path() / "sources";
    for (const auto& entry : std::filesystem::directory_iterator(sources))
    {
        if (entry.path().extension() != ".m2") continue;
        INFO(entry.path().string());
        std::ifstream file(entry.path(), std::ios::binary);
        REQUIRE(file.good());
        const std::vector<u8> original(std::istreambuf_iterator<char>(file), {});
        REQUIRE(original.size() > 8 + sizeof(M2::MD21));
        REQUIRE(std::memcmp(original.data(), "MD21", 4) == 0);
        constexpr u32 base = 8;
        M2::MD21 raw;
        std::memcpy(&raw, original.data() + base, sizeof(raw));
        auto parsed = std::make_shared<Bytebuffer>(nullptr, original.size());
        std::memcpy(parsed->GetDataPointer(), original.data(), original.size());
        parsed->writtenData = original.size();
        M2::Layout layout;
        M2::Parser parser;
        REQUIRE(parser.TryParse(M2::Parser::ParseType::Root, parsed, layout));
        auto check = [&](const auto& source, const auto& resolved)
        {
            REQUIRE(resolved.size == source.size);
            REQUIRE(resolved.offset == source.offset + (source.offset ? base : 0));
            for (u32 index = 0; index < source.size; ++index)
            {
                M2::M2Array<u32> nested;
                std::memcpy(&nested, original.data() + base + source.offset + index * sizeof(nested), sizeof(nested));
                const auto& actual = *resolved.GetElement(parsed, index);
                REQUIRE(actual.size == nested.size);
                REQUIRE(actual.offset == nested.offset + (nested.offset ? base : 0));
            }
        };
        for (u32 index = 0; index < raw.events.size; ++index)
        {
            M2::M2Event event;
            std::memcpy(&event, original.data() + base + raw.events.offset + index * sizeof(event), sizeof(event));
            check(event.enabled.timestamps, layout.md21.events.GetElement(parsed, index)->enabled.timestamps);
        }
        for (u32 index = 0; index < raw.attachments.size; ++index)
        {
            M2::M2Attachment attachment;
            std::memcpy(&attachment, original.data() + base + raw.attachments.offset + index * sizeof(attachment), sizeof(attachment));
            const auto& actual = layout.md21.attachments.GetElement(parsed, index)->isAnimated;
            check(attachment.isAnimated.timestamps, actual.timestamps);
            check(attachment.isAnimated.values, actual.values);
        }
        for (u32 index = 0; index < raw.colors.size; ++index)
        {
            M2::M2Color color;
            std::memcpy(&color, original.data() + base + raw.colors.offset + index * sizeof(color), sizeof(color));
            const auto& actual = *layout.md21.colors.GetElement(parsed, index);
            check(color.color.timestamps, actual.color.timestamps);
            check(color.color.values, actual.color.values);
            check(color.alpha.timestamps, actual.alpha.timestamps);
            check(color.alpha.values, actual.alpha.values);
        }
    }
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
