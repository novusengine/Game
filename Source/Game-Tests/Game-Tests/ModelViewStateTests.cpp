#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/FallbackModel.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/View/ModelViewState.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

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
    CHECK_FALSE(view.IsWorkDirty());
}

TEST_CASE("Model View input preparation skips retired Scene instances", "[Rendering][ModelView]")
{
    ViewFixture fixture;
    const RenderScenes::ModelInstanceHandle instance = fixture.AddInstance();
    REQUIRE(fixture.scene.DestroyModelInstance(instance, 0));
    fixture.scene.ReleaseRetiredHistory(0);

    ModelView::ModelViewState view;
    view.SetDiagnosticSelection(instance);
    view.PrepareInputs(fixture.scene, fixture.geometry);

    CHECK(view.GetInputs().IsEmpty());
    CHECK(view.GetLODHistory().IsEmpty());
    CHECK(view.GetQueueCapacity() == 1);
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

    REQUIRE(view.GetInputs().Count() == 1);
    CHECK(view.GetInputs()[0].instanceIndex == RenderScenes::GetModelInstanceSlot(second));
}
