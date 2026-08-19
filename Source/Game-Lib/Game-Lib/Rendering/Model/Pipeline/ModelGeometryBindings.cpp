#include "ModelGeometryBindings.h"

#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Renderer/RenderGraph.h>

namespace ModelPipeline
{
    bool ModelGeometryBindings::BindOne(Renderer::DescriptorSet& descriptorSet, StringUtils::StringHash name, Renderer::BufferID buffer, Renderer::BufferID& current)
    {
        if (buffer == current)
            return false;
        descriptorSet.Bind(name, buffer);
        current = buffer;
        return true;
    }

    bool ModelGeometryBindings::Bind(Renderer::DescriptorSet& descriptorSet, const ModelLoading::ModelGeometryStorage& geometry, const RenderScenes::RenderScene& scene)
    {
        bool changed = false;
        changed |= BindOne(descriptorSet, "_modelVisibilityInstances"_h, scene.GetModelInstances().GetRecords().GetBuffer(), _instances);
        changed |= BindOne(descriptorSet, "_modelVisibilityModels"_h, geometry.GetRecords().GetBuffer(), _models);
        changed |= BindOne(descriptorSet, "_modelVisibilityMeshes"_h, geometry.GetMeshes().GetBuffer(), _meshes);
        changed |= BindOne(descriptorSet, "_modelVisibilityLODs"_h, geometry.GetMeshLODs().GetBuffer(), _lods);
        changed |= BindOne(descriptorSet, "_modelVisibilitySubmeshes"_h, geometry.GetSubmeshes().GetBuffer(), _submeshes);
        changed |= BindOne(descriptorSet, "_modelVisibilityMeshlets"_h, geometry.GetMeshlets().GetBuffer(), _meshlets);
        changed |= BindOne(descriptorSet, "_modelVisibilityPositions"_h, geometry.GetPositions().GetBuffer(), _positions);
        changed |= BindOne(descriptorSet, "_modelVisibilityVertexAttributes"_h, geometry.GetVertexAttributes().GetBuffer(), _vertexAttributes);
        changed |= BindOne(descriptorSet, "_modelVisibilityVertexIndices"_h, geometry.GetMeshletVertexIndices().GetBuffer(), _vertexIndices);
        changed |= BindOne(descriptorSet, "_modelVisibilityTriangles"_h, geometry.GetMeshletTriangles().GetBuffer(), _triangles);
        changed |= BindOne(descriptorSet, "_modelVisibilityMaterialTable"_h, scene.GetModelMaterialTables().GetEntries().GetBuffer(), _materialTable);
        return changed;
    }

    void ModelGeometryBindings::RegisterUsage(Renderer::RenderGraphBuilder& builder, const ModelLoading::ModelGeometryStorage& geometry, const RenderScenes::RenderScene& scene, Renderer::BufferPassUsage usage)
    {
        builder.Read(scene.GetModelInstances().GetRecords().GetBuffer(), usage);
        builder.Read(geometry.GetRecords().GetBuffer(), usage);
        builder.Read(geometry.GetMeshes().GetBuffer(), usage);
        builder.Read(geometry.GetMeshLODs().GetBuffer(), usage);
        builder.Read(geometry.GetSubmeshes().GetBuffer(), usage);
        builder.Read(geometry.GetMeshlets().GetBuffer(), usage);
        builder.Read(geometry.GetPositions().GetBuffer(), usage);
        builder.Read(geometry.GetVertexAttributes().GetBuffer(), usage);
        builder.Read(geometry.GetMeshletVertexIndices().GetBuffer(), usage);
        builder.Read(geometry.GetMeshletTriangles().GetBuffer(), usage);
        builder.Read(scene.GetModelMaterialTables().GetEntries().GetBuffer(), usage);
    }
}
