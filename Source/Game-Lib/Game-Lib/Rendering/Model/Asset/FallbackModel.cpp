#include "FallbackModel.h"

#include <array>

namespace
{
    namespace Model = FileFormat::Model;

    constexpr Model::Bounds FALLBACK_BOUNDS = {
        .center = vec3(0.0f),
        .sphereRadius = 0.8660254f,
        .extents = vec3(0.5f)
    };

    constexpr std::array<Model::Mesh, 1> MESHES = {{
        {
            .lodOffset = 0,
            .numLODs = 1,
            .materialSlotOffset = 0,
            .numMaterialSlots = 1,
            .bounds = FALLBACK_BOUNDS,
            .positionDecodeOffset = vec3(-0.5f),
            .positionDecodeExtent = vec3(1.0f)
        }
    }};

    constexpr std::array<Model::MeshLOD, 1> MESH_LODS = {{
        {
            .vertexOffset = 0,
            .numVertices = 8,
            .vertexAttributeOffset = 0,
            .numVertexAttributes = 8,
            .submeshOffset = 0,
            .numSubmeshes = 1,
            .meshletOffset = 0,
            .numMeshlets = 1,
            .bounds = FALLBACK_BOUNDS
        }
    }};

    constexpr std::array<Model::Submesh, 1> SUBMESHES = {{
        {
            .meshletOffset = 0,
            .numMeshlets = 1,
            .materialSlotIndex = 0,
            .geometryGroupID = 0
        }
    }};

    constexpr std::array<Model::Meshlet, 1> MESHLETS = {{
        {
            .boundsCenter = vec3(0.0f),
            .boundsRadius = 0.8660254f,
            .vertexOffset = 0,
            .triangleOffset = 0,
            .vertexCount = 8,
            .triangleCount = 12
        }
    }};

    constexpr std::array<Model::PackedPosition, 8> POSITIONS = {{
        {0, 0, 0, 0},
        {65535, 0, 0, 0},
        {65535, 65535, 0, 0},
        {0, 65535, 0, 0},
        {0, 0, 65535, 0},
        {65535, 0, 65535, 0},
        {65535, 65535, 65535, 0},
        {0, 65535, 65535, 0}
    }};

    constexpr std::array<Model::PackedVertexAttributes, 8> VERTEX_ATTRIBUTES = {};
    constexpr std::array<u32, 8> MESHLET_VERTEX_INDICES = {0, 1, 2, 3, 4, 5, 6, 7};

    constexpr u32 PackTriangle(u32 a, u32 b, u32 c)
    {
        return a | (b << 8u) | (c << 16u);
    }

    constexpr std::array<Model::PackedMeshletTriangle, 12> MESHLET_TRIANGLES = {{
        {PackTriangle(0, 2, 1)}, {PackTriangle(0, 3, 2)},
        {PackTriangle(4, 5, 6)}, {PackTriangle(4, 6, 7)},
        {PackTriangle(0, 1, 5)}, {PackTriangle(0, 5, 4)},
        {PackTriangle(1, 2, 6)}, {PackTriangle(1, 6, 5)},
        {PackTriangle(2, 3, 7)}, {PackTriangle(2, 7, 6)},
        {PackTriangle(3, 0, 4)}, {PackTriangle(3, 4, 7)}
    }};

    constexpr std::array<Model::MaterialSlot, 1> MATERIAL_SLOTS = {{
        {
            .defaultMaterialInstanceAssetID = 0,
            .nameHash = 0,
            .stableID = 0
        }
    }};
} // namespace

namespace ModelLoading
{
    ModelAssetView GetFallbackModelAssetView()
    {
        ModelAssetView view;
        view.root.bounds = FALLBACK_BOUNDS;
        view.root.numMeshes = static_cast<u32>(MESHES.size());
        view.root.numMeshLODs = static_cast<u32>(MESH_LODS.size());
        view.root.numSubmeshes = static_cast<u32>(SUBMESHES.size());
        view.root.numMeshlets = static_cast<u32>(MESHLETS.size());
        view.root.numPositions = static_cast<u32>(POSITIONS.size());
        view.root.numVertexAttributes = static_cast<u32>(VERTEX_ATTRIBUTES.size());
        view.root.numMeshletVertexIndices = static_cast<u32>(MESHLET_VERTEX_INDICES.size());
        view.root.numMeshletTriangles = static_cast<u32>(MESHLET_TRIANGLES.size());
        view.root.numMaterialSlots = static_cast<u32>(MATERIAL_SLOTS.size());
        view.root.geometryGroupCount = 1;

        view.meshes = MESHES;
        view.meshLODs = MESH_LODS;
        view.submeshes = SUBMESHES;
        view.meshlets = MESHLETS;
        view.positions = POSITIONS;
        view.vertexAttributes = VERTEX_ATTRIBUTES;
        view.meshletVertexIndices = MESHLET_VERTEX_INDICES;
        view.meshletTriangles = MESHLET_TRIANGLES;
        view.materialSlots = MATERIAL_SLOTS;
        return view;
    }
} // namespace ModelLoading
