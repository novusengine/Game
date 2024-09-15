#pragma once
#include "LuaEventHandlerBase.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"
#include "Game-Lib/Scripting/UI/Panel.h"

#include <Base/Types.h>

namespace Scripting::UI
{
    enum class UIInputEvents : u32
    {
        MouseDown = 0,
        MouseUp = 1,
        MouseHeld = 2,

        HoverBegin = 3,
        HoverEnd = 4,
        Hover = 5,

        Count
    };

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

        // Event calls
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvents inputEvent, Widget* widget);
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvents inputEvent, Widget* widget, i32 value);
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvents inputEvent, Widget* widget, const vec2& value);

    private:
        void CreateUIInputEventTable(lua_State* state);
    };
}