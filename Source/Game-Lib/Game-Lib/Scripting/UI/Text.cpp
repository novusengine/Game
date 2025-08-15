#include "Text.h"

#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/Text.h"
#include "Game-Lib/ECS/Components/UI/TextTemplate.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>

namespace Scripting::UI
{
    void Text::Register(lua_State* state)
    {
        LuaMetaTable<Text>::Register(state, "TextMetaTable");
        LuaMetaTable<Text>::Set(state, widgetMethods);
        LuaMetaTable<Text>::Set(state, widgetInputMethods);
        LuaMetaTable<Text>::Set(state, textMethods);
    }

    namespace TextMethods
    {
        i32 GetText(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(widget->entity);
            ctx.Push(textComponent.text);

            return 1;
        }
        i32 SetText(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            const char* text = ctx.Get(nullptr, 2);
            if (text == nullptr)
            {
                return 0;
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(widget->entity);
            textComponent.rawText = text;
            ECS::Util::UI::RefreshText(registry, widget->entity, text);

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);
            ECS::Util::UI::RefreshClipper(registry, widget->entity);

            return 0;
        }

        i32 GetRawText(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(widget->entity);
            ctx.Push(textComponent.rawText);

            return 1;
        }

        i32 GetSize(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(widget->entity).GetSize();

            ctx.Push(size.x);
            ctx.Push(size.y);
            return 2;
        }

        i32 GetFontSize(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            ECS::Components::UI::TextTemplate& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            ctx.Push(textTemplate.size);

            return 1;
        }

        i32 SetFontSize(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            f32 size = ctx.Get(0.0f, 2);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            textTemplate.size = size;

            registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetTransform>(widget->entity);
            ECS::Util::UI::RefreshClipper(registry, widget->entity);

            return 0;
        }

        i32 GetWidth(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(widget->entity).GetSize();

            ctx.Push(size.x);
            return 1;
        }

        i32 GetHeight(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(widget->entity).GetSize();

            ctx.Push(size.y);
            return 1;
        }

        i32 GetColor(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            ctx.Push(vec3(textTemplate.color.r, textTemplate.color.g, textTemplate.color.b));

            return 1;
        }

        i32 SetColor(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            vec3 color = ctx.Get(vec3(1, 1, 1), -1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            textTemplate.color = Color(color.r, color.g, color.b, 1.0f);
            textTemplate.setFlags.color = true;

            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);
            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);
            return 0;
        }

        i32 SetAlpha(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            f32 alpha = ctx.Get(0.0f, -1);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            textTemplate.color.a = alpha;
            textTemplate.setFlags.color = true;

            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);
            return 0;
        }

        i32 GetWrapWidth(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            ctx.Push(textTemplate.wrapWidth);

            return 1;
        }

        i32 SetWrapWidth(lua_State* state)
        {
            LuaState ctx(state);

            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            f32 wrapWidth = ctx.Get(0.0f, 2);
            wrapWidth = glm::max(0.0f, wrapWidth);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            textTemplate.wrapWidth = wrapWidth;
            textTemplate.setFlags.wrapWidth = wrapWidth >= 0;

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(widget->entity);
            ECS::Util::UI::RefreshText(registry, widget->entity, textComponent.rawText);

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

            return 0;
        }

        i32 GetWrapIndent(lua_State* state)
        {
            LuaState ctx(state);
            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            ctx.Push(static_cast<u32>(textTemplate.wrapIndent));
            return 1;
        }

        i32 SetWrapIndent(lua_State* state)
        {
            LuaState ctx(state);
            Text* widget = ctx.GetUserData<Text>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            u32 wrapIndent = ctx.Get(0u, 2);
            wrapIndent = glm::max(0u, wrapIndent);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            auto& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(widget->entity);
            textTemplate.wrapIndent = wrapIndent;
            textTemplate.setFlags.wrapIndent = wrapIndent >= 0;
            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(widget->entity);
            ECS::Util::UI::RefreshText(registry, widget->entity, textComponent.rawText);

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);
            ECS::Util::UI::RefreshClipper(registry, widget->entity);

            return 0;
        }
    }
}