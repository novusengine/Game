#include "HandleInput.h"

#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/Panel.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/InputSingleton.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/CameraUtil.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/Rendering/Debug/DebugRenderer.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Scripting/Handlers/UIHandler.h"
#include "Game-Lib/Scripting/UI/Widget.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Renderer/Renderer.h>

#include <Input/InputManager.h>
#include <Input/KeybindGroup.h>

#include <Base/Util/DebugHandler.h>

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <tracy/Tracy.hpp>

#include <map>
#include <numeric>

namespace ECS::Systems::UI
{
    inline bool IsWithin(vec2 point, vec2 min, vec2 max)
    {
        return point.x > min.x && point.x < max.x && point.y > min.y && point.y < max.y;
    }

    void HandleInput::Init(entt::registry& registry)
    {
        auto& uiSingleton = registry.ctx().emplace<Singletons::UISingleton>();

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

                    auto& widget = registry.get<Components::UI::Widget>(entity);

                    u32 templateHash = eventInputInfo->onClickTemplateHash;

                    i32 inputDownEvent = eventInputInfo->onMouseDownEvent;
                    i32 inputUpEvent = eventInputInfo->onMouseUpEvent;
                    i32 heldEvent = eventInputInfo->onMouseHeldEvent;
                    bool hasInputEvent = inputDownEvent != -1 || inputUpEvent != -1 || heldEvent != -1;

                    i32 focusBeginEvent = eventInputInfo->onFocusBeginEvent;
                    i32 focusEndEvent = eventInputInfo->onFocusEndEvent;
                    i32 focusHeldEvent = eventInputInfo->onFocusHeldEvent;
                    bool isFocusable = widget.IsFocusable();
                    bool hasFocusEvent = (focusBeginEvent != -1 || focusEndEvent != -1 || focusHeldEvent != -1) && isFocusable;

                    if (templateHash != 0 || hasInputEvent)
                    {
                        eventInputInfo->isClicked = true;

                        if (templateHash != 0)
                        {
                            ECS::Util::UI::RefreshTemplate(&registry, entity, *eventInputInfo);
                        }

                        if (inputDownEvent != -1)
                        {
                            ECS::Util::UI::CallLuaEvent(inputDownEvent, Scripting::UI::UIInputEvent::MouseDown, widget.scriptWidget, key, mousePos);
                        }

                        if (hasFocusEvent)
                        {
                            ECS::Util::UI::FocusWidgetEntity(&registry, entity);
                        }
                        else
                        {
                            ECS::Util::UI::FocusWidgetEntity(&registry, entt::null);
                        }

                        uiSingleton.clickedEntity = entity;
                        return true;
                    }
                }

                ECS::Util::UI::FocusWidgetEntity(&registry, entt::null);
            }

            if (!isDown && uiSingleton.clickedEntity != entt::null)
            {
                auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.clickedEntity);

                if (eventInputInfo)
                {
                    eventInputInfo->isClicked = false;

                    u32 templateHash = eventInputInfo->onClickTemplateHash;
                    if (templateHash)
                    {
                        ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.clickedEntity, *eventInputInfo);
                    }

                    if (eventInputInfo->onMouseUpEvent != -1)
                    {
                        auto& rect = registry.get<Components::UI::BoundingRect>(uiSingleton.clickedEntity);
                        bool isWithin = IsWithin(mousePos, rect.hoveredMin, rect.hoveredMax);
                        if (isWithin)
                        {
                            auto& widget = registry.get<Components::UI::Widget>(uiSingleton.clickedEntity);
                            ECS::Util::UI::CallLuaEvent(eventInputInfo->onMouseUpEvent, Scripting::UI::UIInputEvent::MouseUp, widget.scriptWidget, key, mousePos);
                        }
                    }
                }

                uiSingleton.clickedEntity = entt::null;
            }

            return false;
        });

        keybindGroup->AddKeyboardCallback("UI Click", GLFW_MOUSE_BUTTON_RIGHT, KeybindAction::Click, KeybindModifier::Any, [&registry, inputManager](i32 key, KeybindAction action, KeybindModifier modifier)
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

                    auto& widget = registry.get<Components::UI::Widget>(entity);

                    u32 templateHash = eventInputInfo->onClickTemplateHash;

                    i32 inputDownEvent = eventInputInfo->onMouseDownEvent;
                    i32 inputUpEvent = eventInputInfo->onMouseUpEvent;
                    i32 heldEvent = eventInputInfo->onMouseHeldEvent;
                    bool hasInputEvent = inputDownEvent != -1 || inputUpEvent != -1 || heldEvent != -1;

                    i32 focusBeginEvent = eventInputInfo->onFocusBeginEvent;
                    i32 focusEndEvent = eventInputInfo->onFocusEndEvent;
                    i32 focusHeldEvent = eventInputInfo->onFocusHeldEvent;
                    bool isFocusable = widget.IsFocusable();
                    bool hasFocusEvent = (focusBeginEvent != -1 || focusEndEvent != -1 || focusHeldEvent != -1) && isFocusable;

                    if (templateHash != 0 || hasInputEvent)
                    {
                        eventInputInfo->isClicked = true;

                        if (templateHash != 0)
                        {
                            ECS::Util::UI::RefreshTemplate(&registry, entity, *eventInputInfo);
                        }

                        if (inputDownEvent != -1)
                        {
                            ECS::Util::UI::CallLuaEvent(inputDownEvent, Scripting::UI::UIInputEvent::MouseDown, widget.scriptWidget, key, mousePos);
                        }

                        if (hasFocusEvent)
                        {
                            ECS::Util::UI::FocusWidgetEntity(&registry, entity);
                        }
                        else
                        {
                            ECS::Util::UI::FocusWidgetEntity(&registry, entt::null);
                        }

                        uiSingleton.clickedEntity = entity;
                        return true;
                    }
                }

                ECS::Util::UI::FocusWidgetEntity(&registry, entt::null);
            }

            if (!isDown && uiSingleton.clickedEntity != entt::null)
            {
                auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.clickedEntity);

                if (eventInputInfo)
                {
                    eventInputInfo->isClicked = false;

                    u32 templateHash = eventInputInfo->onClickTemplateHash;
                    if (templateHash)
                    {
                        ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.clickedEntity, *eventInputInfo);
                    }

                    if (eventInputInfo->onMouseUpEvent != -1)
                    {
                        auto& rect = registry.get<Components::UI::BoundingRect>(uiSingleton.clickedEntity);
                        bool isWithin = IsWithin(mousePos, rect.hoveredMin, rect.hoveredMax);
                        if (isWithin)
                        {
                            auto& widget = registry.get<Components::UI::Widget>(uiSingleton.clickedEntity);
                            ECS::Util::UI::CallLuaEvent(eventInputInfo->onMouseUpEvent, Scripting::UI::UIInputEvent::MouseUp, widget.scriptWidget, key, mousePos);
                        }
                    }
                }

                uiSingleton.clickedEntity = entt::null;
            }

            return false;
        });

        keybindGroup->AddMouseScrollCallback([&registry, inputManager](f32 xPos, f32 yPos)
        {
            auto& ctx = registry.ctx();
            auto& uiSingleton = ctx.get<Singletons::UISingleton>();

            vec2 mousePos = inputManager->GetMousePosition();

            Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
            const vec2& renderSize = renderer->GetRenderSize();
            mousePos.y = renderSize.y - mousePos.y; // Flipped because UI is bottom-left origin

            mousePos = mousePos / renderSize;
            mousePos *= vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);

            for (auto& pair : uiSingleton.allHoveredEntities)
            {
                entt::entity entity = pair.second;

                auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(entity);

                if (!eventInputInfo)
                {
                    continue;
                }

                auto& widget = registry.get<Components::UI::Widget>(entity);

                i32 scrollEvent = eventInputInfo->onMouseScrollEvent;

                bool hasInputEvent = scrollEvent != -1;

                i32 focusBeginEvent = eventInputInfo->onFocusBeginEvent;
                i32 focusEndEvent = eventInputInfo->onFocusEndEvent;
                i32 focusHeldEvent = eventInputInfo->onFocusHeldEvent;
                bool isFocusable = widget.IsFocusable();
                bool hasFocusEvent = (focusBeginEvent != -1 || focusEndEvent != -1 || focusHeldEvent != -1) && isFocusable;

                if (hasInputEvent)
                {
                    vec2 scrollPos = vec2(xPos, yPos);
                    ECS::Util::UI::CallLuaEvent(scrollEvent, Scripting::UI::UIInputEvent::MouseScroll, widget.scriptWidget, scrollPos);

                    if (hasFocusEvent)
                    {
                        ECS::Util::UI::FocusWidgetEntity(&registry, entity);
                    }
                    else
                    {
                        ECS::Util::UI::FocusWidgetEntity(&registry, entt::null);
                    }

                    uiSingleton.clickedEntity = entity;
                    return true;
                }
            }

            ECS::Util::UI::FocusWidgetEntity(&registry, entt::null);

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

        keybindGroup->AddAnyUnicodeCallback([&registry](u32 unicode) -> bool
        {
            auto& ctx = registry.ctx();
            auto& uiSingleton = ctx.get<Singletons::UISingleton>();

            if (uiSingleton.focusedEntity != entt::null)
            {
                auto* widget = registry.try_get<Components::UI::Widget>(uiSingleton.focusedEntity);
                auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.focusedEntity);
                if (!widget || !eventInputInfo)
                {
                    return false;
                }

                return ECS::Util::UI::CallUnicodeEvent(eventInputInfo->onKeyboardEvent, widget->scriptWidget, unicode);
            }

            return false;
        });

        keybindGroup->AddKeyboardInputValidator("KeyboardUIInputValidator", [&registry](i32 key, KeybindAction action, KeybindModifier modifier) -> bool
        {
            auto& ctx = registry.ctx();
            auto* uiSingleton = ctx.find<Singletons::UISingleton>();
            auto* inputSingleton = ctx.find<Singletons::InputSingleton>();
            if (!uiSingleton || !inputSingleton)
                return false;

            if (uiSingleton->focusedEntity != entt::null)
            {
                auto* widget = registry.try_get<Components::UI::Widget>(uiSingleton->focusedEntity);
                auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton->focusedEntity);
                if (!widget || !eventInputInfo)
                {
                    return false;
                }

                if (ECS::Util::UI::CallKeyboardEvent(eventInputInfo->onKeyboardEvent, widget->scriptWidget, key, static_cast<i32>(action), static_cast<i32>(modifier)))
                {
                    return true;
                }
            }

            if (!ECS::Util::CameraUtil::IsCapturingMouse())
            {
                for (i32 keyboardEvent : inputSingleton->globalKeyboardEvents)
                {
                    if (ECS::Util::UI::CallKeyboardEvent(keyboardEvent, key, static_cast<i32>(action), static_cast<i32>(modifier)))
                    {
                        return true;
                    }
                }
            }

            return false;
        });
    }

    // Walks `entity`'s widget subtree to collect hovered entities. Each widget's world rect is composed
    // top-down: `originWorldPos` is where `entity`'s child-origin lands in screen space, and every level
    // adds its own local offset. (Translation only — matches the legacy cached-rect behaviour, which also
    // ignores ancestor scale/rotation in hit-testing.) Recursive because a Panel can host a RenderTarget
    // canvas as its texture; there we reset the origin to the host panel's screen rect.
    void RecursivelyFindHoveredInCanvas(entt::registry& registry, entt::entity entity, const vec2& mousePos, std::map<u64, entt::entity>& allHoveredEntities, const vec2& originWorldPos, const vec2& clipMax)
    {
        auto& transform2DSystem = ECS::Transform2DSystem::Get(registry);

        // A hidden (sub)canvas hides its whole subtree (the legacy DFS skipped it via the per-node visibility check).
        if (auto* entityWidget = registry.try_get<Components::UI::Widget>(entity); entityWidget != nullptr && !entityWidget->IsVisible())
            return;

        transform2DSystem.IterateChildren(entity, [&](entt::entity childEntity)
        {
            auto& widget = registry.get<Components::UI::Widget>(childEntity);

            if (!widget.IsVisible())
                return; // skip this widget and its subtree

            auto& childTransform = registry.get<Components::Transform2D>(childEntity);
            auto* rect = registry.try_get<Components::UI::BoundingRect>(childEntity);
            auto* clipper = registry.try_get<Components::UI::Clipper>(childEntity);

            // Compose this widget's world rect from the parent's origin. 3D-anchored widgets are
            // camera-projected (not chain-composed), so fall back to their maintained BoundingRect.
            bool is3D = widget.worldTransformIndex != std::numeric_limits<u32>::max();
            vec2 size = childTransform.GetSize();
            vec2 worldMin = is3D ? (rect != nullptr ? rect->min : originWorldPos)
                                 : originWorldPos + childTransform.GetLocalTranslation();
            vec2 cappedMax = glm::min(worldMin + size, clipMax);

            // Clip pruning: a clipChildren container whose clip region doesn't contain the cursor can't
            // have any hoverable descendant (they're all clipped to it), so skip the whole subtree.
            if (clipper != nullptr && clipper->clipChildren)
            {
                vec2 clipMin = worldMin + size * clipper->clipRegionMin;
                vec2 clipRegionMax = worldMin + size * clipper->clipRegionMax;
                if (!IsWithin(mousePos, clipMin, clipRegionMax))
                    return;
            }

            // Narrow-phase hit test. Non-interactable / canvas / rect-less widgets don't consume input
            // but we still descend into their children.
            if (widget.IsInteractable() && widget.type != Components::UI::WidgetType::Canvas && rect != nullptr)
            {
                bool isWithin = IsWithin(mousePos, worldMin, cappedMax);

                // Respect clipping for input: a widget inside a clip-children ancestor (e.g. a scrollbox)
                // must not capture clicks outside that ancestor's visible region, even though its own
                // rect — when scrolled — extends beyond it.
                if (isWithin)
                {
                    entt::entity clipAncestor = ECS::Util::UI::GetClippingAncestor(&registry, childEntity);
                    if (clipAncestor != entt::null)
                    {
                        auto* clipRect = registry.try_get<Components::UI::BoundingRect>(clipAncestor);
                        auto* ancestorClipper = registry.try_get<Components::UI::Clipper>(clipAncestor);
                        if (clipRect != nullptr && ancestorClipper != nullptr)
                        {
                            vec2 clipSize = clipRect->max - clipRect->min;
                            vec2 clipMin = clipRect->min + clipSize * ancestorClipper->clipRegionMin;
                            vec2 clipRegionMax = clipRect->min + clipSize * ancestorClipper->clipRegionMax;
                            isWithin = IsWithin(mousePos, clipMin, clipRegionMax);
                        }
                    }
                }

                if (isWithin)
                {
                    // Cached for the hover/mouse-up dispatch's hit-test (read on a later frame).
                    rect->hoveredMin = worldMin;
                    rect->hoveredMax = cappedMax;

                    vec2 middlePoint = (worldMin + cappedMax) * 0.5f;
                    u32 distanceToMouse = static_cast<u32>(glm::distance(middlePoint, mousePos));

                    // Invert sortKey: higher sortKey draws on top, but the map dispatches smallest first.
                    u32 invertedSortKey = std::numeric_limits<u32>::max() - widget.sortKey;
                    u64 key = (static_cast<u64>(invertedSortKey) << 32) | distanceToMouse;
                    allHoveredEntities[key] = childEntity;
                }
            }

            // Recurse into a hosted RenderTarget canvas (origin resets to this panel's screen rect)...
            if (widget.type == Components::UI::WidgetType::Panel)
            {
                auto& panelTemplate = registry.get<Components::UI::PanelTemplate>(childEntity);
                if (panelTemplate.setFlags.backgroundRT)
                {
                    RecursivelyFindHoveredInCanvas(registry, panelTemplate.backgroundRTEntity, mousePos, allHoveredEntities, worldMin, cappedMax);
                }
            }

            // ...and into this widget's own children, composing from this widget's world origin.
            RecursivelyFindHoveredInCanvas(registry, childEntity, mousePos, allHoveredEntities, worldMin, clipMax);
        });
    }

    void HandleInput::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("UI::HandleInput::Update");
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

        if (uiSingleton.cursorCanvasEntity != entt::null)
            transform2DSystem.SetWorldPosition(uiSingleton.cursorCanvasEntity, mousePos);

        // Loop over widget roots
        if (!inputManager->IsCursorVirtual())
        {
            ZoneScopedN("UI::HandleInput::FindHovered");
            registry.view<Components::UI::Canvas>(entt::exclude<Components::UI::CanvasRenderTargetTag>).each([&](auto entity, auto& canvas)
            {
                vec2 canvasWorldPos = registry.get<Components::Transform2D>(entity).GetWorldPosition();
                RecursivelyFindHoveredInCanvas(registry, entity, mousePos, uiSingleton.allHoveredEntities, canvasWorldPos, renderSize);
                /*// Loop over children recursively (depth first)
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
                });*/
            });
        }

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
        bool imGuiWantsMouseInput = ImGui::GetIO().WantCaptureMouse;

        bool foundHoverEvent = false;
        if (!imGuiWantsMouseInput)
        {
            ZoneScopedN("UI::HandleInput::HoverEvents");
            for (auto& pair : uiSingleton.allHoveredEntities)
            {
                entt::entity entity = pair.second;

                auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(entity);

                if (!eventInputInfo)
                {
                    continue;
                }

                const bool hasHoverEffect = eventInputInfo->onHoverTemplateHash != 0
                    || eventInputInfo->onHoverBeginEvent != -1
                    || eventInputInfo->onHoverEndEvent != -1
                    || eventInputInfo->onHoverHeldEvent != -1;
                const bool hasMouseEffect = eventInputInfo->onClickTemplateHash != 0
                    || eventInputInfo->onMouseDownEvent != -1
                    || eventInputInfo->onMouseUpEvent != -1
                    || eventInputInfo->onMouseHeldEvent != -1
                    || eventInputInfo->onMouseScrollEvent != -1;

                // A widget with mouse handlers but no hover handlers still consumes
                // the hover so it doesn't fall through to a widget behind it.
                if (!hasHoverEffect && !hasMouseEffect)
                    continue;

                if (uiSingleton.hoveredEntity == entity)
                {
                    if (eventInputInfo->onHoverHeldEvent != -1)
                    {
                        auto& widget = registry.get<Components::UI::Widget>(uiSingleton.hoveredEntity);
                        ECS::Util::UI::CallLuaEvent(eventInputInfo->onHoverHeldEvent, Scripting::UI::UIInputEvent::HoverHeld, widget.scriptWidget);
                    }

                    foundHoverEvent = true;
                    break;
                }

                if (uiSingleton.hoveredEntity != entt::null)
                {
                    auto* oldEventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.hoveredEntity);

                    if (oldEventInputInfo)
                    {
                        oldEventInputInfo->isHovered = false;

                        u32 hoverTemplate = oldEventInputInfo->onHoverTemplateHash;
                        if (hoverTemplate != 0)
                            ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.hoveredEntity, *oldEventInputInfo);

                        if (oldEventInputInfo->onHoverEndEvent != -1)
                        {
                            auto& widget = registry.get<Components::UI::Widget>(uiSingleton.hoveredEntity);
                            ECS::Util::UI::CallLuaEvent(oldEventInputInfo->onHoverEndEvent, Scripting::UI::UIInputEvent::HoverEnd, widget.scriptWidget);
                        }
                    }
                }

                if (hasHoverEffect)
                {
                    eventInputInfo->isHovered = true;

                    u32 hoverTemplate = eventInputInfo->onHoverTemplateHash;
                    if (hoverTemplate != 0)
                        ECS::Util::UI::RefreshTemplate(&registry, entity, *eventInputInfo);

                    if (eventInputInfo->onHoverBeginEvent != -1)
                    {
                        auto& widget = registry.get<Components::UI::Widget>(entity);
                        ECS::Util::UI::CallLuaEvent(eventInputInfo->onHoverBeginEvent, Scripting::UI::UIInputEvent::HoverBegin, widget.scriptWidget);
                    }

                    uiSingleton.hoveredEntity = entity;
                }
                else
                {
                    uiSingleton.hoveredEntity = entt::null;
                }
                foundHoverEvent = true;
                break;
            }
        }

        if (!foundHoverEvent)
        {
            if (uiSingleton.hoveredEntity != entt::null)
            {
                auto* oldEventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.hoveredEntity);

                if (oldEventInputInfo)
                {
                    oldEventInputInfo->isHovered = false;

                    u32 hoverTemplate = oldEventInputInfo->onHoverTemplateHash;
                    if (hoverTemplate != 0)
                        ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.hoveredEntity, *oldEventInputInfo);

                    if (oldEventInputInfo->onHoverEndEvent != -1)
                    {
                        auto& widget = registry.get<Components::UI::Widget>(uiSingleton.hoveredEntity);
                        ECS::Util::UI::CallLuaEvent(oldEventInputInfo->onHoverEndEvent, Scripting::UI::UIInputEvent::HoverEnd, widget.scriptWidget);
                    }
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
                    ZoneScopedN("UI::HandleInput::MouseHeld");
                    auto& widget = registry.get<Components::UI::Widget>(uiSingleton.clickedEntity);
                    ECS::Util::UI::CallLuaEvent(clickedEventInputInfo->onMouseHeldEvent, Scripting::UI::UIInputEvent::MouseHeld, widget.scriptWidget, mousePos);
                }
            }
        }

        if (uiSingleton.focusedEntity != entt::null)
        {
            auto* eventInputInfo = registry.try_get<ECS::Components::UI::EventInputInfo>(uiSingleton.focusedEntity);
            if (eventInputInfo && eventInputInfo->onFocusHeldEvent != -1)
            {
                auto& widget = registry.get<ECS::Components::UI::Widget>(uiSingleton.focusedEntity);
                ECS::Util::UI::CallLuaEvent(eventInputInfo->onFocusHeldEvent, Scripting::UI::UIInputEvent::FocusHeld, widget.scriptWidget, deltaTime);
            }
        }

        uiSingleton.justFocusedEntity = entt::null;
    }
}