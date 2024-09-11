#include "UIUtil.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Components/Name.h"
#include "Game/ECS/Components/UI/BoundingRect.h"
#include "Game/ECS/Components/UI/Canvas.h"
#include "Game/ECS/Components/UI/EventInputInfo.h"
#include "Game/ECS/Components/UI/Panel.h"
#include "Game/ECS/Components/UI/PanelTemplate.h"
#include "Game/ECS/Components/UI/Text.h"
#include "Game/ECS/Components/UI/TextTemplate.h"
#include "Game/ECS/Components/UI/Widget.h"
#include "Game/ECS/Singletons/UISingleton.h"
#include "Game/ECS/Util/Transform2D.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Util/ServiceLocator.h"

#include <Renderer/Font.h>

#include <Base/Util/StringUtils.h>

#include <entt/entt.hpp>

namespace ECS::Util
{
    namespace UI
    {
        entt::entity GetOrEmplaceCanvas(Scripting::UI::Widget* widget, entt::registry* registry, const char* name, vec2 pos, ivec2 size)
        {
            ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            u32 nameHash = StringUtils::fnv1a_32(name, strlen(name));

            if (uiSingleton.nameHashToCanvasEntity.contains(nameHash))
            {
                entt::entity entity = uiSingleton.nameHashToCanvasEntity[nameHash];
                return entity;
            }

            return CreateCanvas(widget, registry, name, pos, size);
        }

        entt::entity CreateCanvas(Scripting::UI::Widget* widget, entt::registry* registry, const char* name, vec2 pos, ivec2 size, entt::entity parent)
        {
            auto& ctx = registry->ctx();
            auto& transform2DSystem = Transform2DSystem::Get(*registry);

            entt::entity entity = registry->create();

            // Transform
            auto& transformComp = registry->emplace<ECS::Components::Transform2D>(entity);

            if (parent != entt::null)
            {
                transform2DSystem.ParentEntityTo(parent, entity);
            }
            else
            {
                registry->emplace<ECS::Components::UI::WidgetRoot>(entity);
            }
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

            // Canvas
            //auto& canvasComp = registry->emplace<ECS::Components::UI::Canvas>(entity);

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

            // Panel
            auto& panelComp = registry->emplace<ECS::Components::UI::Panel>(entity);
            panelComp.layer = layer;

            u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
            if (!uiSingleton.templateHashToPanelTemplateIndex.contains(templateNameHash))
            {
                NC_LOG_ERROR("UI: Tried to create panel with template name '{}' but no template with that name has been registered", templateName);
                return entity;
            }

            u32 templateIndex = uiSingleton.templateHashToPanelTemplateIndex[templateNameHash];
            panelComp.templateIndex = templateIndex;

            const ECS::Components::UI::PanelTemplate& panelTemplate = uiSingleton.panelTemplates[templateIndex];

            // Set this texts specific template data
            ECS::Components::UI::PanelTemplate& panelTemplateComp = registry->emplace<ECS::Components::UI::PanelTemplate>(entity);
            panelTemplateComp = panelTemplate;

            // Event Input Info
            auto& eventInputInfo = registry->emplace<ECS::Components::UI::EventInputInfo>(entity);

            if (!panelTemplate.onClickTemplate.empty())
            {
                SetTemplateEventHash(uiSingleton.templateHashToPanelTemplateIndex, templateName, panelTemplate.onClickTemplate, eventInputInfo.onClickTemplateHash);
            }
            if (!panelTemplate.onHoverTemplate.empty())
            {
                SetTemplateEventHash(uiSingleton.templateHashToPanelTemplateIndex, templateName, panelTemplate.onHoverTemplate, eventInputInfo.onHoverTemplateHash);
            }
            if (!panelTemplate.onUninteractableTemplate.empty())
            {
                SetTemplateEventHash(uiSingleton.templateHashToPanelTemplateIndex, templateName, panelTemplate.onUninteractableTemplate, eventInputInfo.onUninteractableTemplateHash);
            }

            eventInputInfo.onMouseDownEvent = panelTemplate.onMouseDownEvent;
            eventInputInfo.onMouseUpEvent = panelTemplate.onMouseUpEvent;
            eventInputInfo.onMouseHeldEvent = panelTemplate.onMouseHeldEvent;

            eventInputInfo.onHoverBeginEvent = panelTemplate.onHoverBeginEvent;
            eventInputInfo.onHoverEndEvent = panelTemplate.onHoverEndEvent;
            eventInputInfo.onHoverHeldEvent = panelTemplate.onHoverHeldEvent;

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

            // Text
            auto& textComp = registry->emplace<ECS::Components::UI::Text>(entity);
            textComp.text = text;
            textComp.layer = layer;

            u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
            if (!uiSingleton.templateHashToTextTemplateIndex.contains(templateNameHash))
            {
                NC_LOG_ERROR("UI: Tried to create text with template name '{}' but no template with that name has been registered", templateName);
                return entity;
            }

            u32 templateIndex = uiSingleton.templateHashToTextTemplateIndex[templateNameHash];
            textComp.templateIndex = templateIndex;

            const ECS::Components::UI::TextTemplate& textTemplate = uiSingleton.textTemplates[templateIndex];

            const std::string& fontPath = textTemplate.font;
            f32 fontSize = textTemplate.size;
            Renderer::Font* font = Renderer::Font::GetFont(renderer, fontPath, fontSize);

            vec2 textSize = font->CalculateTextSize(text);
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

            return entity;
        }

        void RefreshText(entt::registry* registry, entt::entity entity)
        {
            Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

            auto& ctx = registry->ctx();
            auto& transform2DSystem = Transform2DSystem::Get(*registry);
            auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(entity);

            ECS::Components::UI::Text& textComponent = registry->get<ECS::Components::UI::Text>(entity);

            const std::string& fontPath = uiSingleton.textTemplates[textComponent.templateIndex].font;
            f32 fontSize = uiSingleton.textTemplates[textComponent.templateIndex].size;
            Renderer::Font* font = Renderer::Font::GetFont(renderer, fontPath, fontSize);

            vec2 textSize = font->CalculateTextSize(textComponent.text);
            transform2DSystem.SetSize(entity, textSize);

            auto& transform = registry->get<ECS::Components::Transform2D>(entity);
            transform2DSystem.RefreshTransform(entity, transform);
        }

        void RefreshTemplate(entt::registry* registry, entt::entity entity, ECS::Components::UI::EventInputInfo& eventInputInfo)
        {
            ResetTemplate(registry, entity);
            if (eventInputInfo.isHovered && eventInputInfo.onHoverTemplateHash != -1)
            {
                ApplyTemplateAdditively(registry, entity, eventInputInfo.onHoverTemplateHash);
            }
            if (eventInputInfo.isClicked && eventInputInfo.onClickTemplateHash != -1)
            {
                ApplyTemplateAdditively(registry, entity, eventInputInfo.onClickTemplateHash);
            }
            if (!eventInputInfo.isInteractable && eventInputInfo.onUninteractableTemplateHash != -1)
            {
                ApplyTemplateAdditively(registry, entity, eventInputInfo.onUninteractableTemplateHash);
            }
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

                auto& panelTemplate = uiSingleton.panelTemplates[panel.templateIndex];

                panelTemplateComp = panelTemplate;

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

                if (panelTemplate.setFlags.background)
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
                if (textTemplate.setFlags.borderFade)
                {
                    textTemplateComp.borderFade = textTemplate.borderFade;
                }
                if (textTemplate.setFlags.borderColor)
                {
                    textTemplateComp.borderColor = textTemplate.borderColor;
                }

                registry->get_or_emplace<ECS::Components::UI::DirtyWidgetData>(entity);
            }
        }
    }
}