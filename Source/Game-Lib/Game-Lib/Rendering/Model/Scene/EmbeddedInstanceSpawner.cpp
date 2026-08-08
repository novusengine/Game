#include "EmbeddedInstanceSpawner.h"

#include "Game-Lib/Rendering/Model/Asset/ModelGeometryStorage.h"
#include "Game-Lib/Rendering/Scene/RenderScene.h"

#include <Base/Util/DebugHandler.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <limits>

namespace
{
    mat4x4 MakeLocalTransform(const FileFormat::Model::EmbeddedInstance& instance)
    {
        const mat4x4 rotation = glm::toMat4(glm::normalize(instance.rotation));
        const mat4x4 scale = glm::scale(mat4x4(1.0f), vec3(instance.uniformScale));
        return glm::translate(mat4x4(1.0f), instance.position) * rotation * scale;
    }
}

namespace ModelScene
{
    EmbeddedInstanceSpawner::EmbeddedInstanceSpawner(const ModelLoading::ModelGeometryStorage* geometryStorage,
                                                       RenderScenes::RenderScene* scene,
                                                       ResolveEmbeddedModelCallback resolveModel)
        : _geometryStorage(geometryStorage), _scene(scene), _resolveModel(std::move(resolveModel))
    {
    }

    bool EmbeddedInstanceSpawner::Spawn(RenderAssets::ModelHandle parentModel, const mat4x4& parentWorld,
                                         u32 requestedSet, SpawnedEmbeddedInstances& outInstances)
    {
        ZoneScopedN("EmbeddedInstanceSpawner::Spawn");

        EmbeddedInstanceSpawnCursor cursor;
        if (!BeginSpawn(parentModel, parentWorld, requestedSet, outInstances, cursor))
            return false;

        u32 processed = 0;
        return ContinueSpawn(cursor, outInstances, std::numeric_limits<u32>::max(), processed) ==
               EmbeddedInstanceSpawnStatus::Complete;
    }

    bool EmbeddedInstanceSpawner::BeginSpawn(RenderAssets::ModelHandle parentModel,
                                              const mat4x4& parentWorld, u32 requestedSet,
                                              SpawnedEmbeddedInstances& outInstances,
                                              EmbeddedInstanceSpawnCursor& cursor)
    {
        ZoneScopedN("EmbeddedInstanceSpawner::BeginSpawn");

        if (!_geometryStorage->HasModel(parentModel))
            return false;

        outInstances = {};
        outInstances.parentModel = parentModel;
        // Resolving children can grow the geometry GPUVectors, so keep value snapshots rather than
        // references into their CPU-side staging allocations.
        const ModelLoading::ModelGPURecord parent = _geometryStorage->GetRecord(parentModel);
        outInstances.hasSets = parent.numEmbeddedInstanceSets != 0;
        bool setValid = true;
        outInstances.selectedSet = SelectSet(parentModel, requestedSet, setValid);
        if (!setValid)
            ++_stats.invalidSetSelections;
        outInstances.instances.reserve(parent.numEmbeddedInstances);

        cursor.parentWorld = parentWorld;
        cursor.nextSourceIndex = 0;
        cursor.active = true;
        return true;
    }

    EmbeddedInstanceSpawnStatus EmbeddedInstanceSpawner::ContinueSpawn(
        EmbeddedInstanceSpawnCursor& cursor, SpawnedEmbeddedInstances& instances,
        u32 maxInstances, u32& outProcessed)
    {
        ZoneScopedN("EmbeddedInstanceSpawner::ContinueSpawn");

        outProcessed = 0;
        if (!cursor.active || !_geometryStorage->HasModel(instances.parentModel))
            return EmbeddedInstanceSpawnStatus::Failed;

        const ModelLoading::ModelGPURecord parent =
            _geometryStorage->GetRecord(instances.parentModel);

        const auto& embeddedInstances = _geometryStorage->GetEmbeddedInstances();
        while (cursor.nextSourceIndex < parent.numEmbeddedInstances && outProcessed < maxInstances)
        {
            const u32 sourceIndex = cursor.nextSourceIndex++;
            ++outProcessed;
            const FileFormat::Model::EmbeddedInstance source =
                embeddedInstances[parent.embeddedInstanceBase + sourceIndex];
            RenderAssets::ModelHandle childModel;
            const ModelLoading::EmbeddedModelLoadStatus status = _resolveModel(source.modelAssetID, childModel);
            if (status == ModelLoading::EmbeddedModelLoadStatus::InvalidReference)
            {
                ++_stats.invalidReferenceSkips;
                continue;
            }
            if (status == ModelLoading::EmbeddedModelLoadStatus::MissingRenderableGeometry)
            {
                ++_stats.missingGeometrySkips;
                continue;
            }
            if (status == ModelLoading::EmbeddedModelLoadStatus::Failed)
            {
                ++_stats.unexpectedDependencyFailures;
                if (_reportedDependencyFailures.insert(source.modelAssetID).second)
                {
                    NC_LOG_ERROR("MODEL_EMBEDDED dependency_failure parent={} child_asset={} source_index={}",
                                 static_cast<RenderAssets::ModelHandle::type>(instances.parentModel),
                                 source.modelAssetID, sourceIndex);
                }
            }

            RenderScenes::ModelInstanceDesc desc;
            desc.model = childModel;
            desc.worldTransform = cursor.parentWorld * MakeLocalTransform(source);
            desc.visible = IsEnabled(instances, sourceIndex);
            const RenderScenes::ModelInstanceHandle handle = _scene->CreateModelInstance(desc);
            if (!_scene->IsPending(handle))
            {
                cursor.active = false;
                return EmbeddedInstanceSpawnStatus::Failed;
            }

            instances.instances.push_back({ handle, sourceIndex });
            ++_stats.spawnedInstances;
        }

        if (cursor.nextSourceIndex < parent.numEmbeddedInstances)
            return EmbeddedInstanceSpawnStatus::InProgress;
        cursor.active = false;
        return EmbeddedInstanceSpawnStatus::Complete;
    }

    bool EmbeddedInstanceSpawner::SetInstanceSet(SpawnedEmbeddedInstances& instances, u32 requestedSet)
    {
        if (!_geometryStorage->HasModel(instances.parentModel))
            return false;

        bool setValid = true;
        instances.selectedSet = SelectSet(instances.parentModel, requestedSet, setValid);
        if (!setValid)
            ++_stats.invalidSetSelections;

        bool succeeded = true;
        for (const SpawnedEmbeddedInstance& instance : instances.instances)
            succeeded &= _scene->SetModelVisible(instance.handle, IsEnabled(instances, instance.sourceIndex));
        return succeeded && setValid;
    }

    void EmbeddedInstanceSpawner::Destroy(SpawnedEmbeddedInstances& instances, u64 retireValue)
    {
        ZoneScopedN("EmbeddedInstanceSpawner::Destroy");

        for (const SpawnedEmbeddedInstance& instance : instances.instances)
            _scene->DestroyModelInstance(instance.handle, retireValue);
        instances = {};
    }

    bool EmbeddedInstanceSpawner::IsEnabled(const SpawnedEmbeddedInstances& instances, u32 sourceIndex) const
    {
        if (!instances.hasSets)
            return true;

        const ModelLoading::ModelGPURecord& parent = _geometryStorage->GetRecord(instances.parentModel);
        if (instances.selectedSet >= parent.numEmbeddedInstanceSets)
            return false;

        const FileFormat::Model::EmbeddedInstanceSet& set =
            _geometryStorage->GetEmbeddedInstanceSets()[parent.embeddedInstanceSetBase + instances.selectedSet];
        return sourceIndex >= set.instanceOffset && sourceIndex - set.instanceOffset < set.numInstances;
    }

    u32 EmbeddedInstanceSpawner::SelectSet(RenderAssets::ModelHandle parentModel, u32 requestedSet,
                                            bool& outValid) const
    {
        const ModelLoading::ModelGPURecord& parent = _geometryStorage->GetRecord(parentModel);
        if (parent.numEmbeddedInstanceSets == 0)
        {
            outValid = true;
            return 0;
        }

        const u32 selected = requestedSet == std::numeric_limits<u16>::max() ? 0u : requestedSet;
        outValid = selected < parent.numEmbeddedInstanceSets;
        return outValid ? selected : parent.numEmbeddedInstanceSets;
    }
}
