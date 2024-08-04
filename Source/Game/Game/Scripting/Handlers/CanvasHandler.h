#pragma once
#include "LuaEventHandlerBase.h"
#include "Game/Scripting/LuaDefines.h"

#include <Base/Types.h>

namespace Scripting
{
	struct Canvas;
	struct Panel;
	struct Text;

	class CanvasHandler : public LuaHandlerBase
	{
	private:
		void Register();
		void Clear() { }

	private: 
		// Registered Functions
		// Register templates
		static i32 RegisterButtonTemplate(lua_State* state);
		static i32 RegisterPanelTemplate(lua_State* state);
		static i32 RegisterTextTemplate(lua_State* state);
		
		// UI functions
		static i32 GetCanvas(lua_State* state);

		// Canvas functions
		static i32 CanvasIndex(lua_State* state);
		static i32 CreatePanel(lua_State* state);
		static i32 CreateText(lua_State* state);
	};
}