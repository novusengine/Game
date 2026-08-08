#pragma once
#include "Game-Lib/Rendering/Asset/RenderAssetHandles.h"
#include "Game-Lib/Rendering/Scene/RenderSceneHandles.h"
#include "MeshletHistoryAllocator.h"

#include <Renderer/GPUVector.h>

#include <span>
#include <vector>

namespace Renderer
{
    class Renderer;
}

namespace ModelScene
{
    enum ModelInstanceFlags : u32
    {
        ModelInstanceFlagVisible = 1u << 0u,
        ModelInstanceFlagMotionValid = 1u << 1u,
        ModelInstanceFlagNew = 1u << 2u,
        ModelInstanceFlagTeleported = 1u << 3u,
        ModelInstanceFlagPrivateMaterials = 1u << 4u,
        ModelInstanceFlagPendingPublication = 1u << 5u
    };

    struct ModelInstanceGPURecord
    {
        mat4x4 currentWorld = mat4x4(1.0f);
        mat4x4 previousWorld = mat4x4(1.0f);
        u32 modelIndex = 0;
        u32 materialTableOffset = 0;
        u32 materialTableCount = 0;
        u32 geometryGroupWordOffset = 0;
        u32 geometryGroupWordCount = 0;
        u32 meshletHistoryWordOffset = 0;
        u32 meshletHistoryWordCount = 0;
        u32 deformationHandle = RenderScenes::INVALID_SCENE_INDEX;
        u32 generation = 0;
        u32 flags = 0;
        u32 reserved[2] = {};
    };

    struct ModelInstanceCreateInfo
    {
        RenderAssets::ModelHandle model;
        mat4x4 worldTransform = mat4x4(1.0f);
        RenderScenes::ModelMaterialTableHandle materialTable;
        u32 materialTableOffset = 0;
        u32 materialTableCount = 0;
        RenderScenes::GeometryGroupMaskHandle geometryGroupMask;
        u32 geometryGroupWordOffset = 0;
        u32 geometryGroupWordCount = 0;
        MeshletHistoryRange meshletHistory;
        bool visible = true;
        bool privateMaterials = false;
    };

    struct ModelInstanceResources
    {
        RenderAssets::ModelHandle model;
        RenderScenes::ModelMaterialTableHandle materialTable;
        RenderScenes::GeometryGroupMaskHandle geometryGroupMask;
        MeshletHistoryRange meshletHistory;
    };

    struct ModelInstanceStoreStats
    {
        u32 liveInstances = 0;
        u32 pendingInstances = 0;
        u32 freeSlots = 0;
        u32 slotCapacity = 0;
        u32 pendingSlotClears = 0;
        u32 staleHandleRejects = 0;
    };

    // Owns CPU-side generation-checked instance slots and their GPU-side transform
    // and resource records. It keeps generation-safe CPU lifetime state aligned
    // with dense GPU culling inputs.
    class ModelInstanceStore
    {
      public:
        explicit ModelInstanceStore(bool validateTransfers = false);

        void Reserve(u32 instanceCount);
        RenderScenes::ModelInstanceHandle Create(const ModelInstanceCreateInfo& info);
        bool Destroy(RenderScenes::ModelInstanceHandle handle, ModelInstanceResources& outResources);
        bool SetTransform(RenderScenes::ModelInstanceHandle handle, const mat4x4& transform, bool teleported,
                          bool& outNeedsHistoryClear);
        bool SetVisible(RenderScenes::ModelInstanceHandle handle, bool visible, bool& outNeedsHistoryClear);
        bool SetMaterialTable(RenderScenes::ModelInstanceHandle handle, RenderScenes::ModelMaterialTableHandle table,
                              u32 offset, u32 count, bool isPrivate);
        void AdvanceFrame();
        void PublishPending();
        void SyncToGPU(Renderer::Renderer* renderer);

        bool IsAlive(RenderScenes::ModelInstanceHandle handle) const;
        bool IsPending(RenderScenes::ModelInstanceHandle handle) const;
        const ModelInstanceGPURecord* GetRecord(RenderScenes::ModelInstanceHandle handle) const;
        const ModelInstanceResources* GetResources(RenderScenes::ModelInstanceHandle handle) const;
        void CollectActiveHandles(std::vector<RenderScenes::ModelInstanceHandle>& outHandles) const;
        std::span<const u32> GetPendingSlotClears() const
        {
            return _pendingSlotClears;
        }
        void AcknowledgePendingSlotClears()
        {
            _pendingSlotClears.clear();
        }
        ModelInstanceStoreStats GetStats() const;
        const Renderer::GPUVector<ModelInstanceGPURecord>& GetRecords() const
        {
            return _records;
        }
        u64 GetMembershipRevision() const
        {
            return _membershipRevision;
        }

      private:
        enum class SlotState : u8
        {
            Free,
            Pending,
            Live
        };

        struct Slot
        {
            ModelInstanceResources resources;
            u32 generation = 1;
            SlotState state = SlotState::Free;
            bool desiredVisible = false;
            bool frameAdvanceQueued = false;
        };

        struct SlotGenerationEntry
        {
            u32 slotIndex = 0;
            u32 generation = 0;
        };

        Slot* GetSlot(RenderScenes::ModelInstanceHandle handle);
        const Slot* GetSlot(RenderScenes::ModelInstanceHandle handle) const;
        void QueueFrameAdvance(u32 slotIndex);
        void RecordStaleHandle() const
        {
            _staleHandleRejects++;
        }

        Renderer::GPUVector<ModelInstanceGPURecord> _records;
        std::vector<Slot> _slots;
        std::vector<u32> _freeSlots;
        std::vector<u32> _pendingSlotClears;
        std::vector<SlotGenerationEntry> _pendingPublications;
        std::vector<SlotGenerationEntry> _frameAdvanceEntries;
        u32 _liveInstances = 0;
        u32 _pendingInstances = 0;
        mutable u32 _staleHandleRejects = 0;
        u64 _membershipRevision = 0;
    };
} // namespace ModelScene
