

#include "Game/ECS/Util/Transforms.h"
#include <entt/entt.hpp>
#include "Base/Util/DebugHandler.h"

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
	if (tf) return *tf;
	else {

		//initialize on demand
		ECS::TransformSystem& s = registry.ctx().emplace<ECS::TransformSystem>();
		s.owner = &registry;
		return s;
	}
}

void ECS::TransformSystem::SetPosition(entt::entity entity, const vec3& newPosition)
{
	ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

	if (tf) {
		SetPosition(entity, *tf, newPosition);
	}
}

void ECS::TransformSystem::SetRotation(entt::entity entity, const quat& newRotation)
{
	ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

	if (tf) {
		SetRotation(entity, *tf, newRotation);
	}
}

void ECS::TransformSystem::SetScale(entt::entity entity, const vec3& newScale)
{
	ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

	if (tf) {
		SetScale(entity, *tf, newScale);
	}
}

void ECS::TransformSystem::SetPositionAndRotation(entt::entity entity, const vec3& newPosition, const quat& newRotation)
{
	ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

	if (tf) {
		SetPositionAndRotation(entity, *tf, newPosition, newRotation);
	}
}

void ECS::TransformSystem::SetComponents(entt::entity entity, const vec3& newPosition, const quat& newRotation, const vec3& newScale)
{
	ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

	if (tf) {
		SetComponents(entity, *tf, newPosition, newRotation, newScale);
	}
}

void ECS::TransformSystem::AddOffset(entt::entity entity, const vec3& offset)
{
	ECS::Components::Transform* tf = owner->try_get<ECS::Components::Transform>(entity);

	if (tf) {
		AddOffset(entity, *tf, offset);
	}
}

void ECS::TransformSystem::SetPosition(entt::entity entity, ECS::Components::Transform& transform, const vec3& newPosition)
{
	if (newPosition != transform.position) {
		transform.position = newPosition;
		transform.SetDirty(*this, entity);
		if (transform.ownerNode) {
			transform.ownerNode->refresh_matrix();
			transform.ownerNode->propagate_matrix(this);
		}
	}
}

void ECS::TransformSystem::SetRotation(entt::entity entity, ECS::Components::Transform& transform, const quat& newRotation)
{

	if (newRotation != transform.rotation) {
		transform.rotation = newRotation;
		transform.SetDirty(*this, entity);
		if (transform.ownerNode) {
			transform.ownerNode->refresh_matrix();
			transform.ownerNode->propagate_matrix(this);
		}
	}
}

void ECS::TransformSystem::SetScale(entt::entity entity, ECS::Components::Transform& transform, const vec3& newScale)
{

	if (newScale != transform.scale) {
		transform.scale = newScale;
		transform.SetDirty(*this, entity);
		if (transform.ownerNode) {
			transform.ownerNode->refresh_matrix();
			transform.ownerNode->propagate_matrix(this);
		}
	}
}

void ECS::TransformSystem::SetPositionAndRotation(entt::entity entity, ECS::Components::Transform& transform, const vec3& newPosition, const quat& newRotation)
{

	if ((newPosition != transform.position || newRotation != transform.rotation)) {
		transform.position = newPosition;
		transform.rotation = newRotation;
		transform.SetDirty(*this, entity);
		if (transform.ownerNode) {
			transform.ownerNode->refresh_matrix();
			transform.ownerNode->propagate_matrix(this);
		}
	}
}

void ECS::TransformSystem::SetComponents(entt::entity entity, ECS::Components::Transform& transform, const vec3& newPosition, const quat& newRotation, const vec3& newScale)
{

	if ((newPosition != transform.position || newRotation != transform.rotation || newScale != transform.scale)) {
		transform.position = newPosition;
		transform.rotation = newRotation;
		transform.scale = newScale;
		transform.SetDirty(*this, entity);
		if (transform.ownerNode) {
			transform.ownerNode->refresh_matrix();
			transform.ownerNode->propagate_matrix(this);
		}
	}
}

void ECS::TransformSystem::AddOffset(entt::entity entity, ECS::Components::Transform& transform, const vec3& offset)
{
	transform.position += offset;
	transform.SetDirty(*this, entity);
	if (transform.ownerNode) {
		transform.ownerNode->refresh_matrix();
		transform.ownerNode->propagate_matrix(this);
	}
}

void ECS::TransformSystem::ParentEntityTo(entt::entity parent, entt::entity child)
{
	ECS::Components::Transform* tfp = owner->try_get<ECS::Components::Transform>(parent);
	ECS::Components::Transform* tfc = owner->try_get<ECS::Components::Transform>(child);

	assert(tfp && tfc);

	if (!tfp || !tfc) {
		DebugHandler::PrintError("Transform system, trying to parent entity with no transform!");
	}


	//always emplace the scene-node as we need them for parenting
	ECS::Components::SceneNode& parentNode = owner->get_or_emplace<ECS::Components::SceneNode>(parent, tfp, parent);
	ECS::Components::SceneNode& childNode = owner->get_or_emplace<ECS::Components::SceneNode>(child, tfc, child);

	//its possible the parent gained a scenenode just now, refresh its matrix
	parentNode.refresh_matrix();

	//parent the child
	childNode.setParent(&parentNode);

	//set child matrix as dirty, and propagate matrices down in case the child object has its own children
	tfc->SetDirty(*this, child);
	childNode.refresh_matrix();
	childNode.propagate_matrix(this);
}
