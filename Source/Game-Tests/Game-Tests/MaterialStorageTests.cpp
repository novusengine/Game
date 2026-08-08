#include "Game-Lib/Rendering/Material/MaterialInstancePatcher.h"
#include "Game-Lib/Rendering/Material/MaterialStorage.h"

#include <catch2/catch2.hpp>

#include <array>

namespace
{
    MaterialLoading::MaterialAssetView MakeMaterialView(std::span<const u8> defaults,
                                                         u32 alignment = 16)
    {
        MaterialLoading::MaterialAssetView view;
        view.root.programKey = 43;
        view.root.programID = 42;
        view.root.parameterBlockSize = static_cast<u32>(defaults.size());
        view.root.parameterBlockAlignment = alignment;
        view.root.textureSlotCount = 2;
        view.defaultParameterData = defaults;
        return view;
    }

    FileFormat::Material::MaterialProgramRecord MakeProgram(
        const MaterialLoading::MaterialAssetView& view)
    {
        FileFormat::Material::MaterialProgramRecord program;
        program.programKey = view.root.programKey;
        program.programID = view.root.programID;
        program.flags = view.root.flags;
        program.parameterBlockSize = view.root.parameterBlockSize;
        program.parameterBlockAlignment = view.root.parameterBlockAlignment;
        program.rasterRoutes = {{{0, 5}, {2, 7}, {4, 9}}};
        return program;
    }

    FileFormat::Material::MaterialInstanceAsset MakeConfiguration()
    {
        FileFormat::Material::MaterialInstanceAsset configuration;
        configuration.lightingModelID = 1;
        configuration.rasterClass = FileFormat::Material::RasterClass::AlphaTest;
        return configuration;
    }
}

TEST_CASE("Material instances preserve their material parameter alignment", "[Rendering][MaterialStorage]")
{
    MaterialLoading::MaterialStorage storage;
    REQUIRE(storage.InitializeFallback(0));

    std::array<u8, 64> alignedDefaults = {};
    RenderAssets::MaterialHandle material;
    const MaterialLoading::MaterialAssetView view = MakeMaterialView(alignedDefaults, 64);
    REQUIRE(storage.AppendMaterial(view, MakeProgram(view), material));

    const std::array<u32, 2> textures = {3, 4};
    const std::array<u32, 2> samplers = {0, 1};
    RenderAssets::MaterialInstanceHandle instance;
    REQUIRE(storage.AppendMaterialInstance(material, MakeConfiguration(), alignedDefaults,
                                           textures, samplers, instance));
    CHECK(storage.GetMaterialInstance(instance).parameterOffset % 64 == 0);
}

TEST_CASE("Material instance patcher resolves generic texture slots", "[Rendering][MaterialStorage]")
{
    std::array<u8, 32> parameters = {};
    constexpr std::array<FileFormat::Material::TextureBinding, 2> bindings = {{
        {55, 0, 3, FileFormat::Material::ResourceType::Texture2D,
         FileFormat::Material::ResourceBindingFlags_None},
        {FileFormat::INVALID_ASSET_ID, 1, 7, FileFormat::Material::ResourceType::Texture2D,
         FileFormat::Material::ResourceBindingFlags_Optional}
    }};
    MaterialLoading::MaterialInstanceAssetView view;
    view.parameterData = parameters;
    view.textureBindings = bindings;

    std::vector<u8> patched;
    std::vector<u32> textures;
    std::vector<u32> samplers;
    REQUIRE(MaterialLoading::MaterialInstancePatcher::Patch(
        view,
        [](FileFormat::AssetID assetID, bool optional) {
            CHECK_FALSE(optional);
            CHECK(assetID == 55);
            return 23u;
        },
        2, 9, patched, textures, samplers));

    CHECK(patched == std::vector<u8>(parameters.begin(), parameters.end()));
    CHECK(textures == std::vector<u32>{23, 9});
    CHECK(samplers == std::vector<u32>{3, 7});
}

TEST_CASE("Material storage bootstraps fallbacks and deduplicates instances",
          "[Rendering][MaterialStorage]")
{
    MaterialLoading::MaterialStorage storage;
    REQUIRE(storage.InitializeFallback(9));
    CHECK(static_cast<RenderAssets::MaterialHandle::type>(storage.GetFallbackMaterial()) == 0);
    CHECK(static_cast<RenderAssets::MaterialInstanceHandle::type>(
              storage.GetFallbackMaterialInstance()) == 0);

    std::array<u8, 64> parameters = {};
    RenderAssets::MaterialHandle material;
    const MaterialLoading::MaterialAssetView view = MakeMaterialView(parameters);
    REQUIRE(storage.AppendMaterial(view, MakeProgram(view), material));

    const std::array<u32, 2> textures = {5, 6};
    const std::array<u32, 2> samplers = {1, 2};
    RenderAssets::MaterialInstanceHandle first;
    RenderAssets::MaterialInstanceHandle duplicate;
    REQUIRE(storage.AppendMaterialInstance(material, MakeConfiguration(), parameters,
                                           textures, samplers, first));
    REQUIRE(storage.AppendMaterialInstance(material, MakeConfiguration(), parameters,
                                           textures, samplers, duplicate));
    CHECK(first == duplicate);

    const MaterialLoading::MaterialInstanceGPURecord& record = storage.GetMaterialInstance(first);
    CHECK((record.packedClassification & 0xFFFFu) == 1);
    CHECK((record.packedClassification >> 16u) == 2);
    CHECK(record.materialHandle == static_cast<RenderAssets::MaterialHandle::type>(material));
    CHECK(record.textureCount == 2);

    const std::array table = {first, storage.GetFallbackMaterialInstance()};
    u32 tableOffset = 0;
    REQUIRE(storage.AppendMaterialTable(table, tableOffset));
    CHECK(storage.GetMaterialTableEntry(tableOffset) ==
          static_cast<RenderAssets::MaterialInstanceHandle::type>(first));

    const MaterialLoading::MaterialStorageStats stats = storage.GetStats();
    CHECK(stats.numMaterials == 2);
    CHECK(stats.numMaterialInstances == 2);
    CHECK(stats.instanceDedupHits == 1);
}
