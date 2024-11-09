#pragma once
#include "Game-Lib/Scripting/Handlers/UIHandler.h"

#include <Base/Types.h>

#include <entt/entt.hpp>

namespace Scripting::UI
{
    struct Widget;
}

namespace ECS::Components::UI
{
    struct EventInputInfo;
}

namespace ECS::Util
{
    namespace UI
    {
        entt::entity GetOrEmplaceCanvas(Scripting::UI::Widget* widget, entt::registry* registry, const char* name, vec2 pos, ivec2 size);
        entt::entity CreateCanvas(Scripting::UI::Widget* widget, entt::registry* registry, const char* name, vec2 pos, ivec2 size, entt::entity parent = entt::null);

        entt::entity CreatePanel(Scripting::UI::Widget* widget, entt::registry* registry, vec2 pos, ivec2 size, u32 layer, const char* templateName, entt::entity parent);
        entt::entity CreateText(Scripting::UI::Widget* widget, entt::registry* registry, const char* text, vec2 pos, u32 layer, const char* templateName, entt::entity parent);
        entt::entity CreateWidget(Scripting::UI::Widget* widget, entt::registry* registry, vec2 pos, u32 layer, entt::entity parent);

        void FocusWidgetEntity(entt::registry* registry, entt::entity entity);
        entt::entity GetFocusedWidgetEntity(entt::registry* registry);

        void RefreshText(entt::registry* registry, entt::entity entity, std::string_view newText);
        void RefreshTemplate(entt::registry* registry, entt::entity entity, ECS::Components::UI::EventInputInfo& eventInputInfo);

        void ResetTemplate(entt::registry* registry, entt::entity entity); // Sets it back to base
        void ApplyTemplateAdditively(entt::registry* registry, entt::entity entity, u32 templateHash);

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget);
        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, i32 value);
        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, f32 value);
        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, vec2 value);

        void CallKeyboardEvent(i32 eventRef, Scripting::UI::Widget* widget, i32 key, i32 actionMask, i32 modifierMask);
        void CallUnicodeEvent(i32 eventRef, Scripting::UI::Widget* widget, u32 unicode);
    }
}