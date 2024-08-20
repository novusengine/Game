#pragma once
#include "LuaEventHandlerBase.h"
#include "Game/Scripting/LuaDefines.h"
#include "Game/Scripting/LuaMethodTable.h"
#include "Game/Scripting/UI/Panel.h"

#include <Base/Types.h>

namespace Scripting::UI
{
    class UIHandler : public LuaHandlerBase
    {
    public:
        void Register(lua_State* state) override;
        void Clear() override;

    public: 
        // Registered Functions
        // Register templates
        static i32 RegisterButtonTemplate(lua_State* state);
        static i32 RegisterPanelTemplate(lua_State* state);
        static i32 RegisterTextTemplate(lua_State* state);
        
        // UI functions
        static i32 GetCanvas(lua_State* state);

        // Utils
        static i32 PixelsToTexCoord(lua_State* state);
    };
}