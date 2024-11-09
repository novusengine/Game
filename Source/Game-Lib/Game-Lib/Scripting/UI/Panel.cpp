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

        { nullptr, nullptr }
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
            auto& panelMethods = registry->get<ECS::Components::UI::PanelTemplate>(widget->entity);

            const char* texture = ctx.Get(nullptr, 2);
            if (texture)
            {
                panelMethods.background = texture;
                panelMethods.setFlags.background = true;
            }
            else
            {
                panelMethods.background = "";
                panelMethods.setFlags.background = false;
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
            auto& panelMethods = registry->get<ECS::Components::UI::PanelTemplate>(widget->entity);

            const char* texture = ctx.Get(nullptr, 2);
            if (texture)
            {
                panelMethods.foreground = texture;
                panelMethods.setFlags.foreground = true;
            }
            else
            {
                panelMethods.foreground = "";
                panelMethods.setFlags.foreground = false;
            }

            return 0;
        }
    }
}