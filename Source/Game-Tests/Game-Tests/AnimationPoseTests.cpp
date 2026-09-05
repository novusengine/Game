#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"
#include "Game-Lib/Rendering/Animation/Pose/AnimationIK.h"
#include "Game-Lib/Rendering/Animation/Pose/PoseSourceStore.h"
#include "Game-Lib/Rendering/Animation/Asset/AnimationAssetValidator.h"

#include <Base/Math/Math.h>

#include <catch2/catch2.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>

namespace
{
    namespace AnimationFormat = FileFormat::Animation;

    AnimationFormat::LocalTransform Local(const vec3& translation = vec3(0.0f), const quat& rotation = quat(1.0f, 0.0f, 0.0f, 0.0f), const vec3& scale = vec3(1.0f))
    {
        AnimationFormat::LocalTransform result;
        result.translation = translation;
        result.rotation = rotation;
        result.scale = scale;
        return result;
    }

    mat4a InverseAffine(const mat4a& transform)
    {
        return mat4a(glm::inverse(mat4x4(transform)));
    }

    void CheckVec3(const vec3& value, const vec3& expected, f32 epsilon = 0.0001f)
    {
        CHECK(value.x == Catch::Approx(expected.x).margin(epsilon));
        CHECK(value.y == Catch::Approx(expected.y).margin(epsilon));
        CHECK(value.z == Catch::Approx(expected.z).margin(epsilon));
    }

    AnimationFormat::SkeletonJoint Joint(i32 parent, const AnimationFormat::LocalTransform& rest)
    {
        AnimationFormat::SkeletonJoint joint;
        joint.semanticID = static_cast<u64>(parent + 2);
        joint.parentJointIndex = parent;
        joint.restTransform = rest;
        return joint;
    }
}

TEST_CASE("Animation pose composes identity translation rotation and scale", "[AnimationPose]")
{
    const std::array<AnimationFormat::SkeletonJoint, 1> joints = {Joint(-1, Local())};

    auto evaluate = [&](const AnimationFormat::LocalTransform& local) {
        const std::array<AnimationFormat::LocalTransform, 1> pose = {local};
        const Animation::PoseEvaluationResult result = Animation::EvaluatePose(joints, pose);
        REQUIRE(result);
        return result.palette[0];
    };

    CheckVec3(Animation::TransformPoint(evaluate(Local()), vec3(2.0f, 3.0f, 4.0f)), vec3(2.0f, 3.0f, 4.0f));
    CheckVec3(Animation::TransformPoint(evaluate(Local(vec3(5.0f, -2.0f, 1.0f))), vec3(0.0f)), vec3(5.0f, -2.0f, 1.0f));
    CheckVec3(Animation::TransformVector(evaluate(Local(vec3(0.0f), glm::angleAxis(glm::half_pi<f32>(), vec3(0.0f, 0.0f, 1.0f)))), vec3(1.0f, 0.0f, 0.0f)), vec3(0.0f, 1.0f, 0.0f));
    CheckVec3(Animation::TransformVector(evaluate(Local(vec3(0.0f), quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(2.0f))), vec3(1.0f)), vec3(2.0f));
    CheckVec3(Animation::TransformVector(evaluate(Local(vec3(0.0f), quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(2.0f, 3.0f, 4.0f))), vec3(1.0f)), vec3(2.0f, 3.0f, 4.0f));
}

TEST_CASE("Animation palette has deterministic explicit row-major bytes", "[AnimationPose]")
{
    const mat4a transform = Math::AffineMatrix::TransformMatrix(vec3(10.0f, 11.0f, 12.0f), quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(2.0f, 3.0f, 4.0f));
    const Animation::AffineFloat3x4 packed = Animation::PackAffineFloat3x4(transform);
    const std::array<f32, 12> expected = {2.0f, 0.0f, 0.0f, 10.0f, 0.0f, 3.0f, 0.0f, 11.0f, 0.0f, 0.0f, 4.0f, 12.0f};
    std::array<f32, 12> observed = {};
    std::memcpy(observed.data(), &packed, sizeof(packed));
    CHECK(observed == expected);
    CHECK(Animation::UnpackAffineFloat3x4(packed) == transform);
}

TEST_CASE("Animation rest pose reconstructs bind identity", "[AnimationPose]")
{
    std::array<AnimationFormat::SkeletonJoint, 3> joints = {
        Joint(-1, Local(vec3(2.0f, 0.0f, 0.0f))),
        Joint(0, Local(vec3(0.0f, 3.0f, 0.0f), glm::angleAxis(glm::quarter_pi<f32>(), vec3(0.0f, 0.0f, 1.0f)))),
        Joint(1, Local(vec3(0.0f, 2.0f, 0.0f), quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(1.0f, 2.0f, 1.0f)))
    };
    std::array<mat4a, 3> model;
    for (u32 index = 0; index < joints.size(); ++index)
    {
        const mat4a local = Animation::ComposeLocalTransform(joints[index].restTransform);
        model[index] = joints[index].parentJointIndex < 0 ? local : Math::AffineMatrix::MatrixMul(model[joints[index].parentJointIndex], local);
        joints[index].inverseBindTransform = InverseAffine(model[index]);
    }
    const std::array<AnimationFormat::LocalTransform, 3> rest = {joints[0].restTransform, joints[1].restTransform, joints[2].restTransform};
    const Animation::PoseEvaluationResult result = Animation::EvaluatePose(joints, rest);
    REQUIRE(result);
    for (const Animation::AffineFloat3x4& paletteEntry : result.palette)
    {
        CheckVec3(Animation::TransformPoint(paletteEntry, vec3(2.0f, 3.0f, 4.0f)), vec3(2.0f, 3.0f, 4.0f));
        CheckVec3(Animation::TransformVector(paletteEntry, vec3(1.0f, 2.0f, 3.0f)), vec3(1.0f, 2.0f, 3.0f));
    }
}

TEST_CASE("Animation pose evaluates deep hierarchy and preserves generated shear", "[AnimationPose]")
{
    const std::array<AnimationFormat::SkeletonJoint, 4> joints = {
        Joint(-1, Local(vec3(1.0f, 0.0f, 0.0f), quat(1.0f, 0.0f, 0.0f, 0.0f), vec3(2.0f, 1.0f, 1.0f))),
        Joint(0, Local(vec3(1.0f, 0.0f, 0.0f), glm::angleAxis(glm::quarter_pi<f32>(), vec3(0.0f, 0.0f, 1.0f)))),
        Joint(1, Local(vec3(0.0f, 2.0f, 0.0f))),
        Joint(2, Local(vec3(0.0f, 0.0f, 3.0f)))
    };
    const std::array<AnimationFormat::LocalTransform, 4> pose = {joints[0].restTransform, joints[1].restTransform, joints[2].restTransform, joints[3].restTransform};
    const Animation::PoseEvaluationResult result = Animation::EvaluatePose(joints, pose);
    REQUIRE(result);
    CHECK(result.modelTransforms.size() == 4);
    CheckVec3(result.modelTransforms[3][3], vec3(3.0f - 2.0f * glm::sqrt(2.0f), glm::sqrt(2.0f), 3.0f));
    const vec3 xAxis = result.modelTransforms[1][0];
    const vec3 yAxis = result.modelTransforms[1][1];
    CHECK(std::abs(glm::dot(glm::normalize(xAxis), glm::normalize(yAxis))) > 0.1f);
}

TEST_CASE("Animation pose rejects invalid hierarchy roots and parent ordering", "[AnimationPose]")
{
    SECTION("multiple roots")
    {
        const std::array<AnimationFormat::SkeletonJoint, 2> joints = {Joint(-1, Local()), Joint(-1, Local())};
        const std::array<AnimationFormat::LocalTransform, 2> pose = {Local(), Local()};
        const Animation::PoseEvaluationResult result = Animation::EvaluatePose(joints, pose);
        CHECK(result.error == Animation::PoseEvaluationError::MultipleRoots);
    }

    SECTION("child before parent")
    {
        const std::array<AnimationFormat::SkeletonJoint, 2> joints = {Joint(1, Local()), Joint(-1, Local())};
        const std::array<AnimationFormat::LocalTransform, 2> pose = {Local(), Local()};
        const Animation::PoseEvaluationResult result = Animation::EvaluatePose(joints, pose);
        CHECK(result.error == Animation::PoseEvaluationError::InvalidParent);
        CHECK(result.errorJointIndex == 0);
    }
}

TEST_CASE("Bone masks resolve dense domain weights and reject incompatible poses", "[AnimationPose][BoneMask]")
{
    FileFormat::Animation::BoneMaskAsset root;
    root.poseDomainType = FileFormat::Animation::PoseDomainType::Skeleton;
    root.poseDomainAssetID = 42;
    const std::array<u8, 4> quantized = {0, 64, 128, 255};
    AnimationLoading::BoneMaskAssetView mask = {root, quantized};
    std::vector<f32> weights;
    REQUIRE(Animation::ResolveBoneMask(mask, FileFormat::Animation::PoseDomainType::Skeleton, 42, 4, weights));
    REQUIRE(weights.size() == 4);
    CHECK(weights[0] == 0.0f);
    CHECK(weights[1] == Catch::Approx(64.0f / 255.0f));
    CHECK(weights[2] == Catch::Approx(128.0f / 255.0f));
    CHECK(weights[3] == 1.0f);
    CHECK_FALSE(Animation::ResolveBoneMask(mask, FileFormat::Animation::PoseDomainType::Skeleton, 43, 4, weights));
    CHECK_FALSE(Animation::ResolveBoneMask(mask, FileFormat::Animation::PoseDomainType::Skeleton, 42, 3, weights));
}

TEST_CASE("Masked override and additive layers match local-space golden poses", "[AnimationPose][Layers]")
{
    const std::array<AnimationFormat::LocalTransform, 3> base = {
        Local(vec3(1.0f, 0.0f, 0.0f)),
        Local(vec3(0.0f, 2.0f, 0.0f), glm::angleAxis(glm::radians(10.0f), vec3(0.0f, 0.0f, 1.0f))),
        Local(vec3(0.0f, 0.0f, 3.0f))
    };
    const std::array<AnimationFormat::LocalTransform, 3> overridePose = {
        Local(vec3(9.0f, 0.0f, 0.0f)),
        Local(vec3(0.0f, 6.0f, 0.0f), glm::angleAxis(glm::radians(50.0f), vec3(0.0f, 0.0f, 1.0f))),
        Local(vec3(0.0f, 0.0f, 7.0f))
    };
    const std::array<f32, 3> mask = {0.0f, 0.5f, 1.0f};
    std::array<AnimationFormat::LocalTransform, 3> output = {};
    REQUIRE(Animation::BlendOverrideLocalPoses(base, overridePose, 0.5f, mask, output));
    CheckVec3(output[0].translation, base[0].translation);
    CheckVec3(output[1].translation, vec3(0.0f, 3.0f, 0.0f));
    CheckVec3(output[2].translation, vec3(0.0f, 0.0f, 5.0f));
    CHECK(std::abs(glm::dot(output[1].rotation, glm::angleAxis(glm::radians(20.0f), vec3(0.0f, 0.0f, 1.0f)))) == Catch::Approx(1.0f));

    std::array<AnimationFormat::LocalTransform, 3> additive = {};
    REQUIRE(Animation::BuildAdditiveLocalPose(overridePose, base, additive));
    REQUIRE(Animation::ApplyAdditiveLocalPose(base, additive, 1.0f, {}, output));
    for (u32 jointIndex = 0; jointIndex < output.size(); ++jointIndex)
    {
        CheckVec3(output[jointIndex].translation, overridePose[jointIndex].translation);
        CheckVec3(output[jointIndex].scale, overridePose[jointIndex].scale);
        CHECK(std::abs(glm::dot(output[jointIndex].rotation, overridePose[jointIndex].rotation)) == Catch::Approx(1.0f));
    }
}

TEST_CASE("Ordered additive rotations remain deterministic and noncommutative", "[AnimationPose][Layers]")
{
    const std::array<AnimationFormat::LocalTransform, 1> base = {Local()};
    const std::array<AnimationFormat::LocalTransform, 1> xLayer = {Local(vec3(0.0f), glm::angleAxis(glm::half_pi<f32>(), vec3(1.0f, 0.0f, 0.0f)))};
    const std::array<AnimationFormat::LocalTransform, 1> yLayer = {Local(vec3(0.0f), glm::angleAxis(glm::half_pi<f32>(), vec3(0.0f, 1.0f, 0.0f)))};
    std::array<AnimationFormat::LocalTransform, 1> xy = {};
    std::array<AnimationFormat::LocalTransform, 1> yx = {};
    REQUIRE(Animation::ApplyAdditiveLocalPose(base, xLayer, 1.0f, {}, xy));
    REQUIRE(Animation::ApplyAdditiveLocalPose(xy, yLayer, 1.0f, {}, xy));
    REQUIRE(Animation::ApplyAdditiveLocalPose(base, yLayer, 1.0f, {}, yx));
    REQUIRE(Animation::ApplyAdditiveLocalPose(yx, xLayer, 1.0f, {}, yx));
    CHECK(std::abs(glm::dot(xy[0].rotation, yx[0].rotation)) < 0.75f);
}

TEST_CASE("Target-skeleton IK solves reachable and bounded unreachable goals deterministically", "[AnimationPose][IK]")
{
    const std::array<AnimationFormat::SkeletonJoint, 3> joints = {
        Joint(-1, Local()),
        Joint(0, Local(vec3(1.0f, 0.0f, 0.0f))),
        Joint(1, Local(vec3(1.0f, 0.0f, 0.0f)))
    };
    const std::array<AnimationFormat::LocalTransform, 3> rest = {joints[0].restTransform, joints[1].restTransform, joints[2].restTransform};

    std::array<AnimationFormat::LocalTransform, 3> reachable = rest;
    const Animation::IKSolveResult solved = Animation::SolveTwoBoneIK(joints, 0, 1, 2, vec3(1.0f, 1.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), 2.0f, 1.0f, reachable);
    REQUIRE(solved.status == Animation::IKSolveStatus::Solved);
    CHECK(solved.finalError < 0.001f);
    const Animation::PoseEvaluationResult reachablePose = Animation::EvaluatePose(joints, reachable);
    REQUIRE(reachablePose);
    CheckVec3(reachablePose.modelTransforms[2][3], vec3(1.0f, 1.0f, 0.0f));

    std::array<AnimationFormat::LocalTransform, 3> repeated = rest;
    const Animation::IKSolveResult repeatedResult = Animation::SolveTwoBoneIK(joints, 0, 1, 2, vec3(1.0f, 1.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), 2.0f, 1.0f, repeated);
    REQUIRE(repeatedResult.status == Animation::IKSolveStatus::Solved);
    for (u32 jointIndex = 0; jointIndex < reachable.size(); ++jointIndex)
        CHECK(std::abs(glm::dot(reachable[jointIndex].rotation, repeated[jointIndex].rotation)) == Catch::Approx(1.0f));

    std::array<AnimationFormat::LocalTransform, 3> unreachable = rest;
    const Animation::IKSolveResult clamped = Animation::SolveTwoBoneIK(joints, 0, 1, 2, vec3(4.0f, 0.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), 2.0f, 1.0f, unreachable);
    REQUIRE(clamped.status == Animation::IKSolveStatus::Clamped);
    const Animation::PoseEvaluationResult unreachablePose = Animation::EvaluatePose(joints, unreachable);
    REQUIRE(unreachablePose);
    CHECK(glm::length(vec3(unreachablePose.modelTransforms[2][3])) == Catch::Approx(2.0f).margin(0.0001f));

    std::array<AnimationFormat::LocalTransform, 3> sequential = rest;
    Animation::IKPoseWorkspace workspace;
    REQUIRE(Animation::SolveTwoBoneIK(joints, 0, 1, 2, vec3(1.0f, 1.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), 2.0f, 1.0f, sequential, &workspace));
    const mat4a* workspaceStorage = workspace.modelTransforms.data();
    REQUIRE(Animation::SolveTwoBoneIK(joints, 0, 1, 2, vec3(0.5f, 1.5f, 0.0f), vec3(0.0f, 0.0f, 1.0f), 2.0f, 1.0f, sequential, &workspace));
    CHECK(workspace.current);
    CHECK(workspace.modelTransforms.data() == workspaceStorage);
    CheckVec3(workspace.modelTransforms[2][3], vec3(0.5f, 1.5f, 0.0f));
}

TEST_CASE("Target-skeleton aim IK rejects stale goals and rotates the requested axis", "[AnimationPose][IK]")
{
    const std::array<AnimationFormat::SkeletonJoint, 1> joints = {Joint(-1, Local())};
    std::array<AnimationFormat::LocalTransform, 1> pose = {joints[0].restTransform};
    const Animation::IKSolveResult invalid = Animation::SolveAimIK(joints, 0, vec3(1.0f, 0.0f, 0.0f), vec3(0.0f), 1.0f, pose);
    CHECK(invalid.status == Animation::IKSolveStatus::InvalidGoal);

    const Animation::IKSolveResult solved = Animation::SolveAimIK(joints, 0, vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f), 1.0f, pose);
    REQUIRE(solved.status == Animation::IKSolveStatus::Solved);
    CHECK(solved.finalError < 0.001f);
    const Animation::PoseEvaluationResult evaluated = Animation::EvaluatePose(joints, pose);
    REQUIRE(evaluated);
    CheckVec3(vec3(evaluated.modelTransforms[0] * vec4(1.0f, 0.0f, 0.0f, 0.0f)), vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("Iterative target-skeleton IK solves an aligned offset effector across a shoulder chain", "[AnimationPose][IK]")
{
    const std::array<AnimationFormat::SkeletonJoint, 4> joints = {
        Joint(-1, Local()),
        Joint(0, Local(vec3(0.5f, 0.0f, 0.0f))),
        Joint(1, Local(vec3(1.0f, 0.0f, 0.0f))),
        Joint(2, Local(vec3(1.0f, 0.0f, 0.0f)))
    };
    const std::array<u32, 4> chain = {0, 1, 2, 3};
    std::array<AnimationFormat::LocalTransform, 4> pose = {joints[0].restTransform, joints[1].restTransform, joints[2].restTransform, joints[3].restTransform};
    const vec3 socketPoint(0.0f, 0.2f, 0.0f);
    const vec3 goal(1.5f, 1.2f, 0.0f);
    const Animation::IKSolveResult solved = Animation::SolveIterativeChainIK(joints, chain, 3, goal, 2.5f, 32, 0.0001f, 1.0f, pose, nullptr, socketPoint, vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(solved.status == Animation::IKSolveStatus::Solved);
    CHECK(solved.finalError < 0.001f);
    const Animation::PoseEvaluationResult evaluated = Animation::EvaluatePose(joints, pose);
    REQUIRE(evaluated);
    CheckVec3(vec3(mat4x4(evaluated.modelTransforms[3]) * vec4(socketPoint, 1.0f)), goal);
    CheckVec3(glm::normalize(vec3(evaluated.modelTransforms[3] * vec4(0.0f, 1.0f, 0.0f, 0.0f))), vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("Target-skeleton IK joint limits clamp authored local rotations", "[AnimationPose][IK]")
{
    std::array<AnimationFormat::LocalTransform, 1> pose = {Local(vec3(0.0f), glm::angleAxis(glm::radians(60.0f), vec3(0.0f, 0.0f, 1.0f)))};
    const std::array<u32, 1> joints = {0};
    FileFormat::Animation::IKJointLimit limit;
    limit.minimumRadians = vec3(glm::radians(-10.0f), glm::radians(-10.0f), glm::radians(-30.0f));
    limit.maximumRadians = vec3(glm::radians(10.0f), glm::radians(10.0f), glm::radians(30.0f));
    const std::array<FileFormat::Animation::IKJointLimit, 1> limits = {limit};
    bool clamped = false;
    REQUIRE(Animation::ApplyIKJointLimits(joints, limits, pose, clamped));
    CHECK(clamped);
    CHECK(glm::eulerAngles(pose[0].rotation).z == Catch::Approx(glm::radians(30.0f)).margin(0.0001f));
}

TEST_CASE("Sparse pose queries consume the final post-IK target pose", "[AnimationPose][IK][PoseQueries]")
{
    std::array<AnimationFormat::SkeletonJoint, 3> joints = {
        Joint(-1, Local()),
        Joint(0, Local(vec3(1.0f, 0.0f, 0.0f))),
        Joint(1, Local(vec3(1.0f, 0.0f, 0.0f)))
    };
    joints[0].semanticID = 10;
    joints[1].semanticID = 11;
    joints[2].semanticID = 12;
    std::array<AnimationFormat::LocalTransform, 3> pose = {joints[0].restTransform, joints[1].restTransform, joints[2].restTransform};
    Animation::IKPoseWorkspace workspace;
    REQUIRE(Animation::SolveTwoBoneIK(joints, 0, 1, 2, vec3(1.0f, 1.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f), 2.0f, 1.0f, pose, &workspace));
    REQUIRE(workspace.current);
    AnimationLoading::SkeletonAssetView skeleton;
    skeleton.joints = joints;
    Animation::PoseSourceStore store;
    const Animation::PoseSourceHandle source = store.CreateRestPose({}, skeleton);
    REQUIRE(source);
    REQUIRE(store.SetLocalPose(source, pose, skeleton));
    store.BeginPoseQueryFrame(1);
    const Animation::PoseQueryHandle query = store.RequestJoint(source, skeleton, 12);
    REQUIRE(query);
    store.ResolvePoseQueries();
    REQUIRE(store.GetPoseQuery(query));
    REQUIRE(store.GetPoseQuery(query)->valid);
    CheckVec3(store.GetPoseQuery(query)->modelTransform[3], vec3(1.0f, 1.0f, 0.0f));
    CHECK(store.GetStats().fullPoseQueryPromotions == 1);
}

TEST_CASE("Animation rest pose matches selected Blender Mixamo joint matrices", "[AnimationPose][ExternalFixture]")
{
    const char* skeletonPath = std::getenv("NOVUS_NBS_PHASE2_SKELETON");
    if (!skeletonPath)
        return;

    std::ifstream input(skeletonPath, std::ios::binary);
    REQUIRE(input);
    const std::vector<u8> payload(std::istreambuf_iterator<char>(input), {});
    const AnimationLoading::SkeletonAssetReadResult skeleton = AnimationLoading::AnimationAssetValidator::Validate(AnimationLoading::AnimationAssetReader::ReadSkeleton(payload));
    REQUIRE(skeleton);
    const Animation::PoseEvaluationResult pose = Animation::EvaluateRestPose(skeleton.view);
    REQUIRE(pose);

    struct Reference
    {
        const char* name;
        u32 jointIndex;
        u64 semanticID;
        std::array<f32, 12> columnMajor;
    };
    // Independent headless Blender 5.2 extraction from character.fbx after the
    // NBS Blender-to-Novus basis conversion. Matrices include axes and position.
    const std::array<Reference, 5> references = {{
        {"mixamorig1:Hips", 0, 2810948900220380606ull, {0.01f, 0.0f, 0.0f, 0.0f, 0.0f, -0.01f, 0.0f, 0.01f, 0.0f, -0.000000228882f, 0.997543633f, -0.030078746f}},
        {"mixamorig1:Spine", 1, 10879038013054572596ull, {0.01f, 0.0f, 0.0f, 0.0f, 0.001205720f, -0.009927046f, 0.0f, 0.009927046f, 0.001205720f, -0.000000228882f, 1.094715834f, -0.018276673f}},
        {"mixamorig1:Head", 5, 14964138255946933426ull, {0.01f, 0.0f, 0.0f, 0.0f, 0.0f, -0.01f, 0.0f, 0.01f, 0.0f, -0.000000228882f, 1.550609231f, 0.009211987f}},
        {"mixamorig1:LeftHand", 10, 11310978348904814943ull, {0.000925690f, 0.001079028f, 0.009898421f, -0.000596830f, -0.009917221f, 0.001136891f, 0.009939157f, -0.000696008f, -0.000853626f, 0.674998939f, 1.422124624f, 0.035147954f}},
        {"mixamorig1:RightFoot", 62, 12712341276783455589ull, {-0.009962592f, 0.000818076f, 0.000285417f, 0.000514828f, 0.008239830f, -0.005642741f, -0.000697036f, -0.005606773f, -0.008251050f, -0.151803046f, 0.098707348f, 0.031773958f}}
    }};

    constexpr f32 tolerance = 2.0e-5f;
    for (const Reference& reference : references)
    {
        INFO(reference.name);
        REQUIRE(reference.jointIndex < skeleton.view.joints.size());
        CHECK(skeleton.view.joints[reference.jointIndex].semanticID == reference.semanticID);
        const mat4a& observed = pose.modelTransforms[reference.jointIndex];
        for (u32 column = 0; column < 4; ++column)
            for (u32 row = 0; row < 3; ++row)
                CHECK(observed[column][row] == Catch::Approx(reference.columnMajor[column * 3 + row]).margin(tolerance));
    }
}
