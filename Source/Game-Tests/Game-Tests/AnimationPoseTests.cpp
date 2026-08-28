#include "Game-Lib/Rendering/Animation/Pose/AnimationPose.h"

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

TEST_CASE("Animation rest pose matches selected Blender Mixamo joint matrices", "[AnimationPose][ExternalFixture]")
{
    const char* skeletonPath = std::getenv("NOVUS_NBS_PHASE2_SKELETON");
    if (!skeletonPath)
        return;

    std::ifstream input(skeletonPath, std::ios::binary);
    REQUIRE(input);
    const std::vector<u8> payload(std::istreambuf_iterator<char>(input), {});
    const AnimationLoading::SkeletonAssetReadResult skeleton = AnimationLoading::AnimationAssetReader::ReadSkeleton(payload, AssetLoading::ValidationMode::Full);
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
