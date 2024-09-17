#include "Text.h"

#include "Game-Lib/ECS/Components/UI/Text.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
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
            textComponent.text = text;

            ECS::Util::UI::RefreshText(registry, widget->entity);

            return 0;
        }
    }
}