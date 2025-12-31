#include "UIHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Components/UI/Canvas.h"
#include "Game-Lib/ECS/Singletons/InputSingleton.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Canvas/CanvasRenderer.h"
#include "Game-Lib/Scripting/UI/Box.h"
#include "Game-Lib/Scripting/UI/Canvas.h"
#include "Game-Lib/Scripting/UI/Panel.h"
#include "Game-Lib/Scripting/UI/Text.h"
#include "Game-Lib/UI/Box.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <Input/InputManager.h>
#include <Input/KeybindGroup.h>

#include <MetaGen/Game/Lua/Lua.h>

#include <Scripting/Zenith.h>

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting::UI
{
    void UIHandler::Register(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        registry->ctx().emplace<ECS::Singletons::UISingleton>();
        registry->ctx().emplace<ECS::Singletons::InputSingleton>();

        // UI
        LuaMethodTable::Set(zenith, uiGlobalMethods, "UI");

        // Widgets
        Widget::Register(zenith);
        Canvas::Register(zenith);
        Panel::Register(zenith);
        Text::Register(zenith);

        // Utils
        Box::Register(zenith);

        CreateUIInputEventTable(zenith);

        // Setup Cursor Canvas
        {
            auto& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

            uiSingleton.cursorCanvasEntity = ECS::Util::UI::GetOrEmplaceCanvas(uiSingleton.cursorCanvas, registry, "CursorCanvas", vec2(0, 0), ivec2(48, 48), false);
        }
    }

    void UIHandler::Clear(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        auto& ctx = registry->ctx();
        auto& transformSystem = ECS::Transform2DSystem::Get(*registry);

        registry->view<ECS::Components::UI::Widget>().each([&transformSystem](entt::entity entity, ECS::Components::UI::Widget& widget)
        {
            if (widget.scriptWidget != nullptr)
            {
                delete widget.scriptWidget;
                widget.scriptWidget = nullptr;
            }
            if (transformSystem.HasParent(entity))
            {
                transformSystem.ClearParent(entity);
            }
        });

        registry->clear();

        transformSystem.ClearQueue();
        ServiceLocator::GetGameRenderer()->GetCanvasRenderer()->Clear();

        if (ctx.contains<ECS::Singletons::UISingleton>())
        {
            ECS::Singletons::UISingleton& uiSingleton = ctx.get<ECS::Singletons::UISingleton>();
            uiSingleton.panelTemplates.clear();
            uiSingleton.textTemplates.clear();

            uiSingleton.nameHashToCanvasEntity.clear();
            uiSingleton.templateHashToTextTemplateIndex.clear();
            uiSingleton.templateHashToPanelTemplateIndex.clear();
            uiSingleton.lastClickPosition = vec2(0, 0);
            uiSingleton.clickedEntity = entt::null;
            uiSingleton.hoveredEntity = entt::null;
            uiSingleton.focusedEntity = entt::null;
            uiSingleton.cursorCanvasEntity = entt::null;
            uiSingleton.allHoveredEntities.clear();
            uiSingleton.scriptWidgets.clear();
        }

        if (ctx.contains<ECS::Singletons::InputSingleton>())
        {
            auto& inputSingleton = ctx.get<ECS::Singletons::InputSingleton>();
            inputSingleton.globalKeyboardEvents.clear();
            inputSingleton.eventIDToKeyboardEventIndex.clear();
        }
    }

    // UI
    i32 UIHandler::RegisterPanelTemplate(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 panelTemplateIndex = static_cast<u32>(uiSingleton.panelTemplates.size());
        auto& panelTemplate = uiSingleton.panelTemplates.emplace_back();

        const char* templateName = zenith->CheckVal<const char*>(1);

        if (zenith->GetTableField("background", 2))
        {
            panelTemplate.background = zenith->CheckVal<const char*>(-1);
            panelTemplate.setFlags.background = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("foreground", 2))
        {
            panelTemplate.foreground = zenith->CheckVal<const char*>(-1);
            panelTemplate.setFlags.foreground = 1;
            zenith->Pop();
        }

        panelTemplate.color = Color::White;
        if (zenith->GetTableField("color", 2))
        {
            vec3 color = zenith->CheckVal<vec3>(-1);
            panelTemplate.color = Color(color.x, color.y, color.z);
            panelTemplate.setFlags.color = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("alpha", 2))
        {
            f32 alpha = zenith->CheckVal<f32>(-1);
            panelTemplate.color.a = alpha;
            panelTemplate.setFlags.color = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("cornerRadius", 2))
        {
            panelTemplate.cornerRadius = zenith->CheckVal<f32>(-1);
            panelTemplate.setFlags.cornerRadius = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("texCoords", 2))
        {
            ::UI::Box* box = zenith->GetUserData<::UI::Box>(-1);
            zenith->Pop();

            if (box)
            {
                panelTemplate.texCoords = *box;
            }
            else
            {
                panelTemplate.texCoords.min = vec2(0.0f, 0.0f);
                panelTemplate.texCoords.max = vec2(1.0f, 1.0f);
            }
            panelTemplate.setFlags.texCoords = 1;
        }

        if (zenith->GetTableField("nineSliceCoords", 2))
        {
            ::UI::Box* box = zenith->GetUserData<::UI::Box>(-1);
            zenith->Pop();

            if (box)
            {
                panelTemplate.nineSliceCoords = *box;
            }
            else
            {
                panelTemplate.nineSliceCoords.min = vec2(0.0f, 0.0f);
                panelTemplate.nineSliceCoords.max = vec2(1.0f, 1.0f);
            }
            panelTemplate.setFlags.nineSliceCoords = 1;
        }

        // Event Templates
        if (zenith->GetTableField("onClickTemplate", 2))
        {
            panelTemplate.onClickTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        if (zenith->GetTableField("onHoverTemplate", 2))
        {
            panelTemplate.onHoverTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        if (zenith->GetTableField("onUninteractableTemplate", 2))
        {
            panelTemplate.onUninteractableTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        // Event Callbacks
        if (zenith->GetTableField("onMouseDown", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onMouseDownEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onMouseUp", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onMouseUpEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onMouseHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onMouseHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        if (zenith->GetTableField("onHoverBegin", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onHoverBeginEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onHoverEnd", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onHoverEndEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onHoverHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onHoverHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        if (zenith->GetTableField("onFocusBegin", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onFocusBeginEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onFocusEnd", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onFocusEndEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onFocusHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                panelTemplate.onFocusHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        uiSingleton.templateHashToPanelTemplateIndex[templateNameHash] = panelTemplateIndex;

        return 0;
    }

    i32 UIHandler::RegisterTextTemplate(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 textTemplateIndex = static_cast<u32>(uiSingleton.textTemplates.size());
        auto& textTemplate = uiSingleton.textTemplates.emplace_back();

        const char* templateName = zenith->CheckVal<const char*>(1);

        const char* font = nullptr;
        if (zenith->GetTableField("font", 2))
        {
            textTemplate.font = zenith->CheckVal<const char*>(-1);
            textTemplate.setFlags.font = 1;
            zenith->Pop();
        }

        f32 size = 0.0f;
        if (zenith->GetTableField("size", 2))
        {
            textTemplate.size = zenith->CheckVal<f32>(-1);
            textTemplate.setFlags.size = 1;
            zenith->Pop();
        }

        textTemplate.color = Color::White;
        if (zenith->GetTableField("color", 2))
        {
            vec3 color = zenith->CheckVal<vec3>(-1);
            textTemplate.color = Color(color.x, color.y, color.z);
            textTemplate.setFlags.color = 1;
            zenith->Pop();
        }

        if (zenith->GetTableField("borderSize", 2))
        {
            textTemplate.borderSize = zenith->CheckVal<f32>(-1);
            textTemplate.setFlags.borderSize = 1;
            zenith->Pop();
        }

        textTemplate.borderColor = Color::White;
        if (zenith->GetTableField("borderColor", 2))
        {
            vec3 color = zenith->CheckVal<vec3>(-1);
            textTemplate.borderColor = Color(color.x, color.y, color.z);
            textTemplate.setFlags.borderColor = 1;
            zenith->Pop();
        }

        textTemplate.wrapWidth = 0.0f;
        if (zenith->GetTableField("wrapWidth", 2))
        {
            f32 wrapWidth = zenith->CheckVal<f32>(-1);
            wrapWidth = glm::max(0.0f, wrapWidth);

            textTemplate.wrapWidth = wrapWidth;
            textTemplate.setFlags.wrapWidth = wrapWidth > 0.0f;
            zenith->Pop();
        }

        textTemplate.wrapIndent = 0;
        if (zenith->GetTableField("wrapIndent", 2))
        {
            i32 wrapIndent = zenith->CheckVal<i32>(-1);
            wrapIndent = glm::max(0, wrapIndent);

            textTemplate.wrapIndent = static_cast<u8>(wrapIndent);
            textTemplate.setFlags.wrapIndent = wrapIndent > 0;
            zenith->Pop();
        }

        // Event Templates
        if (zenith->GetTableField("onClickTemplate", 2))
        {
            textTemplate.onClickTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        if (zenith->GetTableField("onHoverTemplate", 2))
        {
            textTemplate.onHoverTemplate = zenith->CheckVal<const char*>(-1);
            zenith->Pop();
        }

        // Event Callbacks
        if (zenith->GetTableField("onMouseDown", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onMouseDownEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onMouseUp", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onMouseUpEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onMouseHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onMouseHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        if (zenith->GetTableField("onHoverBegin", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onHoverBeginEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onHoverEnd", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onHoverEndEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onHoverHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onHoverHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        if (zenith->GetTableField("onFocusBegin", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onFocusBeginEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onFocusEnd", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onFocusEndEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }
        if (zenith->GetTableField("onFocusHeld", 2))
        {
            if (zenith->IsFunction(-1))
            {
                textTemplate.onFocusHeldEvent = zenith->GetRef(-1);
            }
            zenith->Pop();
        }

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        uiSingleton.templateHashToTextTemplateIndex[templateNameHash] = textTemplateIndex;

        return 0;
    }

    i32 UIHandler::RegisterSendMessageToChatCallback(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        if (zenith->IsNil(1))
        {
            // Unregister chat callback
            uiSingleton.sendMessageToChatCallback = -1;
        }

        if (!zenith->IsFunction(1))
            return 0;

        i32 eventID = zenith->GetRef(1);
        uiSingleton.sendMessageToChatCallback = eventID;

        return 0;
    }

    i32 UIHandler::GetCanvas(Zenith* zenith)
    {
        const char* canvasIdentifier = zenith->CheckVal<const char*>(1);
        if (canvasIdentifier == nullptr)
        {
            zenith->Push();
            return 1;
        }

        i32 posX = zenith->CheckVal<i32>(2);
        i32 posY = zenith->CheckVal<i32>(3);

        i32 sizeX = zenith->CheckVal<i32>(4);
        i32 sizeY = zenith->CheckVal<i32>(5);

        bool isRenderTexture = zenith->IsBoolean(6) ? zenith->ToBoolean(6) : false;

        Widget* widget = nullptr;

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        entt::entity entity = ECS::Util::UI::GetOrEmplaceCanvas(widget, registry, canvasIdentifier, vec2(posX, posY), ivec2(sizeX, sizeY), isRenderTexture);

        Widget* pushWidget = zenith->PushUserData<Widget>([](void* x)
        {
            // Very sad canvas is gone now :(
        });
        memcpy(pushWidget, widget, sizeof(Widget));
        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        return 1;
    }

    i32 UIHandler::GetMousePos(Zenith* zenith)
    {
        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        InputManager* inputManager = ServiceLocator::GetInputManager();

        const vec2& renderSize = renderer->GetRenderSize();
        auto mousePos = inputManager->GetMousePosition();

        mousePos.y = renderSize.y - mousePos.y; // Flipped because UI is bottom-left origin
        mousePos = mousePos / renderSize;
        mousePos *= vec2(Renderer::Settings::UI_REFERENCE_WIDTH, Renderer::Settings::UI_REFERENCE_HEIGHT);

        zenith->Push(mousePos.x);
        zenith->Push(mousePos.y);

        return 2;
    }

    i32 UIHandler::GetTextureSize(Zenith* zenith)
    {
        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();

        const char* texturePath = zenith->CheckVal<const char*>(1);
        if (texturePath == nullptr)
        {
            zenith->Push();
            zenith->Push();
        }
        else
        {
            Renderer::TextureDesc textureDesc;
            textureDesc.path = texturePath;
            Renderer::TextureID textureID = renderer->LoadTexture(textureDesc);

            Renderer::TextureBaseDesc baseDesc = renderer->GetDesc(textureID);
            zenith->Push(baseDesc.width);
            zenith->Push(baseDesc.height);
        }

        return 2;
    }

    i32 UIHandler::PixelsToTexCoord(Zenith* zenith)
    {
        i32 posX = zenith->CheckVal<i32>(1);
        i32 posY = zenith->CheckVal<i32>(2);

        i32 sizeX = zenith->CheckVal<i32>(3);
        i32 sizeY = zenith->CheckVal<i32>(4);

        sizeX = Math::Max(sizeX - 1, 1);
        sizeY = Math::Max(sizeY - 1, 1);

        vec2 texCoord = vec2(static_cast<f32>(posX) / static_cast<f32>(sizeX), static_cast<f32>(posY) / static_cast<f32>(sizeY));

        zenith->Push(texCoord.x);
        zenith->Push(texCoord.y);

        return 2;
    }
    i32 UIHandler::CalculateTextSize(Zenith* zenith)
    {
        const char* text = zenith->CheckVal<const char*>(1);
        if (text == nullptr)
        {
            luaL_error(zenith->state, "Expected text as parameter 1");
        }

        const char* templateName = zenith->CheckVal<const char*>(2);
        if (templateName == nullptr)
        {
            luaL_error(zenith->state, "Expected text template name as parameter 2");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        u32 textTemplateIndex = uiSingleton.templateHashToTextTemplateIndex[templateNameHash];

        const auto& textTemplate = uiSingleton.textTemplates[textTemplateIndex];

        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        Renderer::Font* font = Renderer::Font::GetFont(renderer, textTemplate.font);

        std::string textStr = text;
        ECS::Util::UI::ReplaceTextNewLines(textStr);

        if (textTemplate.setFlags.wrapWidth)
        {
            textStr = ECS::Util::UI::GenWrapText(textStr, font, textTemplate.size, textTemplate.borderSize, textTemplate.wrapWidth, textTemplate.wrapIndent);
        }

        vec2 textSize = font->CalculateTextSize(textStr, textTemplate.size, textTemplate.borderSize);

        zenith->Push(textSize.x);
        zenith->Push(textSize.y);
        
        return 2;
    }

    i32 UIHandler::WrapText(Zenith* zenith)
    {
        const char* text = zenith->CheckVal<const char*>(1);
        if (text == nullptr)
        {
            luaL_error(zenith->state, "Expected text as parameter 1");
        }

        const char* templateName = zenith->CheckVal<const char*>(2);
        if (templateName == nullptr)
        {
            luaL_error(zenith->state, "Expected text template name as parameter 2");
        }

        f32 wrapWidth = zenith->CheckVal<f32>(3);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        u32 textTemplateIndex = uiSingleton.templateHashToTextTemplateIndex[templateNameHash];

        const auto& textTemplate = uiSingleton.textTemplates[textTemplateIndex];
        if (wrapWidth == -1.0f)
        {
            wrapWidth = textTemplate.wrapWidth;
        }

        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        Renderer::Font* font = Renderer::Font::GetFont(renderer, textTemplate.font);

        std::string textStr = text;
        if (wrapWidth == 0)
        {
            zenith->Push(text);
        }
        else
        {
            textStr = ECS::Util::UI::GenWrapText(text, font, textTemplate.size, textTemplate.borderSize, wrapWidth, textTemplate.wrapIndent);
            zenith->Push(textStr.c_str());
        }

        vec2 textSize = font->CalculateTextSize(textStr, textTemplate.size, textTemplate.borderSize);
        zenith->Push(textSize.x);
        zenith->Push(textSize.y);

        return 3;
    }

    i32 UIHandler::FocusWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Util::UI::FocusWidgetEntity(registry, widget->entity);

        registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

        return 0;
    }

    i32 UIHandler::UnfocusWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        if (widget->entity == uiSingleton.focusedEntity)
        {
            ECS::Util::UI::FocusWidgetEntity(registry, entt::null);
        }

        registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

        return 0;
    }

    i32 UIHandler::IsFocusedWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        bool isFocusedWidget = widget->entity == uiSingleton.focusedEntity;
        zenith->Push(isFocusedWidget);

        return 1;
    }

    i32 UIHandler::WasJustFocusedWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        bool wasJustFocusedWidget = widget->entity == uiSingleton.justFocusedEntity;
        zenith->Push(wasJustFocusedWidget);

        return 1;
    }

    i32 UIHandler::GetFocusedWidget(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        if (uiSingleton.focusedEntity == entt::null)
        {
            return 0;
        }

        auto& widgetComp = registry->get<ECS::Components::UI::Widget>(uiSingleton.focusedEntity);
        zenith->PushLightUserData(widgetComp.scriptWidget);

        luaL_getmetatable(zenith->state, widgetComp.scriptWidget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        return 1;
    }

    i32 UIHandler::IsHoveredWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        bool isHoveredWidget = widget->entity == uiSingleton.hoveredEntity;
        zenith->Push(isHoveredWidget);

        return 1;
    }

    i32 UIHandler::DestroyWidget(Zenith* zenith)
    {
        Widget* widget = zenith->GetUserData<Widget>(1);
        if (widget == nullptr)
        {
            luaL_error(zenith->state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;

        auto& widgetComp = registry->get<ECS::Components::UI::Widget>(widget->entity);
        if (widgetComp.type != ECS::Components::UI::WidgetType::Panel && widgetComp.type != ECS::Components::UI::WidgetType::Text && widgetComp.type != ECS::Components::UI::WidgetType::Widget)
        {
            luaL_error(zenith->state, "Expected a Panel, Text or Widget for DestroyWidget");
        }

        registry->emplace_or_replace<ECS::Components::UI::DirtyCanvasTag>(widget->canvasEntity);

        if (!ECS::Util::UI::DestroyWidget(registry, widget->entity))
        {
            luaL_error(zenith->state, "Failed to destroy widget");
        }

        return 0;
    }

    i32 UIHandler::AddOnKeyboard(Zenith* zenith)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        auto& regCtx = registry->ctx();
        auto& inputSingleton = regCtx.get<ECS::Singletons::InputSingleton>();

        u32 eventIndex = static_cast<u32>(inputSingleton.globalKeyboardEvents.size());
        i32 callback = zenith->IsFunction(1) ? zenith->GetRef(1) : -1;

        inputSingleton.globalKeyboardEvents.push_back(callback);
        inputSingleton.eventIDToKeyboardEventIndex[callback] = eventIndex;

        return 0;
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget)
    {
        lua_checkstack(zenith->state, 3);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->PCall(2);
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value)
    {
        lua_checkstack(zenith->state, 4);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(value);
        zenith->PCall(3);
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value1, const vec2& value2)
    {
        lua_checkstack(zenith->state, 6);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(value1);
        zenith->Push(value2.x);
        zenith->Push(value2.y);
        zenith->PCall(5);
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, f32 value)
    {
        lua_checkstack(zenith->state, 4);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(value);
        zenith->PCall(3);
    }

    void UIHandler::CallUIInputEvent(Zenith* zenith, i32 eventRef, UIInputEvent inputEvent, Widget* widget, const vec2& value)
    {
        lua_checkstack(zenith->state, 5);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);
        zenith->Push(static_cast<u32>(inputEvent));
        zenith->PushLightUserData(widget);

        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(value.x);
        zenith->Push(value.y);
        zenith->PCall(4);
    }

    bool UIHandler::CallKeyboardInputEvent(Zenith* zenith, i32 eventRef, Widget* widget, i32 key, i32 actionMask, i32 modifierMask)
    {
        lua_checkstack(zenith->state, 7);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);

        zenith->PushLightUserData(widget);
        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(static_cast<i32>(UIKeyboardEvent::Key));
        zenith->Push(key);
        zenith->Push(actionMask);
        zenith->Push(modifierMask);

        zenith->PCall(5, 1);
        bool result = zenith->CheckVal<bool>(-1);
        zenith->Pop();

        return result; // Return if we should consume the event or not
    }

    bool UIHandler::CallKeyboardInputEvent(Zenith* zenith, i32 eventRef, i32 key, i32 actionMask, i32 modifierMask)
    {
        lua_checkstack(zenith->state, 6);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);

        zenith->Push(static_cast<i32>(UIKeyboardEvent::Key));
        zenith->Push(key);
        zenith->Push(actionMask);
        zenith->Push(modifierMask);

        zenith->PCall(4, 1);
        bool result = zenith->CheckVal<bool>(-1);
        zenith->Pop();

        return result; // Return if we should consume the event or not
    }

    bool UIHandler::CallKeyboardUnicodeEvent(Zenith* zenith, i32 eventRef, Widget* widget, u32 unicode)
    {
        lua_checkstack(zenith->state, 5);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);

        zenith->PushLightUserData(widget);
        luaL_getmetatable(zenith->state, widget->metaTableName.c_str());
        lua_setmetatable(zenith->state, -2);

        zenith->Push(static_cast<i32>(UIKeyboardEvent::Unicode));
        zenith->Push(unicode);

        zenith->PCall(3, 1);
        bool result = zenith->CheckVal<bool>(1);
        zenith->Pop();

        return result; // Return if widget should consume the event or not
    }

    void UIHandler::CallSendMessageToChat(Zenith* zenith, i32 eventRef, const std::string& channel, const std::string& playerName, const std::string& text, bool isOutgoing)
    {
        lua_checkstack(zenith->state, 5);
        zenith->GetRawI(LUA_REGISTRYINDEX, eventRef);

        zenith->Push(channel);
        zenith->Push(playerName);
        zenith->Push(text);
        zenith->Push(isOutgoing);

        zenith->PCall(4);
    }

    void UIHandler::CreateUIInputEventTable(Zenith* zenith)
    {
        {
            zenith->CreateTable("UIInputEvent");

            zenith->AddTableField("MouseDown", static_cast<u32>(UIInputEvent::MouseDown));
            zenith->AddTableField("MouseUp", static_cast<u32>(UIInputEvent::MouseUp));
            zenith->AddTableField("MouseHeld", static_cast<u32>(UIInputEvent::MouseHeld));

            zenith->AddTableField("HoverBegin", static_cast<u32>(UIInputEvent::HoverBegin));
            zenith->AddTableField("HoverEnd", static_cast<u32>(UIInputEvent::HoverEnd));
            zenith->AddTableField("HoverHeld", static_cast<u32>(UIInputEvent::HoverHeld));

            zenith->AddTableField("FocusBegin", static_cast<u32>(UIInputEvent::FocusBegin));
            zenith->AddTableField("FocusEnd", static_cast<u32>(UIInputEvent::FocusEnd));
            zenith->AddTableField("FocusHeld", static_cast<u32>(UIInputEvent::FocusHeld));

            zenith->Pop();
        }

        {
            zenith->CreateTable("UIKeyboardEvent");

            zenith->AddTableField("Key", static_cast<u32>(UIKeyboardEvent::Key));
            zenith->AddTableField("Unicode", static_cast<u32>(UIKeyboardEvent::Unicode));

            zenith->Pop();
        }

        {
            // TODO: Move these to something related to input in the future
            zenith->CreateTable("InputAction");

            zenith->AddTableField("Press", static_cast<u32>(KeybindAction::Press));
            zenith->AddTableField("Release", static_cast<u32>(KeybindAction::Release));
            zenith->AddTableField("Click", static_cast<u32>(KeybindAction::Click));

            zenith->Pop();
        }

        {
            zenith->CreateTable("InputModifier");

            zenith->AddTableField("Invalid", static_cast<u32>(KeybindModifier::Invalid));
            zenith->AddTableField("None", static_cast<u32>(KeybindModifier::ModNone));
            zenith->AddTableField("Shift", static_cast<u32>(KeybindModifier::Shift));
            zenith->AddTableField("Ctrl", static_cast<u32>(KeybindModifier::Ctrl));
            zenith->AddTableField("Alt", static_cast<u32>(KeybindModifier::Alt));
            zenith->AddTableField("Any", static_cast<u32>(KeybindModifier::Any));

            zenith->Pop();
        }
    }
}
