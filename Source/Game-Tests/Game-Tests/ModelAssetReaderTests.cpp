#include "Game-Lib/Rendering/Model/Asset/ModelAssetReader.h"

#include <Base/Memory/Bytebuffer.h>

#include <catch2/catch2.hpp>

#include <cstring>
#include <limits>
#include <vector>

namespace
{
    namespace Model = FileFormat::Model;

    std::vector<u8> MakeModelPayload()
    {
        Model::ModelAsset asset;
        asset.geometryGroupCount = 1;

        Model::ModelData data;
        Model::Mesh mesh;
        mesh.numLODs = 1;
        mesh.numMaterialSlots = 1;
        data.meshes.push_back(mesh);

        Model::MeshLOD lod;
        lod.numVertices = 3;
        lod.numVertexAttributes = 3;
        lod.numSubmeshes = 1;
        lod.numMeshlets = 1;
        data.meshLODs.push_back(lod);

        Model::Submesh submesh;
        submesh.numMeshlets = 1;
        data.submeshes.push_back(submesh);

        Model::Meshlet meshlet;
        meshlet.vertexCount = 3;
        meshlet.triangleCount = 1;
        data.meshlets.push_back(meshlet);

        data.positions.resize(3);
        data.vertexAttributes.resize(3);
        data.meshletVertexIndices = {0, 1, 2};
        data.meshletTriangles.push_back({0x00020100u});
        data.materialSlots.push_back({42, 1, 0, 0});

        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::BorrowRuntime(asset.GetSerializedSize(data));
        REQUIRE(asset.Save(buffer, data));
        return {buffer->GetDataPointer(), buffer->GetDataPointer() + buffer->writtenData};
    }

    template <typename T>
    T& At(std::vector<u8>& payload, size_t offset)
    {
        return *reinterpret_cast<T*>(payload.data() + offset);
    }
} // namespace

TEST_CASE("Model asset reader accepts a minimal cooker-final payload", "[Rendering][ModelAssetReader]")
{
    const std::vector<u8> payload = MakeModelPayload();
    const ModelLoading::ModelAssetReadResult result =
        ModelLoading::ModelAssetReader::Read(payload, AssetLoading::ValidationMode::Full);

    REQUIRE(result);
    CHECK(result.view.meshes.size() == 1);
    CHECK(result.view.meshLODs.size() == 1);
    CHECK(result.view.meshlets.size() == 1);
    CHECK(result.limitations.invalidCollisionReferences == 1);
    CHECK(result.limitations.invalidSkeletonReferences == 1);
    CHECK(result.limitations.invalidAnimationBoundsReferences == 1);
}

TEST_CASE("Model asset reader rejects truncated and overflowed root sections", "[Rendering][ModelAssetReader]")
{
    std::vector<u8> truncated = MakeModelPayload();
    truncated.resize(sizeof(Model::ModelAsset) - 1);
    CHECK(ModelLoading::ModelAssetReader::Read(truncated).diagnostic.code == AssetLoading::DiagnosticCode::PayloadTooSmall);

    std::vector<u8> overflowed = MakeModelPayload();
    Model::ModelAsset& root = At<Model::ModelAsset>(overflowed, 0);
    root.positionsOffset = std::numeric_limits<u32>::max() - 15;
    root.numPositions = std::numeric_limits<u32>::max();
    CHECK(ModelLoading::ModelAssetReader::Read(overflowed).diagnostic.code == AssetLoading::DiagnosticCode::InvalidRootSection);

    std::vector<u8> wrongVersion = MakeModelPayload();
    ++At<Model::ModelAsset>(wrongVersion, 0).header.version;
    CHECK(ModelLoading::ModelAssetReader::Read(wrongVersion).diagnostic.code == AssetLoading::DiagnosticCode::InvalidHeader);
}

TEST_CASE("Model asset reader rejects invalid nested geometry ranges", "[Rendering][ModelAssetReader]")
{
    SECTION("Mesh to LOD range")
    {
        std::vector<u8> payload = MakeModelPayload();
        Model::ModelAsset& root = At<Model::ModelAsset>(payload, 0);
        At<Model::Mesh>(payload, root.meshesOffset).lodOffset = 1;
        CHECK(ModelLoading::ModelAssetReader::Read(payload, AssetLoading::ValidationMode::Full).diagnostic.code ==
              AssetLoading::DiagnosticCode::InvalidRange);
    }

    SECTION("Mismatched position and attribute counts")
    {
        std::vector<u8> payload = MakeModelPayload();
        Model::ModelAsset& root = At<Model::ModelAsset>(payload, 0);
        At<Model::MeshLOD>(payload, root.meshLODsOffset).numVertexAttributes = 2;
        CHECK(ModelLoading::ModelAssetReader::Read(payload, AssetLoading::ValidationMode::Full).diagnostic.code ==
              AssetLoading::DiagnosticCode::CountMismatch);
    }

    SECTION("Oversized meshlet")
    {
        std::vector<u8> payload = MakeModelPayload();
        Model::ModelAsset& root = At<Model::ModelAsset>(payload, 0);
        At<Model::Meshlet>(payload, root.meshletsOffset).vertexCount = Model::MAX_MESHLET_VERTICES + 1;
        CHECK(ModelLoading::ModelAssetReader::Read(payload, AssetLoading::ValidationMode::Full).diagnostic.code ==
              AssetLoading::DiagnosticCode::InvalidValue);
    }

    SECTION("Meshlet triangle reserved bits")
    {
        std::vector<u8> payload = MakeModelPayload();
        Model::ModelAsset& root = At<Model::ModelAsset>(payload, 0);
        At<Model::PackedMeshletTriangle>(payload, root.meshletTrianglesOffset).localVertexIndices |= 0x80000000u;
        CHECK(ModelLoading::ModelAssetReader::Read(payload, AssetLoading::ValidationMode::Full).diagnostic.code ==
              AssetLoading::DiagnosticCode::InvalidIndex);
    }

    SECTION("Unsupported root flags")
    {
        std::vector<u8> payload = MakeModelPayload();
        At<Model::ModelAsset>(payload, 0).flags = 1u << 31;
        CHECK(ModelLoading::ModelAssetReader::Read(payload, AssetLoading::ValidationMode::Full).diagnostic.code ==
              AssetLoading::DiagnosticCode::UnsupportedFlags);
    }
}

TEST_CASE("Model asset semantic validation is optional", "[Rendering][ModelAssetReader]")
{
    std::vector<u8> payload = MakeModelPayload();
    At<Model::ModelAsset>(payload, 0).flags = 1u << 31;

    CHECK(ModelLoading::ModelAssetReader::Read(payload));
    CHECK(ModelLoading::ModelAssetReader::Read(payload, AssetLoading::ValidationMode::Minimal));
    CHECK(ModelLoading::ModelAssetReader::Read(payload, AssetLoading::ValidationMode::Full).diagnostic.code ==
          AssetLoading::DiagnosticCode::UnsupportedFlags);
}
