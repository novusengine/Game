#include "Widget.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/UISingleton.h"
#include "Game/ECS/Util/Transform2D.h"
#include "Game/ECS/Util/UIUtil.h"
#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/UI/Button.h"
#include "Game/Scripting/UI/Panel.h"
#include "Game/Scripting/UI/Text.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <entt/entt.hpp>

namespace Scripting::UI
{
    namespace WidgetCreationMethods
    {
        i32 CreateButton(lua_State* state)
        {
            LuaState ctx(state);

            Widget* parent = ctx.GetUserData<Widget>(nullptr, 1);

            i32 posX = ctx.Get(0, 2);
            i32 posY = ctx.Get(0, 3);

            i32 sizeX = ctx.Get(100, 4);
            i32 sizeY = ctx.Get(100, 5);

            i32 layer = ctx.Get(0, 6);

            const char* templateName = ctx.Get(nullptr, 7);
            if (templateName == nullptr)
            {
                ctx.Push();
                return 1;
            }

            u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            u32 buttonTemplateIndex = uiSingleton.templateHashToButtonTemplateIndex[templateNameHash];
            ::UI::ButtonTemplate& buttonTemplate = uiSingleton.buttonTemplates[buttonTemplateIndex];

            const std::string& panelTemplateName = buttonTemplate.panelTemplate;
            const std::string& textTemplateName = buttonTemplate.textTemplate;

            Button* button = ctx.PushUserData<Button>([](void* x)
            {

            });

            entt::entity panelEntity = ECS::Util::UI::CreatePanel(button, registry, vec2(posX, posY), ivec2(sizeX, sizeY), layer, panelTemplateName.c_str(), parent->entity);
            entt::entity textEntity = ECS::Util::UI::CreateText(button, registry, "TEST", vec2(0, 0), layer, textTemplateName.c_str(), panelEntity);

            ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);
            ts.SetAnchor(textEntity, vec2(0.5, 0.5));
            ts.SetRelativePoint(textEntity, vec2(0.5, 0.5));

            button->type = WidgetType::Button;
            button->entity = panelEntity;

            button->panelEntity = panelEntity;
            button->textEntity = textEntity;

            button->metaTableName = "ButtonMetaTable";
            luaL_getmetatable(state, "ButtonMetaTable");
            lua_setmetatable(state, -2);

            return 1;
        }

        i32 CreatePanel(lua_State* state)
        {
            LuaState ctx(state);

            Widget* parent = ctx.GetUserData<Widget>(nullptr, 1);

            i32 posX = ctx.Get(0, 2);
            i32 posY = ctx.Get(0, 3);

            i32 sizeX = ctx.Get(100, 4);
            i32 sizeY = ctx.Get(100, 5);

            i32 layer = ctx.Get(0, 6);

            const char* templateName = ctx.Get(nullptr, 7);
            if (templateName == nullptr)
            {
                ctx.Push();
                return 1;
            }

            Panel* panel = ctx.PushUserData<Panel>([](void* x)
            {

            });

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            entt::entity entity = ECS::Util::UI::CreatePanel(panel, registry, vec2(posX, posY), ivec2(sizeX, sizeY), layer, templateName, parent->entity);

            panel->type = WidgetType::Panel;
            panel->entity = entity;

            panel->metaTableName = "PanelMetaTable";
            luaL_getmetatable(state, "PanelMetaTable");
            lua_setmetatable(state, -2);

            return 1;
        }

        i32 CreateText(lua_State* state)
        {
            LuaState ctx(state);

            Widget* parent = ctx.GetUserData<Widget>(nullptr, 1);

            const char* str = ctx.Get("", 2);
            i32 posX = ctx.Get(0, 3);
            i32 posY = ctx.Get(0, 4);

            u32 layer = ctx.Get(0, 5);

            const char* templateName = ctx.Get(nullptr, 6);
            if (templateName == nullptr)
            {
                ctx.Push();
                return 1;
            }

            Text* text = ctx.PushUserData<Text>([](void* x)
            {

            });

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            entt::entity entity = ECS::Util::UI::CreateText(text, registry, str, vec2(posX, posY), layer, templateName, parent->entity);

            text->type = WidgetType::Text;
            text->entity = entity;

            text->metaTableName = "TextMetaTable";
            luaL_getmetatable(state, "TextMetaTable");
            lua_setmetatable(state, -2);

            return 1;
        }
    }
}

i32 Scripting::UI::WidgetMethods::SetAnchor(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);

    f32 x = ctx.Get(0.0f, 2);
    f32 y = ctx.Get(0.0f, 3);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    ts.SetAnchor(widget->entity, vec2(x, y));

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetRelativePoint(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);

    f32 x = ctx.Get(0.0f, 2);
    f32 y = ctx.Get(0.0f, 3);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    ts.SetRelativePoint(widget->entity, vec2(x, y));

    return 0;
}
