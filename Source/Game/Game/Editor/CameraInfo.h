#pragma once
#include "BaseEditor.h"

namespace Editor
{
	class CameraInfo : public BaseEditor
	{
	public:
		CameraInfo();

		virtual const char* GetName() override { return "CameraInfo"; }

		virtual void DrawImGui() override;

	private:

	private:
	};
}