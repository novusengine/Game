#pragma once
#include "Game/Scripting/LuaDefines.h"

#include <Base/Types.h>
#include <Base/Container/ConcurrentQueue.h>

#include <vector>

namespace Scripting
{
	class LuaSystemBase
	{
	public:
		LuaSystemBase(u32 numStates);

	private:
		friend LuaManager;

		void Init(u32 numStates);
		void Update(f32 deltaTime);
		void PushEvent(LuaSystemEvent systemEvent);

	protected:
		virtual void Prepare(f32 deltaTime) = 0;
		virtual void Run(f32 deltaTime, u32 index) = 0;

		lua_State* CreateState();
		bool DestroyState(u32 index);

		std::vector<lua_State*> _states;
		moodycamel::ConcurrentQueue<LuaSystemEvent> _events;
	};
}