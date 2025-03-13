#include "Panel.h"

#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Components/UI/PanelTemplate.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/UI/Canvas.h"
#include "Game-Lib/Scripting/UI/Text.h"
#include "Game-Lib/Util/ServiceLocator.h"

namespace Scripting::UI
{
    static LuaMethod panelMethods[] =
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

    void Panel::Register(lua_State* state)
    {
        LuaMetaTable<Panel>::Register(state, "PanelMetaTable");

        LuaMetaTable<Panel>::Set(state, widgetCreationMethods);
        LuaMetaTable<Panel>::Set(state, widgetMethods);
        LuaMetaTable<Panel>::Set(state, widgetInputMethods);
        LuaMetaTable<Panel>::Set(state, panelMethods);
    }

    namespace PanelMethods
    {
        i32 GetSize(lua_State* state)
        {
            LuaState ctx(state);

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
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

        i32 GetWidth(lua_State* state)
        {
            LuaState ctx(state);

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
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

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            const vec2& size = registry->get<ECS::Components::Transform2D>(widget->entity).GetSize();

            ctx.Push(size.y);
            return 1;
        }

        i32 SetSize(lua_State* state)
        {
            LuaState ctx(state);

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            f32 x = ctx.Get(0.0f, 2);
            f32 y = ctx.Get(0.0f, 3);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

            ts.SetSize(widget->entity, vec2(x, y));

            return 0;
        }

        i32 SetWidth(lua_State* state)
        {
            LuaState ctx(state);

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            f32 x = ctx.Get(0.0f, 2);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

            vec2 size = registry->get<ECS::Components::Transform2D>(widget->entity).GetSize();
            size.x = x;
            ts.SetSize(widget->entity, size);

            return 0;
        }

        i32 SetHeight(lua_State* state)
        {
            LuaState ctx(state);

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            f32 y = ctx.Get(0.0f, 2);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

            vec2 size = registry->get<ECS::Components::Transform2D>(widget->entity).GetSize();
            size.y = y;
            ts.SetSize(widget->entity, size);

            return 0;
        }

        i32 SetBackground(lua_State* state)
        {
            LuaState ctx(state);
            
            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }
            
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(widget->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);

            const char* texture = ctx.Get(nullptr, 2);
            if (texture)
            {
                panelTemplate.background = texture;
                panelTemplate.setFlags.background = true;
            }
            else
            {
                panelTemplate.background = "";
                panelTemplate.setFlags.background = false;
            }

            return 0;
        }

        i32 SetForeground(lua_State* state)
        {
            LuaState ctx(state);

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(widget->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);

            const char* texture = ctx.Get(nullptr, 2);
            if (texture)
            {
                panelTemplate.foreground = texture;
                panelTemplate.setFlags.foreground = true;
            }
            else
            {
                panelTemplate.foreground = "";
                panelTemplate.setFlags.foreground = false;
            }

            return 0;
        }

        i32 SetTexCoords(lua_State* state)
        {
            LuaState ctx(state);

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(widget->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetTransform>(widget->entity);

            f32 minX = ctx.Get(0.0f, 2);
            f32 minY = ctx.Get(0.0f, 3);
            f32 maxX = ctx.Get(1.0f, 4);
            f32 maxY = ctx.Get(1.0f, 5);

            panelTemplate.setFlags.texCoords = true;
            panelTemplate.texCoords.min = vec2(minX, minY);
            panelTemplate.texCoords.max = vec2(maxX, maxY);

            return 0;
        }

        i32 SetColor(lua_State* state)
        {
            LuaState ctx(state);

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(widget->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);

            vec3 colorVec = ctx.Get(vec3(0,0,0), 2);
            f32 alpha = ctx.Get(-1.0f, 3);

            Color colorWithAlpha = Color(colorVec.r, colorVec.g, colorVec.b, panelTemplate.color.a);
            if (alpha >= 0.0f)
            {
                colorWithAlpha.a = alpha;
            }
            panelTemplate.color = colorWithAlpha;
            panelTemplate.setFlags.color = 1;

            return 0;
        }

        i32 SetAlpha(lua_State* state)
        {
            LuaState ctx(state);

            Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
            if (widget == nullptr)
            {
                luaL_error(state, "Widget is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& panelTemplate = registry->get<ECS::Components::UI::PanelTemplate>(widget->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);

            f32 alpha = ctx.Get(-1.0f, 2);
            panelTemplate.color.a = alpha;
            panelTemplate.setFlags.color = 1;

            return 0;
        }
    }
}