#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/FallbackModel.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/Pipeline/ModelDiagnosticWorkBuilder.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <catch2/catch2.hpp>

#include <array>

namespace
{
    struct DiagnosticFixture
    {
        DiagnosticFixture()
        {
            REQUIRE(materials.InitializeFallback(7));
            const std::array defaultMaterials = { materials.GetFallbackMaterialInstance() };
            REQUIRE(materials.AppendMaterialTable(defaultMaterials, materialTableOffset));
            REQUIRE(geometry.Append(ModelLoading::GetFallbackModelAssetView(), materialTableOffset,
                                    static_cast<u32>(defaultMaterials.size()), fallbackModel));
        }

        RenderScenes::ModelInstanceHandle Add(RenderAssets::ModelHandle model)
        {
            RenderScenes::ModelInstanceDesc desc;
            desc.model = model;
            const RenderScenes::ModelInstanceHandle instance = scene.CreateModelInstance(desc);
            scene.AcknowledgeClearsAndPublish();
            return instance;
        }

        MaterialLoading::MaterialStorage materials;
        ModelLoading::ModelGeometryStorage geometry;
        u32 materialTableOffset = 0;
        RenderAssets::ModelHandle fallbackModel;
        RenderScenes::RenderScene scene{51, &geometry, &materials};
    };

    RenderAssets::ModelHandle AddFallbackWithLODFlags(DiagnosticFixture& fixture, u32 flags)
    {
        ModelLoading::ModelAssetView view = ModelLoading::GetFallbackModelAssetView();
        std::array<FileFormat::Model::MeshLOD, 1> lods = { view.meshLODs.front() };
        lods[0].flags = flags;
        view.meshLODs = lods;

        RenderAssets::ModelHandle model;
        REQUIRE(fixture.geometry.Append(view, fixture.materialTableOffset, 1, model));
        return model;
    }
}

TEST_CASE("Diagnostic model work routes fallback geometry through the two-sided pipeline",
          "[Rendering][ModelDiagnostic]")
{
    DiagnosticFixture fixture;
    const RenderScenes::ModelInstanceHandle instance = fixture.Add(fixture.fallbackModel);
    const std::array selection = { instance };

    const ModelPipeline::DiagnosticWorkBuildResult work = ModelPipeline::ModelDiagnosticWorkBuilder::Build(
        fixture.scene, fixture.geometry, fixture.materials, selection);

    CHECK(work.oneSided.empty());
    REQUIRE(work.twoSided.size() == 1);
    CHECK(work.stats.selectedInstances == 1);
    CHECK(work.stats.twoSidedMeshlets == 1);
    CHECK(work.twoSided[0].instanceIndex == RenderScenes::GetModelInstanceSlot(instance));
    CHECK(work.twoSided[0].meshletIndex == 0);
    CHECK(work.twoSided[0].positionDecodeOffset == vec4(-0.5f, -0.5f, -0.5f, 0.0f));
    CHECK(work.twoSided[0].positionDecodeExtent == vec4(1.0f, 1.0f, 1.0f, 0.0f));
}

TEST_CASE("Diagnostic model work respects geometry-group masks", "[Rendering][ModelDiagnostic]")
{
    DiagnosticFixture fixture;
    const RenderScenes::ModelInstanceHandle instance = fixture.Add(fixture.fallbackModel);
    REQUIRE(fixture.scene.SetGeometryGroupEnabled(instance, 0, false));
    const std::array selection = { instance };

    const ModelPipeline::DiagnosticWorkBuildResult work = ModelPipeline::ModelDiagnosticWorkBuilder::Build(
        fixture.scene, fixture.geometry, fixture.materials, selection);

    CHECK(work.oneSided.empty());
    CHECK(work.twoSided.empty());
    CHECK(work.stats.skippedGeometryGroups == 1);
}

TEST_CASE("Diagnostic model work only accepts explicitly permitted bind-pose skinning",
          "[Rendering][ModelDiagnostic]")
{
    DiagnosticFixture fixture;
    const RenderAssets::ModelHandle unavailable = AddFallbackWithLODFlags(
        fixture, FileFormat::Model::MeshLODFlags_HasSkinningData);
    const RenderScenes::ModelInstanceHandle unavailableInstance = fixture.Add(unavailable);
    const std::array unavailableSelection = { unavailableInstance };

    const ModelPipeline::DiagnosticWorkBuildResult skipped = ModelPipeline::ModelDiagnosticWorkBuilder::Build(
        fixture.scene, fixture.geometry, fixture.materials, unavailableSelection);
    CHECK(skipped.oneSided.empty());
    CHECK(skipped.twoSided.empty());
    CHECK(skipped.stats.skippedSkinnedLODs == 1);

    const RenderAssets::ModelHandle bindPose = AddFallbackWithLODFlags(
        fixture, FileFormat::Model::MeshLODFlags_HasSkinningData |
                     FileFormat::Model::MeshLODFlags_StaticFallbackIsBindPose);
    const RenderScenes::ModelInstanceHandle bindPoseInstance = fixture.Add(bindPose);
    const std::array bindPoseSelection = { bindPoseInstance };
    const ModelPipeline::DiagnosticWorkBuildResult accepted = ModelPipeline::ModelDiagnosticWorkBuilder::Build(
        fixture.scene, fixture.geometry, fixture.materials, bindPoseSelection);
    REQUIRE(accepted.twoSided.size() == 1);
    CHECK(accepted.stats.skippedSkinnedLODs == 0);
}
