#include "Text.h"

#include "Game/ECS/Components/UI/Text.h"
#include "Game/ECS/Util/UIUtil.h"
#include "Game/Scripting/LuaState.h"
#include "Game/Util/ServiceLocator.h"

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