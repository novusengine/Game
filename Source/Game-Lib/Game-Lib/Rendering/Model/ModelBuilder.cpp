#include "ModelBuilder.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cstring>

namespace ModelLoading
{
    namespace
    {
        ModelBuildResult BuildFailure(const std::string& error)
        {
            ModelBuildResult result;
            result.error = error;
            return result;
        }
    }

    ModelBuildResult BuildPreparedModel(const std::string& name, const Model::ComplexModel& model)
    {
        ZoneScopedN("ModelBuilder::BuildPreparedModel");

        const auto& header = model.modelHeader;

        if (header.numVertices == 0)
            return BuildFailure("model has no vertices");

        if (header.numVertices != model.vertices.size())
            return BuildFailure("vertex count does not match the model header");

        if (header.numIndices != model.modelData.indices.size())
            return BuildFailure("index count does not match the model header");

        if (header.numRenderBatches != model.modelData.renderBatches.size())
            return BuildFailure("render batch count does not match the model header");

        if (header.numTextures != model.textures.size())
            return BuildFailure("texture count does not match the model header");

        if (header.numMaterials != model.materials.size())
            return BuildFailure("material count does not match the model header");

        if (header.numTextureTransforms != model.textureTransforms.size())
            return BuildFailure("texture transform count does not match the model header");

        if (header.numBones != model.bones.size())
            return BuildFailure("bone count does not match the model header");

        if (header.numDecorationSets != model.decorationSets.size())
            return BuildFailure("decoration set count does not match the model header");

        if (header.numDecorations != model.decorations.size())
            return BuildFailure("decoration count does not match the model header");

        ModelBuildResult result;
        PreparedRenderModel& prepared = result.preparedModel;
        prepared.debugName = name;
        prepared.cullingData = model.cullingData;
        prepared.vertices = model.vertices;
        prepared.indices = model.modelData.indices;
        prepared.decorationSets = model.decorationSets;
        prepared.decorations = model.decorations;

        prepared.reserveInfo.numModels = 1;
        prepared.reserveInfo.numVertices = header.numVertices;
        prepared.reserveInfo.numIndices = header.numIndices;
        prepared.reserveInfo.numBones = static_cast<u32>(model.bones.size());
        prepared.reserveInfo.numTextureTransforms = static_cast<u32>(model.textureTransforms.size());
        prepared.reserveInfo.numDecorationSets = header.numDecorationSets;
        prepared.reserveInfo.numDecorations = header.numDecorations;
        prepared.isAnimated = !model.sequences.empty() && !model.bones.empty();

        prepared.drawCalls.reserve(model.modelData.renderBatches.size());
        prepared.textureUnits.reserve(header.numTextureUnits);
        prepared.textureLoadRequests.reserve(header.numTextureUnits);

        u32 numOpaqueDrawCalls = 0;
        u32 numTransparentDrawCalls = 0;

        for (const Model::ComplexModel::RenderBatch& renderBatch : model.modelData.renderBatches)
        {
            if (renderBatch.textureUnits.size() > std::numeric_limits<u16>().max())
                return BuildFailure("render batch has too many texture units");

            if (renderBatch.indexStart > prepared.indices.size() || renderBatch.indexCount > prepared.indices.size() - renderBatch.indexStart)
                return BuildFailure("render batch index range is outside the model index data");

            if (renderBatch.vertexStart > prepared.vertices.size() || renderBatch.vertexCount > prepared.vertices.size() - renderBatch.vertexStart)
                return BuildFailure("render batch vertex range is outside the model vertex data");

            PreparedDrawCall preparedDrawCall;
            preparedDrawCall.indexCount = renderBatch.indexCount;
            preparedDrawCall.firstIndex = renderBatch.indexStart;
            preparedDrawCall.vertexOffset = renderBatch.vertexStart;
            preparedDrawCall.textureUnitOffset = static_cast<u32>(prepared.textureUnits.size());
            preparedDrawCall.numTextureUnits = static_cast<u16>(renderBatch.textureUnits.size());
            preparedDrawCall.groupID = renderBatch.groupID;
            preparedDrawCall.isTransparent = renderBatch.isTransparent;

            for (const Model::ComplexModel::TextureUnit& complexTextureUnit : renderBatch.textureUnits)
            {
                if (complexTextureUnit.materialIndex >= model.materials.size())
                    return BuildFailure("texture unit material index is outside the model material data");

                PreparedTextureUnit preparedTextureUnit;
                const Model::ComplexModel::Material& material = model.materials[complexTextureUnit.materialIndex];

                u16 materialFlags = 0;
                static_assert(sizeof(material.flags) == sizeof(materialFlags));
                std::memcpy(&materialFlags, &material.flags, sizeof(materialFlags));

                const u16 materialFlag = materialFlags << 5;
                const u16 blendingMode = static_cast<u16>(material.blendingMode) << 11;

                preparedTextureUnit.data = static_cast<u16>(complexTextureUnit.flags.IsProjectedTexture) | materialFlag | blendingMode;
                preparedTextureUnit.materialType = complexTextureUnit.shaderID;

                if (complexTextureUnit.textureTransformIndexStart < model.textureTransformLookupTable.size())
                    preparedTextureUnit.textureTransformIds[0] = model.textureTransformLookupTable[complexTextureUnit.textureTransformIndexStart];

                if (complexTextureUnit.textureCount > 1 && complexTextureUnit.textureTransformIndexStart + 1u < model.textureTransformLookupTable.size())
                    preparedTextureUnit.textureTransformIds[1] = model.textureTransformLookupTable[complexTextureUnit.textureTransformIndexStart + 1u];

                // Preserve the current renderer's packed-flag interpretation for Phase 1 output equivalence.
                preparedDrawCall.numUnlitTextureUnits += (materialFlag & 0x2) > 0;

                const u32 preparedTextureUnitOffset = static_cast<u32>(prepared.textureUnits.size());
                const u32 textureCount = std::min<u32>(complexTextureUnit.textureCount, 2);
                for (u32 textureSlot = 0; textureSlot < textureCount; textureSlot++)
                {
                    const u32 lookupIndex = complexTextureUnit.textureIndexStart + textureSlot;
                    if (lookupIndex >= model.textureIndexLookupTable.size())
                        return BuildFailure("texture lookup index is outside the model texture lookup table");

                    const u16 textureIndex = model.textureIndexLookupTable[lookupIndex];
                    if (textureIndex == std::numeric_limits<u16>().max())
                        continue;

                    if (textureIndex >= model.textures.size())
                        return BuildFailure("texture index is outside the model texture data");

                    const Model::ComplexModel::Texture& texture = model.textures[textureIndex];
                    if (texture.type != Model::ComplexModel::Texture::Type::None || texture.textureHash == std::numeric_limits<u64>().max())
                        continue;

                    prepared.textureLoadRequests.push_back({
                        .textureUnitOffset = preparedTextureUnitOffset,
                        .textureIndex = textureSlot,
                        .textureHash = texture.textureHash,
                    });

                    u8 textureSamplerIndex = 0;
                    if (texture.flags.wrapX)
                        textureSamplerIndex |= 0x1;
                    if (texture.flags.wrapY)
                        textureSamplerIndex |= 0x2;

                    preparedTextureUnit.data |= textureSamplerIndex << (1 + (textureSlot * 2));
                }

                prepared.textureUnits.push_back(preparedTextureUnit);
            }

            prepared.drawCalls.push_back(preparedDrawCall);
            numOpaqueDrawCalls += !renderBatch.isTransparent;
            numTransparentDrawCalls += renderBatch.isTransparent;
        }

        if (header.numTextureUnits != prepared.textureUnits.size())
            return BuildFailure("texture unit count does not match the model header");

        if (header.numOpaqueRenderBatches != numOpaqueDrawCalls || header.numTransparentRenderBatches != numTransparentDrawCalls)
            return BuildFailure("opaque or transparent render batch count does not match the model header");

        prepared.reserveInfo.numTextureUnits = static_cast<u32>(prepared.textureUnits.size());
        prepared.reserveInfo.numOpaqueDrawCalls = numOpaqueDrawCalls;
        prepared.reserveInfo.numTransparentDrawCalls = numTransparentDrawCalls;
        prepared.estimatedCommitBytes =
            (prepared.vertices.size() * sizeof(Model::ComplexModel::Vertex)) +
            (prepared.indices.size() * sizeof(u16)) +
            (prepared.textureUnits.size() * sizeof(PreparedTextureUnit)) +
            (prepared.drawCalls.size() * sizeof(PreparedDrawCall)) +
            (prepared.decorationSets.size() * sizeof(Model::ComplexModel::DecorationSet)) +
            (prepared.decorations.size() * sizeof(Model::ComplexModel::Decoration));

        return result;
    }
}
