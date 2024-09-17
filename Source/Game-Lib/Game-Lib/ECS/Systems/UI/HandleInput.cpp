#include "HandleInput.h"

#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Handlers/UIHandler.h"
#include "Game-Lib/Scripting/UI/Widget.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Renderer/Renderer.h>

#include <Input/InputManager.h>
#include <Input/KeybindGroup.h>

#include <Base/Util/DebugHandler.h>

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <map>
#include <numeric>

namespace ECS::Systems::UI
{
    inline bool IsWithin(vec2 point, vec2 min, vec2 max)
    {
        return point.x > min.x && point.x < max.x && point.y > min.y && point.y < max.y;
    }

    void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvents inputEvent, Scripting::UI::Widget* widget)
    {
        Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
        lua_State* state = luaManager->GetInternalState();
        
        Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
        uiHandler->CallUIInputEvent(state, eventRef, inputEvent, widget);
    }

    void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvents inputEvent, Scripting::UI::Widget* widget, i32 value)
    {
        Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
        lua_State* state = luaManager->GetInternalState();

        Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
        uiHandler->CallUIInputEvent(state, eventRef, inputEvent, widget, value);
    }

    void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvents inputEvent, Scripting::UI::Widget* widget, vec2 value)
    {
        Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
        lua_State* state = luaManager->GetInternalState();

        Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
        uiHandler->CallUIInputEvent(state, eventRef, inputEvent, widget, value);
    }

    void HandleInput::Init(entt::registry& registry)
    {
        InputManager* inputManager = ServiceLocator::GetInputManager();
        KeybindGroup* keybindGroup = inputManager->CreateKeybindGroup("UI", 200);
        keybindGroup->SetActive(true);

        keybindGroup->AddKeyboardCallback("UI Click", GLFW_MOUSE_BUTTON_LEFT, KeybindAction::Click, KeybindModifier::Any, [&registry, inputManager](i32 key, KeybindAction action, KeybindModifier modifier)
        {
            auto& ctx = registry.ctx();
            auto& uiSingleton = ctx.get<Singletons::UISingleton>();

            vec2 mousePos = inputManager->GetMousePosition();

            Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
            const vec2& renderSize = renderer->GetRenderSize();
            mousePos.y = renderSize.y - mousePos.y; // Flipped because UI is bottom-left origin

            uiSingleton.lastClickPosition = mousePos;

            mousePos = mousePos / renderSize;
            mousePos *= vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);

            bool isDown = action == KeybindAction::Press;

            if (isDown)
            {
                for (auto& pair : uiSingleton.allHoveredEntities)
                {
                    entt::entity entity = pair.second;

                    auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(entity);

                    if (!eventInputInfo)
                    {
                        continue;
                    }

                    u32 templateHash = eventInputInfo->onClickTemplateHash;
                    i32 inputDownEvent = eventInputInfo->onMouseDownEvent;
                    i32 inputUpEvent = eventInputInfo->onMouseUpEvent;
                    i32 heldEvent = eventInputInfo->onMouseHeldEvent;

                    if (templateHash != 0 || inputDownEvent != -1 || inputUpEvent != -1 || heldEvent != -1)
                    {
                        eventInputInfo->isClicked = true;

                        if (templateHash != 0)
                        {
                            ECS::Util::UI::RefreshTemplate(&registry, entity, *eventInputInfo);
                        }

                        if (inputDownEvent != -1)
                        {
                            auto& widget = registry.get<Components::UI::Widget>(entity);
                            CallLuaEvent(inputDownEvent, Scripting::UI::UIInputEvents::MouseDown, widget.scriptWidget);
                        }

                        uiSingleton.clickedEntity = entity;
                        return true;
                    }
                }
            }

            if (!isDown && uiSingleton.clickedEntity != entt::null)
            {
                auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.clickedEntity);

                if (eventInputInfo)
                {
                    eventInputInfo->isClicked = false;
                    ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.clickedEntity, *eventInputInfo);

                    if (eventInputInfo->onMouseUpEvent != -1)
                    {
                        auto& rect = registry.get<Components::UI::BoundingRect>(uiSingleton.clickedEntity);
                        bool isWithin = IsWithin(mousePos, rect.min, rect.max);
                        if (isWithin)
                        {
                            auto& widget = registry.get<Components::UI::Widget>(uiSingleton.clickedEntity);
                            CallLuaEvent(eventInputInfo->onMouseUpEvent, Scripting::UI::UIInputEvents::MouseUp, widget.scriptWidget);
                        }
                    }
                }

                uiSingleton.clickedEntity = entt::null;
            }

            return false;
        });

        keybindGroup->AddMouseInputValidator("MouseUIInputValidator", [&registry](i32 key, KeybindAction action, KeybindModifier modifier) -> bool
        {
            auto& ctx = registry.ctx();
            auto& uiSingleton = ctx.get<Singletons::UISingleton>();

            if (uiSingleton.clickedEntity != entt::null && action == KeybindAction::Release)
            {
                return true;
            }

            return uiSingleton.allHoveredEntities.size() > 0;
        });
    }

    void HandleInput::Update(entt::registry& registry, f32 deltaTime)
    {
        auto& ctx = registry.ctx();
        auto& uiSingleton = ctx.get<Singletons::UISingleton>();

        InputManager* inputManager = ServiceLocator::GetInputManager();
        vec2 mousePos = inputManager->GetMousePosition();

        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        const vec2& renderSize = renderer->GetRenderSize();
        mousePos.y = renderSize.y - mousePos.y; // Flipped because UI is bottom-left origin

        mousePos = mousePos / renderSize;
        mousePos *= vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);

        uiSingleton.allHoveredEntities.clear();

        auto& transform2DSystem = ECS::Transform2DSystem::Get(registry);

        // Loop over widget roots
        registry.view<Components::UI::WidgetRoot>().each([&](auto entity)
        {
            // Loop over children recursively (depth first)
            transform2DSystem.IterateChildrenRecursiveDepth(entity, [&](auto childEntity)
            {
                auto& widget = registry.get<Components::UI::Widget>(childEntity);

                if (!widget.IsVisible())
                    return false;

                if (!widget.IsInteractable())
                    return true;

                if (widget.type == Components::UI::WidgetType::Canvas) // For now we don't let canvas consume input
                    return true;

                auto* rect = registry.try_get<Components::UI::BoundingRect>(childEntity);
                if (rect == nullptr)
                {
                    return true;
                }

                bool isWithin = IsWithin(mousePos, rect->min, rect->max);

                if (isWithin)
                {
                    Components::Transform2D& transform = registry.get<Components::Transform2D>(childEntity);

                    vec2 middlePoint = (rect->min + rect->max) * 0.5f;

                    u16 numParents = std::numeric_limits<u16>::max() - static_cast<u16>(transform.GetHierarchyDepth());
                    u16 layer = std::numeric_limits<u16>::max() - static_cast<u16>(transform.GetLayer());
                    u32 distanceToMouse = static_cast<u32>(glm::distance(middlePoint, mousePos)); // Distance in pixels

                    u64 key = (static_cast<u64>(numParents) << 48) | (static_cast<u64>(layer) << 32) | distanceToMouse;
                    uiSingleton.allHoveredEntities[key] = childEntity;
                }
                return true;
            });
        });

        DebugRenderer* debugRenderer = ServiceLocator::GetGameRenderer()->GetDebugRenderer();

        // Debug draw last click position
        debugRenderer->DrawBox2D(uiSingleton.lastClickPosition, vec2(5.0f), Color::Magenta);

        // Debug draw last clicked entity
        /*if (uiSingleton.clickedEntity != entt::null)
        {
            entt::entity entity = uiSingleton.clickedEntity;
            Components::UI::BoundingRect& rect = registry.get<Components::UI::BoundingRect>(entity);
            vec2 center = (rect.min + rect.max) * 0.5f;
            vec2 extents = (rect.max - rect.min) * 0.5f;

            debugRenderer->DrawBox2D(center, extents, Color::Green);
        }*/

        // Debug draw all hovered entities
        bool foundHoverEvent = false;
        for (auto& pair : uiSingleton.allHoveredEntities)
        {
            entt::entity entity = pair.second;

            auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(entity);

            if (!eventInputInfo)
            {
                continue;
            }

            if (uiSingleton.hoveredEntity == entity)
            {
                // Same entity is being hovered
                foundHoverEvent = true;
                break;
            }

            if (eventInputInfo->onHoverTemplateHash == 0 && eventInputInfo->onHoverBeginEvent == -1)
            {
                continue;
            }
                
            if (uiSingleton.hoveredEntity != entt::null)
            {
                auto* oldEventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.hoveredEntity);

                oldEventInputInfo->isHovered = false;
                if (oldEventInputInfo)
                {
                    ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.hoveredEntity, *oldEventInputInfo);

                    if (oldEventInputInfo->onHoverEndEvent != -1)
                    {
                        auto& widget = registry.get<Components::UI::Widget>(uiSingleton.hoveredEntity);
                        CallLuaEvent(oldEventInputInfo->onHoverEndEvent, Scripting::UI::UIInputEvents::HoverEnd, widget.scriptWidget);
                    }
                }
            }

            eventInputInfo->isHovered = true;
            ECS::Util::UI::RefreshTemplate(&registry, entity, *eventInputInfo);

            if (eventInputInfo->onHoverBeginEvent != -1)
            {
                auto& widget = registry.get<Components::UI::Widget>(entity);
                CallLuaEvent(eventInputInfo->onHoverBeginEvent, Scripting::UI::UIInputEvents::HoverBegin, widget.scriptWidget);
            }

            uiSingleton.hoveredEntity = entity;
            foundHoverEvent = true;
            break;
        }

        if (!foundHoverEvent)
        {
            if (uiSingleton.hoveredEntity != entt::null)
            {
                auto* oldEventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.hoveredEntity);

                if (oldEventInputInfo)
                {
                    oldEventInputInfo->isHovered = false;
                    ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.hoveredEntity, *oldEventInputInfo);

                    /*if (oldEventInputInfo->onHoverEndEvent != -1)
                    {
                        auto& widget = registry.get<Components::UI::Widget>(uiSingleton.hoveredEntity);
                        CallLuaEvent(oldEventInputInfo->onHoverBeginEvent, Scripting::UI::UIInputEvents::HoverEnd, widget.scriptWidget);
                    }*/
                }

                uiSingleton.hoveredEntity = entt::null;
            }
        }

        if (uiSingleton.clickedEntity != entt::null)
        {
            auto* clickedEventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.clickedEntity);
            if (clickedEventInputInfo)
            {
                if (clickedEventInputInfo->onMouseHeldEvent != -1)
                {
                    auto& widget = registry.get<Components::UI::Widget>(uiSingleton.clickedEntity);
                    CallLuaEvent(clickedEventInputInfo->onMouseHeldEvent, Scripting::UI::UIInputEvents::MouseHeld, widget.scriptWidget, mousePos);
                }
            }
        }
    }
}