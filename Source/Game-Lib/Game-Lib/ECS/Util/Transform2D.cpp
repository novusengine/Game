#include "Game-Lib/ECS/Util/Transform2D.h"

#include <Base/Util/DebugHandler.h>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>

// We are using Unitys Right Handed coordinate system
// +X = right
// +Y = up
const vec2 ECS::Components::Transform2D::WORLD_RIGHT = vec2(1.0f, 0.0f);
const vec2 ECS::Components::Transform2D::WORLD_UP = vec2(0.0f, 1.0f);

ECS::Transform2DSystem& ECS::Transform2DSystem::Get(entt::registry& registry)
{
    ECS::Transform2DSystem* tf = registry.ctx().find<ECS::Transform2DSystem>();
    if (tf)
    { 
        return *tf;
    }
    else
    {
        // initialize on demand
        ECS::Transform2DSystem& s = registry.ctx().emplace<ECS::Transform2DSystem>();
        s.owner = &registry;
        return s;
    }
}

void ECS::Transform2DSystem::Clear()
{
    TransformQueueItem temp;
    while (elements.try_dequeue(temp))
    {
        // Just emptying the queue
    }
}

void ECS::Transform2DSystem::SetLocalPosition(entt::entity entity, const vec2& newPosition)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);

    if (tf) SetLocalPosition(entity, *tf, newPosition);
}

void ECS::Transform2DSystem::SetWorldPosition(entt::entity entity, const vec2& newPosition)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);
    if (tf)
    {
        // we have a parent, need to calculate the transform properly
        if (ECS::Components::Transform2D* parentTransform = tf->GetParentTransform())
        {
            mat4x4 worldMatrix = parentTransform->GetMatrix();
            mat4x4 worldInv = glm::inverse(worldMatrix);
            vec4 localSpace = worldInv * vec4(newPosition, 0.0f, 1.f);
            SetLocalPosition(entity, *tf, vec2(localSpace));
        }
        // no parent or scenenode, so we can just set local position
        else
        {
            SetLocalPosition(entity, *tf, newPosition);
        }
    }
}

void ECS::Transform2DSystem::SetLocalRotation(entt::entity entity, const quat& newRotation)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);
    if (tf) SetLocalRotation(entity, *tf, newRotation);
}

void ECS::Transform2DSystem::SetWorldRotation(entt::entity entity, const quat& newRotation)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);
    if (tf)
    {
        // we have a parent, need to calculate the transform properly
        if (ECS::Components::Transform2D* parentTransform = tf->GetParentTransform())
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

void ECS::Transform2DSystem::SetLocalScale(entt::entity entity, const vec2& newScale)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);
    if (tf) SetLocalScale(entity, *tf, newScale);
}

void ECS::Transform2DSystem::SetLocalPositionAndRotation(entt::entity entity, const vec2& newPosition, const quat& newRotation)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);

    if (tf) SetLocalPositionAndRotation(entity, *tf, newPosition, newRotation);
}

void ECS::Transform2DSystem::SetLocalTransform(entt::entity entity, const vec2& newPosition, const quat& newRotation, const vec2& newScale)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);

    if (tf) SetLocalTransform(entity, *tf, newPosition, newRotation, newScale);
}

void ECS::Transform2DSystem::AddLocalOffset(entt::entity entity, const vec2& offset)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);

    if (tf) AddLocalOffset(entity, *tf, offset);
}

void ECS::Transform2DSystem::SetLayer(entt::entity entity, u32 newLayer)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);
    if (tf) SetLayer(entity, *tf, newLayer);
}

void ECS::Transform2DSystem::SetSize(entt::entity entity, const vec2& newSize)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);

    if (tf) SetSize(entity, *tf, newSize);
}

void ECS::Transform2DSystem::SetAnchor(entt::entity entity, const vec2& newAnchor)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);

    if (tf) SetAnchor(entity, *tf, newAnchor);
}

void ECS::Transform2DSystem::SetRelativePoint(entt::entity entity, const vec2& newRelativePoint)
{
    ECS::Components::Transform2D* tf = owner->try_get<ECS::Components::Transform2D>(entity);

    if (tf) SetRelativePoint(entity, *tf, newRelativePoint);
}

void ECS::Transform2DSystem::RefreshTransform(entt::entity entity, ECS::Components::Transform2D& transform)
{
    transform.SetDirty(*this, entity);
    if (transform.ownerNode)
    {
        transform.ownerNode->RefreshMatrix();
        transform.ownerNode->PropagateMatrixToChildren(this);
    }
}

void ECS::Transform2DSystem::SetLocalPosition(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newPosition)
{
    if (newPosition != transform.position)
    {
        transform.position = newPosition; 
        RefreshTransform(entity,transform);
    }
}

void ECS::Transform2DSystem::SetLocalRotation(entt::entity entity, ECS::Components::Transform2D& transform, const quat& newRotation)
{
    if (newRotation != transform.rotation)
    {
        transform.rotation = newRotation;
        RefreshTransform(entity, transform);
    }
}

void ECS::Transform2DSystem::SetLocalScale(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newScale)
{
    if (newScale != transform.scale)
    {
        transform.scale = newScale;
        RefreshTransform(entity, transform);
    }
}

void ECS::Transform2DSystem::SetLocalPositionAndRotation(entt::entity entity,
                                                       ECS::Components::Transform2D& transform,
                                                       const vec2& newPosition,
                                                       const quat& newRotation)
{
    if ((newPosition != transform.position || newRotation != transform.rotation))
    {
        transform.position = newPosition;
        transform.rotation = newRotation;
        RefreshTransform(entity, transform);
    }
}

void ECS::Transform2DSystem::SetLocalTransform(entt::entity entity,
                                             ECS::Components::Transform2D& transform,
                                             const vec2& newPosition,
                                             const quat& newRotation,
                                             const vec2& newScale)
{
    if ((newPosition != transform.position || newRotation != transform.rotation || newScale != transform.scale))
    {
        transform.position = newPosition;
        transform.rotation = newRotation;
        transform.scale = newScale;
        RefreshTransform(entity, transform);
    }
}

void ECS::Transform2DSystem::AddLocalOffset(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& offset)
{
    transform.position += offset;
    RefreshTransform(entity, transform);
}

void ECS::Transform2DSystem::SetLayer(entt::entity entity, ECS::Components::Transform2D& transform, u32 newLayer)
{
    if (newLayer != transform.layer)
    {
        transform.layer = newLayer;
        RefreshTransform(entity, transform);
    }
}

void ECS::Transform2DSystem::SetSize(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newSize)
{
    if (newSize != transform.size)
    {
        transform.size = newSize;
        RefreshTransform(entity, transform);
    }
}

void ECS::Transform2DSystem::SetAnchor(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newAnchor)
{
    if (newAnchor != transform.anchor)
    {
        transform.anchor = newAnchor;
        RefreshTransform(entity, transform);
    }
}

void ECS::Transform2DSystem::SetRelativePoint(entt::entity entity, ECS::Components::Transform2D& transform, const vec2& newRelativePoint)
{
    if (newRelativePoint != transform.relativePoint)
    {
        transform.relativePoint = newRelativePoint;
        RefreshTransform(entity, transform);
    }
}

void ECS::Transform2DSystem::ParentEntityTo(entt::entity parent, entt::entity child)
{
    ECS::Components::Transform2D* tfp = owner->try_get<ECS::Components::Transform2D>(parent);
    ECS::Components::Transform2D* tfc = owner->try_get<ECS::Components::Transform2D>(child);

    assert(tfp && tfc);

    if (!tfp || !tfc)
    {
        NC_LOG_ERROR("Transform system, trying to parent entity with no transform!");
    }

    // always emplace the scene-node as we need them for parenting
    ECS::Components::SceneNode2D& parentNode = owner->get_or_emplace<ECS::Components::SceneNode2D>(parent, tfp, parent);
    ECS::Components::SceneNode2D& childNode = owner->get_or_emplace<ECS::Components::SceneNode2D>(child, tfc, child);

    // its possible the parent gained a scenenode just now, refresh its matrix
    parentNode.RefreshMatrix();

    // parent the child
    childNode.SetParent(&parentNode);

    // set child matrix as dirty, and propagate matrices down in case the child object has its own children
    RefreshTransform(child,*tfc);
}

void ECS::Transform2DSystem::ClearParent(entt::entity entity)
{
    ECS::Components::Transform2D* transform = owner->try_get<ECS::Components::Transform2D>(entity);

    if (!transform)
    {
        NC_LOG_ERROR("Transform system, trying to clear parent from entity with no transform!");
        assert(transform);
    }

    ECS::Components::SceneNode2D* sceneNode = owner->try_get<ECS::Components::SceneNode2D>(entity);
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

ECS::Components::Transform2D* ECS::Components::Transform2D::GetParentTransform() const
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

u32 ECS::Components::Transform2D::GetHierarchyDepth() const
{
    // TODO: Do this while parenting...

    if (ownerNode)
    {
        u32 depth = 0;

        SceneNode2D* parent = ownerNode->GetParent();
        while (parent)
        {
            depth++;
            parent = parent->GetParent();
        }

        return depth;
    }

    return 0;
}
