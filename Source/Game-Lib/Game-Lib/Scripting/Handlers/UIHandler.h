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
        MouseScroll = 4,

        HoverBegin = 5,
        HoverEnd = 6,
        HoverHeld = 7,

        FocusBegin = 8,
        FocusEnd = 9,
        FocusHeld = 10,

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
        static i32 RegisterPanelTemplate(lua_State* state);
        static i32 RegisterTextTemplate(lua_State* state);
        
        // UI functions
        static i32 GetCanvas(lua_State* state);
        static i32 GetMousePos(lua_State* state);
        static i32 GetTextureSize(lua_State* state);

        // Utils
        static i32 PixelsToTexCoord(lua_State* state);
        static i32 CalculateTextSize(lua_State* state);
        static i32 WrapText(lua_State* state);

        static i32 FocusWidget(lua_State* state);
        static i32 UnfocusWidget(lua_State* state);
        static i32 IsFocusedWidget(lua_State* state);
        static i32 WasJustFocusedWidget(lua_State* state);
        static i32 GetFocusedWidget(lua_State* state);

        static i32 IsHoveredWidget(lua_State* state);

        static i32 DestroyWidget(lua_State* state);

        // Global input (probably split into InputHandler)
        //i32 AddOnMouseDown(lua_State* state);
        //i32 AddOnMouseUp(lua_State* state);
        //i32 AddOnMouseHeld(lua_State* state);
        //i32 AddOnMouseScroll(lua_State* state);

        static i32 AddOnKeyboard(lua_State* state);

        // Event calls
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget);
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value);
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value1, const vec2& value2);
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, f32 value);
        void CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, const vec2& value);

        bool CallKeyboardInputEvent(lua_State* state, i32 eventRef, Widget* widget, i32 key, i32 actionMask, i32 modifierMask);
        bool CallKeyboardInputEvent(lua_State* state, i32 eventRef, i32 key, i32 actionMask, i32 modifierMask);
        bool CallKeyboardUnicodeEvent(lua_State* state, i32 eventRef, Widget* widget, u32 unicode);

    private:
        void CreateUIInputEventTable(lua_State* state);
    };
}