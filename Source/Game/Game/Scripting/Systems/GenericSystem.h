#pragma once
#include "LuaSystemBase.h"

#include <Base/Types.h>

namespace Scripting
{
	class GenericSystem : public LuaSystemBase
	{
	public:
		GenericSystem(u32 numStates);

		void Prepare(f32 deltaTime);
		void Run(f32 deltaTime, u32 index);
	};
}