#include "Game-Lib/Rendering/Animation/Pose/AnimationClipSampler.h"

#include <catch2/catch2.hpp>

#include <array>
#include <glm/gtx/quaternion.hpp>

namespace
{
    namespace AnimationFormat = FileFormat::Animation;

    void CheckVec3(const vec3& value, const vec3& expected)
    {
        CHECK(value.x == Catch::Approx(expected.x).margin(0.0001f));
        CHECK(value.y == Catch::Approx(expected.y).margin(0.0001f));
        CHECK(value.z == Catch::Approx(expected.z).margin(0.0001f));
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
