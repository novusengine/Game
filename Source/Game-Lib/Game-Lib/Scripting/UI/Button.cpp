#include "Button.h"

#include "Game-Lib/ECS/Components/UI/Text.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/UI/Text.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <entt/entt.hpp>
#include <lua.h>

namespace Scripting::UI
{
    static LuaMethod buttonMethods[] =
    {
        { "SetText", ButtonMethods::SetText },

        { "GetPanelWidget", ButtonMethods::GetPanelWidget },
        { "GetTextWidget", ButtonMethods::GetTextWidget },


        { nullptr, nullptr }
    };

    void Button::Register(lua_State* state)
    {
        LuaMetaTable<Button>::Register(state, "ButtonMetaTable");

        LuaMetaTable<Button>::Set(state, widgetCreationMethods);
        LuaMetaTable<Button>::Set(state, widgetMethods);
        LuaMetaTable<Button>::Set(state, widgetInputMethods);
        LuaMetaTable<Button>::Set(state, buttonMethods);
    }

    namespace ButtonMethods
    {
        i32 SetText(lua_State* state)
        {
            LuaState ctx(state);

            Button* button = ctx.GetUserData<Button>(nullptr, 1);
            if (button == nullptr)
            {
                luaL_error(state, "Button is null");
            }

            const char* text = ctx.Get(nullptr, 2);
            if (text == nullptr)
            {
                return 0;
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Util::UI::RefreshText(registry, button->textWidget.entity, text);

            return 0;
        }

        i32 GetPanelWidget(lua_State* state)
        {
            LuaState ctx(state);
            Button* button = ctx.GetUserData<Button>(nullptr, 1);
            if (button == nullptr)
            {
                luaL_error(state, "Button is null");
            }

            lua_pushlightuserdata(state, &button->panelWidget);

            luaL_getmetatable(state, button->panelWidget.metaTableName.c_str());
            lua_setmetatable(state, -2);

            return 1;
        }

        i32 GetTextWidget(lua_State* state)
        {
            LuaState ctx(state);
            Button* button = ctx.GetUserData<Button>(nullptr, 1);
            if (button == nullptr)
            {
                luaL_error(state, "Button is null");
            }

            lua_pushlightuserdata(state, &button->textWidget);

            luaL_getmetatable(state, button->textWidget.metaTableName.c_str());
            lua_setmetatable(state, -2);

            return 1;
        }
    }
}