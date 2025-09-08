#pragma once
#include "Game-Lib/Scripting/UI/Widget.h"

#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::UI
{
    struct Panel : public Widget
    {
    public:
        static void Register(Zenith* zenith);
    };

    namespace PanelMethods
    {
        i32 GetSize(Zenith* zenith, Panel* panel);
        i32 GetWidth(Zenith* zenith, Panel* panel);
        i32 GetHeight(Zenith* zenith, Panel* panel);
        i32 SetSize(Zenith* zenith, Panel* panel);
        i32 SetWidth(Zenith* zenith, Panel* panel);
        i32 SetHeight(Zenith* zenith, Panel* panel);
        i32 SetBackground(Zenith* zenith, Panel* panel);
        i32 SetForeground(Zenith* zenith, Panel* panel);
        i32 SetTexCoords(Zenith* zenith, Panel* panel);
        i32 SetColor(Zenith* zenith, Panel* panel);
        i32 SetAlpha(Zenith* zenith, Panel* panel);
    };

    static LuaRegister<Panel> panelMethods[] =
    {
        { "GetSize", PanelMethods::GetSize },
        { "GetWidth", PanelMethods::GetWidth },
        { "GetHeight", PanelMethods::GetHeight },

        { "SetSize", PanelMethods::SetSize },
        { "SetWidth", PanelMethods::SetWidth },
        { "SetHeight", PanelMethods::SetHeight },

        { "SetBackground", PanelMethods::SetBackground },
        { "SetForeground", PanelMethods::SetForeground },

        { "SetTexCoords", PanelMethods::SetTexCoords },

        { "SetColor", PanelMethods::SetColor },
        { "SetAlpha", PanelMethods::SetAlpha }
    };
}