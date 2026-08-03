#include "Game-Lib/Rendering/Material/MaterialInstancePatcher.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"

#include <catch2/catch2.hpp>

#include <array>
#include <cstring>

namespace
{
    MaterialLoading::MaterialAssetView MakeMaterialView(std::span<const u8> defaultData, u32 alignment = 16)
    {
        MaterialLoading::MaterialAssetView view;
        view.root.programID = 42;
        view.root.lightingModelID = 1;
        view.root.materialExecutionGroupID = 2;
        view.root.rasterClass = FileFormat::Material::RasterClass::AlphaTest;
        view.root.parameterBlockSize = static_cast<u32>(defaultData.size());
        view.root.parameterBlockAlignment = alignment;
        view.defaultParameterData = defaultData;
        return view;
    }
}

TEST_CASE("Material instances preserve their material parameter alignment", "[Rendering][MaterialStorage]")
{
    MaterialLoading::MaterialStorage storage;
    REQUIRE(storage.InitializeFallback(0));

    std::array<u8, 64> alignedDefaults = {};
    alignedDefaults[0] = 1;
    RenderAssets::MaterialHandle alignedMaterial;
    REQUIRE(storage.AppendMaterial(MakeMaterialView(alignedDefaults, 64), alignedMaterial));

    std::array<u8, 16> interleavedDefaults = {};
    interleavedDefaults[0] = 2;
    RenderAssets::MaterialHandle interleavedMaterial;
    REQUIRE(storage.AppendMaterial(MakeMaterialView(interleavedDefaults), interleavedMaterial));

    std::array<u8, 64> instanceParameters = alignedDefaults;
    instanceParameters[0] = 3;
    RenderAssets::MaterialInstanceHandle instance;
    REQUIRE(storage.AppendMaterialInstance(alignedMaterial, instanceParameters, instance));
    CHECK(storage.GetMaterialInstance(instance).parameterOffset % 64 == 0);
}

TEST_CASE("Material instance patcher writes only declared resource words", "[Rendering][MaterialStorage]")
{
    std::array<u8, 32> parameters = {};
    constexpr std::array<FileFormat::Material::ResourceBinding, 2> bindings = {{
        {55, 16, 3, FileFormat::Material::ResourceType::Texture2D, FileFormat::Material::ResourceBindingFlags_None},
        {FileFormat::INVALID_ASSET_ID, 28, 7, FileFormat::Material::ResourceType::Sampler, FileFormat::Material::ResourceBindingFlags_None}
    }};

    MaterialLoading::MaterialInstanceAssetView view;
    view.parameterData = parameters;
    view.resourceBindings = bindings;

    u32 resolvedAsset = 0;
    u32 packedSamplerIDs = 0;
    std::vector<u8> patched;
    REQUIRE(MaterialLoading::MaterialInstancePatcher::Patch(
        view,
        [&resolvedAsset](FileFormat::AssetID assetID, bool optional) {
            CHECK_FALSE(optional);
            resolvedAsset = static_cast<u32>(assetID);
            return 23u;
        },
        patched, &packedSamplerIDs));

    u32 textureIndex = 0;
    u32 samplerIndex = 0;
    std::memcpy(&textureIndex, patched.data() + 16, sizeof(textureIndex));
    std::memcpy(&samplerIndex, patched.data() + 28, sizeof(samplerIndex));
    CHECK(resolvedAsset == 55);
    CHECK(textureIndex == 23);
    CHECK(samplerIndex == 7);
    CHECK(packedSamplerIDs == 3);
    CHECK(patched[0] == 0);
}

TEST_CASE("Material storage bootstraps fallbacks and deduplicates instances", "[Rendering][MaterialStorage]")
{
    MaterialLoading::MaterialStorage storage;
    REQUIRE(storage.InitializeFallback(9));
    CHECK(static_cast<RenderAssets::MaterialHandle::type>(storage.GetFallbackMaterial()) == 0);
    CHECK(static_cast<RenderAssets::MaterialInstanceHandle::type>(storage.GetFallbackMaterialInstance()) == 0);
    CHECK(storage.GetMaterial(storage.GetFallbackMaterial()).programID == MaterialLoading::FALLBACK_MATERIAL_PROGRAM_ID);

    std::array<u8, 16> parameters = {};
    parameters[0] = 4;
    RenderAssets::MaterialHandle material;
    REQUIRE(storage.AppendMaterial(MakeMaterialView(parameters), material));
    CHECK(static_cast<RenderAssets::MaterialHandle::type>(material) == 1);
    CHECK(storage.GetMaterial(material).groupLocalMaterialID == 1);
    CHECK(storage.GetMaterial(material).rasterClass ==
          static_cast<u8>(FileFormat::Material::RasterClass::AlphaTest));

    RenderAssets::MaterialInstanceHandle first;
    RenderAssets::MaterialInstanceHandle duplicate;
    REQUIRE(storage.AppendMaterialInstance(material, parameters, first));
    REQUIRE(storage.AppendMaterialInstance(material, parameters, duplicate));
    CHECK(first == duplicate);

    const std::array table = {first, storage.GetFallbackMaterialInstance()};
    u32 tableOffset = 0;
    REQUIRE(storage.AppendMaterialTable(table, tableOffset));
    CHECK(storage.GetMaterialTableEntry(tableOffset) == static_cast<RenderAssets::MaterialInstanceHandle::type>(first));
    CHECK(storage.GetMaterialTableEntry(tableOffset + 1) ==
          static_cast<RenderAssets::MaterialInstanceHandle::type>(storage.GetFallbackMaterialInstance()));

    const MaterialLoading::MaterialStorageStats stats = storage.GetStats();
    CHECK(stats.numMaterials == 2);
    CHECK(stats.numMaterialInstances == 2);
    CHECK(stats.numMaterialTableEntries == 2);
    CHECK(stats.instanceDedupHits == 1);
}
