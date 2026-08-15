#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/FallbackModel.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/Scene/MeshletHistoryAllocator.h"
#include "Game-Lib/Rendering/Model/Scene/ModelSceneBridge.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <catch2/catch2.hpp>

#include <array>

namespace
{
    struct SceneFixture
    {
        SceneFixture()
        {
            REQUIRE(materials.InitializeFallback(7));
            const std::array defaultMaterials = {materials.GetFallbackMaterialInstance()};
            u32 materialTableOffset = 0;
            REQUIRE(materials.AppendMaterialTable(defaultMaterials, materialTableOffset));
            REQUIRE(geometry.Append(ModelLoading::GetFallbackModelAssetView(), materialTableOffset,
                                    static_cast<u32>(defaultMaterials.size()), model));
        }

        MaterialLoading::MaterialStorage materials;
        ModelLoading::ModelGeometryStorage geometry;
        RenderAssets::ModelHandle model;
    };

    mat4x4 Translation(f32 x)
    {
        mat4x4 transform(1.0f);
        transform[3].x = x;
        return transform;
    }
} // namespace

TEST_CASE("Render scene rejects stale generations after slot reuse", "[Rendering][RenderScene]")
{
    SceneFixture fixture;
    RenderScenes::RenderScene scene(17, &fixture.geometry, &fixture.materials);

    RenderScenes::ModelInstanceDesc desc;
    desc.model = fixture.model;
    const RenderScenes::ModelInstanceHandle first = scene.CreateModelInstance(desc);
    CHECK(scene.IsPending(first));
    CHECK_FALSE(scene.IsAlive(first));
    REQUIRE(scene.GetPendingClearRequests().instanceSlots.size() == 1);
    REQUIRE(scene.GetPendingClearRequests().meshletHistoryRanges.size() == 1);

    scene.AcknowledgeClearsAndPublish();
    CHECK(scene.IsAlive(first));
    REQUIRE(scene.DestroyModelInstance(first, 5));
    CHECK_FALSE(scene.IsAlive(first));
    CHECK_FALSE(scene.SetModelVisible(first, true));

    const RenderScenes::ModelInstanceHandle second = scene.CreateModelInstance(desc);
    CHECK(RenderScenes::GetModelInstanceSlot(second) == RenderScenes::GetModelInstanceSlot(first));
    CHECK(RenderScenes::GetModelInstanceGeneration(second) != RenderScenes::GetModelInstanceGeneration(first));
    CHECK(scene.IsPending(second));
    scene.AcknowledgeClearsAndPublish();
    CHECK(scene.IsAlive(second));
    CHECK_FALSE(scene.IsAlive(first));
    scene.AdvanceFrame();
    CHECK((scene.GetModelInstance(second)->flags & ModelScene::ModelInstanceFlagMotionValid) != 0);

    const auto stats = scene.GetStats();
    CHECK(stats.instances.staleHandleRejects == 1);
    CHECK(stats.meshletHistory.retiredWords == 1);
}

TEST_CASE("Render scene ignores stale queued publication slots after pending reuse", "[Rendering][RenderScene]")
{
    SceneFixture fixture;
    RenderScenes::RenderScene scene(18, &fixture.geometry, &fixture.materials);

    RenderScenes::ModelInstanceDesc desc;
    desc.model = fixture.model;
    const RenderScenes::ModelInstanceHandle stale = scene.CreateModelInstance(desc);
    REQUIRE(scene.DestroyModelInstance(stale, 0));
    const RenderScenes::ModelInstanceHandle replacement = scene.CreateModelInstance(desc);
    REQUIRE(RenderScenes::GetModelInstanceSlot(stale) == RenderScenes::GetModelInstanceSlot(replacement));

    scene.AcknowledgeClearsAndPublish();
    CHECK_FALSE(scene.IsAlive(stale));
    CHECK(scene.IsAlive(replacement));
    CHECK(scene.GetStats().instances.liveInstances == 1);
}

TEST_CASE("Meshlet history reuse is deferred, coalesced, trimmed, and cleared", "[Rendering][RenderScene]")
{
    ModelScene::MeshletHistoryAllocator allocator;
    const ModelScene::MeshletHistoryRange first = allocator.Allocate(3);
    const ModelScene::MeshletHistoryRange second = allocator.Allocate(2);
    allocator.AcknowledgePendingClears();

    allocator.Retire(first, 4);
    allocator.ReleaseRetired(3);
    const ModelScene::MeshletHistoryRange beforeRetirement = allocator.Allocate(3);
    CHECK(beforeRetirement.wordOffset == 5);
    allocator.AcknowledgePendingClears();

    allocator.ReleaseRetired(4);
    const ModelScene::MeshletHistoryRange reused = allocator.Allocate(3);
    CHECK(reused.wordOffset == first.wordOffset);
    REQUIRE(allocator.GetPendingClears().size() == 1);
    CHECK(allocator.GetPendingClears()[0].wordOffset == reused.wordOffset);

    allocator.AcknowledgePendingClears();
    allocator.Retire(reused, 6);
    allocator.Retire(second, 6);
    allocator.Retire(beforeRetirement, 6);
    allocator.ReleaseRetired(6);
    const auto stats = allocator.GetStats();
    CHECK(stats.liveWords == 0);
    CHECK(stats.retiredWords == 0);
    CHECK(stats.addressSpaceWords == 0);
    CHECK(stats.freeRanges == 0);
    CHECK(stats.highWaterWords == 8);
}

TEST_CASE("Render scene tracks transforms, teleports, visibility, groups, and "
          "private materials",
          "[Rendering][RenderScene]")
{
    SceneFixture fixture;
    RenderScenes::RenderScene scene(21, &fixture.geometry, &fixture.materials);

    RenderScenes::ModelInstanceDesc desc;
    desc.model = fixture.model;
    desc.worldTransform = Translation(1.0f);
    const RenderScenes::ModelInstanceHandle instance = scene.CreateModelInstance(desc);
    scene.AcknowledgeClearsAndPublish();

    REQUIRE(scene.SetModelTransform(instance, Translation(2.0f)));
    const ModelScene::ModelInstanceGPURecord* record = scene.GetModelInstance(instance);
    REQUIRE(record);
    CHECK(record->previousWorld[3].x == 1.0f);
    CHECK(record->currentWorld[3].x == 2.0f);

    scene.AdvanceFrame();
    CHECK(record->previousWorld[3].x == 2.0f);
    REQUIRE(scene.SetModelTransform(instance, Translation(9.0f), true));
    CHECK(record->previousWorld[3].x == 9.0f);
    CHECK((record->flags & ModelScene::ModelInstanceFlagTeleported) != 0);
    CHECK((record->flags & ModelScene::ModelInstanceFlagMotionValid) == 0);
    CHECK(scene.GetPendingClearRequests().instanceSlots.size() == 1);
    CHECK(scene.GetPendingClearRequests().meshletHistoryRanges.size() == 1);
    scene.AcknowledgeClearsAndPublish();

    REQUIRE(scene.SetModelVisible(instance, false));
    CHECK((record->flags & ModelScene::ModelInstanceFlagVisible) == 0);
    REQUIRE(scene.SetModelVisible(instance, true));
    CHECK((record->flags & ModelScene::ModelInstanceFlagVisible) != 0);
    CHECK(scene.GetPendingClearRequests().instanceSlots.size() == 1);
    CHECK(scene.GetPendingClearRequests().meshletHistoryRanges.size() == 1);

    REQUIRE(scene.SetGeometryGroupEnabled(instance, 0, false));
    CHECK_FALSE(scene.GetGeometryGroupMasks().IsEnabled(
        scene.GetModelInstances().GetResources(instance)->geometryGroupMask, 0));
    REQUIRE(scene.SetGeometryGroupEnabled(instance, 0, true));

    const auto sharedTable = scene.GetModelInstances().GetResources(instance)->materialTable;
    REQUIRE_FALSE(scene.GetModelMaterialTables().IsPrivate(sharedTable));
    const RenderAssets::MaterialInstanceHandle overrideMaterial = fixture.materials.GetFallbackMaterialInstance();
    REQUIRE(scene.SetModelMaterial(instance, 0, overrideMaterial));
    const auto privateTable = scene.GetModelInstances().GetResources(instance)->materialTable;
    CHECK(scene.GetModelMaterialTables().IsPrivate(privateTable));
    CHECK(scene.GetModelMaterialTables().GetMaterial(privateTable, 0) ==
          static_cast<RenderAssets::MaterialInstanceHandle::type>(overrideMaterial));
    CHECK(scene.GetModelMaterialTables().GetMaterial(sharedTable, 0) ==
          static_cast<RenderAssets::MaterialInstanceHandle::type>(fixture.materials.GetFallbackMaterialInstance()));

    REQUIRE(scene.ResetModelMaterials(instance));
    CHECK_FALSE(
        scene.GetModelMaterialTables().IsPrivate(scene.GetModelInstances().GetResources(instance)->materialTable));
}

TEST_CASE("Render scene shadow state tracks caster bounds and lifecycle invalidations",
          "[Rendering][RenderScene][Shadow]")
{
    SceneFixture fixture;
    RenderScenes::RenderScene scene(22, &fixture.geometry, &fixture.materials);
    const FileFormat::Model::Bounds& bounds = fixture.geometry.GetRecord(fixture.model).bounds;

    RenderScenes::ModelInstanceDesc desc;
    desc.model = fixture.model;
    desc.worldTransform = Translation(4.0f);
    const RenderScenes::ModelInstanceHandle instance = scene.CreateModelInstance(desc);

    std::vector<vec4> invalidations;
    REQUIRE(scene.DrainShadowInvalidations(invalidations, 16) == 1);
    REQUIRE(invalidations.size() == 2);
    CHECK(invalidations[0].x == Catch::Approx(bounds.center.x + 4.0f - bounds.extents.x));
    CHECK(invalidations[1].x == Catch::Approx(bounds.center.x + 4.0f + bounds.extents.x));

    scene.AcknowledgeClearsAndPublish();
    invalidations.clear();
    REQUIRE(scene.SetModelTransform(instance, Translation(9.0f)));
    REQUIRE(scene.DrainShadowInvalidations(invalidations, 16) == 1);
    CHECK(scene.GetShadowStats().dynamicCasters == 1);
    CHECK((scene.GetModelInstance(instance)->flags & ModelScene::ModelInstanceFlagDynamicShadowCaster) != 0);
    const std::span<const vec4> dynamicBounds = scene.GetDynamicShadowAABBs();
    REQUIRE(dynamicBounds.size() == 2);
    CHECK(dynamicBounds[0].x == Catch::Approx(bounds.center.x + 9.0f - bounds.extents.x));
    CHECK(dynamicBounds[1].x == Catch::Approx(bounds.center.x + 9.0f + bounds.extents.x));

    scene.AdvanceFrame();
    scene.AdvanceFrame();
    scene.AdvanceFrame();
    CHECK(scene.GetShadowStats().dynamicCasters == 0);
    CHECK((scene.GetModelInstance(instance)->flags & ModelScene::ModelInstanceFlagDynamicShadowCaster) == 0);
    invalidations.clear();
    REQUIRE(scene.DrainShadowInvalidations(invalidations, 16) == 1);
    CHECK(invalidations[0].x == Catch::Approx(bounds.center.x + 9.0f - bounds.extents.x));

    invalidations.clear();
    REQUIRE(scene.SetModelCastsShadows(instance, false));
    CHECK((scene.GetModelInstance(instance)->flags & ModelScene::ModelInstanceFlagCastsShadows) == 0);
    CHECK(scene.DrainShadowInvalidations(invalidations, 16) == 1);
    invalidations.clear();
    REQUIRE(scene.SetModelTransform(instance, Translation(10.0f)));
    CHECK(scene.DrainShadowInvalidations(invalidations, 16) == 0);
    REQUIRE(scene.SetModelCastsShadows(instance, true));
    CHECK((scene.GetModelInstance(instance)->flags & ModelScene::ModelInstanceFlagCastsShadows) != 0);
    CHECK(scene.DrainShadowInvalidations(invalidations, 16) == 1);
    invalidations.clear();
    REQUIRE(scene.SetGeometryGroupEnabled(instance, 0, false));
    CHECK(scene.DrainShadowInvalidations(invalidations, 16) == 1);
    invalidations.clear();
    REQUIRE(scene.SetModelVisible(instance, false));
    CHECK(scene.DrainShadowInvalidations(invalidations, 16) == 1);
    invalidations.clear();
    REQUIRE(scene.SetModelVisible(instance, true));
    CHECK(scene.DrainShadowInvalidations(invalidations, 16) == 1);
    invalidations.clear();
    REQUIRE(scene.DestroyModelInstance(instance, 0));
    CHECK(scene.DrainShadowInvalidations(invalidations, 16) == 1);

    desc.visible = false;
    const RenderScenes::ModelInstanceHandle hidden = scene.CreateModelInstance(desc);
    invalidations.clear();
    CHECK(scene.DrainShadowInvalidations(invalidations, 16) == 0);
    REQUIRE(scene.DestroyModelInstance(hidden, 0));
    CHECK(scene.DrainShadowInvalidations(invalidations, 16) == 0);
}

TEST_CASE("Render scene survives heavy slot churn without publishing uncleared reuse", "[Rendering][RenderScene]")
{
    SceneFixture fixture;
    RenderScenes::RenderScene scene(33, &fixture.geometry, &fixture.materials);
    RenderScenes::ModelInstanceDesc desc;
    desc.model = fixture.model;

    std::array<RenderScenes::ModelInstanceHandle, 256> handles;
    for (u32 iteration = 0; iteration < 20; ++iteration)
    {
        for (RenderScenes::ModelInstanceHandle& handle : handles)
        {
            handle = scene.CreateModelInstance(desc);
            CHECK(scene.IsPending(handle));
        }
        CHECK(scene.GetPendingClearRequests().instanceSlots.size() == handles.size());
        scene.AcknowledgeClearsAndPublish();

        for (RenderScenes::ModelInstanceHandle handle : handles)
            REQUIRE(scene.DestroyModelInstance(handle, iteration));
        scene.ReleaseRetiredHistory(iteration);
    }

    const auto stats = scene.GetStats();
    CHECK(stats.instances.liveInstances == 0);
    CHECK(stats.instances.pendingInstances == 0);
    CHECK(stats.instances.slotCapacity == handles.size());
    CHECK(stats.meshletHistory.liveWords == 0);
    CHECK(stats.meshletHistory.addressSpaceWords == 0);
}

TEST_CASE("Model scene bridge keeps gameplay entity adaptation outside scene storage", "[Rendering][RenderScene]")
{
    SceneFixture fixture;
    RenderScenes::RenderScene scene(41, &fixture.geometry, &fixture.materials);
    ModelScene::ModelSceneBridge bridge(&scene);
    const entt::entity entity = entt::entity{7};

    const RenderScenes::ModelInstanceHandle instance = bridge.Add(entity, fixture.model, Translation(3.0f));
    CHECK(scene.IsPending(instance));
    CHECK(bridge.Get(entity) == instance);
    scene.AcknowledgeClearsAndPublish();

    REQUIRE(bridge.SetTransform(entity, Translation(4.0f)));
    CHECK(scene.GetModelInstance(instance)->currentWorld[3].x == 4.0f);
    REQUIRE(bridge.SetVisible(entity, false));
    REQUIRE(bridge.Remove(entity, 2));
    CHECK(RenderScenes::GetModelInstanceSlot(bridge.Get(entity)) == RenderScenes::INVALID_SCENE_INDEX);
    CHECK_FALSE(scene.IsAlive(instance));
}

TEST_CASE("Render scene shares identical complete material tables", "[Rendering][RenderScene]")
{
    SceneFixture fixture;
    RenderScenes::RenderScene scene(43, &fixture.geometry, &fixture.materials);
    RenderScenes::ModelInstanceDesc desc;
    desc.model = fixture.model;

    const RenderScenes::ModelInstanceHandle first = scene.CreateModelInstance(desc);
    const RenderScenes::ModelInstanceHandle second = scene.CreateModelInstance(desc);
    const std::array completeTable = {fixture.materials.GetFallbackMaterialInstance()};
    REQUIRE(scene.SetModelMaterials(first, completeTable));
    REQUIRE(scene.SetModelMaterials(second, completeTable));

    const auto firstTable = scene.GetModelInstances().GetResources(first)->materialTable;
    const auto secondTable = scene.GetModelInstances().GetResources(second)->materialTable;
    CHECK(firstTable == secondTable);
    CHECK_FALSE(scene.GetModelMaterialTables().IsPrivate(firstTable));
}
