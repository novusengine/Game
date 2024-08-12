#pragma once
#include "LuaHandlerBase.h"
#include "Game-Lib/Scripting/LuaDefines.h"

#include <Base/Types.h>

namespace Scripting
{
	class GlobalHandler : public LuaHandlerBase
	{
	private:
		void Register();
		void Clear() { }

	private: // Registered Functions
		static i32 AddCursor(lua_State* state);
		static i32 SetCursor(lua_State* state);
		static i32 GetCurrentMap(lua_State* state);
		static i32 LoadMap(lua_State* state);

		static i32 PanelCreate(lua_State* state);
		static i32 PanelGetPosition(lua_State* state);
		static i32 PanelGetExtents(lua_State* state);
		static i32 PanelToString(lua_State* state);
		static i32 PanelIndex(lua_State* state);
	};
}