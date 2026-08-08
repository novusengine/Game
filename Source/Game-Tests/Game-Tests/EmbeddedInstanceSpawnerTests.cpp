#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Model/Asset/FallbackModel.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/Scene/EmbeddedInstanceSpawner.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <catch2/catch2.hpp>

#include <array>
#include <limits>

TEST_CASE("Embedded instances survive geometry growth and switch doodad sets by visibility", "[Rendering][EmbeddedInstances]")
{
    MaterialLoading::MaterialStorage materials;
    REQUIRE(materials.InitializeFallback(7));
    const std::array defaultMaterials = { materials.GetFallbackMaterialInstance() };
    u32 materialTableOffset = 0;
    REQUIRE(materials.AppendMaterialTable(defaultMaterials, materialTableOffset));

    std::array<FileFormat::Model::EmbeddedInstance, 5> definitions = {};
    definitions[0].modelAssetID = 100;
    definitions[0].position = vec3(1.0f, 2.0f, 3.0f);
    definitions[1].modelAssetID = 101;
    definitions[1].position = vec3(4.0f, 5.0f, 6.0f);
    definitions[2].modelAssetID = FileFormat::INVALID_ASSET_ID;
    definitions[3].modelAssetID = 103;
    definitions[4].modelAssetID = 104;

    const std::array sets = {
        FileFormat::Model::EmbeddedInstanceSet{ .instanceOffset = 0, .numInstances = 1 },
        FileFormat::Model::EmbeddedInstanceSet{ .instanceOffset = 1, .numInstances = 1 }
    };

    ModelLoading::ModelAssetView parentView = ModelLoading::GetFallbackModelAssetView();
    parentView.root.flags |= FileFormat::Model::ModelFlags_HasEmbeddedInstances;
    parentView.embeddedInstanceSets = sets;
    parentView.embeddedInstances = definitions;

    ModelLoading::ModelGeometryStorage geometry;
    RenderAssets::ModelHandle parent;
    REQUIRE(geometry.Append(parentView, materialTableOffset, 1, parent));
    RenderScenes::RenderScene scene(61, &geometry, &materials);

    ModelScene::EmbeddedInstanceSpawner spawner(
        &geometry, &scene,
        [&](FileFormat::AssetID assetID, RenderAssets::ModelHandle& outHandle) {
            if (assetID == FileFormat::INVALID_ASSET_ID)
                return ModelLoading::EmbeddedModelLoadStatus::InvalidReference;
            if (assetID == 103)
                return ModelLoading::EmbeddedModelLoadStatus::MissingRenderableGeometry;
            if (assetID == 104)
            {
                outHandle = parent;
                return ModelLoading::EmbeddedModelLoadStatus::Failed;
            }

            REQUIRE(geometry.Append(ModelLoading::GetFallbackModelAssetView(), materialTableOffset, 1, outHandle));
            return ModelLoading::EmbeddedModelLoadStatus::Loaded;
        });

    mat4x4 parentWorld(1.0f);
    parentWorld[3].x = 10.0f;
    ModelScene::SpawnedEmbeddedInstances spawned;
    ModelScene::EmbeddedInstanceSpawnCursor cursor;
    REQUIRE(spawner.BeginSpawn(parent, parentWorld, 0, spawned, cursor));
    u32 processed = 0;
    CHECK(spawner.ContinueSpawn(cursor, spawned, 2, processed) ==
          ModelScene::EmbeddedInstanceSpawnStatus::InProgress);
    CHECK(processed == 2);
    CHECK(spawned.instances.size() == 2);
    CHECK(spawner.ContinueSpawn(cursor, spawned, 2, processed) ==
          ModelScene::EmbeddedInstanceSpawnStatus::InProgress);
    CHECK(processed == 2);
    CHECK(spawner.ContinueSpawn(cursor, spawned, 2, processed) ==
          ModelScene::EmbeddedInstanceSpawnStatus::Complete);
    CHECK(processed == 1);
    REQUIRE(spawned.instances.size() == 3);
    CHECK(spawner.GetStats().spawnedInstances == 3);
    CHECK(spawner.GetStats().invalidReferenceSkips == 1);
    CHECK(spawner.GetStats().missingGeometrySkips == 1);
    CHECK(spawner.GetStats().unexpectedDependencyFailures == 1);

    scene.AcknowledgeClearsAndPublish();
    const auto* first = scene.GetModelInstance(spawned.instances[0].handle);
    const auto* second = scene.GetModelInstance(spawned.instances[1].handle);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first->currentWorld[3].x == 11.0f);
    CHECK(first->currentWorld[3].y == 2.0f);
    CHECK(first->currentWorld[3].z == 3.0f);
    CHECK((first->flags & ModelScene::ModelInstanceFlagVisible) != 0);
    CHECK((second->flags & ModelScene::ModelInstanceFlagVisible) == 0);

    REQUIRE(spawner.SetInstanceSet(spawned, 1));
    CHECK((scene.GetModelInstance(spawned.instances[0].handle)->flags & ModelScene::ModelInstanceFlagVisible) == 0);
    CHECK((scene.GetModelInstance(spawned.instances[1].handle)->flags & ModelScene::ModelInstanceFlagVisible) != 0);

    CHECK_FALSE(spawner.SetInstanceSet(spawned, std::numeric_limits<u16>::max() - 1u));
    for (const ModelScene::SpawnedEmbeddedInstance& instance : spawned.instances)
        CHECK((scene.GetModelInstance(instance.handle)->flags & ModelScene::ModelInstanceFlagVisible) == 0);

    spawner.Destroy(spawned, 0);
    CHECK(spawned.instances.empty());
    CHECK(scene.GetStats().instances.liveInstances == 0);
}
