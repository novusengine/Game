#include "Game-Lib/ECS/Util/Transforms.h"
#include <Base/Util/DebugHandler.h>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>

// We are using Unitys Right Handed coordinate system
// +X = right
// +Y = up
// +Z = forward
const vec3 ECS::Components::Transform::WORLD_FORWARD = vec3(0.0f, 0.0f, 1.0f);
const vec3 ECS::Components::Transform::WORLD_RIGHT = vec3(1.0f, 0.0f, 0.0f);
const vec3 ECS::Components::Transform::WORLD_UP = vec3(0.0f, 1.0f, 0.0f);

ECS::TransformSystem& ECS::TransformSystem::Get(entt::registry& registry)
{
    ECS::TransformSystem* tf = registry.ctx().find<ECS::TransformSystem>();
    if (tf)
    { 
        return *tf;
    }
    else
    {
        // initialize on demand
        ECS::TransformSystem& s = registry.ctx().emplace<ECS::TransformSystem>();
        s.owner = &registry;
        return s;
    }
}

void ECS::TransformSystem::SetLocalPosition(entt::entity entity, const vec3& newPosition)
{
    ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

    if (tf) SetLocalPosition(entity, *tf, newPosition);
}

void ECS::TransformSystem::SetWorldPosition(entt::entity entity, const vec3& newPosition)
{
    ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);
    if (tf)
    {
        // we have a parent, need to calculate the transform properly
        if (ECS::Components::Transform* parentTransform = tf->GetParentTransform())
        {
            mat4x4 worldMatrix = parentTransform->GetMatrix();
            mat4x4 worldInv = glm::inverse(worldMatrix);
            vec4 localSpace = worldInv * vec4(newPosition, 1.f);
            SetLocalPosition(entity, *tf, vec3(localSpace));
        }
        // no parent or scenenode, so we can just set local position
        else
        {
            SetLocalPosition(entity, *tf, newPosition);
        }
    }
}

void ECS::TransformSystem::SetLocalRotation(entt::entity entity, const quat& newRotation)
{
    ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);
    if (tf) SetLocalRotation(entity, *tf, newRotation);
}

void ECS::TransformSystem::SetWorldRotation(entt::entity entity, const quat& newRotation)
{
    ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);
    if (tf)
    {
        // we have a parent, need to calculate the transform properly
        if (ECS::Components::Transform* parentTransform = tf->GetParentTransform())
        {
            quat parent = parentTransform->GetWorldRotation();
            quat delta = glm::inverse(parent) * newRotation;
            SetLocalRotation(entity, *tf, delta);
        }
        // no parent or scenenode, so we can just set local rotation directly
        else
        {
            SetLocalRotation(entity, *tf, newRotation);
        }
    }
}

void ECS::TransformSystem::SetLocalScale(entt::entity entity, const vec3& newScale)
{
    ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);
    if (tf) SetLocalScale(entity, *tf, newScale);
}

void ECS::TransformSystem::SetLocalPositionAndRotation(entt::entity entity, const vec3& newPosition, const quat& newRotation)
{
    ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

    if (tf) SetLocalPositionAndRotation(entity, *tf, newPosition, newRotation);
}

void ECS::TransformSystem::SetLocalTransform(entt::entity entity, const vec3& newPosition, const quat& newRotation, const vec3& newScale)
{
    ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

    if (tf) SetLocalTransform(entity, *tf, newPosition, newRotation, newScale);
}

void ECS::TransformSystem::SetLocalTransformMatrix(entt::entity entity, const mat4a& transform)
{
    ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

    if (tf) SetLocalTransformMatrix(entity, *tf, transform);
}

void ECS::TransformSystem::AddLocalOffset(entt::entity entity, const vec3& offset)
{
    ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

    if (tf) AddLocalOffset(entity, *tf, offset);
}

void ECS::TransformSystem::RefreshTransform(entt::entity entity, ECS::Components::Transform& transform)
{
    transform.SetDirty(*this, entity);
    if (transform.ownerNode)
    {
        transform.ownerNode->RefreshMatrix();
        transform.ownerNode->PropagateMatrixToChildren(this);
    }
}

void ECS::TransformSystem::SetLocalPosition(entt::entity entity, ECS::Components::Transform& transform, const vec3& newPosition)
{
    if (newPosition != transform.position)
    {
        transform.position = newPosition; 
        RefreshTransform(entity,transform);
    }
}

void ECS::TransformSystem::SetLocalRotation(entt::entity entity, ECS::Components::Transform& transform, const quat& newRotation)
{
    if (newRotation != transform.rotation)
    {
        transform.rotation = newRotation;
        RefreshTransform(entity, transform);
    }
}

void ECS::TransformSystem::SetLocalScale(entt::entity entity, ECS::Components::Transform& transform, const vec3& newScale)
{
    if (newScale != transform.scale)
    {
        transform.scale = newScale;
        RefreshTransform(entity, transform);
    }
}

void ECS::TransformSystem::SetLocalPositionAndRotation(entt::entity entity,
                                                       ECS::Components::Transform& transform,
                                                       const vec3& newPosition,
                                                       const quat& newRotation)
{
    if ((newPosition != transform.position || newRotation != transform.rotation))
    {
        transform.position = newPosition;
        transform.rotation = newRotation;
        RefreshTransform(entity, transform);
    }
}

void ECS::TransformSystem::SetLocalTransform(entt::entity entity,
                                             ECS::Components::Transform& transform,
                                             const vec3& newPosition,
                                             const quat& newRotation,
                                             const vec3& newScale)
{
    if ((newPosition != transform.position || newRotation != transform.rotation || newScale != transform.scale))
    {
        transform.position = newPosition;
        transform.rotation = newRotation;
        transform.scale = newScale;
        RefreshTransform(entity, transform);
    }
}

void ECS::TransformSystem::SetLocalTransformMatrix(entt::entity entity, ECS::Components::Transform& transform, const mat4a& newmatrix)
{
    transform.SetDirty(*this, entity);

    if (transform.ownerNode)
    {
        if (transform.ownerNode->parent)
        {
            transform.ownerNode->matrix = Math::AffineMatrix::MatrixMul(transform.ownerNode->parent->matrix, newmatrix);
        }
        else
        {
            transform.ownerNode->matrix = newmatrix;
        }

        transform.ownerNode->PropagateMatrixToChildren(this);
    }
}

void ECS::TransformSystem::AddLocalOffset(entt::entity entity, ECS::Components::Transform& transform, const vec3& offset)
{
    transform.position += offset;
    RefreshTransform(entity, transform);
}

void ECS::TransformSystem::ParentEntityTo(entt::entity parent, entt::entity child)
{
    ECS::Components::Transform* tfp = owner->try_get<ECS::Components::Transform>(parent);
    ECS::Components::Transform* tfc = owner->try_get<ECS::Components::Transform>(child);

    assert(tfp && tfc);

    if (!tfp || !tfc)
    {
        NC_LOG_ERROR("Transform system, trying to parent entity with no transform!");
    }

    // always emplace the scene-node as we need them for parenting
    ECS::Components::SceneNode& parentNode = owner->get_or_emplace<ECS::Components::SceneNode>(parent, tfp, parent);
    ECS::Components::SceneNode& childNode = owner->get_or_emplace<ECS::Components::SceneNode>(child, tfc, child);

    // its possible the parent gained a scenenode just now, refresh its matrix
    parentNode.RefreshMatrix();

    // parent the child
    childNode.SetParent(&parentNode);

    // set child matrix as dirty, and propagate matrices down in case the child object has its own children
    RefreshTransform(child,*tfc);
}

void ECS::TransformSystem::ClearParent(entt::entity entity)
{
    ECS::Components::Transform* transform = owner->try_get<ECS::Components::Transform>(entity);

    if (!transform)
    {
        NC_LOG_ERROR("Transform system, trying to clear parent from entity with no transform!");
        assert(transform);
    }

    ECS::Components::SceneNode* sceneNode = owner->try_get<ECS::Components::SceneNode>(entity);
    if (!sceneNode)
    {
        NC_LOG_ERROR("Transform system, trying to clear parent from entity with no scene node!");
        assert(sceneNode);
    }

    if (sceneNode->HasParent())
    {
        sceneNode->DetachParent();
        RefreshTransform(entity, *transform);
    }
}

ECS::Components::Transform* ECS::Components::Transform::GetParentTransform() const
{
    if (ownerNode && ownerNode->parent && ownerNode->parent->transform)
    {
        return ownerNode->parent->transform;
    }
    else
    {
        return nullptr;
    }
}