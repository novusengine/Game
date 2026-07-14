#pragma once

#include <Base/Types.h>

#include <FileFormat/Novus/Model/ComplexModel.h>

#include <limits>
#include <memory>
#include <string>
#include <vector>

constexpr u32 MODEL_INVALID_TEXTURE_ID = 0; // This refers to the debug texture
constexpr u32 MODEL_INVALID_TEXTURE_TRANSFORM_ID = std::numeric_limits<u16>().max();
constexpr u32 MODEL_INVALID_TEXTURE_DATA_ID = std::numeric_limits<u32>().max();
constexpr u8 MODEL_INVALID_TEXTURE_UNIT_INDEX = std::numeric_limits<u8>().max();

namespace ModelLoading
{
    using ModelLoadRequestID = u64;
    constexpr ModelLoadRequestID INVALID_MODEL_LOAD_REQUEST_ID = 0;

    struct PreparedTextureUnit
    {
    public:
        u16 data = 0; // Texture Flag + Material Flag + Material Blending Mode
        u16 materialType = 0; // Shader ID
        u32 textureIds[2] = { MODEL_INVALID_TEXTURE_ID, MODEL_INVALID_TEXTURE_ID };
        u16 textureTransformIds[2] = { MODEL_INVALID_TEXTURE_TRANSFORM_ID, MODEL_INVALID_TEXTURE_TRANSFORM_ID };
        u32 rgba = 0xFFFFFFFF;
        u32 padding0 = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
    };
    static_assert(sizeof(PreparedTextureUnit) == 32, "Prepared texture units must preserve the GPU texture-unit layout");

    struct PreparedTextureLoadRequest
    {
    public:
        u32 textureUnitOffset = 0;
        u32 textureIndex = 0;
        u64 textureHash = 0;
    };

    struct PreparedDrawCall
    {
    public:
        u32 indexCount = 0;
        u32 firstIndex = 0;
        u32 vertexOffset = 0;

        u32 textureUnitOffset = 0;
        u16 numTextureUnits = 0;
        u16 numUnlitTextureUnits = 0;

        u32 groupID = 0;
        bool isTransparent = false;
    };

    struct PreparedModelReserveInfo
    {
    public:
        u32 numModels = 0;
        u32 numOpaqueDrawCalls = 0;
        u32 numTransparentDrawCalls = 0;
        u32 numVertices = 0;
        u32 numIndices = 0;
        u32 numTextureUnits = 0;
        u32 numBones = 0;
        u32 numTextureTransforms = 0;
        u32 numDecorationSets = 0;
        u32 numDecorations = 0;
    };

    struct PreparedRenderModel
    {
    public:
        std::string debugName;
        Model::ComplexModel::CullingData cullingData;

        std::vector<Model::ComplexModel::Vertex> vertices;
        std::vector<u16> indices;
        std::vector<PreparedTextureUnit> textureUnits;
        std::vector<PreparedTextureLoadRequest> textureLoadRequests;
        std::vector<PreparedDrawCall> drawCalls;

        std::vector<Model::ComplexModel::DecorationSet> decorationSets;
        std::vector<Model::ComplexModel::Decoration> decorations;

        PreparedModelReserveInfo reserveInfo;
        u64 estimatedCommitBytes = 0;
        bool isAnimated = false;
    };

    struct ModelBuildResult
    {
    public:
        PreparedRenderModel preparedModel;
        std::string error;

        explicit operator bool() const { return error.empty(); }
    };

    struct PreparedModelResult
    {
    public:
        u64 epoch = 0;
        u64 modelHash = std::numeric_limits<u64>().max();
        std::string debugName;
        std::unique_ptr<Model::ComplexModel> model;
        PreparedRenderModel preparedModel;
        std::string error;

        explicit operator bool() const { return error.empty() && model != nullptr; }
    };
}
