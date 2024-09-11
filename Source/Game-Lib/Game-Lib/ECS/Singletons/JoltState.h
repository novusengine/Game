#pragma once
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/JoltState.h"
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

#include <thread>

namespace Jolt
{
    namespace Settings
    {
        static constexpr u32 maxBodies = 65536 * 100;
        static constexpr u32 numBodyMutexes = 0;
        static constexpr u32 maxBodyPairs = 65536 * 100;
        static constexpr u32 maxContactConstraints = 10240 * 10;
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
    struct JoltState
    {
    public:
        JoltState() : allocator(1000u * 1024u * 1024u), scheduler(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1) { }
        
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
    };
}
