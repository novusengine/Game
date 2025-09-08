#pragma once
#include "Game-Lib/Scripting/UI/Panel.h"

#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

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
    private:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith);

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

    public: 
        // Registered Functions
        // Register templates
        static i32 RegisterPanelTemplate(Zenith* zenith);
        static i32 RegisterTextTemplate(Zenith* zenith);

        static i32 RegisterSendMessageToChatCallback(Zenith* zenith);
        
        // UI functions
        static i32 GetCanvas(Zenith* zenith);
        static i32 GetMousePos(Zenith* zenith);
        static i32 GetTextureSize(Zenith* zenith);

        // Utils
        static i32 PixelsToTexCoord(Zenith* zenith);
        static i32 CalculateTextSize(Zenith* zenith);
        static i32 WrapText(Zenith* zenith);

        static i32 FocusWidget(Zenith* zenith);
        static i32 UnfocusWidget(Zenith* zenith);
        static i32 IsFocusedWidget(Zenith* zenith);
        static i32 WasJustFocusedWidget(Zenith* zenith);
        static i32 GetFocusedWidget(Zenith* zenith);

        static i32 IsHoveredWidget(Zenith* zenith);

        static i32 DestroyWidget(Zenith* zenith);

        // Global input (probably split into InputHandler)
        //i32 AddOnMouseDown(Zenith* zenith);
        //i32 AddOnMouseUp(Zenith* zenith);
        //i32 AddOnMouseHeld(Zenith* zenith);
        //i32 AddOnMouseScroll(Zenith* zenith);

        static i32 AddOnKeyboard(Zenith* zenith);

        // Event calls
        void CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget);
        void CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value);
        void CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value1, const vec2& value2);
        void CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, f32 value);
        void CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, const vec2& value);

        bool CallKeyboardInputEvent(Zenith* zenith, i32 eventRef, Widget* widget, i32 key, i32 actionMask, i32 modifierMask);
        bool CallKeyboardInputEvent(Zenith* zenith, i32 eventRef, i32 key, i32 actionMask, i32 modifierMask);
        bool CallKeyboardUnicodeEvent(Zenith* zenith, i32 eventRef, Widget* widget, u32 unicode);

        void CallSendMessageToChat(Zenith* zenith, i32 eventRef, const std::string& channel, const std::string& playerName, const std::string& text, bool isOutgoing);

    private:
        void CreateUIInputEventTable(Zenith* zenith);
    };

    static LuaRegister<> uiGlobalMethods[] =
    {
        { "RegisterPanelTemplate", UIHandler::RegisterPanelTemplate },
        { "RegisterTextTemplate", UIHandler::RegisterTextTemplate },

        { "RegisterSendMessageToChatCallback", UIHandler::RegisterSendMessageToChatCallback },

        { "GetCanvas", UIHandler::GetCanvas },
        { "GetMousePos", UIHandler::GetMousePos },

        // Utils
        { "GetTextureSize", UIHandler::GetTextureSize },
        { "PixelsToTexCoord", UIHandler::PixelsToTexCoord },
        { "CalculateTextSize", UIHandler::CalculateTextSize },
        { "WrapText", UIHandler::WrapText },

        { "FocusWidget", UIHandler::FocusWidget },
        { "UnfocusWidget", UIHandler::UnfocusWidget },
        { "IsFocusedWidget", UIHandler::IsFocusedWidget },
        { "WasJustFocusedWidget", UIHandler::WasJustFocusedWidget },
        { "GetFocusedWidget", UIHandler::GetFocusedWidget },

        { "IsHoveredWidget", UIHandler::IsHoveredWidget },

        { "DestroyWidget", UIHandler::DestroyWidget },

        // Global input functions
        { "AddOnKeyboard", UIHandler::AddOnKeyboard }
    };
}