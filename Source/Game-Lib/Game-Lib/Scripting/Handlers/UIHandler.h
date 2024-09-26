#pragma once
#include "LuaEventHandlerBase.h"
#include "Game-Lib/Scripting/LuaDefines.h"
#include "Game-Lib/Scripting/LuaMethodTable.h"
#include "Game-Lib/Scripting/UI/Panel.h"

#include <Base/Types.h>

namespace Scripting::UI
{
    enum class UIInputEvent : u32
    {
        MouseDown = 1,
        MouseUp = 2,
        MouseHeld = 3,

        HoverBegin = 4,
        HoverEnd = 5,
        HoverHeld = 6,

        FocusBegin = 7,
        FocusEnd = 8,
        FocusHeld = 9,

        Count
    };

    enum class UIKeyboardEvent : u32
    {
        Key = 1,
        Unicode = 2,

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
        static i32 CalculateTextSize(lua_State* state);

        static i32 FocusWidget(lua_State* state);
        static i32 UnfocusWidget(lua_State* state);
        static i32 IsFocusedWidget(lua_State* state);
        static i32 GetFocusedWidget(lua_State* state);

        // Event calls
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget);
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value);
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, f32 value);
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, const vec2& value);

        void CallKeyboardInputEvent(lua_State* state, i32 eventRef, Widget* widget, i32 key, i32 actionMask, i32 modifierMask);
        void CallKeyboardUnicodeEvent(lua_State* state, i32 eventRef, Widget* widget, u32 unicode);

    private:
        void CreateUIInputEventTable(lua_State* state);
    };
}