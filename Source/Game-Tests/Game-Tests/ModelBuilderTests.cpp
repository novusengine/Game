#include "Game-Lib/Rendering/Model/ModelBuilder.h"

#include <catch2/catch2.hpp>

#include <type_traits>

static_assert(std::is_move_constructible_v<ModelLoading::PreparedModelResult>);
static_assert(!std::is_copy_constructible_v<ModelLoading::PreparedModelResult>);

namespace
{
    Model::ComplexModel MakeMinimalModel()
    {
        Model::ComplexModel model;
        model.modelHeader.numVertices = 1;
        model.modelHeader.numIndices = 1;
        model.modelHeader.numRenderBatches = 1;
        model.modelHeader.numOpaqueRenderBatches = 1;
        model.modelHeader.numTransparentRenderBatches = 0;
        model.modelHeader.numTextures = 1;
        model.modelHeader.numTextureUnits = 1;
        model.modelHeader.numMaterials = 1;
        model.modelHeader.numTextureTransforms = 1;

        model.vertices.resize(1);
        model.modelData.indices.push_back(0);

        Model::ComplexModel::Material material;
        material.flags.unLit = true;
        material.blendingMode = Model::ComplexModel::Material::BlendingMode::Alpha;
        model.materials.push_back(material);

        Model::ComplexModel::Texture texture;
        texture.type = Model::ComplexModel::Texture::Type::None;
        texture.flags.wrapX = true;
        texture.flags.wrapY = true;
        texture.textureHash = 42;
        model.textures.push_back(texture);
        model.textureIndexLookupTable.push_back(0);
        model.textureTransformLookupTable.push_back(7);

        Model::ComplexModel::TextureUnit textureUnit;
        textureUnit.flags.IsProjectedTexture = true;
        textureUnit.shaderID = 3;
        textureUnit.materialIndex = 0;
        textureUnit.textureCount = 1;
        textureUnit.textureIndexStart = 0;
        textureUnit.textureTransformIndexStart = 0;

        Model::ComplexModel::RenderBatch renderBatch;
        renderBatch.groupID = 9;
        renderBatch.vertexCount = 1;
        renderBatch.indexCount = 1;
        renderBatch.textureUnits.push_back(textureUnit);
        model.modelData.renderBatches.push_back(renderBatch);

        model.textureTransforms.resize(1);
        return model;
    }
}

TEST_CASE("Model builder prepares relative renderer data without live renderer state", "[Rendering][ModelBuilder]")
{
    Model::ComplexModel model = MakeMinimalModel();

    ModelLoading::ModelBuildResult result = ModelLoading::BuildPreparedModel("test-model", model);

    REQUIRE(static_cast<bool>(result));
    const ModelLoading::PreparedRenderModel& prepared = result.preparedModel;

    model.vertices.clear();
    model.modelData.indices.clear();
    model.modelData.renderBatches.clear();

    CHECK(prepared.debugName == "test-model");
    CHECK(prepared.vertices.size() == 1);
    CHECK(prepared.indices == std::vector<u16>{ 0 });
    CHECK(prepared.reserveInfo.numModels == 1);
    CHECK(prepared.reserveInfo.numOpaqueDrawCalls == 1);
    CHECK(prepared.reserveInfo.numTransparentDrawCalls == 0);
    CHECK(prepared.reserveInfo.numTextureUnits == 1);
    CHECK(prepared.estimatedCommitBytes > 0);

    REQUIRE(prepared.drawCalls.size() == 1);
    const ModelLoading::PreparedDrawCall& drawCall = prepared.drawCalls[0];
    CHECK(drawCall.firstIndex == 0);
    CHECK(drawCall.vertexOffset == 0);
    CHECK(drawCall.textureUnitOffset == 0);
    CHECK(drawCall.numTextureUnits == 1);
    CHECK(drawCall.groupID == 9);
    CHECK_FALSE(drawCall.isTransparent);

    REQUIRE(prepared.textureUnits.size() == 1);
    const ModelLoading::PreparedTextureUnit& textureUnit = prepared.textureUnits[0];
    CHECK(textureUnit.materialType == 3);
    CHECK(textureUnit.textureTransformIds[0] == 7);
    CHECK(textureUnit.textureTransformIds[1] == MODEL_INVALID_TEXTURE_TRANSFORM_ID);
    CHECK(textureUnit.data == 4135);

    REQUIRE(prepared.textureLoadRequests.size() == 1);
    CHECK(prepared.textureLoadRequests[0].textureUnitOffset == 0);
    CHECK(prepared.textureLoadRequests[0].textureIndex == 0);
    CHECK(prepared.textureLoadRequests[0].textureHash == 42);
}

TEST_CASE("Model builder rejects inconsistent model header counts", "[Rendering][ModelBuilder]")
{
    Model::ComplexModel model = MakeMinimalModel();
    model.modelHeader.numTextureUnits = 2;

    const ModelLoading::ModelBuildResult result = ModelLoading::BuildPreparedModel("invalid-model", model);

    CHECK_FALSE(static_cast<bool>(result));
    CHECK_FALSE(result.error.empty());
}
