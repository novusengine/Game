#pragma once
#include "Game-Lib/Scripting/UI/Widget.h"

#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::UI
{
    struct Text : public Widget
    {
    public:
        static void Register(Zenith* zenith);
    };

    namespace TextMethods
    {
        i32 GetText(Zenith* zenith, Text* text);
        i32 SetText(Zenith* zenith, Text* text);
        i32 GetRawText(Zenith* zenith, Text* text);
        i32 GetSize(Zenith* zenith, Text* text);
        i32 GetFontSize(Zenith* zenith, Text* text);
        i32 SetFontSize(Zenith* zenith, Text* text);
        i32 GetWidth(Zenith* zenith, Text* text);
        i32 GetHeight(Zenith* zenith, Text* text);
        i32 GetColor(Zenith* zenith, Text* text);
        i32 SetColor(Zenith* zenith, Text* text);
        i32 SetAlpha(Zenith* zenith, Text* text);
        i32 GetWrapWidth(Zenith* zenith, Text* text);
        i32 SetWrapWidth(Zenith* zenith, Text* text);
        i32 GetWrapIndent(Zenith* zenith, Text* text);
        i32 SetWrapIndent(Zenith* zenith, Text* text);
    };

    static LuaRegister<Text> textMethods[] =
    {
        { "GetText", TextMethods::GetText },
        { "SetText", TextMethods::SetText },
        { "GetRawText", TextMethods::GetRawText },
        { "GetSize", TextMethods::GetSize },
        { "GetFontSize", TextMethods::GetFontSize },
        { "SetFontSize", TextMethods::SetFontSize },
        { "GetWidth", TextMethods::GetWidth },
        { "GetHeight", TextMethods::GetHeight },
        { "GetColor", TextMethods::GetColor },
        { "SetColor", TextMethods::SetColor },
        { "SetAlpha", TextMethods::SetAlpha },
        { "GetWrapWidth", TextMethods::GetWrapWidth },
        { "SetWrapWidth", TextMethods::SetWrapWidth },
        { "GetWrapIndent", TextMethods::GetWrapIndent },
        { "SetWrapIndent", TextMethods::SetWrapIndent }
    };
}