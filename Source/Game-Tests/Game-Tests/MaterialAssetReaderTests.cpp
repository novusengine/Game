#include "Game-Lib/Rendering/Material/MaterialAssetReader.h"

#include <Base/Memory/Bytebuffer.h>

#include <catch2/catch2.hpp>

#include <vector>

namespace
{
    namespace Material = FileFormat::Material;

    struct MaterialPayloads
    {
        std::vector<u8> material;
        std::vector<u8> instance;
    };

    MaterialPayloads MakeMaterialPayloads()
    {
        Material::MaterialData materialData;
        materialData.parameters.push_back({1, 0, 4, Material::ParameterType::Texture2D, 1});
        materialData.parameters.push_back({2, 16, 16, Material::ParameterType::Float4, 1});
        materialData.defaultParameterData.resize(32);

        Material::MaterialAsset material;
        material.parameterBlockSize = 32;
        material.parameterBlockAlignment = 16;
        std::shared_ptr<Bytebuffer> materialBuffer = Bytebuffer::BorrowRuntime(material.GetSerializedSize(materialData));
        REQUIRE(material.Save(materialBuffer, materialData));

        Material::MaterialInstanceData instanceData;
        instanceData.parameterData.resize(32);
        instanceData.resourceBindings.push_back({42, 0, 0, Material::ResourceType::Texture2D, Material::ResourceBindingFlags_None});

        Material::MaterialInstanceAsset instance;
        instance.materialAssetID = 77;
        instance.parameterLayoutHash = Material::CalculateParameterLayoutHash(materialData.parameters, 32);
        std::shared_ptr<Bytebuffer> instanceBuffer = Bytebuffer::BorrowRuntime(instance.GetSerializedSize(instanceData));
        REQUIRE(instance.Save(instanceBuffer, instanceData));

        return {{materialBuffer->GetDataPointer(), materialBuffer->GetDataPointer() + materialBuffer->writtenData},
                {instanceBuffer->GetDataPointer(), instanceBuffer->GetDataPointer() + instanceBuffer->writtenData}};
    }

    template <typename T>
    T& At(std::vector<u8>& payload, size_t offset)
    {
        return *reinterpret_cast<T*>(payload.data() + offset);
    }
} // namespace

TEST_CASE("Material readers accept matching flat material assets", "[Rendering][MaterialAssetReader]")
{
    const MaterialPayloads payloads = MakeMaterialPayloads();
    const auto material = MaterialLoading::MaterialAssetReader::ReadMaterial(payloads.material, AssetLoading::ValidationMode::Full);
    REQUIRE(material);

    const auto instance =
        MaterialLoading::MaterialAssetReader::ReadMaterialInstance(payloads.instance, material.view, AssetLoading::ValidationMode::Full);
    REQUIRE(instance);
    CHECK(instance.view.parameterData.size() == 32);
    CHECK(instance.view.resourceBindings.size() == 1);
}

TEST_CASE("Material instance structural decode discovers its dependency without material validation", "[Rendering][MaterialAssetReader]")
{
    MaterialPayloads payloads = MakeMaterialPayloads();
    ++At<Material::MaterialInstanceAsset>(payloads.instance, 0).parameterLayoutHash;

    const auto decoded = MaterialLoading::MaterialAssetReader::DecodeMaterialInstance(payloads.instance);
    REQUIRE(decoded);
    CHECK(decoded.view.root.materialAssetID == 77);
    CHECK(decoded.view.parameterData.size() == 32);
}

TEST_CASE("Material reader rejects invalid parameter patches and compatibility", "[Rendering][MaterialAssetReader]")
{
    SECTION("Resource patch outside its parameter")
    {
        MaterialPayloads payloads = MakeMaterialPayloads();
        const auto material = MaterialLoading::MaterialAssetReader::ReadMaterial(payloads.material, AssetLoading::ValidationMode::Full);
        REQUIRE(material);
        const Material::MaterialInstanceAsset& root = At<Material::MaterialInstanceAsset>(payloads.instance, 0);
        At<Material::ResourceBinding>(payloads.instance, root.resourceBindingsOffset).parameterByteOffset = 32;
        CHECK(MaterialLoading::MaterialAssetReader::ReadMaterialInstance(payloads.instance, material.view, AssetLoading::ValidationMode::Full)
                  .diagnostic.code ==
              AssetLoading::DiagnosticCode::InvalidRange);
    }

    SECTION("Layout hash mismatch")
    {
        MaterialPayloads payloads = MakeMaterialPayloads();
        const auto material = MaterialLoading::MaterialAssetReader::ReadMaterial(payloads.material, AssetLoading::ValidationMode::Full);
        REQUIRE(material);
        ++At<Material::MaterialInstanceAsset>(payloads.instance, 0).parameterLayoutHash;
        CHECK(MaterialLoading::MaterialAssetReader::ReadMaterialInstance(payloads.instance, material.view, AssetLoading::ValidationMode::Full)
                  .diagnostic.code ==
              AssetLoading::DiagnosticCode::LayoutHashMismatch);
    }

    SECTION("Required texture reference is invalid")
    {
        MaterialPayloads payloads = MakeMaterialPayloads();
        const auto material = MaterialLoading::MaterialAssetReader::ReadMaterial(payloads.material, AssetLoading::ValidationMode::Full);
        REQUIRE(material);
        const Material::MaterialInstanceAsset& root = At<Material::MaterialInstanceAsset>(payloads.instance, 0);
        At<Material::ResourceBinding>(payloads.instance, root.resourceBindingsOffset).resourceAssetID = FileFormat::INVALID_ASSET_ID;
        CHECK(MaterialLoading::MaterialAssetReader::ReadMaterialInstance(payloads.instance, material.view, AssetLoading::ValidationMode::Full)
                  .diagnostic.code ==
              AssetLoading::DiagnosticCode::MissingRequiredReference);
    }

    SECTION("Unsupported material flags")
    {
        MaterialPayloads payloads = MakeMaterialPayloads();
        At<Material::MaterialAsset>(payloads.material, 0).flags = 1u << 31;
        CHECK(MaterialLoading::MaterialAssetReader::ReadMaterial(payloads.material, AssetLoading::ValidationMode::Full).diagnostic.code ==
              AssetLoading::DiagnosticCode::UnsupportedFlags);
    }
}

TEST_CASE("Material semantic validation is optional", "[Rendering][MaterialAssetReader]")
{
    MaterialPayloads payloads = MakeMaterialPayloads();
    At<Material::MaterialAsset>(payloads.material, 0).flags = 1u << 31;

    CHECK(MaterialLoading::MaterialAssetReader::ReadMaterial(payloads.material));
    CHECK(MaterialLoading::MaterialAssetReader::ReadMaterial(payloads.material, AssetLoading::ValidationMode::Minimal));
    CHECK(MaterialLoading::MaterialAssetReader::ReadMaterial(payloads.material, AssetLoading::ValidationMode::Full).diagnostic.code ==
          AssetLoading::DiagnosticCode::UnsupportedFlags);
}
