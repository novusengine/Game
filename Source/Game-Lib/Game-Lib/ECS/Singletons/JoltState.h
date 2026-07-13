#pragma once
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
#include "Game-Lib/Util/JoltMemoryTelemetry.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <entt/entt.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>

namespace Jolt
{
    namespace Settings
    {
        // Biggest continent map currently attempts ~123k resident static physics bodies.
        // Keep this independent from pair/contact budgets, which depend on active collision density.
        static constexpr u32 maxBodies = 196608;
        static constexpr u32 numBodyMutexes = 0;
        static constexpr u32 maxBodyPairs = 8192;
        static constexpr u32 maxContactConstraints = 10240;
    }

    namespace Layers
    {
        static constexpr u8 NON_MOVING = 0;
        static constexpr u8 MOVING = 1;
        static constexpr u8 NUM_LAYERS = 2;
    }; 
    
    namespace BroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr u32 NUM_LAYERS(2);
    };

    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
        {
            switch (inObject1)
            {
                case Layers::NON_MOVING:
                    return inObject2 == Layers::MOVING; // Non moving only collides with moving

                case Layers::MOVING:
                    return true; // Moving collides with everything
                
                default: return false;
            }
        }
    };

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
    public:
        BPLayerInterfaceImpl()
        {
            // Create a mapping table from object to broad phase layer
            objectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            objectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
        }

        virtual u32 GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            assert(inLayer < Layers::NUM_LAYERS);
            return objectToBroadPhase[inLayer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        {
            switch ((JPH::BroadPhaseLayer::Type)inLayer)
            {
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
                default: return "INVALID";
            }
        }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

    private:
        JPH::BroadPhaseLayer objectToBroadPhase[Layers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
        {
            switch (inLayer1)
            {
                case Layers::NON_MOVING:
                    return inLayer2 == BroadPhaseLayers::MOVING;

                case Layers::MOVING:
                    return true;

                default: return false;
            }
        }
    };

    class MyContactListener : public JPH::ContactListener
    {
    public:
        // See: ContactListener
        virtual JPH::ValidateResult	OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override
        {
            //NC_LOG_INFO("Contact Validate Callback");

            // Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }

        virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
        {
            //NC_LOG_INFO("A contact was added");
        }

        virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
        {
            //NC_LOG_INFO("A contact was persisted");
        }

        virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
        {
            //NC_LOG_INFO("A contact was removed");
        }
    };

    class MyBodyActivationListener : public JPH::BodyActivationListener
    {
    public:
        virtual void OnBodyActivated(const JPH::BodyID& inBodyID, u64 inBodyUserData) override
        {
            //NC_LOG_INFO("A body got activated");
        }

        virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, u64 inBodyUserData) override
        {
            //NC_LOG_INFO("A body went to sleep");
        }
    };
};

namespace ECS::Singletons
{
    enum class JoltBodyTelemetrySource : u8
    {
        TerrainChunk,
        StaticPlacement,
        DynamicInstance,
        StaticMeshComponent,
        KinematicMeshComponent,
        DynamicMeshComponent,
        Count
    };

    struct JoltBodyTelemetryCounter
    {
    public:
        std::atomic<u64> attempts = 0;
        std::atomic<u64> created = 0;
        std::atomic<u64> failed = 0;
    };

    struct JoltState
    {
    public:
        JoltState() : allocator(64u * 1024u * 1024u), scheduler(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1) { }
        
        JPH::PhysicsSystem physicsSystem;
        JPH::TempAllocatorImpl allocator;
        JPH::JobSystemThreadPool scheduler;

        Jolt::BPLayerInterfaceImpl broadPhaseLayerInterface;
        Jolt::ObjectVsBroadPhaseLayerFilterImpl objectVSBroadPhaseLayerFilter;
        Jolt::ObjectLayerPairFilterImpl objectVSObjectLayerFilter;
        Jolt::MyBodyActivationListener bodyActivationListener;
        Jolt::MyContactListener contactListener;

        // Should run at 30Hz but we're running at 60Hz for now
        static constexpr f32 FixedDeltaTime = 1.0f / 60.0f;
        f32 updateTimer = 0.0f;

        std::string telemetryMapName = "NoMap";
        std::array<JoltBodyTelemetryCounter, static_cast<std::size_t>(JoltBodyTelemetrySource::Count)> bodyTelemetryCounters;
        std::atomic<u64> peakNumBodies = 0;

        static const char* GetTelemetrySourceName(JoltBodyTelemetrySource source)
        {
            switch (source)
            {
                case JoltBodyTelemetrySource::TerrainChunk: return "TerrainChunk";
                case JoltBodyTelemetrySource::StaticPlacement: return "StaticPlacement";
                case JoltBodyTelemetrySource::DynamicInstance: return "DynamicInstance";
                case JoltBodyTelemetrySource::StaticMeshComponent: return "StaticMeshComponent";
                case JoltBodyTelemetrySource::KinematicMeshComponent: return "KinematicMeshComponent";
                case JoltBodyTelemetrySource::DynamicMeshComponent: return "DynamicMeshComponent";
                default: return "Unknown";
            }
        }

        void ResetPhysicsTelemetry(const std::string& mapName)
        {
            telemetryMapName = mapName.empty() ? "NoMap" : mapName;
            peakNumBodies.store(physicsSystem.GetNumBodies(), std::memory_order_relaxed);

            for (JoltBodyTelemetryCounter& counter : bodyTelemetryCounters)
            {
                counter.attempts.store(0, std::memory_order_relaxed);
                counter.created.store(0, std::memory_order_relaxed);
                counter.failed.store(0, std::memory_order_relaxed);
            }

            if (!::Util::JoltMemoryTelemetry::IsEnabled())
                return;

            NC_LOG_INFO("JoltTelemetry : Started capture for '{0}' (maxBodies={1}, numBodyMutexes={2}, maxBodyPairs={3}, maxContactConstraints={4})",
                telemetryMapName,
                Jolt::Settings::maxBodies,
                Jolt::Settings::numBodyMutexes,
                Jolt::Settings::maxBodyPairs,
                Jolt::Settings::maxContactConstraints);
        }

        void RefreshPhysicsTelemetryHighWater()
        {
            if (!::Util::JoltMemoryTelemetry::IsEnabled())
                return;

            u64 numBodies = physicsSystem.GetNumBodies();
            u64 previousPeak = peakNumBodies.load(std::memory_order_relaxed);
            while (numBodies > previousPeak && !peakNumBodies.compare_exchange_weak(previousPeak, numBodies, std::memory_order_relaxed))
            {
            }
        }

        void RecordBodyCreate(JoltBodyTelemetrySource source, bool success)
        {
            if (!::Util::JoltMemoryTelemetry::IsEnabled())
                return;

            JoltBodyTelemetryCounter& counter = bodyTelemetryCounters[static_cast<std::size_t>(source)];
            counter.attempts.fetch_add(1, std::memory_order_relaxed);

            if (success)
            {
                counter.created.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            u64 failed = counter.failed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (failed <= 8 || failed % 1000 == 0)
            {
                NC_LOG_ERROR("JoltTelemetry : CreateBody failed for {0} on '{1}' ({2} failures for this source, {3}/{4} bodies currently allocated)",
                    GetTelemetrySourceName(source),
                    telemetryMapName,
                    failed,
                    physicsSystem.GetNumBodies(),
                    physicsSystem.GetMaxBodies());
            }
        }

        void LogPhysicsTelemetrySummary(const char* reason)
        {
            if (!::Util::JoltMemoryTelemetry::IsEnabled())
                return;

            RefreshPhysicsTelemetryHighWater();

            JPH::PhysicsSystem::BodyStats stats = physicsSystem.GetBodyStats();
            ::Util::JoltMemoryTelemetry::Stats memoryStats = ::Util::JoltMemoryTelemetry::GetStats();
            NC_LOG_INFO("JoltTelemetry : Summary ({0}) map='{1}' bodies={2}/{3} peak={4} static={5} kinematic={6} activeKinematic={7} dynamic={8} activeDynamic={9} soft={10} activeSoft={11} joltHeap={12}MiB peakJoltHeap={13}MiB liveJoltAllocs={14} tempAllocator={15}MiB tempUsage={16}MiB",
                reason,
                telemetryMapName,
                stats.mNumBodies,
                stats.mMaxBodies,
                peakNumBodies.load(std::memory_order_relaxed),
                stats.mNumBodiesStatic,
                stats.mNumBodiesKinematic,
                stats.mNumActiveBodiesKinematic,
                stats.mNumBodiesDynamic,
                stats.mNumActiveBodiesDynamic,
                stats.mNumSoftBodies,
                stats.mNumActiveSoftBodies,
                memoryStats.currentBytes / (1024ull * 1024ull),
                memoryStats.peakBytes / (1024ull * 1024ull),
                memoryStats.liveAllocations,
                allocator.GetSize() / (1024ull * 1024ull),
                allocator.GetUsage() / (1024ull * 1024ull));

            for (std::size_t i = 0; i < static_cast<std::size_t>(JoltBodyTelemetrySource::Count); i++)
            {
                JoltBodyTelemetrySource source = static_cast<JoltBodyTelemetrySource>(i);
                const JoltBodyTelemetryCounter& counter = bodyTelemetryCounters[i];
                u64 attempts = counter.attempts.load(std::memory_order_relaxed);
                u64 created = counter.created.load(std::memory_order_relaxed);
                u64 failed = counter.failed.load(std::memory_order_relaxed);

                if (attempts == 0 && created == 0 && failed == 0)
                    continue;

                NC_LOG_INFO("JoltTelemetry : Source {0}: attempts={1}, created={2}, failed={3}",
                    GetTelemetrySourceName(source),
                    attempts,
                    created,
                    failed);
            }
        }
    };
}
