#include "UIUtil.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/Name.h"
#include "Game-Lib/ECS/Components/UI/BoundingRect.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Components/UI/EventInputInfo.h"
#include "Game-Lib/ECS/Components/UI/Panel.h"
#include "Game-Lib/ECS/Components/UI/PanelTemplate.h"
#include "Game-Lib/ECS/Components/UI/Text.h"
#include "Game-Lib/ECS/Components/UI/TextTemplate.h"
#include "Game-Lib/ECS/Components/UI/Widget.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Scripting/Handlers/UIHandler.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/UI/Widget.h"
#include "Game-Lib/Util/ServiceLocator.h"

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

            eventInputInfo.onFocusBeginEvent = panelTemplate.onFocusBeginEvent;
            eventInputInfo.onFocusEndEvent = panelTemplate.onFocusEndEvent;
            eventInputInfo.onFocusHeldEvent = panelTemplate.onFocusHeldEvent;

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
            ReplaceTextNewLines(textComp.text);

            u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
            u32 templateIndex = uiSingleton.templateHashToTextTemplateIndex[templateNameHash];
            textComp.templateIndex = templateIndex;

            const ECS::Components::UI::TextTemplate& textTemplate = uiSingleton.textTemplates[templateIndex];

            Renderer::Font* font = Renderer::Font::GetFont(renderer, textTemplate.font);

            if (textTemplate.setFlags.wrapWidth)
            {
                textComp.text = GenWrapText(textComp.text, font, textTemplate.size, textTemplate.borderSize, textTemplate.wrapWidth);
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

            return entity;
        }

        entt::entity CreateWidget(Scripting::UI::Widget* widget, entt::registry* registry, vec2 pos, u32 layer, entt::entity parent)
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

            // Widget
            auto& widgetComp = registry->emplace<ECS::Components::UI::Widget>(entity);
            widgetComp.type = ECS::Components::UI::WidgetType::Widget;
            widgetComp.scriptWidget = widget;

            return entity;
        }

        bool DestroyWidget(entt::registry* registry, entt::entity entity)
        {
            if (!registry->all_of<ECS::Components::UI::Widget>(entity))
                return false;

            auto& transform2DSystem = Transform2DSystem::Get(*registry);
            transform2DSystem.ClearParent(entity);

            registry->get_or_emplace<ECS::Components::UI::DestroyWidget>(entity);
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

            size_t newLength = newText.size();

            textComponent.sizeChanged |= newLength != textComponent.text.size();
            textComponent.hasGrown |= newLength > textComponent.text.size();

            textComponent.text = newText;
            ReplaceTextNewLines(textComponent.text);

            const ECS::Components::UI::TextTemplate& textTemplate = uiSingleton.textTemplates[textComponent.templateIndex];

            Renderer::Font* font = Renderer::Font::GetFont(renderer, textTemplate.font);

            if (textTemplate.setFlags.wrapWidth)
            {
                textComponent.text = GenWrapText(textComponent.text, font, textTemplate.size, textTemplate.borderSize, textTemplate.wrapWidth);
            }

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
                    registry->get_or_emplace<ECS::Components::UI::DirtyWidgetTransform>(entity);
                }
                if (panelTemplate.setFlags.nineSliceCoords)
                {
                    panelTemplateComp.nineSliceCoords = panelTemplate.nineSliceCoords;
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
            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            lua_State* state = luaManager->GetInternalState();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
            uiHandler->CallUIInputEvent(state, eventRef, inputEvent, widget);
        }

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, i32 value)
        {
            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            lua_State* state = luaManager->GetInternalState();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
            uiHandler->CallUIInputEvent(state, eventRef, inputEvent, widget, value);
        }

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, i32 value1, vec2 value2)
        {
            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            lua_State* state = luaManager->GetInternalState();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
            uiHandler->CallUIInputEvent(state, eventRef, inputEvent, widget, value1, value2);
        }

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, f32 value)
        {
            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            lua_State* state = luaManager->GetInternalState();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
            uiHandler->CallUIInputEvent(state, eventRef, inputEvent, widget, value);
        }

        void CallLuaEvent(i32 eventRef, Scripting::UI::UIInputEvent inputEvent, Scripting::UI::Widget* widget, vec2 value)
        {
            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            lua_State* state = luaManager->GetInternalState();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
            uiHandler->CallUIInputEvent(state, eventRef, inputEvent, widget, value);
        }

        void CallKeyboardEvent(i32 eventRef, Scripting::UI::Widget* widget, i32 key, i32 actionMask, i32 modifierMask)
        {
            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            lua_State* state = luaManager->GetInternalState();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
            uiHandler->CallKeyboardInputEvent(state, eventRef, widget, key, actionMask, modifierMask);
        }

        void CallUnicodeEvent(i32 eventRef, Scripting::UI::Widget* widget, u32 unicode)
        {
            Scripting::LuaManager* luaManager = ServiceLocator::GetLuaManager();
            lua_State* state = luaManager->GetInternalState();

            Scripting::UI::UIHandler* uiHandler = luaManager->GetLuaHandler<Scripting::UI::UIHandler*>(Scripting::LuaHandlerType::UI);
            uiHandler->CallKeyboardUnicodeEvent(state, eventRef, widget, unicode);
        }
        
        std::string GenWrapText(const std::string& text, Renderer::Font* font, f32 fontSize, f32 borderSize, f32 maxWidth)
        {
            // Early exit if entire text fits within maxWidth
            f32 totalWidth = 0;
            for (char c : text)
            {
                totalWidth += font->CalculateCharWidth(c, fontSize, borderSize);
                if (totalWidth > maxWidth) // Early break for large text
                {
                    break;
                }
            }
            if (totalWidth <= maxWidth)
            {
                return text;
            }

            std::string wrappedText;
            wrappedText.reserve(text.size() + text.size() / 4); // Pre-estimate with margin

            f32 currentLineWidth = 0;
            size_t wordStart = 0;
            f32 currentWordWidth = 0;
            std::string buffer; // Batch write buffer
            buffer.reserve(text.size() + text.size() / 4); // Reserve for buffer to minimize reallocations

            for (size_t i = 0; i < text.length(); ++i)
            {
                char c = text[i];

                if (c == ' ' || c == '\n')
                {
                    size_t wordLength = i - wordStart;

                    if (currentLineWidth + currentWordWidth > maxWidth)
                    {
                        buffer += '\n';
                        currentLineWidth = 0;
                    }

                    buffer.append(text, wordStart, wordLength);
                    currentLineWidth += currentWordWidth;

                    if (c == ' ')
                    {
                        buffer += ' ';
                        currentLineWidth += font->CalculateCharWidth(c, fontSize, borderSize);
                    }
                    else // Newline
                    {
                        buffer += '\n';
                        currentLineWidth = 0;
                    }

                    wordStart = i + 1;
                    currentWordWidth = 0;
                }
                else
                {
                    currentWordWidth += font->CalculateCharWidth(c, fontSize, borderSize);

                    // Force break for long words
                    if (currentWordWidth > maxWidth)
                    {
                        buffer.append(text, wordStart, i - wordStart);
                        buffer += '\n';
                        wordStart = i;
                        currentWordWidth = font->CalculateCharWidth(c, fontSize, borderSize);
                        currentLineWidth = currentWordWidth;
                    }
                }
            }

            // Handle the last word
            if (wordStart < text.length())
            {
                if (currentLineWidth + currentWordWidth > maxWidth)
                {
                    buffer += '\n';
                }
                buffer.append(text, wordStart, text.length() - wordStart);
            }

            wrappedText = std::move(buffer); // Efficient assignment
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
    }
}