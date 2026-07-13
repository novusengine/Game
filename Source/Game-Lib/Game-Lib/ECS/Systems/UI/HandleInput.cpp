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
#include "Game-Lib/ECS/Util/UIInputUtil.h"
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
#include <Base/CVarSystem/CVarSystem.h>

#include <entt/entt.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <numeric>

AutoCVar_Int CVAR_UIInputDebugLevel(CVarCategory::Client | CVarCategory::Rendering, "uiInputDebugLevel", "UI input debug detail: 0=off, 1=accepted/hovered, 2=in-bounds candidates, 3=discarded elements", 0);

namespace ECS::Systems::UI
{
    using UIInputCandidate = Singletons::UIInputCandidate;

    void RecursivelyFindHoveredInCanvas(entt::registry& registry, entt::entity entity, const vec2& mousePos, std::vector<UIInputCandidate>& allHoveredEntities, const vec2& originWorldPos, const vec2& clipMax, std::vector<Singletons::UIInputDebugRecord>* discardedRecords);

    bool RebuildCandidates(entt::registry& registry, const vec2& physicalMousePosition, Singletons::UISingleton& uiSingleton, vec2& outMousePosition, bool gatherDiscarded)
    {
        if (gatherDiscarded)
            uiSingleton.inputDebugSnapshot.records.clear();

        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        const vec2 referenceSize(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);
        if (!ECS::Util::UIInput::PhysicalTopLeftToReference(physicalMousePosition, renderer->GetRenderSize(), referenceSize, outMousePosition))
        {
            uiSingleton.allHoveredEntities.clear();
            return false;
        }

        uiSingleton.allHoveredEntities.clear();
        auto* discarded = gatherDiscarded ? &uiSingleton.inputDebugSnapshot.records : nullptr;
        registry.view<Components::UI::Canvas>(entt::exclude<Components::UI::CanvasRenderTargetTag>).each([&](auto entity, auto& canvas)
        {
            vec2 canvasWorldPos = registry.get<Components::Transform2D>(entity).GetWorldPosition();
            RecursivelyFindHoveredInCanvas(registry, entity, outMousePosition, uiSingleton.allHoveredEntities, canvasWorldPos, referenceSize, discarded);
        });

        std::sort(uiSingleton.allHoveredEntities.begin(), uiSingleton.allHoveredEntities.end(), [](const UIInputCandidate& lhs, const UIInputCandidate& rhs)
        {
            if (lhs.sortKey != rhs.sortKey)
                return lhs.sortKey > rhs.sortKey;
            if (lhs.distanceToMouse != rhs.distanceToMouse)
                return lhs.distanceToMouse < rhs.distanceToMouse;
            return entt::to_integral(lhs.entity) < entt::to_integral(rhs.entity);
        });
        return true;
    }

    i32 GetUIInputDebugLevel()
    {
        return std::clamp(CVAR_UIInputDebugLevel.Get(), 0, 3);
    }

    void BeginDebugSnapshot(Singletons::UISingleton& uiSingleton, Singletons::UIInputEventKind eventKind, const vec2& mousePosition, i32 debugLevel)
    {
        auto& snapshot = uiSingleton.inputDebugSnapshot;
        if (debugLevel < 3)
            snapshot.records.clear();
        snapshot.eventKind = eventKind;
        snapshot.mousePosition = mousePosition;
        snapshot.consumedWithoutAcceptedElement = false;
        snapshot.truncated = 0;

        if (debugLevel >= 2)
        {
            const u32 availableRecords = Singletons::UIInputDebugSnapshot::MAX_RECORDS - static_cast<u32>(snapshot.records.size());
            const u32 count = std::min(static_cast<u32>(uiSingleton.allHoveredEntities.size()), availableRecords);
            snapshot.records.reserve(std::max(snapshot.records.capacity(), static_cast<size_t>(count)));
            for (u32 i = 0; i < count; i++)
            {
                const auto& candidate = uiSingleton.allHoveredEntities[i];
                snapshot.records.push_back({ candidate.entity, candidate.min, candidate.max, Singletons::UIInputDebugResult::Considered, candidate.sortKey, i });
            }
            snapshot.truncated += static_cast<u32>(uiSingleton.allHoveredEntities.size()) - count;
        }
    }

    void MarkAccepted(Singletons::UISingleton& uiSingleton, const UIInputCandidate& candidate, i32 debugLevel)
    {
        auto& records = uiSingleton.inputDebugSnapshot.records;
        for (auto& record : records)
        {
            if (record.entity == candidate.entity && record.result == Singletons::UIInputDebugResult::Considered)
            {
                record.result = Singletons::UIInputDebugResult::Accepted;
                return;
            }
        }

        if (debugLevel >= 1 && records.size() < Singletons::UIInputDebugSnapshot::MAX_RECORDS)
            records.push_back({ candidate.entity, candidate.min, candidate.max, Singletons::UIInputDebugResult::Accepted, candidate.sortKey, 0 });
    }

    void CancelClickedEntity(entt::registry& registry, Singletons::UISingleton& uiSingleton)
    {
        if (uiSingleton.clickedEntity == entt::null)
            return;

        if (auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.clickedEntity))
        {
            eventInputInfo->isClicked = false;
            if (eventInputInfo->onClickTemplateHash != 0)
                ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.clickedEntity, *eventInputInfo);
        }

        uiSingleton.clickedEntity = entt::null;
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

            bool isDown = action == KeybindAction::Press;

            if (inputManager->IsCursorVirtual() || ImGui::GetIO().WantCaptureMouse)
            {
                if (!isDown)
                    CancelClickedEntity(registry, uiSingleton);
                return false;
            }

            vec2 mousePos;
            const i32 debugLevel = GetUIInputDebugLevel();
            if (!RebuildCandidates(registry, inputManager->GetMousePosition(), uiSingleton, mousePos, debugLevel >= 3))
                return false;
            BeginDebugSnapshot(uiSingleton, isDown ? Singletons::UIInputEventKind::Press : Singletons::UIInputEventKind::Release, mousePos, debugLevel);

            if (isDown)
            {
                for (auto& pair : uiSingleton.allHoveredEntities)
                {
                    entt::entity entity = pair.entity;

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
                        MarkAccepted(uiSingleton, pair, debugLevel);
                        return true;
                    }
                }

                ECS::Util::UI::FocusWidgetEntity(&registry, entt::null);
            }

            if (!isDown && uiSingleton.clickedEntity != entt::null)
            {
                auto candidateIt = std::find_if(uiSingleton.allHoveredEntities.begin(), uiSingleton.allHoveredEntities.end(), [&](const UIInputCandidate& candidate)
                {
                    return candidate.entity == uiSingleton.clickedEntity;
                });
                auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.clickedEntity);

                if (eventInputInfo)
                {
                    eventInputInfo->isClicked = false;

                    u32 templateHash = eventInputInfo->onClickTemplateHash;
                    if (templateHash)
                    {
                        ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.clickedEntity, *eventInputInfo);
                    }

                    if (candidateIt != uiSingleton.allHoveredEntities.end())
                    {
                        if (eventInputInfo->onMouseUpEvent != -1)
                        {
                            auto& widget = registry.get<Components::UI::Widget>(uiSingleton.clickedEntity);
                            ECS::Util::UI::CallLuaEvent(eventInputInfo->onMouseUpEvent, Scripting::UI::UIInputEvent::MouseUp, widget.scriptWidget, key, mousePos);
                        }

                        MarkAccepted(uiSingleton, *candidateIt, debugLevel);
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

            bool isDown = action == KeybindAction::Press;

            if (inputManager->IsCursorVirtual() || ImGui::GetIO().WantCaptureMouse)
            {
                if (!isDown)
                    CancelClickedEntity(registry, uiSingleton);
                return false;
            }

            vec2 mousePos;
            const i32 debugLevel = GetUIInputDebugLevel();
            if (!RebuildCandidates(registry, inputManager->GetMousePosition(), uiSingleton, mousePos, debugLevel >= 3))
                return false;
            BeginDebugSnapshot(uiSingleton, isDown ? Singletons::UIInputEventKind::Press : Singletons::UIInputEventKind::Release, mousePos, debugLevel);
            if (isDown)
            {
                for (auto& pair : uiSingleton.allHoveredEntities)
                {
                    entt::entity entity = pair.entity;

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
                        MarkAccepted(uiSingleton, pair, debugLevel);
                        return true;
                    }
                }

                ECS::Util::UI::FocusWidgetEntity(&registry, entt::null);
            }

            if (!isDown && uiSingleton.clickedEntity != entt::null)
            {
                auto candidateIt = std::find_if(uiSingleton.allHoveredEntities.begin(), uiSingleton.allHoveredEntities.end(), [&](const UIInputCandidate& candidate)
                {
                    return candidate.entity == uiSingleton.clickedEntity;
                });
                auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(uiSingleton.clickedEntity);

                if (eventInputInfo)
                {
                    eventInputInfo->isClicked = false;

                    u32 templateHash = eventInputInfo->onClickTemplateHash;
                    if (templateHash)
                    {
                        ECS::Util::UI::RefreshTemplate(&registry, uiSingleton.clickedEntity, *eventInputInfo);
                    }

                    if (candidateIt != uiSingleton.allHoveredEntities.end())
                    {
                        if (eventInputInfo->onMouseUpEvent != -1)
                        {
                            auto& widget = registry.get<Components::UI::Widget>(uiSingleton.clickedEntity);
                            ECS::Util::UI::CallLuaEvent(eventInputInfo->onMouseUpEvent, Scripting::UI::UIInputEvent::MouseUp, widget.scriptWidget, key, mousePos);
                        }

                        MarkAccepted(uiSingleton, *candidateIt, debugLevel);
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

            if (inputManager->IsCursorVirtual() || ImGui::GetIO().WantCaptureMouse)
                return false;

            vec2 mousePos;
            const i32 debugLevel = GetUIInputDebugLevel();
            if (!RebuildCandidates(registry, inputManager->GetMousePosition(), uiSingleton, mousePos, debugLevel >= 3))
                return false;
            BeginDebugSnapshot(uiSingleton, Singletons::UIInputEventKind::Wheel, mousePos, debugLevel);

            for (auto& pair : uiSingleton.allHoveredEntities)
            {
                entt::entity entity = pair.entity;

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

                    MarkAccepted(uiSingleton, pair, debugLevel);
                    return true;
                }
            }

            ECS::Util::UI::FocusWidgetEntity(&registry, entt::null);

            return false;
        });

        keybindGroup->AddMouseInputValidator("MouseUIInputValidator", [&registry, inputManager](i32 key, KeybindAction action, KeybindModifier modifier) -> bool
        {
            auto& ctx = registry.ctx();
            auto& uiSingleton = ctx.get<Singletons::UISingleton>();

            if (uiSingleton.clickedEntity != entt::null && action == KeybindAction::Release)
            {
                return true;
            }

            if (inputManager->IsCursorVirtual() || ImGui::GetIO().WantCaptureMouse)
                return false;

            for (const auto& candidate : uiSingleton.allHoveredEntities)
            {
                const auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(candidate.entity);
                if (eventInputInfo == nullptr)
                    continue;

                if (eventInputInfo->onClickTemplateHash != 0
                    || eventInputInfo->onMouseDownEvent != -1
                    || eventInputInfo->onMouseUpEvent != -1
                    || eventInputInfo->onMouseHeldEvent != -1)
                    return true;
            }

            return false;
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
    void RecursivelyFindHoveredInCanvas(entt::registry& registry, entt::entity entity, const vec2& mousePos, std::vector<UIInputCandidate>& allHoveredEntities, const vec2& originWorldPos, const vec2& clipMax, std::vector<Singletons::UIInputDebugRecord>* discardedRecords)
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
            const bool hasDrawableRect = cappedMax.x > worldMin.x && cappedMax.y > worldMin.y;

            // Clip pruning: a clipChildren container whose clip region doesn't contain the cursor can't
            // have any hoverable descendant (they're all clipped to it), so skip the whole subtree.
            if (clipper != nullptr && clipper->clipChildren)
            {
                vec2 clipMin = worldMin + size * clipper->clipRegionMin;
                vec2 clipRegionMax = worldMin + size * clipper->clipRegionMax;
                if (!ECS::Util::UIInput::IsWithin(mousePos, clipMin, clipRegionMax))
                {
                    if (discardedRecords != nullptr && hasDrawableRect && discardedRecords->size() < Singletons::UIInputDebugSnapshot::MAX_RECORDS)
                        discardedRecords->push_back({ childEntity, worldMin, cappedMax, Singletons::UIInputDebugResult::Clipped, widget.sortKey, 0 });
                    return;
                }
            }

            // Narrow-phase hit test. Non-interactable / canvas / rect-less widgets don't consume input
            // but we still descend into their children.
            if (widget.IsInteractable() && widget.type != Components::UI::WidgetType::Canvas && rect != nullptr)
            {
                bool isWithin = ECS::Util::UIInput::IsWithin(mousePos, worldMin, cappedMax);

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
                            isWithin = ECS::Util::UIInput::IsWithin(mousePos, clipMin, clipRegionMax);
                        }
                    }
                }

                if (isWithin)
                {
                    // Retained for compatibility with existing hover inspection; event dispatch uses
                    // the candidate rectangle captured by the fresh event-time evaluation.
                    rect->hoveredMin = worldMin;
                    rect->hoveredMax = cappedMax;

                    vec2 middlePoint = (worldMin + cappedMax) * 0.5f;
                    allHoveredEntities.push_back({ childEntity, worldMin, cappedMax, widget.sortKey, glm::distance(middlePoint, mousePos) });
                }
                else if (discardedRecords != nullptr && hasDrawableRect && discardedRecords->size() < Singletons::UIInputDebugSnapshot::MAX_RECORDS)
                {
                    discardedRecords->push_back({ childEntity, worldMin, cappedMax, Singletons::UIInputDebugResult::OutsideBounds, widget.sortKey, 0 });
                }
            }
            else if (discardedRecords != nullptr && rect != nullptr && hasDrawableRect && widget.type != Components::UI::WidgetType::Canvas && discardedRecords->size() < Singletons::UIInputDebugSnapshot::MAX_RECORDS)
            {
                discardedRecords->push_back({ childEntity, worldMin, cappedMax, Singletons::UIInputDebugResult::NotInteractable, widget.sortKey, 0 });
            }

            // Recurse into a hosted RenderTarget canvas (origin resets to this panel's screen rect)...
            if (widget.type == Components::UI::WidgetType::Panel)
            {
                auto& panelTemplate = registry.get<Components::UI::PanelTemplate>(childEntity);
                if (panelTemplate.setFlags.backgroundRT)
                {
                    RecursivelyFindHoveredInCanvas(registry, panelTemplate.backgroundRTEntity, mousePos, allHoveredEntities, worldMin, cappedMax, discardedRecords);
                }
            }

            // ...and into this widget's own children, composing from this widget's world origin.
            RecursivelyFindHoveredInCanvas(registry, childEntity, mousePos, allHoveredEntities, worldMin, clipMax, discardedRecords);
        });
    }

    void HandleInput::Update(entt::registry& registry, f32 deltaTime)
    {
        ZoneScopedN("UI::HandleInput::Update");
        auto& ctx = registry.ctx();
        auto& uiSingleton = ctx.get<Singletons::UISingleton>();

        InputManager* inputManager = ServiceLocator::GetInputManager();
        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        const vec2 referenceSize(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);
        vec2 mousePos;
        const bool cursorIsVirtual = inputManager->IsCursorVirtual();
        const bool hasValidMousePosition = ECS::Util::UIInput::PhysicalTopLeftToReference(inputManager->GetMousePosition(), renderer->GetRenderSize(), referenceSize, mousePos);

        // Cursor-following visuals are independent of whether the cursor may
        // currently interact with UI. In virtual/captured mode InputManager
        // retains the last physical position specifically for UI presentation.
        const bool hasValidInputCandidates = !cursorIsVirtual
            && hasValidMousePosition
            && RebuildCandidates(registry, inputManager->GetMousePosition(), uiSingleton, mousePos, false);

        auto& transform2DSystem = ECS::Transform2DSystem::Get(registry);

        if (hasValidMousePosition && uiSingleton.cursorCanvasEntity != entt::null)
            transform2DSystem.SetWorldPosition(uiSingleton.cursorCanvasEntity, mousePos);

        if (!hasValidInputCandidates)
            uiSingleton.allHoveredEntities.clear();

        DebugRenderer* debugRenderer = ServiceLocator::GetGameRenderer()->GetDebugRenderer();

        const i32 debugLevel = GetUIInputDebugLevel();
        if (debugLevel > 0)
        {
            const vec2& renderSize = ServiceLocator::GetGameRenderer()->GetRenderer()->GetRenderSize();
            if (renderSize.x > 0.0f && renderSize.y > 0.0f)
            {
                const vec2 referenceSize(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);
                const vec2 scale = renderSize / referenceSize;
                auto drawRect = [&](const vec2& min, const vec2& max, Color color)
                {
                    const vec2 physicalMin = min * scale;
                    const vec2 physicalMax = max * scale;
                    debugRenderer->DrawBox2D((physicalMin + physicalMax) * 0.5f, (physicalMax - physicalMin) * 0.5f, color);
                };
                auto drawResult = [&](Singletons::UIInputDebugResult result, Color color)
                {
                    for (const auto& record : uiSingleton.inputDebugSnapshot.records)
                    {
                        if (record.result != result)
                            continue;

                        drawRect(record.min, record.max, color);
                    }
                };

                if (debugLevel >= 3)
                {
                    drawResult(Singletons::UIInputDebugResult::OutsideBounds, Color::Red);
                    drawResult(Singletons::UIInputDebugResult::Clipped, Color::Red);
                    drawResult(Singletons::UIInputDebugResult::Hidden, Color::Red);
                    drawResult(Singletons::UIInputDebugResult::NotInteractable, Color::Red);
                    drawResult(Singletons::UIInputDebugResult::MissingBounds, Color::Red);
                }
                if (debugLevel >= 2)
                    drawResult(Singletons::UIInputDebugResult::Considered, Color::Yellow);

                if (!ImGui::GetIO().WantCaptureMouse)
                {
                    for (const auto& candidate : uiSingleton.allHoveredEntities)
                    {
                        const auto* eventInputInfo = registry.try_get<Components::UI::EventInputInfo>(candidate.entity);
                        if (eventInputInfo == nullptr)
                            continue;

                        const bool hasHoverEffect = eventInputInfo->onHoverTemplateHash != 0
                            || eventInputInfo->onHoverBeginEvent != -1
                            || eventInputInfo->onHoverEndEvent != -1
                            || eventInputInfo->onHoverHeldEvent != -1;
                        const bool hasMouseEffect = eventInputInfo->onClickTemplateHash != 0
                            || eventInputInfo->onMouseDownEvent != -1
                            || eventInputInfo->onMouseUpEvent != -1
                            || eventInputInfo->onMouseHeldEvent != -1
                            || eventInputInfo->onMouseScrollEvent != -1;
                        if (!hasHoverEffect && !hasMouseEffect)
                            continue;

                        drawRect(candidate.min, candidate.max, Color::Cyan);
                        break;
                    }
                }

                drawResult(Singletons::UIInputDebugResult::Accepted, Color::Green);
            }
        }

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

        if (cursorIsVirtual || imGuiWantsMouseInput)
            CancelClickedEntity(registry, uiSingleton);

        bool foundHoverEvent = false;
        if (!imGuiWantsMouseInput)
        {
            ZoneScopedN("UI::HandleInput::HoverEvents");
            for (auto& pair : uiSingleton.allHoveredEntities)
            {
                entt::entity entity = pair.entity;

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
