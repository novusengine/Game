#include "Game-Lib/Rendering/Animation/Pose/PoseSourceStore.h"
#include "Game-Lib/Rendering/Model/Scene/ModelInstanceStore.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <catch2/catch2.hpp>
#include <array>

TEST_CASE("Animation history retains the last demanded frame across intermediate samples and holds", "[AnimationMotion]")
{
    std::array<FileFormat::Animation::SkeletonJoint, 1> joints;
    joints[0].parentJointIndex = -1;
    AnimationLoading::SkeletonAssetView skeleton;
    skeleton.joints = joints;
    Animation::PoseSourceStore store;
    const auto source = store.CreateRestPose({}, skeleton);
    REQUIRE(source);
    std::array<FileFormat::Animation::LocalTransform, 1> pose = {joints[0].restTransform};
    REQUIRE(store.MarkDemand(source, 1));
    CHECK_FALSE(store.Get(source)->motionValid);
    store.CommitDemandFrame();

    store.BeginDemandFrame(2);
    REQUIRE(store.MarkDemand(source, 2));
    pose[0].translation.x = 1.0f;
    REQUIRE(store.SetLocalPose(source, pose, skeleton));
    pose[0].translation.x = 2.0f;
    REQUIRE(store.SetLocalPose(source, pose, skeleton));
    CHECK(store.Get(source)->motionValid);
    CHECK(store.Get(source)->previousModelTransforms[0][3].x == 0.0f);
    CHECK(store.Get(source)->modelTransforms[0][3].x == 2.0f);
    store.CommitDemandFrame();

    for (u64 frame = 3; frame <= 5; ++frame)
    {
        REQUIRE(store.MarkDemand(source, frame));
        CHECK(store.Get(source)->motionValid);
        CHECK(store.Get(source)->previousModelTransforms[0][3].x == 2.0f);
        store.CommitDemandFrame();
    }
    REQUIRE(store.MarkDemand(source, 6));
    pose[0].translation.x = 8.0f;
    REQUIRE(store.SetLocalPose(source, pose, skeleton));
    CHECK(store.Get(source)->previousModelTransforms[0][3].x == 2.0f);
    CHECK(store.Get(source)->motionValid);
    store.CommitDemandFrame();

    store.BeginDemandFrame(7);
    store.CommitDemandFrame();
    REQUIRE(store.MarkDemand(source, 8));
    CHECK_FALSE(store.Get(source)->motionValid);
    store.CommitDemandFrame();
    REQUIRE(store.MarkDemand(source, 9));
    REQUIRE(store.Invalidate(source));
    const auto generation = store.Get(source)->motionGeneration;
    REQUIRE(store.SetLocalPose(source, pose, skeleton));
    REQUIRE(store.MarkDemand(source, 9));
    CHECK_FALSE(store.Get(source)->motionValid);
    CHECK(store.Get(source)->motionGeneration == generation);
    store.CommitDemandFrame();
    REQUIRE(store.MarkDemand(source, 10));
    CHECK(store.Get(source)->motionValid);
    REQUIRE(store.Destroy(source));
    const auto replacement = store.CreateRestPose({}, skeleton);
    REQUIRE(replacement);
    CHECK_FALSE(replacement == source);
    REQUIRE(store.MarkDemand(replacement, 11));
    CHECK_FALSE(store.Get(replacement)->motionValid);
}

TEST_CASE("Instance deformation history cannot be borrowed after a gap or fallback", "[AnimationMotion]")
{
    ModelScene::ModelInstanceStore store;
    const auto instance = store.Create({});
    REQUIRE(store.MarkDeformationFrame(instance, 1, true));
    CHECK((store.GetRecord(instance)->flags & ModelScene::ModelInstanceFlagDeformationMotionValid) == 0);
    REQUIRE(store.MarkDeformationFrame(instance, 2, true));
    CHECK((store.GetRecord(instance)->flags & ModelScene::ModelInstanceFlagDeformationMotionValid) != 0);
    REQUIRE(store.MarkDeformationFrame(instance, 4, true));
    CHECK((store.GetRecord(instance)->flags & ModelScene::ModelInstanceFlagDeformationMotionValid) == 0);
    REQUIRE(store.MarkDeformationFrame(instance, 5, false));
    REQUIRE(store.MarkDeformationFrame(instance, 6, true));
    CHECK((store.GetRecord(instance)->flags & ModelScene::ModelInstanceFlagDeformationMotionValid) == 0);
    REQUIRE(store.MarkDeformationFrame(instance, 7, true));
    bool clear = false;
    mat4x4 moved(1.0f);
    moved[3].x = 20.0f;
    REQUIRE(store.SetTransform(instance, moved, true, clear));
    CHECK(clear);
    CHECK((store.GetRecord(instance)->flags & ModelScene::ModelInstanceFlagMotionValid) == 0);
    CHECK(store.GetRecord(instance)->previousWorld[3].x == 20.0f);
    ModelScene::ModelInstanceResources released;
    REQUIRE(store.Destroy(instance, released));
    const auto replacement = store.Create({});
    REQUIRE(store.MarkDeformationFrame(replacement, 8, true));
    CHECK((store.GetRecord(replacement)->flags & ModelScene::ModelInstanceFlagDeformationMotionValid) == 0);
}

TEST_CASE("Temporal resets and rendering gaps are local to each View", "[AnimationMotion]")
{
    RenderScenes::RenderView main({});
    RenderScenes::RenderView transient({});
    const mat4x4 camera(1.0f);
    main.PrepareTemporalCamera(camera, 1);
    transient.PrepareTemporalCamera(camera, 1);
    main.PrepareTemporalCamera(camera, 2);
    transient.PrepareTemporalCamera(camera, 2);
    REQUIRE(main.IsTemporalCameraValid());
    REQUIRE(transient.IsTemporalCameraValid());
    transient.RequestTemporalReset();
    main.PrepareTemporalCamera(camera, 3);
    transient.PrepareTemporalCamera(camera, 3);
    CHECK(main.IsTemporalCameraValid());
    CHECK_FALSE(transient.IsTemporalCameraValid());
    main.PrepareTemporalCamera(camera, 4);
    main.PrepareTemporalCamera(camera, 5);
    transient.PrepareTemporalCamera(camera, 5);
    CHECK(main.IsTemporalCameraValid());
    CHECK_FALSE(transient.IsTemporalCameraValid());
    transient.PrepareTemporalCamera(camera, 6);
    CHECK(transient.IsTemporalCameraValid());
    main.RequestCameraCut();
    main.PrepareTemporalCamera(camera, 6);
    CHECK_FALSE(main.IsTemporalCameraValid());
    CHECK(transient.IsTemporalCameraValid());
}
