#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/FallbackModel.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/View/ModelViewState.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"
#include "Game-Lib/Rendering/Scene/RenderView.h"

#include <catch2/catch2.hpp>

#include <array>

namespace
{
    struct ViewFixture
    {
        ViewFixture()
        {
            REQUIRE(materials.InitializeFallback(7));
            const std::array defaultMaterials = { materials.GetFallbackMaterialInstance() };
            REQUIRE(materials.AppendMaterialTable(defaultMaterials, materialTableOffset));
            REQUIRE(geometry.Append(ModelLoading::GetFallbackModelAssetView(), materialTableOffset,
                                    static_cast<u32>(defaultMaterials.size()), fallbackModel));
        }

        RenderScenes::ModelInstanceHandle AddInstance()
        {
            RenderScenes::ModelInstanceDesc desc;
            desc.model = fallbackModel;
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
}

TEST_CASE("Model View inputs retain stable Scene slots and LOD history", "[Rendering][ModelView]")
{
    ViewFixture fixture;
    const RenderScenes::ModelInstanceHandle instance = fixture.AddInstance();

    ModelView::ModelViewState view;
    view.SetDiagnosticSelection(instance);
    view.PrepareInputs(fixture.scene, fixture.geometry);

    REQUIRE(view.GetInputs().Count() == 1);
    CHECK(view.GetInputs()[0].instanceIndex == RenderScenes::GetModelInstanceSlot(instance));
    CHECK(view.GetInputs()[0].lodHistoryOffset == 0);
    REQUIRE(view.GetLODHistory().Count() == 1);
    CHECK(view.GetLODHistory()[0] == ModelView::INVALID_LOD_HISTORY);
    CHECK(view.GetQueueCapacity() == 2);
    CHECK(view.GetLoadedLOD0Meshlets() == 1);
    CHECK(view.GetLoadedLOD0Triangles() > 0);
    CHECK_FALSE(view.IsWorkDirty());
}

TEST_CASE("Model View input preparation skips retired Scene instances", "[Rendering][ModelView]")
{
    ViewFixture fixture;
    const RenderScenes::ModelInstanceHandle instance = fixture.AddInstance();
    REQUIRE(fixture.scene.DestroyModelInstance(instance));
    fixture.scene.ReleaseRetiredHistory(0);

    ModelView::ModelViewState view;
    view.SetDiagnosticSelection(instance);
    view.PrepareInputs(fixture.scene, fixture.geometry);

    CHECK(view.GetActiveInputCount() == 0);
    CHECK(view.GetDispatchInputCount() == 0);
    CHECK(view.GetLODHistory().IsEmpty());
    CHECK(view.GetQueueCapacity() == 1);
    CHECK(view.GetLoadedLOD0Meshlets() == 0);
    CHECK(view.GetLoadedLOD0Triangles() == 0);
}

TEST_CASE("Model View diagnostic selection replaces the previous instance", "[Rendering][ModelView]")
{
    ViewFixture fixture;
    const RenderScenes::ModelInstanceHandle first = fixture.AddInstance();
    const RenderScenes::ModelInstanceHandle second = fixture.AddInstance();

    ModelView::ModelViewState view;
    view.SetDiagnosticSelection(first);
    view.SetDiagnosticSelection(second);
    view.PrepareInputs(fixture.scene, fixture.geometry);

    REQUIRE(view.GetActiveInputCount() == 1);
    const u32 secondSlot = RenderScenes::GetModelInstanceSlot(second);
    REQUIRE(view.GetInputs().Count() > secondSlot);
    CHECK(view.GetInputs()[secondSlot].instanceIndex == secondSlot);
}

TEST_CASE("Model View prepares every active Scene instance without a diagnostic override", "[Rendering][ModelView]")
{
    ViewFixture fixture;
    const RenderScenes::ModelInstanceHandle first = fixture.AddInstance();
    const RenderScenes::ModelInstanceHandle second = fixture.AddInstance();

    ModelView::ModelViewState view;
    view.PrepareInputs(fixture.scene, fixture.geometry);
    fixture.scene.AcknowledgeModelMembershipChanges();

    REQUIRE(view.GetInputs().Count() == 2);
    CHECK(view.GetActiveInputCount() == 2);
    CHECK(view.GetInputs()[0].instanceIndex == RenderScenes::GetModelInstanceSlot(first));
    CHECK(view.GetInputs()[1].instanceIndex == RenderScenes::GetModelInstanceSlot(second));
    CHECK(view.GetLoadedLOD0Meshlets() == 2);
    CHECK(view.GetLoadedLOD0Triangles() > 0);
    CHECK(view.GetPreparedSceneRevision() == fixture.scene.GetModelInstances().GetMembershipRevision());
}

TEST_CASE("Model View membership churn preserves unrelated slot and LOD history ownership", "[Rendering][ModelView]")
{
    ViewFixture fixture;
    const RenderScenes::ModelInstanceHandle first = fixture.AddInstance();
    const RenderScenes::ModelInstanceHandle stable = fixture.AddInstance();

    ModelView::ModelViewState view;
    view.PrepareInputs(fixture.scene, fixture.geometry);
    fixture.scene.AcknowledgeModelMembershipChanges();
    const u32 stableSlot = RenderScenes::GetModelInstanceSlot(stable);
    const u32 stableHistory = view.GetInputs()[stableSlot].lodHistoryOffset;
    view.GetLODHistory()[stableHistory] = 3;

    fixture.scene.SetHistoryRetireValue(1);
    REQUIRE(fixture.scene.DestroyModelInstance(first));
    view.PrepareChangedInputs(fixture.scene, fixture.geometry, fixture.scene.GetModelMembershipChanges());
    fixture.scene.AcknowledgeModelMembershipChanges();

    CHECK(view.GetActiveInputCount() == 1);
    CHECK(view.GetInputs()[stableSlot].lodHistoryOffset == stableHistory);
    CHECK(view.GetLODHistory()[stableHistory] == 3);

    fixture.scene.ReleaseRetiredHistory(1);
    const RenderScenes::ModelInstanceHandle replacement = fixture.AddInstance();
    view.PrepareChangedInputs(fixture.scene, fixture.geometry, fixture.scene.GetModelMembershipChanges());
    CHECK(RenderScenes::GetModelInstanceSlot(replacement) == RenderScenes::GetModelInstanceSlot(first));
    CHECK(view.GetInputs()[stableSlot].lodHistoryOffset == stableHistory);
    CHECK(view.GetLODHistory()[stableHistory] == 3);
}

TEST_CASE("Model View retains targeted clear requests until graph consumption", "[Rendering][ModelView]")
{
    ModelView::ModelViewState view;
    const std::array slots = {2u, 9u};
    const std::array ranges = {ModelScene::MeshletHistoryRange{4, 3}, ModelScene::MeshletHistoryRange{20, 2}};
    view.QueueTemporalClears(slots, ranges);

    REQUIRE(view.GetPendingInstanceClears().size() == slots.size());
    REQUIRE(view.GetPendingMeshletClears().size() == ranges.size());
    CHECK(view.GetPendingMeshletClears()[1].wordOffset == 20);

    view.AcknowledgeTemporalClears();
    CHECK(view.GetPendingInstanceClears().empty());
    CHECK(view.GetPendingMeshletClears().empty());
}

TEST_CASE("Model View counts loaded transparent LOD 0 meshlets", "[Rendering][ModelView]")
{
    ViewFixture fixture;
    const RenderScenes::ModelInstanceHandle opaque = fixture.AddInstance();
    const RenderScenes::ModelInstanceHandle faded = fixture.AddInstance();
    REQUIRE(fixture.scene.SetModelOpacity(faded, 0.5f));

    ModelView::ModelViewState view;
    view.PrepareInputs(fixture.scene, fixture.geometry);
    view.PrepareTransparentStats(fixture.scene, fixture.geometry, fixture.materials);

    CHECK(view.GetLoadedLOD0TransparentMeshlets() == 1);
    CHECK(view.GetLoadedLOD0TransparentTriangles() == view.GetLoadedLOD0Triangles() / 2u);
    CHECK(view.GetPreparedTransparentRoutingRevision() == fixture.scene.GetTransparentRoutingRevision());

    REQUIRE(fixture.scene.SetModelOpacity(faded, 1.0f));
    CHECK(view.GetPreparedTransparentRoutingRevision() != fixture.scene.GetTransparentRoutingRevision());
    view.PrepareTransparentStats(fixture.scene, fixture.geometry, fixture.materials);
    CHECK(view.GetLoadedLOD0TransparentMeshlets() == 0);
    CHECK(view.GetLoadedLOD0TransparentTriangles() == 0);
    CHECK(fixture.scene.IsAlive(opaque));
}

TEST_CASE("Retained Render Views render only when dirty", "[Rendering][RenderView]")
{
    RenderScenes::RenderViewDesc desc;
    desc.viewID = 7;
    desc.debugName = "Retained Test";
    desc.dimensions = uvec2(320, 480);
    desc.refresh = RenderScenes::RenderViewRefresh::Retained;
    RenderScenes::RenderView view(desc);

    CHECK(view.ShouldRender());
    view.MarkRendered();
    CHECK_FALSE(view.ShouldRender());

    view.MarkDirty();
    CHECK(view.ShouldRender());
    view.MarkRendered();
    view.RequestTemporalReset();
    CHECK(view.ShouldRender());
    CHECK(view.GetTemporalResetGeneration() == 1);
}

TEST_CASE("Render View resize invalidates temporal camera history", "[Rendering][RenderView]")
{
    RenderScenes::RenderViewDesc desc;
    desc.dimensions = uvec2(320, 480);
    RenderScenes::RenderView view(desc);

    const mat4x4 worldToClip(1.0f);
    view.PrepareTemporalCamera(worldToClip);
    CHECK_FALSE(view.IsTemporalCameraValid());
    mat4x4 movedWorldToClip(1.0f);
    movedWorldToClip[3][0] = 0.25f;
    view.PrepareTemporalCamera(movedWorldToClip);
    CHECK(view.IsTemporalCameraValid());
    CHECK(view.GetPreviousWorldToClip()[3][0] == worldToClip[3][0]);

    view.SetDimensions(uvec2(640, 960));
    CHECK(view.GetTemporalResetGeneration() == 1);
    view.PrepareTemporalCamera(worldToClip);
    CHECK_FALSE(view.IsTemporalCameraValid());

    view.PrepareTemporalCamera(worldToClip);
    CHECK(view.IsTemporalCameraValid());
    view.SetDimensions(uvec2(640, 960));
    view.PrepareTemporalCamera(worldToClip);
    CHECK(view.IsTemporalCameraValid());
}
