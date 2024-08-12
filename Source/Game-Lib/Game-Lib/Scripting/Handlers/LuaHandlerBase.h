#pragma once
#include <Base/Types.h>

struct lua_State;

namespace Scripting
{
	class LuaHandlerBase
	{
	public:
		virtual void Register() = 0;
		virtual void Clear() = 0;
	};
}