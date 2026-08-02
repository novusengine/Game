#include "Game-Lib/Rendering/Material/MaterialRegistry.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Material/ModelTextureResolver.h"
#include "Game-Lib/Rendering/Model/Asset/ModelAssetRegistry.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"

#include <catch2/catch2.hpp>

TEST_CASE("Render asset registries publish stable fallbacks once per failed asset", "[Rendering][RenderAssetRegistry]")
{
    MaterialLoading::MaterialStorage materialStorage;
    REQUIRE(materialStorage.InitializeFallback(3));

    MaterialLoading::MaterialRegistry materialRegistry(nullptr, &materialStorage, nullptr);
    ModelLoading::ModelGeometryStorage geometryStorage;
    ModelLoading::ModelAssetRegistry modelRegistry(nullptr, &geometryStorage, &materialStorage, &materialRegistry);
    REQUIRE(modelRegistry.InitializeFallback());

    const RenderAssets::MaterialHandle fallbackMaterial = materialRegistry.LoadMaterial(FileFormat::INVALID_ASSET_ID);
    const RenderAssets::MaterialInstanceHandle fallbackInstance = materialRegistry.LoadMaterialInstance(FileFormat::INVALID_ASSET_ID);
    const RenderAssets::ModelHandle fallbackModel = modelRegistry.Load(FileFormat::INVALID_ASSET_ID);

    CHECK(fallbackMaterial == materialStorage.GetFallbackMaterial());
    CHECK(fallbackInstance == materialStorage.GetFallbackMaterialInstance());
    CHECK(fallbackModel == modelRegistry.GetFallbackModel());
    CHECK(static_cast<RenderAssets::ModelHandle::type>(fallbackModel) == 0);

    CHECK(materialRegistry.LoadMaterial(FileFormat::INVALID_ASSET_ID) == fallbackMaterial);
    CHECK(materialRegistry.LoadMaterialInstance(FileFormat::INVALID_ASSET_ID) == fallbackInstance);
    CHECK(modelRegistry.Load(FileFormat::INVALID_ASSET_ID) == fallbackModel);

    const MaterialLoading::MaterialRegistryStats materialStats = materialRegistry.GetStats();
    CHECK(materialStats.materialFailures == 1);
    CHECK(materialStats.materialInstanceFailures == 1);
    CHECK(materialStats.cacheHits == 2);
    CHECK(materialStats.materialReferences == 2);
    CHECK(materialStats.materialInstanceReferences == 2);

    const ModelLoading::ModelAssetRegistryStats modelStats = modelRegistry.GetStats();
    CHECK(modelStats.failures == 1);
    CHECK(modelStats.cacheHits == 1);
    CHECK(modelStats.references == 2);
}

TEST_CASE("Texture resolver caches a failed load transition at the fallback index", "[Rendering][RenderAssetRegistry]")
{
    MaterialLoading::ModelTextureResolver textureResolver(nullptr, nullptr);

    const u32 first = textureResolver.Resolve(FileFormat::INVALID_ASSET_ID, 42, true);
    const u32 cached = textureResolver.Resolve(FileFormat::INVALID_ASSET_ID, 42, true);

    CHECK(first == textureResolver.GetFallbackTextureIndex());
    CHECK(cached == first);

    const MaterialLoading::ModelTextureResolverStats stats = textureResolver.GetStats();
    CHECK(stats.fallbackTextures == 1);
    CHECK(stats.cacheHits == 1);
}
