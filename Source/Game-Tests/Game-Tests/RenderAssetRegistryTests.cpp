#include "Game-Lib/Rendering/Material/MaterialRegistry.h"
#include "Game-Lib/Rendering/Material/MaterialProgramLibrary.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"
#include "Game-Lib/Rendering/Material/MaterialTextureRegistry.h"
#include "Game-Lib/Rendering/Model/Asset/ModelAssetRegistry.h"
#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Model/DisplayResolver.h"

#include <FileFormat/Novus/ClientDB/ClientDB.h>

#include <MetaGen/Game/ClientDB/ClientDB.h>

#include <catch2/catch2.hpp>

TEST_CASE("Render asset registries publish stable fallbacks once per failed asset", "[Rendering][RenderAssetRegistry]")
{
    MaterialLoading::MaterialStorage materialStorage;
    REQUIRE(materialStorage.InitializeFallback(3));

    MaterialLoading::MaterialProgramLibrary materialPrograms;
    MaterialLoading::MaterialRegistry materialRegistry(nullptr, &materialStorage, &materialPrograms, nullptr);
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

TEST_CASE("Material texture registry caches a failed load transition at the fallback index",
          "[Rendering][RenderAssetRegistry]")
{
    MaterialLoading::MaterialTextureRegistry textureRegistry(nullptr, nullptr);

    const u32 first = textureRegistry.Resolve(FileFormat::INVALID_ASSET_ID, 42, true);
    const u32 cached = textureRegistry.Resolve(FileFormat::INVALID_ASSET_ID, 42, true);

    CHECK(first == textureRegistry.GetFallbackTextureIndex());
    CHECK(cached == first);

    const MaterialLoading::MaterialTextureRegistryStats stats = textureRegistry.GetStats();
    CHECK(stats.fallbackTextures == 1);
    CHECK(stats.cacheHits == 1);
}

TEST_CASE("display resolver indexes registrations and parameter overrides",
          "[Rendering][RenderAssetRegistry]")
{
    using RegistrationRecord = MetaGen::Game::ClientDB::DisplayRegistrationRecord;
    using ParameterRecord = MetaGen::Game::ClientDB::DisplayParameterRecord;

    ClientDB::Data registrations;
    REQUIRE(registrations.Initialize<RegistrationRecord>());
    registrations.Add(RegistrationRecord{.modelAssetID = 100, .displayID = 26,
        .source = static_cast<u8>(ModelLoading::DisplaySource::CreatureDisplayInfo)});
    registrations.Add(RegistrationRecord{.modelAssetID = 200, .displayID = 91,
        .source = static_cast<u8>(ModelLoading::DisplaySource::ItemDisplayInfo), .modelVariant = 1});

    ClientDB::Data parameters;
    REQUIRE(parameters.Initialize<ParameterRecord>());
    parameters.Add(ParameterRecord{.value0 = 1000, .displayRegistrationID = 1,
        .modelParameterStableID = 0,
        .type = static_cast<u8>(FileFormat::Model::ParameterType::Texture2D)});
    parameters.Add(ParameterRecord{.value0 = 2000, .displayRegistrationID = 2,
        .modelParameterStableID = 4,
        .type = static_cast<u8>(FileFormat::Model::ParameterType::Texture2D)});

    ModelLoading::DisplayResolver resolver(nullptr);
    REQUIRE(resolver.Initialize(registrations, parameters));
    const ModelLoading::DisplayResolverStats stats = resolver.GetStats();
    CHECK(stats.selections == 2);
    CHECK(stats.assignments == 2);
}
