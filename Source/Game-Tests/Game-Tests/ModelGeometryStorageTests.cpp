#include "Game-Lib/Rendering/Model/Asset/FallbackModel.h"
#include "Game-Lib/Rendering/Model/Asset/ModelAssetValidator.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"

#include <catch2/catch2.hpp>

TEST_CASE("Fallback model is valid cooker-final geometry", "[Rendering][ModelGeometryStorage]")
{
    ModelLoading::ModelAssetReadResult result;
    result.view = ModelLoading::GetFallbackModelAssetView();

    result = ModelLoading::ModelAssetValidator::Validate(result);

    REQUIRE(result);
    CHECK(result.view.meshes.size() == 1);
    CHECK(result.view.meshLODs.size() == 1);
    CHECK(result.view.meshlets.size() == 1);
    CHECK(result.view.meshletTriangles.size() == 12);
}

TEST_CASE("Model geometry storage bulk appends immutable file-local arrays", "[Rendering][ModelGeometryStorage]")
{
    ModelLoading::ModelGeometryStorage storage;
    const ModelLoading::ModelAssetView fallback = ModelLoading::GetFallbackModelAssetView();

    RenderAssets::ModelHandle first;
    RenderAssets::ModelHandle second;
    REQUIRE(storage.Append(fallback, 3, 1, first));
    REQUIRE(storage.Append(fallback, 7, 1, second));

    REQUIRE(static_cast<RenderAssets::ModelHandle::type>(first) == 0);
    REQUIRE(static_cast<RenderAssets::ModelHandle::type>(second) == 1);

    const ModelLoading::ModelGPURecord& firstRecord = storage.GetRecord(first);
    const ModelLoading::ModelGPURecord& secondRecord = storage.GetRecord(second);
    CHECK(firstRecord.meshBase == 0);
    CHECK(secondRecord.meshBase == 1);
    CHECK(firstRecord.positionBase == 0);
    CHECK(secondRecord.positionBase == fallback.positions.size());
    CHECK(firstRecord.defaultMaterialTableOffset == 3);
    CHECK(secondRecord.defaultMaterialTableOffset == 7);

    CHECK(storage.GetMeshes()[secondRecord.meshBase].lodOffset == 0);
    CHECK(storage.GetMeshLODs()[secondRecord.meshLODBase].vertexOffset == 0);
    CHECK(storage.GetMeshlets()[secondRecord.meshletBase].vertexOffset == 0);

    const ModelLoading::ModelGeometryStorageStats stats = storage.GetStats();
    CHECK(stats.numModels == 2);
    CHECK(stats.usedBytes > 0);
    CHECK(stats.reservedBytes >= stats.usedBytes);
}
