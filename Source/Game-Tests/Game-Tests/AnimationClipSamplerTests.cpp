#include "Game-Lib/Rendering/Animation/Pose/AnimationClipSampler.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"
#include "Game-Lib/Rendering/Animation/Pose/PoseSourceStore.h"

#include <catch2/catch2.hpp>

#include <array>
#include <cstdlib>
#include <fstream>
#include <glm/gtx/quaternion.hpp>
#include <json/json.hpp>

namespace
{
    namespace AnimationFormat = FileFormat::Animation;

    void CheckVec3(const vec3& value, const vec3& expected)
    {
        CHECK(value.x == Catch::Approx(expected.x).margin(0.0001f));
        CHECK(value.y == Catch::Approx(expected.y).margin(0.0001f));
        CHECK(value.z == Catch::Approx(expected.z).margin(0.0001f));
    }

    std::vector<u8> ReadPayload(const char* path)
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input);
        return std::vector<u8>(std::istreambuf_iterator<char>(input), {});
    }
}

TEST_CASE("Animation clip maps loop and clamp time deterministically", "[AnimationClipSampler]")
{
    AnimationFormat::AnimationClipAsset clip;
    clip.sampleRateHz = 2;
    clip.sampleCount = 3;
    clip.durationMicroseconds = 1'000'000;

    Animation::ClipSampleLocation location = Animation::MapClipTime(clip, 0.25, false);
    CHECK(location.firstSample == 0);
    CHECK(location.secondSample == 1);
    CHECK(location.alpha == Catch::Approx(0.5f));

    location = Animation::MapClipTime(clip, 1.0, false);
    CHECK(location.firstSample == 2);
    CHECK(location.secondSample == 2);
    CHECK(location.alpha == 0.0f);

    location = Animation::MapClipTime(clip, 5.25, true);
    CHECK(location.firstSample == 0);
    CHECK(location.secondSample == 1);
    CHECK(location.alpha == Catch::Approx(0.5f));

    location = Animation::MapClipTime(clip, -0.25, true);
    CHECK(location.firstSample == 1);
    CHECK(location.secondSample == 2);
    CHECK(location.alpha == Catch::Approx(0.5f));
}

TEST_CASE("Animation clip samples complete rest-defaulted pose and shortest rotations", "[AnimationClipSampler]")
{
    std::array<AnimationFormat::SkeletonJoint, 2> joints = {};
    joints[0].parentJointIndex = -1;
    joints[0].restTransform.translation = vec3(10.0f);
    joints[1].parentJointIndex = 0;
    joints[1].restTransform.translation = vec3(20.0f);
    AnimationLoading::SkeletonAssetView skeleton;
    skeleton.joints = joints;

    AnimationFormat::AnimationClipAsset root;
    root.sampleRateHz = 2;
    root.sampleCount = 3;
    root.durationMicroseconds = 1'000'000;
    AnimationFormat::AnimationTrack track;
    track.sourceJointIndex = 0;
    track.flags = AnimationFormat::AnimationTrackFlags_HasTranslationSamples | AnimationFormat::AnimationTrackFlags_HasRotationSamples;
    track.numTranslations = 3;
    track.numRotations = 3;
    track.defaultScale = vec3(2.0f);
    const std::array<AnimationFormat::AnimationTrack, 1> tracks = {track};
    const std::array<vec3, 3> translations = {vec3(0.0f), vec3(2.0f, 0.0f, 0.0f), vec3(4.0f, 0.0f, 0.0f)};
    const quat turn = glm::angleAxis(glm::two_pi<f32>() / 3.0f, vec3(0.0f, 1.0f, 0.0f));
    const std::array<quat, 3> rotations = {quat(1.0f, 0.0f, 0.0f, 0.0f), -turn, quat(1.0f, 0.0f, 0.0f, 0.0f)};
    AnimationLoading::AnimationClipAssetView clip;
    clip.root = root;
    clip.tracks = tracks;
    clip.translationSamples = translations;
    clip.rotationSamples = rotations;

    std::array<AnimationFormat::LocalTransform, 2> pose;
    REQUIRE(Animation::SampleClipLocalPose(clip, skeleton, 0.25, false, pose) == Animation::ClipSampleError::None);
    CheckVec3(pose[0].translation, vec3(1.0f, 0.0f, 0.0f));
    CheckVec3(pose[0].scale, vec3(2.0f));
    CheckVec3(pose[1].translation, vec3(20.0f));
    CHECK(std::abs(glm::dot(pose[0].rotation, glm::angleAxis(glm::pi<f32>() / 3.0f, vec3(0.0f, 1.0f, 0.0f)))) == Catch::Approx(1.0f).margin(0.0001f));
}

TEST_CASE("Pose source generations invalidation and demand participation are explicit", "[AnimationPose][PoseSourceStore]")
{
    std::array<AnimationFormat::SkeletonJoint, 1> joints = {};
    joints[0].parentJointIndex = -1;
    AnimationLoading::SkeletonAssetView skeleton;
    skeleton.joints = joints;

    Animation::PoseSourceStore store;
    const Animation::PoseSourceHandle first = store.CreateRestPose({}, skeleton);
    REQUIRE(first);
    REQUIRE(store.Get(first));
    CHECK(store.Get(first)->poseGeneration == 1);
    CHECK(store.Get(first)->sampleTimeMicroseconds == 0);

    store.BeginDemandFrame(10);
    REQUIRE(store.MarkDemand(first, 10));
    CHECK(store.Get(first)->lastDemandFrame == 10);
    CHECK(store.Get(first)->participatedCurrentFrame);
    CHECK_FALSE(store.Get(first)->participatedPreviousFrame);

    store.BeginDemandFrame(11);
    CHECK_FALSE(store.Get(first)->participatedCurrentFrame);
    CHECK(store.Get(first)->participatedPreviousFrame);

    const u64 generationBeforeInvalidation = store.Get(first)->poseGeneration;
    REQUIRE(store.Invalidate(first));
    CHECK(store.Get(first)->poseGeneration == generationBeforeInvalidation + 1);
    CHECK_FALSE(store.Get(first)->participatedCurrentFrame);
    CHECK_FALSE(store.Get(first)->participatedPreviousFrame);

    std::array<AnimationFormat::LocalTransform, 1> localPose = {};
    localPose[0].translation = vec3(2.0f, 3.0f, 4.0f);
    REQUIRE(store.SetLocalPose(first, localPose, skeleton, 123456));
    CHECK(store.Get(first)->sampleTimeMicroseconds == 123456);
    CHECK(store.Get(first)->poseGeneration == generationBeforeInvalidation + 2);

    REQUIRE(store.Destroy(first));
    CHECK(store.Get(first) == nullptr);
    CHECK_FALSE(store.MarkDemand(first, 12));

    const Animation::PoseSourceHandle reused = store.CreateRestPose({}, skeleton);
    REQUIRE(reused);
    CHECK(reused.GetRawValue() != first.GetRawValue());
    CHECK(store.Get(first) == nullptr);
    CHECK(store.Get(reused) != nullptr);

    const Animation::PoseSourceStoreStats stats = store.GetStats();
    CHECK(stats.liveSources == 1);
    CHECK(stats.invalidations == 1);
    CHECK(stats.poseEvaluations == 3);
    CHECK(stats.staleHandleRejects >= 3);
}

TEST_CASE("Semantic attachments and batched joint queries use CPU pose transforms", "[AnimationPose][Attachments][PoseQueries]")
{
    std::array<AnimationFormat::SkeletonJoint, 3> joints = {};
    joints[0].semanticID = 10;
    joints[0].parentJointIndex = -1;
    joints[1].semanticID = 11;
    joints[1].parentJointIndex = 0;
    joints[1].restTransform.translation = vec3(0.0f, 1.0f, 0.0f);
    joints[2].semanticID = 12;
    joints[2].parentJointIndex = 1;
    joints[2].restTransform.translation = vec3(0.0f, 2.0f, 0.0f);
    FileFormat::Animation::SkeletonAttachment socket;
    socket.semanticID = 100;
    socket.jointIndex = 2;
    socket.localTransform.translation = vec3(1.0f, 0.0f, 0.0f);
    const std::array<FileFormat::Animation::SkeletonAttachment, 1> attachments = {socket};
    AnimationLoading::SkeletonAssetView skeleton;
    skeleton.joints = joints;
    skeleton.attachments = attachments;

    Animation::PoseSourceStore store;
    const Animation::PoseSourceHandle poseSource = store.CreateRestPose({}, skeleton);
    REQUIRE(poseSource);
    const Animation::PoseAttachmentHandle attachment = store.CreateAttachment(poseSource, skeleton, 100, true, vec3(0.5f, 1.0f, 0.25f));
    REQUIRE(attachment);
    REQUIRE(store.Get(poseSource));
    CheckVec3(store.Get(poseSource)->attachmentBoundsExpansion, vec3(1.5f, 1.0f, 0.25f));
    store.BeginDemandFrame(1);
    store.ResolveAttachments(1);
    CHECK(store.GetAttachment(attachment)->lastResolvedFrame == 0);
    CHECK_FALSE(store.GetAttachment(attachment)->valid);
    REQUIRE(store.MarkDemand(poseSource, 1));
    store.ResolveAttachments(1);
    REQUIRE(store.GetAttachment(attachment));
    CHECK(store.GetAttachment(attachment)->valid);
    CheckVec3(store.GetAttachment(attachment)->currentTransform[3], vec3(1.0f, 3.0f, 0.0f));
    CHECK_FALSE(store.GetAttachment(attachment)->motionValid);

    std::array<AnimationFormat::LocalTransform, 3> pose = {joints[0].restTransform, joints[1].restTransform, joints[2].restTransform};
    pose[0].translation = vec3(5.0f, 0.0f, 0.0f);
    store.CommitDemandFrame();
    store.BeginDemandFrame(2);
    REQUIRE(store.MarkDemand(poseSource, 2));
    REQUIRE(store.SetLocalPose(poseSource, pose, skeleton));
    store.ResolveAttachments(2);
    CheckVec3(store.GetAttachment(attachment)->previousTransform[3], vec3(1.0f, 3.0f, 0.0f));
    CheckVec3(store.GetAttachment(attachment)->currentTransform[3], vec3(6.0f, 3.0f, 0.0f));
    CHECK(store.GetAttachment(attachment)->motionValid);

    REQUIRE(store.Invalidate(poseSource));
    store.ResolveAttachments(3);
    CHECK_FALSE(store.GetAttachment(attachment)->valid);
    CHECK_FALSE(store.GetAttachment(attachment)->motionValid);
    REQUIRE(store.MarkDemand(poseSource, 3));
    store.ResolveAttachments(3);
    CHECK(store.GetAttachment(attachment)->valid);
    CHECK_FALSE(store.GetAttachment(attachment)->motionValid);

    store.BeginPoseQueryFrame(20);
    const Animation::PoseQueryHandle first = store.RequestJoint(poseSource, skeleton, 12);
    const Animation::PoseQueryHandle duplicate = store.RequestJoint(poseSource, skeleton, 12);
    REQUIRE(first);
    CHECK(first.index == duplicate.index);
    store.ResolvePoseQueries();
    REQUIRE(store.GetPoseQuery(first));
    REQUIRE(store.GetPoseQuery(first)->valid);
    CheckVec3(store.GetPoseQuery(first)->modelTransform[3], vec3(5.0f, 3.0f, 0.0f));
    CHECK(store.GetStats().deduplicatedQueries == 1);
    CHECK(store.GetStats().fullPoseQueryPromotions == 1);

    REQUIRE(store.Destroy(poseSource));
    store.ResolveAttachments(21);
    CHECK_FALSE(store.GetAttachment(attachment)->valid);
    CHECK_FALSE(store.GetAttachment(attachment)->motionValid);
    REQUIRE(store.DestroyAttachment(attachment));
    CHECK(store.GetAttachment(attachment) == nullptr);
}

TEST_CASE("Game clip sampling and hierarchy evaluation match independent Blender goldens", "[AnimationClipSampler][ExternalFixture]")
{
    const char* skeletonPath = std::getenv("NOVUS_NBS_PHASE5_SKELETON");
    const char* clipPath = std::getenv("NOVUS_NBS_PHASE5_CLIP");
    const char* goldenPath = std::getenv("NOVUS_NBS_PHASE5_GOLDEN");
    if (!skeletonPath || !clipPath || !goldenPath)
        return;

    const std::vector<u8> skeletonPayload = ReadPayload(skeletonPath);
    const std::vector<u8> clipPayload = ReadPayload(clipPath);
    const AnimationLoading::SkeletonAssetReadResult skeleton = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadSkeleton(skeletonPayload));
    const AnimationLoading::AnimationClipAssetReadResult clip = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadAnimationClip(clipPayload));
    REQUIRE(skeleton);
    REQUIRE(clip);
    std::ifstream goldenInput(goldenPath);
    REQUIRE(goldenInput);
    const nlohmann::json golden = nlohmann::json::parse(goldenInput);
    REQUIRE(golden.at("schema") == "novus.animation-v2.blender-golden.v1");
    const u64 skeletonAssetID = golden.at("skeleton_asset_id").get<u64>();
    REQUIRE_FALSE(AnimationLoading::AnimationAssetValidator::ValidateDomain(clip.view, AnimationFormat::PoseDomainType::Skeleton, skeletonAssetID));
    REQUIRE(golden.at("duration_microseconds").get<u64>() == clip.view.root.durationMicroseconds);
    REQUIRE(golden.at("sample_rate_hz").get<u32>() == clip.view.root.sampleRateHz);
    REQUIRE(golden.at("sample_count").get<u32>() == clip.view.root.sampleCount);
    REQUIRE(golden.at("joint_count").get<u32>() == skeleton.view.joints.size());

    constexpr f32 localTolerance = 2.0e-5f;
    constexpr f32 modelTolerance = 4.0e-5f;
    std::vector<AnimationFormat::LocalTransform> localPose(skeleton.view.joints.size());
    for (const nlohmann::json& sample : golden.at("samples"))
    {
        const u64 timeMicroseconds = sample.at("time_microseconds").get<u64>();
        INFO("sample " << sample.at("sample_index").get<u32>() << " at " << timeMicroseconds << " us");
        REQUIRE(Animation::SampleClipLocalPose(clip.view, skeleton.view, static_cast<f64>(timeMicroseconds) / 1'000'000.0, false, localPose) == Animation::ClipSampleError::None);
        const Animation::PoseEvaluationResult evaluated = Animation::EvaluatePose(skeleton.view.joints, localPose);
        REQUIRE(evaluated);
        REQUIRE(sample.at("joints").size() == skeleton.view.joints.size());

        for (const nlohmann::json& joint : sample.at("joints"))
        {
            const u32 jointIndex = joint.at("index").get<u32>();
            INFO("joint " << joint.at("name").get<std::string>() << " [" << jointIndex << "]");
            REQUIRE(jointIndex < skeleton.view.joints.size());
            CHECK(joint.at("semantic_id").get<u64>() == skeleton.view.joints[jointIndex].semanticID);

            const auto& expectedTranslation = joint.at("translation");
            const auto& expectedRotation = joint.at("rotation_xyzw");
            const auto& expectedScale = joint.at("scale");
            for (u32 axis = 0; axis < 3; ++axis)
            {
                CHECK(localPose[jointIndex].translation[axis] == Catch::Approx(expectedTranslation.at(axis).get<f32>()).margin(localTolerance));
                CHECK(localPose[jointIndex].scale[axis] == Catch::Approx(expectedScale.at(axis).get<f32>()).margin(localTolerance));
            }
            const quat expectedQuaternion(expectedRotation.at(3).get<f32>(), expectedRotation.at(0).get<f32>(), expectedRotation.at(1).get<f32>(), expectedRotation.at(2).get<f32>());
            CHECK(std::abs(glm::dot(localPose[jointIndex].rotation, expectedQuaternion)) >= 1.0f - localTolerance);

            const auto& expectedModel = joint.at("model_column_major_3x4");
            for (u32 column = 0; column < 4; ++column)
                for (u32 row = 0; row < 3; ++row)
                    CHECK(evaluated.modelTransforms[jointIndex][column][row] == Catch::Approx(expectedModel.at(column * 3 + row).get<f32>()).margin(modelTolerance));
        }
    }
}
