#pragma once
#include "BaseEditor.h"

class CVarParameter;

namespace Editor
{
	class CVarEditor : public BaseEditor
	{
	public:
		CVarEditor();

		virtual const char* GetName() override { return "CVars"; }

		virtual void DrawImGuiSubMenuBar() override;
		virtual void DrawImGui() override;

	private:
		void EditParameter(CVarParameter* p, float textWidth);

	private:
		std::vector<CVarParameter*> cachedEditParameters;
	};
}