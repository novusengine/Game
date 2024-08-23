#include "Button.h"

#include "Game/ECS/Components/UI/Text.h"
#include "Game/ECS/Util/UIUtil.h"
#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/UI/Text.h"
#include "Game/Util/ServiceLocator.h"

#include <entt/entt.hpp>

namespace Scripting::UI
{
    static LuaMethod buttonMethods[] =
    {
        { "SetText", ButtonMethods::SetText },

        { nullptr, nullptr }
    };

    void Button::Register(lua_State* state)
    {
        LuaMetaTable<Button>::Register(state, "ButtonMetaTable");

        LuaMetaTable<Button>::Set(state, widgetCreationMethods);
        LuaMetaTable<Button>::Set(state, widgetMethods);
        LuaMetaTable<Button>::Set(state, buttonMethods);
    }

    namespace ButtonMethods
    {
        i32 SetText(lua_State* state)
        {
            LuaState ctx(state);

            Button* button = ctx.GetUserData<Button>(nullptr, 1);

            const char* text = ctx.Get(nullptr, 2);
            if (text == nullptr)
            {
                return 0;
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(button->textEntity);
            textComponent.text = text;

            ECS::Util::UI::RefreshText(registry, button->textEntity);

            return 0;
        }
    }
}