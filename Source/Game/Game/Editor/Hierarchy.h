#pragma once
#include "BaseEditor.h"

#include <entt/fwd.hpp>

namespace Editor
{
	class Inspector;

	class Hierarchy : public BaseEditor
	{
	public:
		Hierarchy();
		void SetInspector(Inspector* inspector);

		virtual const char* GetName() override { return "Hierarchy"; }

		virtual void UpdateMode(bool mode) override;
		virtual void DrawImGui() override;

		void SelectEntity(entt::entity entity);

	private:
		void OnDoubleClicked(entt::registry* registry, entt::entity entity);

	private:
		Inspector* _inspector = nullptr;
		entt::entity _selectedEntity;
		bool _scrollToSelected = false;
	};
}