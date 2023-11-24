#pragma once
#include <Base/Types.h>

struct ImGuiWindow;

namespace Editor
{
    class Viewport;
}

namespace Util
{
	namespace Physics
	{
		bool GetMouseWorldPosition(Editor::Viewport* viewport, vec3& mouseWorldPosition);
	}
}