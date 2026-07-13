#include "JoltMemoryTelemetry.h"

#include <Base/CVarSystem/CVarSystem.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace Util::JoltMemoryTelemetry
{
    AutoCVar_Int CVAR_PhysicsTelemetry(CVarCategory::Client | CVarCategory::Physics, "telemetry", "enables Jolt physics body and memory telemetry", 0, CVarFlags::EditCheckbox);

    namespace
    {
        struct Tracker
        {
        public:
            std::mutex mutex;
            std::unordered_map<void*, size_t> allocationSizes;
            std::atomic<u64> currentBytes = 0;
            std::atomic<u64> peakBytes = 0;
            std::atomic<u64> totalAllocatedBytes = 0;
            std::atomic<u64> totalFreedBytes = 0;
            std::atomic<u64> liveAllocations = 0;
            std::atomic<u64> totalAllocations = 0;
        };

        std::atomic<bool> telemetryEnabled = false;
        std::atomic<bool> callbackRegistered = false;

        Tracker& GetTracker()
        {
            static Tracker* tracker = new Tracker();
            return *tracker;
        }

        void TrackAllocate(void* block, size_t size)
        {
            if (!block || !IsEnabled())
                return;

            Tracker& tracker = GetTracker();
            {
                std::scoped_lock lock(tracker.mutex);
                tracker.allocationSizes[block] = size;
            }

            u64 currentBytes = tracker.currentBytes.fetch_add(size, std::memory_order_relaxed) + size;
            tracker.totalAllocatedBytes.fetch_add(size, std::memory_order_relaxed);
            tracker.liveAllocations.fetch_add(1, std::memory_order_relaxed);
            tracker.totalAllocations.fetch_add(1, std::memory_order_relaxed);

            u64 previousPeak = tracker.peakBytes.load(std::memory_order_relaxed);
            while (currentBytes > previousPeak && !tracker.peakBytes.compare_exchange_weak(previousPeak, currentBytes, std::memory_order_relaxed))
            {
            }
        }

        size_t TrackFree(void* block)
        {
            if (!block)
                return 0;

            Tracker& tracker = GetTracker();
            size_t size = 0;
            {
                std::scoped_lock lock(tracker.mutex);
                auto itr = tracker.allocationSizes.find(block);
                if (itr != tracker.allocationSizes.end())
                {
                    size = itr->second;
                    tracker.allocationSizes.erase(itr);
                }
            }

            if (size > 0)
            {
                tracker.currentBytes.fetch_sub(size, std::memory_order_relaxed);
                tracker.totalFreedBytes.fetch_add(size, std::memory_order_relaxed);
                tracker.liveAllocations.fetch_sub(1, std::memory_order_relaxed);
            }

            return size;
        }

        void* Allocate(size_t size)
        {
            void* block = std::malloc(size);
            TrackAllocate(block, size);
            return block;
        }

        void* Reallocate(void* block, [[maybe_unused]] size_t oldSize, size_t newSize)
        {
            void* newBlock = std::realloc(block, newSize);
            if (!newBlock)
                return nullptr;

            TrackFree(block);
            TrackAllocate(newBlock, newSize);

            return newBlock;
        }

        void Free(void* block)
        {
            TrackFree(block);
            std::free(block);
        }

        void* AlignedAllocate(size_t size, size_t alignment)
        {
#if defined(_WIN32)
            void* block = _aligned_malloc(size, alignment);
#else
            void* block = nullptr;
            if (posix_memalign(&block, alignment, size) != 0)
                block = nullptr;
#endif
            TrackAllocate(block, size);
            return block;
        }

        void AlignedFree(void* block)
        {
            TrackFree(block);
#if defined(_WIN32)
            _aligned_free(block);
#else
            std::free(block);
#endif
        }
    }

    void RegisterAllocator()
    {
        telemetryEnabled.store(CVAR_PhysicsTelemetry.Get() != 0, std::memory_order_relaxed);
        bool expectedCallbackRegistered = false;
        if (callbackRegistered.compare_exchange_strong(expectedCallbackRegistered, true, std::memory_order_relaxed))
        {
            CVAR_PhysicsTelemetry.AddOnValueChanged([](const i32& value)
            {
                telemetryEnabled.store(value != 0, std::memory_order_relaxed);
            });
        }

#ifndef JPH_DISABLE_CUSTOM_ALLOCATOR
        JPH::Allocate = Allocate;
        JPH::Reallocate = Reallocate;
        JPH::Free = Free;
        JPH::AlignedAllocate = AlignedAllocate;
        JPH::AlignedFree = AlignedFree;
#else
        JPH::RegisterDefaultAllocator();
#endif
    }

    bool IsEnabled()
    {
        return telemetryEnabled.load(std::memory_order_relaxed);
    }

    Stats GetStats()
    {
        Tracker& tracker = GetTracker();

        Stats stats;
        stats.currentBytes = tracker.currentBytes.load(std::memory_order_relaxed);
        stats.peakBytes = tracker.peakBytes.load(std::memory_order_relaxed);
        stats.totalAllocatedBytes = tracker.totalAllocatedBytes.load(std::memory_order_relaxed);
        stats.totalFreedBytes = tracker.totalFreedBytes.load(std::memory_order_relaxed);
        stats.liveAllocations = tracker.liveAllocations.load(std::memory_order_relaxed);
        stats.totalAllocations = tracker.totalAllocations.load(std::memory_order_relaxed);
        return stats;
    }
}
