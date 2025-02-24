#include "Widget.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/UI/Panel.h"
#include "Game-Lib/Scripting/UI/Text.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <entt/entt.hpp>

namespace Scripting::UI
{
    namespace WidgetCreationMethods
    {
        i32 CreatePanel(lua_State* state)
        {
            LuaState ctx(state);

            Widget* parent = ctx.GetUserData<Widget>(nullptr, 1);
            if (parent == nullptr)
            {
                luaL_error(state, "Parent is null");
            }

            i32 posX = ctx.Get(0, 2);
            i32 posY = ctx.Get(0, 3);

            i32 sizeX = ctx.Get(100, 4);
            i32 sizeY = ctx.Get(100, 5);

            i32 layer = ctx.Get(0, 6);

            const char* templateName = ctx.Get(nullptr, 7);
            if (templateName == nullptr)
            {
                luaL_error(state, "Template name is null");
            }

            Panel* panel = ctx.PushUserData<Panel>([](void* x)
            {

            });

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
            if (!uiSingleton.templateHashToPanelTemplateIndex.contains(templateNameHash))
            {
                luaL_error(state, "Tried to use template name '%s' but no panel template with that name has been registered", templateName);
            }

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
            if (parent == nullptr)
            {
                luaL_error(state, "Parent is null");
            }

            const char* str = ctx.Get("", 2);
            i32 posX = ctx.Get(0, 3);
            i32 posY = ctx.Get(0, 4);

            u32 layer = ctx.Get(0, 5);

            const char* templateName = ctx.Get(nullptr, 6);
            if (templateName == nullptr)
            {
                luaL_error(state, "Template name is null");
            }

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();
            
            u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
            if (!uiSingleton.templateHashToTextTemplateIndex.contains(templateNameHash))
            {
                luaL_error(state, "Tried to use template name '%s' but no text template with that name has been registered", templateName);
            }

            Text* text = ctx.PushUserData<Text>([](void* x)
            {

            });

            entt::entity entity = ECS::Util::UI::CreateText(text, registry, str, vec2(posX, posY), layer, templateName, parent->entity);

            text->type = WidgetType::Text;
            text->entity = entity;

            text->metaTableName = "TextMetaTable";
            luaL_getmetatable(state, "TextMetaTable");
            lua_setmetatable(state, -2);

            return 1;
        }

        i32 CreateWidget(lua_State* state)
        {
            LuaState ctx(state);

            Widget* parent = ctx.GetUserData<Widget>(nullptr, 1);
            if (parent == nullptr)
            {
                luaL_error(state, "Parent is null");
            }

            i32 posX = ctx.Get(0, 2);
            i32 posY = ctx.Get(0, 3);

            u32 layer = ctx.Get(0, 4);

            Widget* widget = ctx.PushUserData<Widget>([](void* x)
            {

            });

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            entt::entity entity = ECS::Util::UI::CreateWidget(widget, registry, vec2(posX, posY), layer, parent->entity);

            widget->type = WidgetType::Widget;
            widget->entity = entity;

            widget->metaTableName = "WidgetMetaTable";
            luaL_getmetatable(state, "WidgetMetaTable");
            lua_setmetatable(state, -2);

            return 1;
        }
    }

    void Widget::Register(lua_State* state)
    {
        LuaMetaTable<Widget>::Register(state, "WidgetMetaTable");
        LuaMetaTable<Widget>::Set(state, widgetMethods);
        LuaMetaTable<Widget>::Set(state, widgetCreationMethods);
    }
}

i32 Scripting::UI::WidgetMethods::SetEnabled(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    bool enabled = ctx.Get(true, 2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    u32 toAdd = static_cast<u32>(widgetComponent.flags);
    u32 toRemove = static_cast<u32>(widgetComponent.flags);

    toAdd |= static_cast<u32>(ECS::Components::UI::WidgetFlags::Enabled);
    toRemove &= ~static_cast<u32>(ECS::Components::UI::WidgetFlags::Enabled);

    widgetComponent.flags = static_cast<ECS::Components::UI::WidgetFlags>(toAdd * enabled + toRemove * !enabled);
    registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetFlags>(widget->entity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetVisible(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    bool visible = ctx.Get(true, 2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    u32 toAdd = static_cast<u32>(widgetComponent.flags);
    u32 toRemove = static_cast<u32>(widgetComponent.flags);

    toAdd |= static_cast<u32>(ECS::Components::UI::WidgetFlags::Visible);
    toRemove &= ~static_cast<u32>(ECS::Components::UI::WidgetFlags::Visible);

    widgetComponent.flags = static_cast<ECS::Components::UI::WidgetFlags>(toAdd * visible + toRemove * !visible);
    registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetFlags>(widget->entity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetInteractable(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    bool interactable = ctx.Get(true, 2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    u32 toAdd = static_cast<u32>(widgetComponent.flags);
    u32 toRemove = static_cast<u32>(widgetComponent.flags);

    toAdd |= static_cast<u32>(ECS::Components::UI::WidgetFlags::Interactable);
    toRemove &= ~static_cast<u32>(ECS::Components::UI::WidgetFlags::Interactable);

    widgetComponent.flags = static_cast<ECS::Components::UI::WidgetFlags>(toAdd * interactable + toRemove * !interactable);
    registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetFlags>(widget->entity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetFocusable(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    bool focusable = ctx.Get(true, 2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    u32 toAdd = static_cast<u32>(widgetComponent.flags);
    u32 toRemove = static_cast<u32>(widgetComponent.flags);

    toAdd |= static_cast<u32>(ECS::Components::UI::WidgetFlags::Focusable);
    toRemove &= ~static_cast<u32>(ECS::Components::UI::WidgetFlags::Focusable);

    widgetComponent.flags = static_cast<ECS::Components::UI::WidgetFlags>(toAdd * focusable + toRemove * !focusable);
    registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetFlags>(widget->entity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::IsEnabled(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    bool isEnabled = (widgetComponent.flags & ECS::Components::UI::WidgetFlags::Enabled) == ECS::Components::UI::WidgetFlags::Enabled;
    ctx.Push(isEnabled);
    return 1;
}

i32 Scripting::UI::WidgetMethods::IsVisible(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    bool isVisible = (widgetComponent.flags & ECS::Components::UI::WidgetFlags::Visible) == ECS::Components::UI::WidgetFlags::Visible;
    ctx.Push(isVisible);
    return 1;
}

i32 Scripting::UI::WidgetMethods::IsInteractable(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    bool isInteractable = (widgetComponent.flags & ECS::Components::UI::WidgetFlags::Interactable) == ECS::Components::UI::WidgetFlags::Interactable;
    ctx.Push(isInteractable);
    return 1;
}

i32 Scripting::UI::WidgetMethods::IsFocusable(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    bool isFocusable = (widgetComponent.flags & ECS::Components::UI::WidgetFlags::Focusable) == ECS::Components::UI::WidgetFlags::Focusable;
    ctx.Push(isFocusable);
    return 1;
}

i32 Scripting::UI::WidgetMethods::GetAnchor(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& transform = registry->get<ECS::Components::Transform2D>(widget->entity);

    const vec2& anchor = transform.GetAnchor();

    ctx.Push(anchor.x);
    ctx.Push(anchor.y);

    return 2;
}

i32 Scripting::UI::WidgetMethods::GetRelativePoint(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& transform = registry->get<ECS::Components::Transform2D>(widget->entity);

    const vec2& relativePoint = transform.GetRelativePoint();

    ctx.Push(relativePoint.x);
    ctx.Push(relativePoint.y);

    return 2;
}

i32 Scripting::UI::WidgetMethods::SetAnchor(lua_State* state)
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

    ts.SetAnchor(widget->entity, vec2(x, y));

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetRelativePoint(lua_State* state)
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

    ts.SetRelativePoint(widget->entity, vec2(x, y));

    return 0;
}

i32 Scripting::UI::WidgetMethods::GetPos(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    ctx.Push(pos.x);
    ctx.Push(pos.y);

    return 2;
}

i32 Scripting::UI::WidgetMethods::GetPosX(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    ctx.Push(pos.x);

    return 1;
}

i32 Scripting::UI::WidgetMethods::GetPosY(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    ctx.Push(pos.y);

    return 1;
}

i32 Scripting::UI::WidgetMethods::SetPos(lua_State* state)
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

    ts.SetLocalPosition(widget->entity, vec2(x, y));

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetPosX(lua_State* state)
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

    vec2 pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    pos.x = x;
    ts.SetLocalPosition(widget->entity, pos);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetPosY(lua_State* state)
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

    vec2 pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    pos.y = y;
    ts.SetLocalPosition(widget->entity, pos);

    return 0;
}

i32 Scripting::UI::WidgetMethods::GetWorldPos(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetWorldPosition();
    ctx.Push(pos.x);
    ctx.Push(pos.y);

    return 2;
}

i32 Scripting::UI::WidgetMethods::GetWorldPosX(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetWorldPosition();
    ctx.Push(pos.x);

    return 1;
}

i32 Scripting::UI::WidgetMethods::GetWorldPosY(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetWorldPosition();
    ctx.Push(pos.y);

    return 1;
}

i32 Scripting::UI::WidgetMethods::SetWorldPos(lua_State* state)
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

    ts.SetWorldPosition(widget->entity, vec2(x, y));

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetWorldPosX(lua_State* state)
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

    vec2 pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    pos.x = x;
    ts.SetWorldPosition(widget->entity, pos);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetWorldPosY(lua_State* state)
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

    vec2 pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    pos.y = y;
    ts.SetWorldPosition(widget->entity, pos);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnMouseDown(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onMouseDownEvent = callback;

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnMouseUp(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onMouseUpEvent = callback;

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnMouseHeld(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onMouseHeldEvent = callback;

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnHoverBegin(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onHoverBeginEvent = callback;

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnHoverEnd(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onHoverEndEvent = callback;

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnHoverHeld(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onHoverHeldEvent = callback;

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnFocusBegin(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onFocusBeginEvent = callback;

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnFocusEnd(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onFocusEndEvent = callback;

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnFocusHeld(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onFocusHeldEvent = callback;

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnKeyboard(lua_State* state)
{
    LuaState ctx(state);

    Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
    if (widget == nullptr)
    {
        luaL_error(state, "Widget is null");
    }

    i32 callback = -1;
    if (lua_type(state, 2) == LUA_TFUNCTION)
    {
        callback = ctx.GetRef(2);
    }

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onKeyboardEvent = callback;

    return 0;
}
