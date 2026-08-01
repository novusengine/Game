#pragma once
#include "Game-Lib/Rendering/Asset/AssetDiagnostic.h"
#include "Game-Lib/Rendering/Asset/AssetValidation.h"

#include <FileFormat/Novus/Model/Model.h>

#include <span>

namespace ModelLoading
{
    struct ModelAssetView
    {
        FileFormat::Model::ModelAsset root;
        std::span<const FileFormat::Model::Mesh> meshes;
        std::span<const FileFormat::Model::MeshLOD> meshLODs;
        std::span<const FileFormat::Model::Submesh> submeshes;
        std::span<const FileFormat::Model::Meshlet> meshlets;
        std::span<const FileFormat::Model::PackedPosition> positions;
        std::span<const FileFormat::Model::PackedVertexAttributes> vertexAttributes;
        std::span<const FileFormat::Model::PackedSkinningData> skinningData;
        std::span<const u32> meshletVertexIndices;
        std::span<const FileFormat::Model::PackedMeshletTriangle> meshletTriangles;
        std::span<const u16> jointPaletteRemaps;
        std::span<const FileFormat::Model::MaterialSlot> materialSlots;
        std::span<const FileFormat::Model::EmbeddedInstanceSet> embeddedInstanceSets;
        std::span<const FileFormat::Model::EmbeddedInstance> embeddedInstances;
    };

    struct ModelAssetLimitations
    {
        u32 invalidSkeletonReferences = 0;
        u32 invalidAnimationBoundsReferences = 0;
        u32 invalidCollisionReferences = 0;
        u32 invalidEmbeddedModelReferences = 0;
    };

    struct ModelAssetReadResult
    {
        ModelAssetView view;
        ModelAssetLimitations limitations;
        AssetLoading::Diagnostic diagnostic;

        explicit operator bool() const
        {
            return !diagnostic;
        }
    };

    class ModelAssetReader
    {
      public:
        // Returned spans borrow payload and remain valid only while it remains alive.
        static ModelAssetReadResult Read(std::span<const u8> payload, AssetLoading::ValidationMode validationMode = AssetLoading::ValidationMode::Default);
    };
} // namespace ModelLoading
