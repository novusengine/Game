#include "Widget.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Rendering/Canvas/CanvasRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Scripting/UI/Panel.h"
#include "Game-Lib/Scripting/UI/Text.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>

namespace Scripting::UI
{
    namespace WidgetCreationMethods
    {
        i32 CreatePanel(Zenith* zenith, Widget* parent)
        {
            i32 posX = zenith->CheckVal<i32>(2);
            i32 posY = zenith->CheckVal<i32>(3);

            u32 sizeX = zenith->CheckVal<u32>(4);
            u32 sizeY = zenith->CheckVal<u32>(5);

            u32 layer = zenith->CheckVal<u32>(6);

            const char* templateName = zenith->IsString(7) ? zenith->Get<const char*>(7) : nullptr;

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            Panel* panel = new Panel();
            uiSingleton.scriptWidgets.push_back(panel);

            if (templateName != nullptr)
            {
                u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
                if (!uiSingleton.templateHashToPanelTemplateIndex.contains(templateNameHash))
                {
                    luaL_error(zenith->state, "Tried to use template name '%s' but no panel template with that name has been registered", templateName);
                }
            }

            entt::entity entity = ECS::Util::UI::CreatePanel(panel, registry, vec2(posX, posY), ivec2(sizeX, sizeY), layer, templateName, parent->entity);

            panel->type = WidgetType::Panel;
            panel->entity = entity;
            panel->canvasEntity = (parent->type == WidgetType::Canvas) ? parent->entity : parent->canvasEntity;
            panel->metaTableName = "PanelMetaTable";

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(panel->canvasEntity);

            Panel* pushPanel = zenith->PushUserData<Panel>([](void* x)
            {

            });
            memcpy(pushPanel, panel, sizeof(Panel));
            luaL_getmetatable(zenith->state, panel->metaTableName.c_str());
            lua_setmetatable(zenith->state, -2);

            return 1;
        }

        i32 CreateText(Zenith* zenith, Widget* parent)
        {
            const char* str = zenith->CheckVal<const char*>(2);
            i32 posX = zenith->CheckVal<i32>(3);
            i32 posY = zenith->CheckVal<i32>(4);

            u32 layer = zenith->CheckVal<u32>(5);

            const char* templateName = zenith->CheckVal<const char*>(6);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();
            
            u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
            if (!uiSingleton.templateHashToTextTemplateIndex.contains(templateNameHash))
            {
                luaL_error(zenith->state, "Tried to use template name '%s' but no text template with that name has been registered", templateName);
            }

            Text* text = new Text();
            uiSingleton.scriptWidgets.push_back(text);

            entt::entity entity = ECS::Util::UI::CreateText(text, registry, str, vec2(posX, posY), layer, templateName, parent->entity);

            text->type = WidgetType::Text;
            text->entity = entity;
            text->canvasEntity = (parent->type == WidgetType::Canvas) ? parent->entity : parent->canvasEntity;
            text->metaTableName = "TextMetaTable";

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(text->canvasEntity);

            Text* pushText = zenith->PushUserData<Text>([](void* x)
            {

            });
            memcpy(pushText, text, sizeof(Text));
            luaL_getmetatable(zenith->state, text->metaTableName.c_str());
            lua_setmetatable(zenith->state, -2);

            return 1;
        }

        i32 CreateWidget(Zenith* zenith, Widget* parent)
        {
            i32 posX = zenith->CheckVal<i32>(2);
            i32 posY = zenith->CheckVal<i32>(3);

            u32 layer = zenith->CheckVal<u32>(4);

            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            Widget* widget = new Widget();
            uiSingleton.scriptWidgets.push_back(widget);

            entt::entity entity = ECS::Util::UI::CreateWidget(widget, registry, vec2(posX, posY), layer, parent->entity);

            widget->type = WidgetType::Widget;
            widget->entity = entity;
            widget->canvasEntity = (parent->type == WidgetType::Canvas) ? parent->entity : parent->canvasEntity;
            widget->metaTableName = "WidgetMetaTable";

            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

            Widget* pushWidget = zenith->PushUserData<Widget>([](void* x)
            {

            });
            memcpy(pushWidget, widget, sizeof(Widget));
            luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
            lua_setmetatable(zenith->state, -2);

            return 1;
        }
    }

    void Widget::Register(Zenith* zenith)
    {
        LuaMetaTable<Widget>::Register(zenith, "WidgetMetaTable");
        LuaMetaTable<Widget>::Set(zenith, widgetMethods);
        LuaMetaTable<Widget>::Set(zenith, widgetCreationMethods);
    }
}

i32 Scripting::UI::WidgetMethods::SetEnabled(Zenith* zenith, Widget* widget)
{
    bool enabled = zenith->CheckVal<bool>(2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    u32 toAdd = static_cast<u32>(widgetComponent.flags);
    u32 toRemove = static_cast<u32>(widgetComponent.flags);

    toAdd |= static_cast<u32>(ECS::Components::UI::WidgetFlags::Enabled);
    toRemove &= ~static_cast<u32>(ECS::Components::UI::WidgetFlags::Enabled);

    widgetComponent.flags = static_cast<ECS::Components::UI::WidgetFlags>(toAdd * enabled + toRemove * !enabled);
    registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetFlags>(widget->entity);

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetVisible(Zenith* zenith, Widget* widget)
{
    bool visible = zenith->CheckVal<bool>(2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    u32 toAdd = static_cast<u32>(widgetComponent.flags);
    u32 toRemove = static_cast<u32>(widgetComponent.flags);

    toAdd |= static_cast<u32>(ECS::Components::UI::WidgetFlags::Visible);
    toRemove &= ~static_cast<u32>(ECS::Components::UI::WidgetFlags::Visible);

    widgetComponent.flags = static_cast<ECS::Components::UI::WidgetFlags>(toAdd * visible + toRemove * !visible);
    registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetFlags>(widget->entity);

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetInteractable(Zenith* zenith, Widget* widget)
{
    bool interactable = zenith->CheckVal<bool>(2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    u32 toAdd = static_cast<u32>(widgetComponent.flags);
    u32 toRemove = static_cast<u32>(widgetComponent.flags);

    toAdd |= static_cast<u32>(ECS::Components::UI::WidgetFlags::Interactable);
    toRemove &= ~static_cast<u32>(ECS::Components::UI::WidgetFlags::Interactable);

    widgetComponent.flags = static_cast<ECS::Components::UI::WidgetFlags>(toAdd * interactable + toRemove * !interactable);
    registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetFlags>(widget->entity);

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetFocusable(Zenith* zenith, Widget* widget)
{
    bool focusable = zenith->CheckVal<bool>(2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    u32 toAdd = static_cast<u32>(widgetComponent.flags);
    u32 toRemove = static_cast<u32>(widgetComponent.flags);

    toAdd |= static_cast<u32>(ECS::Components::UI::WidgetFlags::Focusable);
    toRemove &= ~static_cast<u32>(ECS::Components::UI::WidgetFlags::Focusable);

    widgetComponent.flags = static_cast<ECS::Components::UI::WidgetFlags>(toAdd * focusable + toRemove * !focusable);
    registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetFlags>(widget->entity);

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::IsEnabled(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    bool isEnabled = (widgetComponent.flags & ECS::Components::UI::WidgetFlags::Enabled) == ECS::Components::UI::WidgetFlags::Enabled;
    zenith->Push(isEnabled);
    return 1;
}

i32 Scripting::UI::WidgetMethods::IsVisible(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    bool isVisible = (widgetComponent.flags & ECS::Components::UI::WidgetFlags::Visible) == ECS::Components::UI::WidgetFlags::Visible;
    zenith->Push(isVisible);
    return 1;
}

i32 Scripting::UI::WidgetMethods::IsInteractable(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    bool isInteractable = (widgetComponent.flags & ECS::Components::UI::WidgetFlags::Interactable) == ECS::Components::UI::WidgetFlags::Interactable;
    zenith->Push(isInteractable);
    return 1;
}

i32 Scripting::UI::WidgetMethods::IsFocusable(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& widgetComponent = registry->get<ECS::Components::UI::Widget>(widget->entity);

    bool isFocusable = (widgetComponent.flags & ECS::Components::UI::WidgetFlags::Focusable) == ECS::Components::UI::WidgetFlags::Focusable;
    zenith->Push(isFocusable);
    return 1;
}

i32 Scripting::UI::WidgetMethods::GetParent(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& transform = registry->get<ECS::Components::Transform2D>(widget->entity);

    if (transform.ownerNode == nullptr)
    {
        zenith->Push();
        return 1;
    }

    ECS::Components::SceneNode2D* parentNode = transform.ownerNode->GetParent();
    if (parentNode == nullptr)
    {
        zenith->Push();
        return 1;
    }

    entt::entity parentEntity = parentNode->GetOwner();
    if (!registry->valid(parentEntity))
    {
        zenith->Push();
        return 1;
    }

    auto* parentWidgetComp = registry->try_get<ECS::Components::UI::Widget>(parentEntity);
    if (parentWidgetComp == nullptr)
    {
        zenith->Push();
        return 1;
    }

    Widget* parentWidget = parentWidgetComp->scriptWidget;

    Widget* pushWidget = zenith->PushUserData<Widget>([](void* x)
    {

    });
    memcpy(pushWidget, parentWidget, sizeof(ECS::Components::UI::Widget));
    luaL_getmetatable(zenith->state, parentWidget->metaTableName.c_str());
    lua_setmetatable(zenith->state, -2);

    return 1;
}

i32 Scripting::UI::WidgetMethods::GetChildren(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);
    u32 numWidgetsPushed = 0;

    zenith->CreateTable();
    ts.IterateChildren(widget->entity, [&](auto childEntity)
    {
        auto* widgetComp = registry->try_get<ECS::Components::UI::Widget>(childEntity);
        if (widgetComp != nullptr)
        {
            Widget* pushWidget = zenith->PushUserData<Widget>([](void* x)
            {

            });

            Widget* parentWidget = widgetComp->scriptWidget;
            memcpy(pushWidget, parentWidget, sizeof(Widget));
            luaL_getmetatable(zenith->state, parentWidget->metaTableName.c_str());
            lua_setmetatable(zenith->state, -2);

            zenith->SetTableKey(++numWidgetsPushed);
        }
    });

    return 1;
}

i32 Scripting::UI::WidgetMethods::GetChildrenRecursive(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);
    u32 numWidgetsPushed = 0;

    zenith->CreateTable();
    ts.IterateChildrenRecursiveBreadth(widget->entity, [&](auto childEntity)
    {
        auto* widgetComp = registry->try_get<ECS::Components::UI::Widget>(childEntity);
        if (widgetComp != nullptr)
        {
            Widget* pushWidget = zenith->PushUserData<Widget>([](void* x)
            {

            });

            Widget* parentWidget = widgetComp->scriptWidget;
            memcpy(pushWidget, parentWidget, sizeof(Widget));
            luaL_getmetatable(zenith->state, parentWidget->metaTableName.c_str());
            lua_setmetatable(zenith->state, -2);

            zenith->SetTableKey(++numWidgetsPushed);
        }
    });

    return 1;
}

i32 Scripting::UI::WidgetMethods::GetAnchor(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& transform = registry->get<ECS::Components::Transform2D>(widget->entity);

    const vec2& anchor = transform.GetAnchor();

    zenith->Push(anchor.x);
    zenith->Push(anchor.y);

    return 2;
}

i32 Scripting::UI::WidgetMethods::GetRelativePoint(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& transform = registry->get<ECS::Components::Transform2D>(widget->entity);

    const vec2& relativePoint = transform.GetRelativePoint();

    zenith->Push(relativePoint.x);
    zenith->Push(relativePoint.y);

    return 2;
}

i32 Scripting::UI::WidgetMethods::SetAnchor(Zenith* zenith, Widget* widget)
{
    f32 x = zenith->CheckVal<f32>(2);
    f32 y = zenith->CheckVal<f32>(3);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    ts.SetAnchor(widget->entity, vec2(x, y));

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetRelativePoint(Zenith* zenith, Widget* widget)
{
    f32 x = zenith->CheckVal<f32>(2);
    f32 y = zenith->CheckVal<f32>(3);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    ts.SetRelativePoint(widget->entity, vec2(x, y));

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::IsClipChildren(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& clipper = registry->get<ECS::Components::UI::Clipper>(widget->entity);

    zenith->Push(clipper.clipChildren);

    return 1;
}

i32 Scripting::UI::WidgetMethods::SetClipChildren(Zenith* zenith, Widget* widget)
{
    bool clipChildren = zenith->CheckVal<bool>(2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& clipper = registry->get<ECS::Components::UI::Clipper>(widget->entity);

    clipper.clipChildren = clipChildren;

    registry->emplace_or_replace<ECS::Components::UI::DirtyChildClipper>(widget->entity);
    if (!clipper.clipChildren)
    {
        // Also needs to reenable clipping on the widget itself after it disables on all children
        registry->emplace_or_replace<ECS::Components::UI::DirtyClipper>(widget->entity);
    }

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::GetClipRect(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& clipper = registry->get<ECS::Components::UI::Clipper>(widget->entity);

    zenith->Push(clipper.clipRegionMin.x);
    zenith->Push(clipper.clipRegionMin.y);
    zenith->Push(clipper.clipRegionMax.x);
    zenith->Push(clipper.clipRegionMax.y);

    return 4;
}

i32 Scripting::UI::WidgetMethods::SetClipRect(Zenith* zenith, Widget* widget)
{
    f32 minX = zenith->CheckVal<f32>(2);
    f32 minY = zenith->CheckVal<f32>(3);
    f32 maxX = zenith->CheckVal<f32>(4);
    f32 maxY = zenith->CheckVal<f32>(5);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& clipper = registry->get<ECS::Components::UI::Clipper>(widget->entity);

    clipper.clipRegionMin = vec2(minX, minY);
    clipper.clipRegionMax = vec2(maxX, maxY);

    if (clipper.clipChildren)
    {
        registry->emplace_or_replace<ECS::Components::UI::DirtyChildClipper>(widget->entity);
    }
    else
    {
        registry->emplace_or_replace<ECS::Components::UI::DirtyClipper>(widget->entity);
        registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetData>(widget->entity);
    }

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::GetClipMaskTexture(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& clipper = registry->get<ECS::Components::UI::Clipper>(widget->entity);

    if (clipper.hasClipMaskTexture)
    {
        zenith->Push();
    }
    else
    {
        zenith->Push(clipper.clipMaskTexture.c_str());
    }

    return 1;
}

i32 Scripting::UI::WidgetMethods::SetClipMaskTexture(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& clipper = registry->get<ECS::Components::UI::Clipper>(widget->entity);

    const char* texture = zenith->IsString(2) ? zenith->Get<const char*>(2) : nullptr;
    if (texture == nullptr)
    {
        clipper.hasClipMaskTexture = false;
        clipper.clipMaskTexture.clear();
    }
    else
    {
        clipper.hasClipMaskTexture = true;
        clipper.clipMaskTexture = texture;
    }

    if (clipper.clipChildren)
    {
        registry->emplace_or_replace<ECS::Components::UI::DirtyChildClipper>(widget->entity);
    }
    else
    {
        registry->emplace_or_replace<ECS::Components::UI::DirtyClipper>(widget->entity);
    }

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::GetPos(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    zenith->Push(pos.x);
    zenith->Push(pos.y);

    return 2;
}

i32 Scripting::UI::WidgetMethods::GetPosX(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    zenith->Push(pos.x);

    return 1;
}

i32 Scripting::UI::WidgetMethods::GetPosY(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    zenith->Push(pos.y);

    return 1;
}

i32 Scripting::UI::WidgetMethods::SetPos(Zenith* zenith, Widget* widget)
{
    f32 x = zenith->CheckVal<f32>(2);
    f32 y = zenith->CheckVal<f32>(3);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    ts.SetLocalPosition(widget->entity, vec2(x, y));

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetPosX(Zenith* zenith, Widget* widget)
{
    f32 x = zenith->CheckVal<f32>(2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    vec2 pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    pos.x = x;
    ts.SetLocalPosition(widget->entity, pos);

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetPosY(Zenith* zenith, Widget* widget)
{
    f32 y = zenith->CheckVal<f32>(2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    vec2 pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    pos.y = y;
    ts.SetLocalPosition(widget->entity, pos);

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::GetWorldPos(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetWorldPosition();
    zenith->Push(pos.x);
    zenith->Push(pos.y);

    return 2;
}

i32 Scripting::UI::WidgetMethods::GetWorldPosX(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetWorldPosition();
    zenith->Push(pos.x);

    return 1;
}

i32 Scripting::UI::WidgetMethods::GetWorldPosY(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    const vec2& pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetWorldPosition();
    zenith->Push(pos.y);

    return 1;
}

i32 Scripting::UI::WidgetMethods::SetWorldPos(Zenith* zenith, Widget* widget)
{
    f32 x = zenith->CheckVal<f32>(2);
    f32 y = zenith->CheckVal<f32>(3);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    ts.SetWorldPosition(widget->entity, vec2(x, y));

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetWorldPosX(Zenith* zenith, Widget* widget)
{
    f32 x = zenith->CheckVal<f32>(2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    vec2 pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    pos.x = x;
    ts.SetWorldPosition(widget->entity, pos);

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetWorldPosY(Zenith* zenith, Widget* widget)
{
    f32 y = zenith->CheckVal<f32>(2);

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    ECS::Transform2DSystem& ts = ECS::Transform2DSystem::Get(*registry);

    vec2 pos = registry->get<ECS::Components::Transform2D>(widget->entity).GetLocalPosition();
    pos.y = y;
    ts.SetWorldPosition(widget->entity, pos);

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetMethods::SetPos3D(Zenith* zenith, Widget* widget)
{
    if (zenith->IsNil(2))
    {
        ECS::Util::UI::ClearPos3D(widget);
    }
    else
    {
        vec3 pos = zenith->CheckVal<vec3>(2);
        ECS::Util::UI::SetPos3D(widget, pos);
    }

    return 0;
}

i32 Scripting::UI::WidgetMethods::ForceRefresh(Zenith* zenith, Widget* widget)
{
    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

    registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);
    registry->get_or_emplace<ECS::Components::UI::DirtyWidgetTransform>(widget->entity);
    registry->get_or_emplace<ECS::Components::UI::DirtyWidgetWorldTransformIndex>(widget->entity);

    ECS::Util::UI::RefreshClipper(registry, widget->entity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnMouseDown(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : - 1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onMouseDownEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnMouseUp(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onMouseUpEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnMouseHeld(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onMouseHeldEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnMouseScroll(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onMouseScrollEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnHoverBegin(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onHoverBeginEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnHoverEnd(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onHoverEndEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnHoverHeld(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onHoverHeldEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnFocusBegin(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onFocusBeginEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnFocusEnd(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onFocusEndEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnFocusHeld(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onFocusHeldEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}

i32 Scripting::UI::WidgetInputMethods::SetOnKeyboard(Zenith* zenith, Widget* widget)
{
    i32 callback = zenith->IsFunction(2) ? zenith->GetRef(2) : -1;

    entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
    auto& eventInputInfo = registry->get<ECS::Components::UI::EventInputInfo>(widget->entity);
    eventInputInfo.onKeyboardEvent = callback;

    registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

    return 0;
}
