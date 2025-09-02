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

namespace Renderer
{
    struct Font;
}

namespace ECS::Util
{
    namespace UI
    {
        entt::entity GetOrEmplaceCanvas(Scripting::UI::Widget*& widget, entt::registry* registry, const char* name, vec2 pos, ivec2 size, bool isRenderTexture);
        entt::entity CreateCanvas(Scripting::UI::Widget* widget, entt::registry* registry, const char* name, vec2 pos, ivec2 size, bool isRenderTexture);

        entt::entity CreatePanel(Scripting::UI::Widget* widget, entt::registry* registry, vec2 pos, ivec2 size, u32 layer, const char* templateName, entt::entity parent);
        entt::entity CreateText(Scripting::UI::Widget* widget, entt::registry* registry, const char* text, vec2 pos, u32 layer, const char* templateName, entt::entity parent);
        entt::entity CreateWidget(Scripting::UI::Widget* widget, entt::registry* registry, vec2 pos, u32 layer, entt::entity parent);
        
        bool DestroyWidget(entt::registry* registry, entt::entity entity);

        void FocusWidgetEntity(entt::registry* registry, entt::entity entity);
        entt::entity GetFocusedWidgetEntity(entt::registry* registry);

        void RefreshText(entt::registry* registry, entt::entity entity, std::string_view newText);
        void RefreshTemplate(entt::registry* registry, entt::entity entity, ECS::Components::UI::EventInputInfo& eventInputInfo);
        void RefreshClipper(entt::registry* registry, entt::entity entity);

        void ResetTemplate(entt::registry* registry, entt::entity entity); // Sets it back to base
        void ApplyTemplateAdditively(entt::registry* registry, entt::entity entity, u32 templateHash);

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget);
        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, i32 value);
        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, i32 value1, vec2 value2);
        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, f32 value);
        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, vec2 value);

        bool CallKeyboardEvent(i32 eventRef, Scripting::UI::Widget* widget, i32 key, i32 actionMask, i32 modifierMask);
        bool CallKeyboardEvent(i32 eventRef, i32 key, i32 actionMask, i32 modifierMask);
        bool CallUnicodeEvent(i32 eventRef, Scripting::UI::Widget* widget, u32 unicode);

        void CallSendMessageToChat(i32 eventRef, const std::string& channel, const std::string& playerName, const std::string& text, bool isOutgoing);

        std::string GenWrapText(const std::string& text, Renderer::Font* font, f32 fontSize, f32 borderSize, f32 maxWidth, u8 indents);
        void ReplaceTextNewLines(std::string& input);

        void SetPos3D(Scripting::UI::Widget* widget, vec3& pos);
        void ClearPos3D(Scripting::UI::Widget* widget);
    }
}