#pragma once
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Types.h>
#include <Base/Util/DebugHandler.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <jolt/physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#include <entt/entt.hpp>

#include <thread>

namespace Jolt
{
	namespace Settings
	{
		static constexpr u32 maxBodies = 4096;
		static constexpr u32 numBodyMutexes = 0;
		static constexpr u32 maxBodyPairs = 4096;
		static constexpr u32 maxContactConstraints = 16384;
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
			//DebugHandler::Print("Contact Validate Callback");

			// Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
			return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
		}

		virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
		{
			//DebugHandler::Print("A contact was added");
		}

		virtual void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
		{
			//DebugHandler::Print("A contact was persisted");
		}

		virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
		{
			//DebugHandler::Print("A contact was removed");
		}
	};

	class MyBodyActivationListener : public JPH::BodyActivationListener
	{
	public:
		virtual void OnBodyActivated(const JPH::BodyID& inBodyID, u64 inBodyUserData) override
		{
			//DebugHandler::Print("A body got activated");
		}

		virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, u64 inBodyUserData) override
		{
			//DebugHandler::Print("A body went to sleep");
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

		JPH::BodyID floorID;
	};
}