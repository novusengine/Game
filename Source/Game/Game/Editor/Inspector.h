#pragma once
#include "BaseEditor.h"
#include <Base/Math/Geometry.h>
#include <Renderer/Descriptors/TextureDesc.h>
#include <entt/entt.hpp>

#include <Input/InputManager.h>

class DebugRenderer;

namespace ECS::Components
{
	struct Transform;
}

namespace Editor
{
	class Viewport;
	class Hierarchy;

	enum QueryObjectType
	{
		None = 0,
		Terrain,
		ModelOpaque,
		ModelTransparent
	};

	class Inspector : public BaseEditor
	{
	public:
		Inspector();
		void SetViewport(Viewport* viewport);
		void SetHierarchy(Hierarchy* hierarchy);

		virtual const char* GetName() override { return "Inspector"; }

		virtual void Update(f32 deltaTime) override;
		virtual void DrawImGui() override;

		void SelectEntity(entt::entity entity);
		void ClearSelection();
		void DirtySelection();

		bool OnMouseClickLeft(i32 key, KeybindAction action, KeybindModifier modifier);

	private:
		void SelectModel(u32 instanceID);

		void InspectEntity(entt::entity entity);
		void InspectEntityTransforms(entt::entity entity);
		bool DrawGizmo(entt::registry* registry, entt::entity entity, ECS::Components::Transform& transform);
		void DrawGizmoControls();

	private:
		Viewport* _viewport = nullptr;
		Hierarchy* _hierarchy = nullptr;

		u32 _activeToken = 0;
		u32 _queriedToken = 0;

		entt::entity _selectedEntity = entt::null;

		u32 _operation = 7; // ImGuizmo::OPERATION::TRANSLATE
		u32 _mode = 1; // ImGuizmo::MODE::WORLD
	};
}
