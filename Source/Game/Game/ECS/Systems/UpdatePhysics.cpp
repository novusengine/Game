#include "UpdatePhysics.h"

#include <Game/ECS/Components/DebugRenderTransform.h>
#include "Game/ECS/Components/DynamicMesh.h"
#include "Game/ECS/Components/KinematicMesh.h"
#include "Game/ECS/Components/StaticMesh.h"
#include "Game/ECS/Singletons/JoltState.h"
#include "Game/ECS/Singletons/ActiveCamera.h"
#include "Game/ECS/Util/Transforms.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Debug/DebugRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/CVarSystem/CVarSystem.h>
#include <Base/Util/DebugHandler.h>

#include <Input/KeybindGroup.h>
#include <Input/InputManager.h>

#include <entt/entt.hpp>

#include <GLFW/glfw3.h>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

using namespace JPH::literals;

AutoCVar_Int CVAR_PhysicsEnabled("physics.enabled", "enables the physics engine", 0, CVarFlags::EditCheckbox);
AutoCVar_Int CVAR_PhysicsOptimizeBP("physics.optimizeBP", "enables automatically optimizing the broadphase during load", 1, CVarFlags::EditCheckbox);

namespace ECS::Systems
{
    void OnStaticMeshCreated(entt::registry& registry, entt::entity entity)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& joltState = ctx.get<Singletons::JoltState>();
        JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

        // Create Body
        {
            auto& transform = registry.get<ECS::Components::Transform>(entity);

            JPH::BoxShapeSettings shapeSetting(JPH::Vec3(transform.GetLocalScale().x * 0.5f, transform.GetLocalScale().y * 0.5f, transform.GetLocalScale().z * 0.5f));

            // Create the shape
            JPH::ShapeSettings::ShapeResult shapeResult = shapeSetting.Create();
            JPH::ShapeRefC shape = shapeResult.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

            // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
            JPH::BodyCreationSettings bodySettings(shape, JPH::RVec3(transform.GetWorldPosition().x, transform.GetWorldPosition().y, transform.GetWorldPosition().z), JPH::Quat::sIdentity(), JPH::EMotionType::Static, Jolt::Layers::NON_MOVING);

            // Create the actual rigid body
            JPH::Body* body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr

            body->SetUserData(JPH::uint64(entt::to_integral(entity)));

            // Add it to the world
            JPH::BodyID bodyID = body->GetID();
            bodyInterface.AddBody(bodyID, JPH::EActivation::DontActivate);
        }
    }

    void OnKinematicMeshCreated(entt::registry& registry, entt::entity entity)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& joltState = ctx.get<Singletons::JoltState>();
        JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

        // Create Body
        {
            auto& transform = registry.get<ECS::Components::Transform>(entity);

            JPH::BoxShapeSettings shapeSetting(JPH::Vec3(transform.GetLocalScale().x * 0.5f, transform.GetLocalScale().y * 0.5f, transform.GetLocalScale().z * 0.5f));

            // Create the shape
            JPH::ShapeSettings::ShapeResult shapeResult = shapeSetting.Create();
            JPH::ShapeRefC shape = shapeResult.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

            // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
            JPH::BodyCreationSettings bodySettings(shape, JPH::RVec3(transform.GetWorldPosition().x, transform.GetWorldPosition().y, transform.GetWorldPosition().z), JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, Jolt::Layers::MOVING);

            // Create the actual rigid body
            JPH::Body* body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr

            body->SetUserData(JPH::uint64(entt::to_integral(entity)));

            // Add it to the world
            JPH::BodyID bodyID = body->GetID();
            bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);
        }
    }

    void OnDynamicMeshCreated(entt::registry& registry, entt::entity entity)
    {
        auto& activeCamera = registry.ctx().get<ECS::Singletons::ActiveCamera>();

        if (activeCamera.entity == entt::null)
            return;

        auto& cameraTransform = registry.get<ECS::Components::Transform>(activeCamera.entity);

        entt::registry::context& ctx = registry.ctx();
        auto& joltState = ctx.get<Singletons::JoltState>();
        JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

        // Create Body
        {
            auto& transform = registry.get<ECS::Components::Transform>(entity);

            JPH::BoxShapeSettings shapeSetting(JPH::Vec3(transform.GetLocalScale().x * 0.5f, transform.GetLocalScale().y * 0.5f, transform.GetLocalScale().z * 0.5f));

            // Create the shape
            JPH::ShapeSettings::ShapeResult shapeResult = shapeSetting.Create();
            JPH::ShapeRefC shape = shapeResult.Get(); // We don't expect an error here, but you can check floor_shape_result for HasError() / GetError()

            // Create the settings for the body itself. Note that here you can also set other properties like the restitution / friction.
            JPH::BodyCreationSettings bodySettings(shape, JPH::RVec3(transform.GetWorldPosition().x, transform.GetWorldPosition().y, transform.GetWorldPosition().z), JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, Jolt::Layers::MOVING);

            // Create the actual rigid body
            JPH::Body* body = bodyInterface.CreateBody(bodySettings); // Note that if we run out of bodies this can return nullptr
            body->GetMotionProperties()->SetAngularDamping(0.8f);

            body->SetUserData(JPH::uint64(entt::to_integral(entity)));

            // Add it to the world
            JPH::BodyID bodyID = body->GetID();
            bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);
        }
    }

    void UpdatePhysics::Init(entt::registry& registry)
    {
        entt::registry::context& ctx = registry.ctx();

        // We must initialize Jolt before creating the JoltState Singleton as it depends on Jolt
        auto& joltState = ctx.emplace<Singletons::JoltState>();
        
        joltState.physicsSystem.Init(Jolt::Settings::maxBodies, Jolt::Settings::numBodyMutexes, Jolt::Settings::maxBodyPairs, Jolt::Settings::maxContactConstraints, joltState.broadPhaseLayerInterface, joltState.objectVSBroadPhaseLayerFilter, joltState.objectVSObjectLayerFilter);
        joltState.physicsSystem.SetBodyActivationListener(&joltState.bodyActivationListener);
        joltState.physicsSystem.SetContactListener(&joltState.contactListener);

        // Setup StaticMesh Sink
        {
            auto sink = registry.on_construct<Components::StaticMesh>();
            sink.connect<&OnStaticMeshCreated>();
        }

        // Setup KinematicMesh Sink
        {
            auto sink = registry.on_construct<Components::KinematicMesh>();
            sink.connect<&OnKinematicMeshCreated>();
        }

        // Setup DynamicMesh Sink
        {
            auto sink = registry.on_construct<Components::DynamicMesh>();
            sink.connect<&OnDynamicMeshCreated>();
        }

        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->GetKeybindGroupByHash("Debug"_h);
        keybindGroup->AddKeyboardCallback("Spawn Physics OBB", GLFW_KEY_G, KeybindAction::Press, KeybindModifier::ModNone, [&](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->gameRegistry;
            auto& activeCamera = registry->ctx().get<ECS::Singletons::ActiveCamera>();
            auto& tSystem = ECS::TransformSystem::Get(*registry);

            if (activeCamera.entity == entt::null)
            {
                DebugHandler::PrintError("[Keybind:Debug] ActiveCamera::entity not set!");
                return false;
            }

            auto& cameraTransform = registry->get<ECS::Components::Transform>(activeCamera.entity);

            entt::entity entity = registry->create();

            auto& transform = registry->emplace<ECS::Components::Transform>(entity);

            tSystem.SetLocalPosition(entity, cameraTransform.GetLocalPosition());
            tSystem.SetLocalScale(entity, vec3(1.0f, 1.0f, 1.0f));

            auto& debugRenderTransform = registry->emplace<ECS::Components::DebugRenderTransform>(entity);
            debugRenderTransform.color = Color::Magenta;

            registry->emplace<ECS::Components::DynamicMesh>(entity);

            return true;
        });
    }

    void UpdatePhysics::Update(entt::registry& registry, f32 deltaTime)
    {
        entt::registry::context& ctx = registry.ctx();
        auto& joltState = ctx.get<Singletons::JoltState>();
        auto& tSystem = TransformSystem::Get(registry);

        GameRenderer* gameRenderer = ServiceLocator::GetGameRenderer();
        DebugRenderer* debugRenderer = gameRenderer->GetDebugRenderer();

        // Step the world
        {
            constexpr f32 minTimePerStep = 1 / 60.0f;
            i32 collisionSteps = static_cast<i32>(glm::ceil(deltaTime / minTimePerStep));
            collisionSteps = glm::clamp(collisionSteps, 1, 4);

            joltState.physicsSystem.Update(deltaTime, collisionSteps, &joltState.allocator, &joltState.scheduler);
        }

        // Update ECS with new Physics State
        {
            JPH::BodyIDVector activeBodyIDs;
            joltState.physicsSystem.GetActiveBodies(JPH::EBodyType::RigidBody, activeBodyIDs);

            if (!activeBodyIDs.empty())
            {
                JPH::BodyInterface& bodyInterface = joltState.physicsSystem.GetBodyInterface();

                for (auto bodyID : activeBodyIDs)
                {
                    u32 userData = static_cast<u32>(bodyInterface.GetUserData(bodyID));
                    auto entityID = static_cast<entt::entity>(userData);

                    bool needsPhysicsWriteToECS = registry.any_of<Components::Transform>(entityID);
                    if (needsPhysicsWriteToECS)
                    {
                        auto& transform = registry.get<ECS::Components::Transform>(entityID);

                        JPH::Vec3 bodyPos{};
                        JPH::Quat bodyRot{};
                        bodyInterface.GetPositionAndRotation(bodyID, bodyPos, bodyRot);

                        vec3 newPosition = vec3(bodyPos.GetX(), bodyPos.GetY(), bodyPos.GetZ());
                        quat newRotation = glm::quat(bodyRot.GetW(), bodyRot.GetX(), bodyRot.GetY(), bodyRot.GetZ());

                        tSystem.SetLocalPositionAndRotation(entityID, newPosition, newRotation);
                    }
                }
            }
        }
    }
}
