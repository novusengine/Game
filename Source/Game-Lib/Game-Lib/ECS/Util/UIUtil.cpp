#include "UIUtil.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/Clipper.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/Panel.h"
#include "Game-Lib/ECS/Components/UI/PanelTemplate.h"
#include "Game-Lib/ECS/Components/UI/Text.h"
#include "Game-Lib/ECS/Components/UI/TextTemplate.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Canvas/CanvasRenderer.h"
#include "Game-Lib/Scripting/Handlers/UIHandler.h"
#include "Game-Lib/Scripting/UI/Widget.h"
#include "Game-Lib/Scripting/Util/ZenithUtil.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <MetaGen/Game/Lua/Lua.h>

#include <Scripting/LuaManager.h>
#include <Scripting/Zenith.h>

#include <Renderer/Font.h>
#include <Renderer/RenderSettings.h>

#include <Base/Util/StringUtils.h>

#include <entt/entt.hpp>

namespace ECS::Util
{
    namespace UI
    {
        entt::entity FindOwningCanvas(entt::registry* registry, entt::entity entity)
        {
            if (entity == entt::null) 
                return entt::null;

            auto* widget = registry->try_get<ECS::Components::UI::Widget>(entity);
            if (!widget) 
                return entt::null;

            if (widget->type == ECS::Components::UI::WidgetType::Canvas)
                return entity;

            if (widget->scriptWidget)
                return widget->scriptWidget->canvasEntity;

            return entt::null;
        }

        void MarkCanvasSortDirty(entt::registry* registry, entt::entity canvasEntity)
        {
            if (canvasEntity == entt::null) 
                return;
            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasSort>(canvasEntity);
        }

        void MarkAllCanvasSortDirty(entt::registry* registry)
        {
            registry->view<ECS::Components::UI::Canvas>().each([&](entt::entity canvasEntity, auto&)
            {
                registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasSort>(canvasEntity);
            });
            // The canvas SET changed -> canvasOrder ranking is stale; gates the (relatively
            // expensive) RebuildCanvasOrder pass next time CanvasRenderer::Update runs.
            registry->ctx().emplace<ECS::Components::UI::DirtyCanvasOrderFlag>();
        }

        entt::entity GetOrEmplaceCanvas(Scripting::UI::Widget*& widget, entt::registry* registry, const char* name, vec2 pos, ivec2 size, bool isRenderTexture)
        {
            ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            u32 nameHash = StringUtils::fnv1a_32(name, strlen(name));

            if (uiSingleton.nameHashToCanvasEntity.contains(nameHash))
            {
                entt::entity entity = uiSingleton.nameHashToCanvasEntity[nameHash];
                widget = registry->get<ECS::Components::UI::Widget>(entity).scriptWidget;
                return entity;
            }

            widget = new Scripting::UI::Widget();
            widget->type = Scripting::UI::WidgetType::Canvas;
            widget->metaTableName = "CanvasMetaTable";

            entt::entity entity = CreateCanvas(widget, registry, name, pos, size, isRenderTexture);
            widget->entity = entity;

            uiSingleton.nameHashToCanvasEntity[nameHash] = entity;
            return entity;
        }

        entt::entity CreateCanvas(Scripting::UI::Widget* widget, entt::registry* registry, const char* name, vec2 pos, ivec2 size, bool isRenderTexture)
        {
            auto& ctx = registry->ctx();
            auto& transform2DSystem = Transform2DSystem::Get(*registry);

            entt::entity entity = registry->create();

            // Transform
            auto& transformComp = registry->emplace<ECS::Components::Transform2D>(entity);

            transform2DSystem.SetLocalPosition(entity, pos);
            transform2DSystem.SetSize(entity, size);

            // Bounding Rect (screen space)
            registry->emplace<ECS::Components::UI::BoundingRect>(entity);

            // Name
            auto& nameComp = registry->emplace<ECS::Components::Name>(entity);
            nameComp.fullName = name;
            nameComp.name = name;
            nameComp.nameHash = StringUtils::fnv1a_32(name, strlen(name));

            // Widget
            auto& widgetComp = registry->emplace<ECS::Components::UI::Widget>(entity);
            widgetComp.type = ECS::Components::UI::WidgetType::Canvas;
            widgetComp.scriptWidget = widget;

            // Clipper
            registry->emplace<ECS::Components::UI::Clipper>(entity);

            // Canvas
            auto& canvasComp = registry->emplace<ECS::Components::UI::Canvas>(entity);
            canvasComp.name = name;

            if (isRenderTexture)
            {
                Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

                Renderer::DataTextureDesc desc;
                desc.debugName = name;
                desc.format = renderer->GetSwapChainImageFormat();
                desc.width = size.x;
                desc.height = size.y;
                desc.layers = 1;
                desc.mipLevels = 1;
                desc.renderable = true;

                canvasComp.renderTexture = renderer->CreateDataTexture(desc);
                registry->emplace<ECS::Components::UI::CanvasRenderTargetTag>(entity);
            }

            // A new canvas entering the system shifts canvasOrder for everyone;
            // mark every canvas (including this one) so all widget sortKeys get their
            // canvasOrder bits refreshed on the next CanvasRenderer::Update tick.
            MarkAllCanvasSortDirty(registry);

            return entity;
        }

        void SetTemplateEventHash(const robin_hood::unordered_map<u32, u32>& templateHashToTemplateIndex, std::string_view templateName, std::string_view eventTemplateName, u32& hash)
        {
            u32 templateHash = StringUtils::fnv1a_32(eventTemplateName.data(), eventTemplateName.size());

            if (templateHashToTemplateIndex.contains(templateHash))
            {
                hash = templateHash;
            }
            else
            {
                NC_LOG_ERROR("UI: Panel template '{}' has an on event template '{}' but no template with that name has been registered", templateName, eventTemplateName);
            }
        }

        entt::entity CreatePanel(Scripting::UI::Widget* widget, entt::registry* registry, vec2 pos, ivec2 size, u32 layer, const char* templateName, entt::entity parent)
        {
            auto& ctx = registry->ctx();
            auto& transform2DSystem = Transform2DSystem::Get(*registry);
            auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            entt::entity entity = registry->create();

            // Transform
            auto& transformComp = registry->emplace<ECS::Components::Transform2D>(entity);

            transform2DSystem.ParentEntityTo(parent, entity);
            transform2DSystem.SetLayer(entity, layer);
            transform2DSystem.SetLocalPosition(entity, pos);
            transform2DSystem.SetSize(entity, size);

            // Bounding Rect (screen space)
            registry->emplace<ECS::Components::UI::BoundingRect>(entity);

            // Widget
            auto& widgetComp = registry->emplace<ECS::Components::UI::Widget>(entity);
            widgetComp.type = ECS::Components::UI::WidgetType::Panel;
            widgetComp.scriptWidget = widget;

            registry->emplace<ECS::Components::UI::DirtyWidgetData>(entity);
            registry->emplace<ECS::Components::UI::DirtyWidgetTransform>(entity);

            // Clipper
            auto& panelClipper = registry->emplace<ECS::Components::UI::Clipper>(entity);
            panelClipper.clipRegionOverrideEntity = GetClippingAncestor(registry, entity);

            // Panel
            auto& panelComp = registry->emplace<ECS::Components::UI::Panel>(entity);
            panelComp.layer = layer;

            // Set this texts specific template data
            ECS::Components::UI::PanelTemplate& panelTemplateComp = registry->emplace<ECS::Components::UI::PanelTemplate>(entity);

            // Copy template if provided
            if (templateName != nullptr)
            {
                u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
                u32 templateIndex = uiSingleton.templateHashToPanelTemplateIndex[templateNameHash];

                const ECS::Components::UI::PanelTemplate& panelTemplate = uiSingleton.panelTemplates[templateIndex];
                panelTemplateComp = panelTemplate;
                panelComp.templateIndex = templateIndex;
            }

            // Event Input Info
            auto& eventInputInfo = registry->emplace<ECS::Components::UI::EventInputInfo>(entity);

            if (!panelTemplateComp.onClickTemplate.empty())
            {
                SetTemplateEventHash(uiSingleton.templateHashToPanelTemplateIndex, templateName, panelTemplateComp.onClickTemplate, eventInputInfo.onClickTemplateHash);
            }
            if (!panelTemplateComp.onHoverTemplate.empty())
            {
                SetTemplateEventHash(uiSingleton.templateHashToPanelTemplateIndex, templateName, panelTemplateComp.onHoverTemplate, eventInputInfo.onHoverTemplateHash);
            }
            if (!panelTemplateComp.onUninteractableTemplate.empty())
            {
                SetTemplateEventHash(uiSingleton.templateHashToPanelTemplateIndex, templateName, panelTemplateComp.onUninteractableTemplate, eventInputInfo.onUninteractableTemplateHash);
            }

            eventInputInfo.onMouseDownEvent = panelTemplateComp.onMouseDownEvent;
            eventInputInfo.onMouseUpEvent = panelTemplateComp.onMouseUpEvent;
            eventInputInfo.onMouseHeldEvent = panelTemplateComp.onMouseHeldEvent;

            eventInputInfo.onHoverBeginEvent = panelTemplateComp.onHoverBeginEvent;
            eventInputInfo.onHoverEndEvent = panelTemplateComp.onHoverEndEvent;
            eventInputInfo.onHoverHeldEvent = panelTemplateComp.onHoverHeldEvent;

            eventInputInfo.onFocusBeginEvent = panelTemplateComp.onFocusBeginEvent;
            eventInputInfo.onFocusEndEvent = panelTemplateComp.onFocusEndEvent;
            eventInputInfo.onFocusHeldEvent = panelTemplateComp.onFocusHeldEvent;

            // New widget entering the tree -> owning canvas needs sort-key rebuild.
            MarkCanvasSortDirty(registry, FindOwningCanvas(registry, parent));

            return entity;
        }

        entt::entity CreateText(Scripting::UI::Widget* widget, entt::registry* registry, const char* text, vec2 pos, u32 layer, const char* templateName, entt::entity parent)
        {
            Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

            auto& ctx = registry->ctx();
            auto& transform2DSystem = Transform2DSystem::Get(*registry);
            auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            entt::entity entity = registry->create();

            // Transform
            auto& transformComp = registry->emplace<ECS::Components::Transform2D>(entity);

            transform2DSystem.ParentEntityTo(parent, entity);
            transform2DSystem.SetLayer(entity, layer);
            transform2DSystem.SetLocalPosition(entity, pos);

            // Bounding Rect (screen space)
            registry->emplace<ECS::Components::UI::BoundingRect>(entity);

            // Widget
            auto& widgetComp = registry->emplace<ECS::Components::UI::Widget>(entity);
            widgetComp.type = ECS::Components::UI::WidgetType::Text;
            widgetComp.scriptWidget = widget;

            registry->emplace<ECS::Components::UI::DirtyWidgetData>(entity);
            registry->emplace<ECS::Components::UI::DirtyWidgetTransform>(entity);

            // Clipper
            auto& textClipper = registry->emplace<ECS::Components::UI::Clipper>(entity);
            textClipper.clipRegionOverrideEntity = GetClippingAncestor(registry, entity);

            // Text
            auto& textComp = registry->emplace<ECS::Components::UI::Text>(entity);
            textComp.rawText = text;
            textComp.layer = layer;
            ReplaceTextNewLines(textComp.rawText);
            textComp.text = textComp.rawText;

            u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
            u32 templateIndex = uiSingleton.templateHashToTextTemplateIndex[templateNameHash];
            textComp.templateIndex = templateIndex;

            const ECS::Components::UI::TextTemplate& textTemplate = uiSingleton.textTemplates[templateIndex];

            Renderer::Font* font = Renderer::Font::GetFont(renderer, textTemplate.font);

            if (textTemplate.setFlags.wrapWidth)
            {
                textComp.text = GenWrapText(textComp.rawText, font, textTemplate.size, textTemplate.borderSize, textTemplate.wrapWidth, textTemplate.wrapIndent);
            }

            vec2 textSize = font->CalculateTextSize(textComp.text.c_str(), textTemplate.size, textTemplate.borderSize);
            transform2DSystem.SetSize(entity, textSize);

            // Set this texts specific template data
            ECS::Components::UI::TextTemplate& textTemplateComp = registry->emplace<ECS::Components::UI::TextTemplate>(entity);
            textTemplateComp = textTemplate;

            // Event Input Info
            auto& eventInputInfo = registry->emplace<ECS::Components::UI::EventInputInfo>(entity);

            if (!textTemplate.onClickTemplate.empty())
            {
                SetTemplateEventHash(uiSingleton.templateHashToTextTemplateIndex, templateName, textTemplate.onClickTemplate, eventInputInfo.onClickTemplateHash);
            }
            if (!textTemplate.onHoverTemplate.empty())
            {
                SetTemplateEventHash(uiSingleton.templateHashToTextTemplateIndex, templateName, textTemplate.onHoverTemplate, eventInputInfo.onHoverTemplateHash);
            }

            eventInputInfo.onMouseDownEvent = textTemplate.onMouseDownEvent;
            eventInputInfo.onMouseUpEvent = textTemplate.onMouseUpEvent;
            eventInputInfo.onMouseHeldEvent = textTemplate.onMouseHeldEvent;

            eventInputInfo.onHoverBeginEvent = textTemplate.onHoverBeginEvent;
            eventInputInfo.onHoverEndEvent = textTemplate.onHoverEndEvent;
            eventInputInfo.onHoverHeldEvent = textTemplate.onHoverHeldEvent;

            eventInputInfo.onFocusBeginEvent = textTemplate.onFocusBeginEvent;
            eventInputInfo.onFocusEndEvent = textTemplate.onFocusEndEvent;
            eventInputInfo.onFocusHeldEvent = textTemplate.onFocusHeldEvent;

            // New widget entering the tree -> owning canvas needs sort-key rebuild.
            MarkCanvasSortDirty(registry, FindOwningCanvas(registry, parent));

            return entity;
        }

        entt::entity CreateWidget(Scripting::UI::Widget* widget, entt::registry* registry, vec2 pos, u32 layer, entt::entity parent)
        {
            auto& ctx = registry->ctx();
            auto& transform2DSystem = Transform2DSystem::Get(*registry);
            auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            entt::entity entity = registry->create();

            // Transform
            auto& parentTransformComp = registry->get<ECS::Components::Transform2D>(parent);
            const vec2& parentSize = parentTransformComp.GetSize();
            auto& transformComp = registry->emplace<ECS::Components::Transform2D>(entity);

            transform2DSystem.ParentEntityTo(parent, entity);
            transform2DSystem.SetLayer(entity, layer);
            transform2DSystem.SetLocalPosition(entity, pos);
            transform2DSystem.SetSize(entity, parentSize);

            // Widget
            auto& widgetComp = registry->emplace<ECS::Components::UI::Widget>(entity);
            widgetComp.type = ECS::Components::UI::WidgetType::Widget;
            widgetComp.scriptWidget = widget;

            // Containers don't render, but they get a matrix slot so their children can chain through them.
            // A move then only re-uploads this slot and the GPU recomposes the subtree (no CPU propagation).
            registry->emplace<ECS::Components::UI::DirtyWidgetTransform>(entity);

            // New widget entering the tree -> owning canvas needs sort-key rebuild.
            MarkCanvasSortDirty(registry, FindOwningCanvas(registry, parent));

            return entity;
        }

        bool DestroyWidget(entt::registry* registry, entt::entity entity)
        {
            if (!registry->all_of<ECS::Components::UI::Widget>(entity))
                return false;

            // Widgets leaving the tree changes the sibling set in their owning canvas.
            // Mark it dirty BEFORE we mutate the scriptWidget or clear the parent, so FindOwningCanvas still resolves.
            MarkCanvasSortDirty(registry, FindOwningCanvas(registry, entity));

            auto& transform2DSystem = Transform2DSystem::Get(*registry);
            transform2DSystem.ClearParent(entity);

            // Remove all children
            transform2DSystem.IterateChildrenRecursiveDepth(entity, [&](entt::entity childEntity)
            {
                if (!registry->all_of<ECS::Components::UI::Widget>(childEntity))
                    return true;

                auto& widget = registry->get<ECS::Components::UI::Widget>(childEntity);
                if (widget.type != ECS::Components::UI::WidgetType::Panel && widget.type != ECS::Components::UI::WidgetType::Text && widget.type != ECS::Components::UI::WidgetType::Widget)
                    return true;

                registry->get_or_emplace<ECS::Components::UI::DestroyWidget>(childEntity);
                return true;
            });

            return true;
        }

        void FocusWidgetEntity(entt::registry* registry, entt::entity entity)
        {
            auto& ctx = registry->ctx();
            auto& uiSingleton = ctx.get<ECS::Singletons::UISingleton>();

            if (uiSingleton.focusedEntity == entity)
            {
                return;
            }

            if (entity != entt::null)
            {
                ECS::Components::UI::Widget& widget = registry->get<ECS::Components::UI::Widget>(entity);
                if (!widget.IsFocusable())
                {
                    return;
                }
            }

            uiSingleton.justFocusedEntity = uiSingleton.focusedEntity;

            entt::entity oldFocus = uiSingleton.focusedEntity;
            if (oldFocus != entt::null)
            {
                auto* eventInputInfo = registry->try_get<ECS::Components::UI::EventInputInfo>(oldFocus);
                if (eventInputInfo && eventInputInfo->onFocusEndEvent != -1)
                {
                    auto& widget = registry->get<ECS::Components::UI::Widget>(oldFocus);
                    CallLuaEvent(eventInputInfo->onFocusEndEvent, Scripting::UI::UIInputEvent::FocusEnd, widget.scriptWidget);
                }
            }

            uiSingleton.focusedEntity = entity;

            if (entity != entt::null)
            {
                auto* eventInputInfo = registry->try_get<ECS::Components::UI::EventInputInfo>(entity);
                if (eventInputInfo && eventInputInfo->onFocusBeginEvent != -1)
                {
                    auto& widget = registry->get<ECS::Components::UI::Widget>(entity);
                    CallLuaEvent(eventInputInfo->onFocusBeginEvent, Scripting::UI::UIInputEvent::FocusBegin, widget.scriptWidget);
                }
            }

            // Focus affects sortKey (priority bits), so both the previously focused and the newly focused widget's canvases need their sortKeys rebuilt.
            MarkCanvasSortDirty(registry, FindOwningCanvas(registry, oldFocus));
            MarkCanvasSortDirty(registry, FindOwningCanvas(registry, entity));
        }

        entt::entity GetFocusedWidgetEntity(entt::registry* registry)
        {
            auto& ctx = registry->ctx();
            auto& uiSingleton = ctx.get<ECS::Singletons::UISingleton>();

            return uiSingleton.focusedEntity;
        }

        void RefreshText(entt::registry* registry, entt::entity entity, std::string_view newText)
        {
            Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

            auto& ctx = registry->ctx();
            auto& transform2DSystem = Transform2DSystem::Get(*registry);
            auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetTransform>(entity);

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(entity);

            std::string oldText = textComponent.text;

            textComponent.text = newText;
            ReplaceTextNewLines(textComponent.text);

            const ECS::Components::UI::TextTemplate& textTemplate = registry->get<ECS::Components::UI::TextTemplate>(entity);

            Renderer::Font* font = Renderer::Font::GetFont(renderer, textTemplate.font);

            if (textTemplate.setFlags.wrapWidth)
            {
                textComponent.text = GenWrapText(textComponent.rawText, font, textTemplate.size, textTemplate.borderSize, textTemplate.wrapWidth, textTemplate.wrapIndent);
            }

            // sizeChanged gates recomputing numCharsNonWhitespace -- the non-whitespace glyph count that
            // drives vertex/draw-data allocation, the dirty range, and the indirect-draw instanceCount.
            // Compare the resolved CONTENT, not the byte length: same-length text can have a different
            // glyph count (e.g. swapping a space for a letter), which would otherwise leave the glyph
            // machinery desynced and overrun/under-fill the GPU slots.
            textComponent.sizeChanged |= oldText != textComponent.text;

            // Note: the draw bucket is kept in sync by CanvasRenderer (incremental slot patch for
            // glyph-count changes; full rebuild only when the text appears/disappears), not here.

            vec2 textSize = font->CalculateTextSize(textComponent.text, textTemplate.size, textTemplate.borderSize);
            transform2DSystem.SetSize(entity, textSize);

            auto& transform = registry->get<ECS::Components::Transform2D>(entity);

            vec2 pos = transform.GetWorldPosition();
            vec2 size = transform.GetSize();

            auto* rect = registry->try_get<ECS::Components::UI::BoundingRect>(entity);
            if (rect == nullptr)
            {
                return;
            }

            rect->min = pos;
            rect->max = pos + size;
        }

        void RefreshTemplate(entt::registry* registry, entt::entity entity, ECS::Components::UI::EventInputInfo& eventInputInfo)
        {
            ResetTemplate(registry, entity);
            if (eventInputInfo.isHovered && eventInputInfo.onHoverTemplateHash != 0)
            {
                ApplyTemplateAdditively(registry, entity, eventInputInfo.onHoverTemplateHash);
            }
            if (eventInputInfo.isClicked && eventInputInfo.onClickTemplateHash != 0)
            {
                ApplyTemplateAdditively(registry, entity, eventInputInfo.onClickTemplateHash);
            }
            if (!eventInputInfo.isInteractable && eventInputInfo.onUninteractableTemplateHash != 0)
            {
                ApplyTemplateAdditively(registry, entity, eventInputInfo.onUninteractableTemplateHash);
            }

            auto& widget = registry->get<ECS::Components::UI::Widget>(entity);
            registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget.scriptWidget->canvasEntity);
        }

        void RefreshClipper(entt::registry* registry, entt::entity entity)
        {
            auto* clipper = registry->try_get<ECS::Components::UI::Clipper>(entity);

            if (clipper == nullptr)
                return;

            if (clipper->clipRegionOverrideEntity == entt::null)
            {
                registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetClipper>(entity);
                registry->emplace_or_replace<ECS::Components::UI::DirtyChildClipper>(entity);
            }
            else
            {
                registry->emplace_or_replace<ECS::Components::UI::DirtyWidgetClipper>(clipper->clipRegionOverrideEntity);
                registry->emplace_or_replace<ECS::Components::UI::DirtyChildClipper>(clipper->clipRegionOverrideEntity);
            }
        }

        entt::entity GetClippingAncestor(entt::registry* registry, entt::entity child)
        {
            auto* childTransform = registry->try_get<ECS::Components::Transform2D>(child);
            if (childTransform == nullptr)
                return entt::null;

            ECS::Components::Transform2D* cursor = childTransform->GetParentTransform();
            while (cursor != nullptr)
            {
                entt::entity ancestorEntity = cursor->ownerNode != nullptr ? cursor->ownerNode->GetOwner() : entt::null;
                if (ancestorEntity != entt::null)
                {
                    auto* ancestorClipper = registry->try_get<ECS::Components::UI::Clipper>(ancestorEntity);
                    if (ancestorClipper != nullptr)
                    {
                        if (ancestorClipper->clipChildren)
                            return ancestorEntity;
                        if (ancestorClipper->clipRegionOverrideEntity != entt::null)
                            return ancestorClipper->clipRegionOverrideEntity;
                    }
                }
                cursor = cursor->GetParentTransform();
            }
            return entt::null;
        }

        void RecomputeClipSlots(entt::registry* registry, entt::entity entity)
        {
            auto* clipper = registry->try_get<ECS::Components::UI::Clipper>(entity);
            auto* rect = registry->try_get<ECS::Components::UI::BoundingRect>(entity);
            if (clipper == nullptr || rect == nullptr)
                return;

            const bool hasClipSlot = clipper->clipRectBufferIndex != 0;
            const bool hasMaskSlot = clipper->maskBufferIndex != 0;
            if (!hasClipSlot && !hasMaskSlot)
                return;

            auto* canvasRenderer = ServiceLocator::GetGameRenderer()->GetCanvasRenderer();

            const vec2 refSize(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);
            const vec2 size = rect->max - rect->min;

            if (hasClipSlot)
            {
                vec2 clipMin = (rect->min + size * clipper->clipRegionMin) / refSize;
                vec2 clipMax = (rect->min + size * clipper->clipRegionMax) / refSize;
                canvasRenderer->UpdateClipRect(clipper->clipRectBufferIndex, vec4(clipMin.x, clipMin.y, clipMax.x, clipMax.y));
            }

            if (hasMaskSlot)
            {
                vec2 maskMin = rect->min / refSize;
                vec2 maskMax = rect->max / refSize;
                u32 textureIndex = clipper->hasClipMaskTexture
                    ? canvasRenderer->LoadTextureByPath(clipper->clipMaskTexture)
                    : 0;
                canvasRenderer->UpdateMaskInfo(clipper->maskBufferIndex, vec4(maskMin.x, maskMin.y, maskMax.x, maskMax.y), textureIndex);
            }
        }

        void ReserveClipRectSlot(entt::registry* registry, entt::entity entity)
        {
            auto& clipper = registry->get<ECS::Components::UI::Clipper>(entity);
            if (clipper.clipRectBufferIndex != 0)
                return;
            auto* canvasRenderer = ServiceLocator::GetGameRenderer()->GetCanvasRenderer();
            clipper.clipRectBufferIndex = canvasRenderer->ReserveClipRect();
            registry->ctx().get<ECS::Singletons::UISingleton>().clipSourceEntities.insert(entity);
            RecomputeClipSlots(registry, entity);
        }

        void ReserveMaskSlot(entt::registry* registry, entt::entity entity)
        {
            auto& clipper = registry->get<ECS::Components::UI::Clipper>(entity);
            if (clipper.maskBufferIndex != 0)
                return;
            auto* canvasRenderer = ServiceLocator::GetGameRenderer()->GetCanvasRenderer();
            clipper.maskBufferIndex = canvasRenderer->ReserveMaskInfo();
            registry->ctx().get<ECS::Singletons::UISingleton>().clipSourceEntities.insert(entity);
            RecomputeClipSlots(registry, entity);
        }

        void ReleaseClipRectSlot(entt::registry* registry, entt::entity entity)
        {
            auto& clipper = registry->get<ECS::Components::UI::Clipper>(entity);
            if (clipper.clipRectBufferIndex == 0)
                return;
            auto* canvasRenderer = ServiceLocator::GetGameRenderer()->GetCanvasRenderer();
            canvasRenderer->ReleaseClipRect(clipper.clipRectBufferIndex);
            clipper.clipRectBufferIndex = 0;
        }

        void ReleaseMaskSlot(entt::registry* registry, entt::entity entity)
        {
            auto& clipper = registry->get<ECS::Components::UI::Clipper>(entity);
            if (clipper.maskBufferIndex == 0)
                return;
            auto* canvasRenderer = ServiceLocator::GetGameRenderer()->GetCanvasRenderer();
            canvasRenderer->ReleaseMaskInfo(clipper.maskBufferIndex);
            clipper.maskBufferIndex = 0;
        }

        void ResetTemplate(entt::registry* registry, entt::entity entity)
        {
            auto& ctx = registry->ctx();
            auto& uiSingleton = ctx.get<ECS::Singletons::UISingleton>();

            auto& widget = registry->get<ECS::Components::UI::Widget>(entity);

            if (widget.type == ECS::Components::UI::WidgetType::Panel)
            {
                auto& panel = registry->get<ECS::Components::UI::Panel>(entity);
                auto& panelTemplateComp = registry->get<ECS::Components::UI::PanelTemplate>(entity);

                if (panel.templateIndex == -1)
                {
                    panelTemplateComp = ECS::Components::UI::PanelTemplate();
                }
                else
                {
                    auto& panelTemplate = uiSingleton.panelTemplates[panel.templateIndex];
                    panelTemplateComp = panelTemplate;
                }

                if (panelTemplateComp.setFlags.texCoords)
                {
                    registry->get_or_emplace<ECS::Components::UI::DirtyWidgetTransform>(entity);
                }

                registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(entity);
            }
            else if (widget.type == ECS::Components::UI::WidgetType::Text)
            {
                auto& text = registry->get<ECS::Components::UI::Text>(entity);
                auto& textTemplateComp = registry->get<ECS::Components::UI::TextTemplate>(entity);

                auto& textTemplate = uiSingleton.textTemplates[text.templateIndex];

                textTemplateComp = textTemplate;

                registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(entity);
            }
        }

        void ApplyTemplateAdditively(entt::registry* registry, entt::entity entity, u32 templateHash)
        {
            auto& ctx = registry->ctx();
            auto& uiSingleton = ctx.get<ECS::Singletons::UISingleton>();

            auto& widget = registry->get<ECS::Components::UI::Widget>(entity);

            if (widget.type == ECS::Components::UI::WidgetType::Panel)
            {
                if (!uiSingleton.templateHashToPanelTemplateIndex.contains(templateHash))
                {
                    return;
                }

                auto& panelTemplateComp = registry->get<ECS::Components::UI::PanelTemplate>(entity);

                u32 templateIndex = uiSingleton.templateHashToPanelTemplateIndex[templateHash];
                auto& panelTemplate = uiSingleton.panelTemplates[templateIndex];

                if (panelTemplate.setFlags.backgroundRT)
                {
                    panelTemplateComp.backgroundRT = panelTemplate.backgroundRT;
                }
                else if (panelTemplate.setFlags.background)
                {
                    panelTemplateComp.background = panelTemplate.background;
                }
                if (panelTemplate.setFlags.foreground)
                {
                    panelTemplateComp.foreground = panelTemplate.foreground;
                }
                if (panelTemplate.setFlags.color)
                {
                    panelTemplateComp.color = panelTemplate.color;
                }
                if (panelTemplate.setFlags.cornerRadius)
                {
                    panelTemplateComp.cornerRadius = panelTemplate.cornerRadius;
                }
                if (panelTemplate.setFlags.texCoords)
                {
                    panelTemplateComp.texCoords = panelTemplate.texCoords;
                    registry->get_or_emplace<ECS::Components::UI::DirtyWidgetTransform>(entity);
                }
                if (panelTemplate.setFlags.nineSliceInsets)
                {
                    panelTemplateComp.nineSliceInsets = panelTemplate.nineSliceInsets;
                }

                registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(entity);
            }
            else if (widget.type == ECS::Components::UI::WidgetType::Text)
            {
                if (!uiSingleton.templateHashToTextTemplateIndex.contains(templateHash))
                {
                    return;
                }

                auto& textTemplateComp = registry->get<ECS::Components::UI::TextTemplate>(entity);

                u32 templateIndex = uiSingleton.templateHashToTextTemplateIndex[templateHash];
                auto& textTemplate = uiSingleton.textTemplates[templateIndex];

                if (textTemplate.setFlags.font)
                {
                    textTemplateComp.font = textTemplate.font;
                }
                if (textTemplate.setFlags.size)
                {
                    textTemplateComp.size = textTemplate.size;
                }
                if (textTemplate.setFlags.color)
                {
                    textTemplateComp.color = textTemplate.color;
                }
                if (textTemplate.setFlags.borderSize)
                {
                    textTemplateComp.borderSize = textTemplate.borderSize;
                }
                if (textTemplate.setFlags.borderColor)
                {
                    textTemplateComp.borderColor = textTemplate.borderColor;
                }

                registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(entity);
            }
        }

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget)
        {
            if (eventRef == -1)
            {
                return; // No event to call
            }

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>((u16)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI);
            uiHandler->CallUIInputEvent(zenith, eventRef, inputEvent, widget);
        }

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, i32 value)
        {
            if (eventRef == -1)
            {
                return; // No event to call
            }

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>((u16)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI);
            uiHandler->CallUIInputEvent(zenith, eventRef, inputEvent, widget, value);
        }

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, i32 value1, vec2 value2)
        {
            if (eventRef == -1)
            {
                return; // No event to call
            }

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>((u16)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI);
            uiHandler->CallUIInputEvent(zenith, eventRef, inputEvent, widget, value1, value2);
        }

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, f32 value)
        {
            if (eventRef == -1)
            {
                return; // No event to call
            }

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>((u16)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI);
            uiHandler->CallUIInputEvent(zenith, eventRef, inputEvent, widget, value);
        }

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, vec2 value)
        {
            if (eventRef == -1)
            {
                return; // No event to call
            }

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>((u16)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI);
            uiHandler->CallUIInputEvent(zenith, eventRef, inputEvent, widget, value);
        }

        bool CallKeyboardEvent(i32 eventRef, Scripting::UI::Widget* widget, i32 key, i32 actionMask, i32 modifierMask)
        {
            if (eventRef == -1)
            {
                return false; // No event to call
            }

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>((u16)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI);
            return uiHandler->CallKeyboardInputEvent(zenith, eventRef, widget, key, actionMask, modifierMask);
        }

        bool CallKeyboardEvent(i32 eventRef, i32 key, i32 actionMask, i32 modifierMask)
        {
            if (eventRef == -1)
            {
                return false; // No event to call
            }

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>((u16)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI);
            return uiHandler->CallKeyboardInputEvent(zenith, eventRef, key, actionMask, modifierMask);
        }

        bool CallUnicodeEvent(i32 eventRef, Scripting::UI::Widget* widget, u32 unicode)
        {
            if (eventRef == -1)
            {
                return false; // No event to call
            }

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>((u16)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI);
            return uiHandler->CallKeyboardUnicodeEvent(zenith, eventRef, widget, unicode);
        }

        void CallSendMessageToChat(i32 eventRef, const std::string& channel, const std::string& playerName, const std::string& text, bool isOutgoing)
        {
            if (eventRef == -1)
            {
                return; // No event to call
            }

            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            Scripting::Zenith* zenith = Scripting::Util::Zenith::GetGlobal();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler>((u16)MetaGen::Game::Lua::LuaHandlerTypeEnum::UI);
            uiHandler->CallSendMessageToChat(zenith, eventRef, channel, playerName, text, isOutgoing);
        }
        
        std::string GenWrapText(const std::string& text, Renderer::Font* font, f32 fontSize, f32 borderSize, f32 maxWidth, u8 indents)
        {
            f32 currentLineWidth = 0;
            size_t wordStart = 0;
            f32 currentWordWidth = 0;
            bool lineHasContent = false;

            std::string wrappedText;
            wrappedText.reserve(text.size() + text.size() / 4);

            f32 indentWidth = font->CalculateCharWidth(' ', fontSize, borderSize);
            auto beginWrappedLine = [&]()
            {
                wrappedText += '\n';
                currentLineWidth = 0;

                for (u8 j = 0; j < indents; ++j)
                {
                    wrappedText += ' ';
                    currentLineWidth += indentWidth;
                }

                // The wrap indent is not message content. A word that is wider than the
                // remaining line still needs to be split instead of repeatedly re-wrapped.
                lineHasContent = false;
            };

            auto appendWord = [&](size_t wordEnd)
            {
                if (wordStart == wordEnd)
                {
                    return;
                }

                if (lineHasContent && currentLineWidth + currentWordWidth > maxWidth)
                {
                    beginWrappedLine();
                }

                wrappedText.append(text, wordStart, wordEnd - wordStart);
                currentLineWidth += currentWordWidth;
                currentWordWidth = 0;
                lineHasContent = true;
            };

            for (size_t i = 0; i < text.length(); ++i)
            {
                char c = text[i];

                if (c == ' ' || c == '\n')
                {
                    appendWord(i);

                    if (c == ' ')
                    {
                        wrappedText += ' ';
                        currentLineWidth += indentWidth;
                        lineHasContent = true;
                    }
                    else
                    {
                        // Preserve server-provided line breaks without applying the wrap indent.
                        // The indent only belongs on continuation lines introduced above.
                        wrappedText += '\n';
                        currentLineWidth = 0;
                        lineHasContent = false;
                    }

                    wordStart = i + 1;
                }
                else
                {
                    f32 charWidth = 0;
                    if (font->IsValidGlyph(c))
                    {
                        charWidth = font->CalculateCharWidth(c, fontSize, borderSize);
                    }
                    currentWordWidth += charWidth;

                    // Keep ordinary words intact by first moving them to a fresh line. Only
                    // split when the word itself is too wide to fit on that fresh line.
                    if (currentLineWidth + currentWordWidth > maxWidth)
                    {
                        if (lineHasContent)
                        {
                            beginWrappedLine();
                        }

                        if (currentLineWidth + currentWordWidth > maxWidth && i > wordStart)
                        {
                            wrappedText.append(text, wordStart, i - wordStart);
                            beginWrappedLine();
                            wordStart = i;
                            currentWordWidth = charWidth;
                        }
                    }
                }
            }

            appendWord(text.length());
            return wrappedText;
        }

        void ReplaceTextNewLines(std::string& input)
        {
            size_t write_index = 0;
            for (size_t i = 0; i < input.size(); ++i)
            {
                if (input[i] == '\\' && i + 1 < input.size() && input[i + 1] == 'n')
                {
                    input[write_index++] = '\n';
                    ++i; // Skip 'n'
                }
                else
                {
                    input[write_index++] = input[i];
                }
            }
            input.resize(write_index);
        }

        void SetPos3D(Scripting::UI::Widget* widget, vec3& pos)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& widgetComp = registry->get<ECS::Components::UI::Widget>(widget->entity);
            auto& transform = registry->get<ECS::Components::Transform2D>(widget->entity);

            auto* canvasRenderer = ServiceLocator::GetGameRenderer()->GetCanvasRenderer();

            transform.SetIgnoreParent(true);

            const bool needsWorldTransform = widgetComp.worldTransformIndex == std::numeric_limits<u32>().max();
            if (needsWorldTransform)
                widgetComp.worldTransformIndex = canvasRenderer->ReserveWorldTransform();

            canvasRenderer->UpdateWorldTransform(widgetComp.worldTransformIndex, pos);

            // The world-position buffer is updated independently. Widget data and descendant indexes
            // only need rebuilding when this widget first becomes a 3D-anchored subtree.
            if (needsWorldTransform)
            {
                registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);
                registry->get_or_emplace<ECS::Components::UI::DirtyWidgetTransform>(widget->entity);
                registry->get_or_emplace<ECS::Components::UI::DirtyWidgetWorldTransformIndex>(widget->entity);
                registry->get_or_emplace<ECS::Components::UI::DirtyCanvasSort>(widget->canvasEntity);
            }
        }

        void ClearPos3D(Scripting::UI::Widget* widget)
        {
            entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
            auto& widgetComp = registry->get<ECS::Components::UI::Widget>(widget->entity);
            auto& transform = registry->get<ECS::Components::Transform2D>(widget->entity);

            auto* canvasRenderer = ServiceLocator::GetGameRenderer()->GetCanvasRenderer();

            transform.SetIgnoreParent(false);

            if (widgetComp.worldTransformIndex != std::numeric_limits<u32>().max())
            {
                canvasRenderer->ReleaseWorldTransform(widgetComp.worldTransformIndex);
                registry->get_or_emplace<ECS::Components::UI::DirtyCanvasSort>(widget->canvasEntity);
            }

            widgetComp.worldTransformIndex = std::numeric_limits<u32>().max();

            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(widget->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetTransform>(widget->entity);
            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetWorldTransformIndex>(widget->entity);
        }
    }
}
