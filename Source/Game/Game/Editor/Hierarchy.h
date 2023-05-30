#pragma once
#include "BaseEditor.h"

namespace Editor
{
	class Inspector;

	class Hierarchy : public BaseEditor
	{
	public:
		Hierarchy();
		void SetInspector(Inspector* inspector);

		virtual const char* GetName() override { return "Hierarchy"; }

		virtual void DrawImGui() override;

		void SelectEntity(entt::entity entity);

	private:
		void OnDoubleClicked(entt::registry* registry, entt::entity entity);

	private:
		Inspector* _inspector = nullptr;
		entt::entity _selectedEntity = entt::null;
		bool _scrollToSelected = false;
	};
}