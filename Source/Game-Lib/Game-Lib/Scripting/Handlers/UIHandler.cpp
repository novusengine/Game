#include "UIHandler.h"
#include "Game-Lib/Application/EnttRegistries.h"
#include "Game-Lib/ECS/Singletons/UISingleton.h"
#include "Game-Lib/ECS/Util/Transform2D.h"
#include "Game-Lib/ECS/Util/UIUtil.h"
#include "Game-Lib/Rendering/GameRenderer.h"
#include "Game-Lib/Rendering/Canvas/CanvasRenderer.h"
#include "Game-Lib/Scripting/LuaState.h"
#include "Game-Lib/Scripting/LuaManager.h"
#include "Game-Lib/Scripting/Systems/LuaSystemBase.h"
#include "Game-Lib/Scripting/UI/Box.h"
#include "Game-Lib/Scripting/UI/Canvas.h"
#include "Game-Lib/Scripting/UI/Panel.h"
#include "Game-Lib/Scripting/UI/Text.h"
#include "Game-Lib/UI/Box.h"
#include "Game-Lib/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <Input/KeybindGroup.h>

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting::UI
{
    static LuaMethod uiMethods[] =
    {
        { "RegisterPanelTemplate", UIHandler::RegisterPanelTemplate },
        { "RegisterTextTemplate", UIHandler::RegisterTextTemplate },

        { "GetCanvas", UIHandler::GetCanvas },

        // Utils
        { "PixelsToTexCoord", UIHandler::PixelsToTexCoord },
        { "CalculateTextSize", UIHandler::CalculateTextSize },

        { "FocusWidget", UIHandler::FocusWidget },
        { "UnfocusWidget", UIHandler::UnfocusWidget },
        { "IsFocusedWidget", UIHandler::IsFocusedWidget },
        { "GetFocusedWidget", UIHandler::GetFocusedWidget },

        { nullptr, nullptr }
    };

    void UIHandler::Register(lua_State* state)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        registry->ctx().emplace<ECS::Singletons::UISingleton>();

        // UI
        LuaMethodTable::Set(state, uiMethods, "UI");

        // Widgets
        Widget::Register(state);
        Canvas::Register(state);
        Panel::Register(state);
        Text::Register(state);

        // Utils
        Box::Register(state);

        CreateUIInputEventTable(state);
    }

    void UIHandler::Clear()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        registry->clear();

        ECS::Transform2DSystem::Get(*registry).Clear();
        ServiceLocator::GetGameRenderer()->GetCanvasRenderer()->Clear();
    }

    // UI
    i32 UIHandler::RegisterPanelTemplate(lua_State* state)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 panelTemplateIndex = static_cast<u32>(uiSingleton.panelTemplates.size());
        auto& panelTemplate = uiSingleton.panelTemplates.emplace_back();

        LuaState ctx(state);
        const char* templateName = ctx.Get(nullptr, 1);

        if (ctx.GetTableField("background", 2))
        {
            panelTemplate.background = ctx.Get("", -1);
            panelTemplate.setFlags.background = 1;
            ctx.Pop();
        }

        if (ctx.GetTableField("foreground", 2))
        {
            panelTemplate.foreground = ctx.Get("", -1);
            panelTemplate.setFlags.foreground = 1;
            ctx.Pop();
        }

        panelTemplate.color = Color::White;
        if (ctx.GetTableField("color", 2))
        {
            vec3 color = ctx.Get(vec3(1, 1, 1), -1);
            panelTemplate.color = Color(color.x, color.y, color.z);
            panelTemplate.setFlags.color = 1;
            ctx.Pop();
        }

        if (ctx.GetTableField("cornerRadius", 2))
        {
            panelTemplate.cornerRadius = ctx.Get(0.0f, -1);
            panelTemplate.setFlags.cornerRadius = 1;
            ctx.Pop();
        }

        if (ctx.GetTableField("texCoords", 2))
        {
            ::UI::Box* box = ctx.GetUserData<::UI::Box>(nullptr, -1);
            ctx.Pop();

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

        if (ctx.GetTableField("nineSliceCoords", 2))
        {
            ::UI::Box* box = ctx.GetUserData<::UI::Box>(nullptr, -1);
            ctx.Pop();

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
        if (ctx.GetTableField("onClickTemplate", 2))
        {
            panelTemplate.onClickTemplate = ctx.Get("", -1);
            ctx.Pop();
        }

        if (ctx.GetTableField("onHoverTemplate", 2))
        {
            panelTemplate.onHoverTemplate = ctx.Get("", -1);
            ctx.Pop();
        }

        if (ctx.GetTableField("onUninteractableTemplate", 2))
        {
            panelTemplate.onUninteractableTemplate = ctx.Get("", -1);
            ctx.Pop();
        }

        // Event Callbacks
        if (ctx.GetTableField("onMouseDown", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                panelTemplate.onMouseDownEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onMouseUp", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                panelTemplate.onMouseUpEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onMouseHeld", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                panelTemplate.onMouseHeldEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }

        if (ctx.GetTableField("onHoverBegin", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                panelTemplate.onHoverBeginEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onHoverEnd", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                panelTemplate.onHoverEndEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onHoverHeld", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                panelTemplate.onHoverHeldEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }

        if (ctx.GetTableField("onFocusBegin", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                panelTemplate.onFocusBeginEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onFocusEnd", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                panelTemplate.onFocusEndEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onFocusHeld", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                panelTemplate.onFocusHeldEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        uiSingleton.templateHashToPanelTemplateIndex[templateNameHash] = panelTemplateIndex;

        return 0;
    }

    i32 UIHandler::RegisterTextTemplate(lua_State* state)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 textTemplateIndex = static_cast<u32>(uiSingleton.textTemplates.size());
        auto& textTemplate = uiSingleton.textTemplates.emplace_back();

        LuaState ctx(state);
        const char* templateName = ctx.Get(nullptr, 1);

        const char* font = nullptr;
        if (ctx.GetTableField("font", 2))
        {
            textTemplate.font = ctx.Get(nullptr, -1);
            textTemplate.setFlags.font = 1;
            ctx.Pop();
        }

        f32 size = 0.0f;
        if (ctx.GetTableField("size", 2))
        {
            textTemplate.size = ctx.Get(0.0f, -1);
            textTemplate.setFlags.size = 1;
            ctx.Pop();
        }

        textTemplate.color = Color::White;
        if (ctx.GetTableField("color", 2))
        {
            vec3 color = ctx.Get(vec3(1, 1, 1), -1);
            textTemplate.color = Color(color.x, color.y, color.z);
            textTemplate.setFlags.color = 1;
            ctx.Pop();
        }

        if (ctx.GetTableField("borderSize", 2))
        {
            textTemplate.borderSize = ctx.Get(0.0f, -1);
            textTemplate.setFlags.borderSize = 1;
            ctx.Pop();
        }

        textTemplate.borderColor = Color::White;
        if (ctx.GetTableField("borderColor", 2))
        {
            vec3 color = ctx.Get(vec3(1, 1, 1), -1);
            textTemplate.borderColor = Color(color.x, color.y, color.z);
            textTemplate.setFlags.borderColor = 1;
            ctx.Pop();
        }

        // Event Templates
        if (ctx.GetTableField("onClickTemplate", 2))
        {
            textTemplate.onClickTemplate = ctx.Get("", -1);
            ctx.Pop();
        }

        if (ctx.GetTableField("onHoverTemplate", 2))
        {
            textTemplate.onHoverTemplate = ctx.Get("", -1);
            ctx.Pop();
        }

        // Event Callbacks
        if (ctx.GetTableField("onMouseDown", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                textTemplate.onMouseDownEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onMouseUp", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                textTemplate.onMouseUpEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onMouseHeld", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                textTemplate.onMouseHeldEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }

        if (ctx.GetTableField("onHoverBegin", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                textTemplate.onHoverBeginEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onHoverEnd", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                textTemplate.onHoverEndEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onHoverHeld", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                textTemplate.onHoverHeldEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }

        if (ctx.GetTableField("onFocusBegin", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                textTemplate.onFocusBeginEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onFocusEnd", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                textTemplate.onFocusEndEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }
        if (ctx.GetTableField("onFocusHeld", 2))
        {
            if (lua_isfunction(ctx.RawState(), -1))
            {
                textTemplate.onFocusHeldEvent = ctx.GetRef(-1);
            }
            ctx.Pop();
        }

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        uiSingleton.templateHashToTextTemplateIndex[templateNameHash] = textTemplateIndex;

        return 0;
    }

    i32 UIHandler::GetCanvas(lua_State* state)
    {
        LuaState ctx(state);

        const char* canvasIdentifier = ctx.Get(nullptr, 1);
        if (canvasIdentifier == nullptr)
        {
            ctx.Push();
            return 1;
        }

        i32 posX = ctx.Get(0, 2);
        i32 posY = ctx.Get(0, 3);

        i32 sizeX = ctx.Get(100, 4);
        i32 sizeY = ctx.Get(100, 5);

        Widget* canvas = ctx.PushUserData<Widget>([](void* x)
        {
            // Very sad canvas is gone now :(
        });

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        entt::entity entity = ECS::Util::UI::GetOrEmplaceCanvas(canvas, registry, canvasIdentifier, vec2(posX, posY), ivec2(sizeX, sizeY));

        canvas->type = WidgetType::Canvas;
        canvas->entity = entity;

        canvas->metaTableName = "CanvasMetaTable";
        luaL_getmetatable(state, "CanvasMetaTable");
        lua_setmetatable(state, -2);

        return 1;
    }

    i32 UIHandler::PixelsToTexCoord(lua_State* state)
    {
        LuaState ctx(state);
        
        i32 posX = ctx.Get(0, 1);
        i32 posY = ctx.Get(0, 2);

        i32 sizeX = ctx.Get(1, 3);
        i32 sizeY = ctx.Get(1, 4);

        sizeX = Math::Max(sizeX - 1, 1);
        sizeY = Math::Max(sizeY - 1, 1);

        ctx.Pop(4);

        vec2 texCoord = vec2(static_cast<f32>(posX) / static_cast<f32>(sizeX), static_cast<f32>(posY) / static_cast<f32>(sizeY));

        u32 top = ctx.GetTop();

        ctx.Push(texCoord.x);
        ctx.Push(texCoord.y);

        top = ctx.GetTop();

        return 2;
    }

    i32 UIHandler::CalculateTextSize(lua_State* state)
    {
        LuaState ctx(state);

        const char* text = ctx.Get(nullptr, 1);
        if (text == nullptr)
        {
            luaL_error(state, "Expected text as parameter 1");
        }

        const char* templateName = ctx.Get(nullptr, 1);
        if (templateName == nullptr)
        {
            luaL_error(state, "Expected text template name as parameter 2");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        u32 textTemplateIndex = uiSingleton.templateHashToTextTemplateIndex[templateNameHash];

        const auto& textTemplate = uiSingleton.textTemplates[textTemplateIndex];

        Renderer::Renderer* renderer = ServiceLocator::GetGameRenderer()->GetRenderer();
        Renderer::Font* font = Renderer::Font::GetFont(renderer, textTemplate.font);

        vec2 textSize = font->CalculateTextSize(text, textTemplate.size, textTemplate.borderSize);

        ctx.Push(textSize.x);
        ctx.Push(textSize.y);
        
        return 2;
    }

    i32 UIHandler::FocusWidget(lua_State* state)
    {
        LuaState ctx(state);

        Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
        if (widget == nullptr)
        {
            luaL_error(state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Util::UI::FocusWidgetEntity(registry, widget->entity);

        return 0;
    }

    i32 UIHandler::UnfocusWidget(lua_State* state)
    {
        LuaState ctx(state);

        Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
        if (widget == nullptr)
        {
            luaL_error(state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        if (widget->entity == uiSingleton.focusedEntity)
        {
            ECS::Util::UI::FocusWidgetEntity(registry, entt::null);
        }

        return 0;
    }

    i32 UIHandler::IsFocusedWidget(lua_State* state)
    {
        LuaState ctx(state);

        Widget* widget = ctx.GetUserData<Widget>(nullptr, 1);
        if (widget == nullptr)
        {
            luaL_error(state, "Expected widget as parameter 1");
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        bool isFocusedWidget = widget->entity == uiSingleton.focusedEntity;
        ctx.Push(isFocusedWidget);

        return 1;
    }

    i32 UIHandler::GetFocusedWidget(lua_State* state)
    {
        LuaState ctx(state);

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        if (uiSingleton.focusedEntity == entt::null)
        {
            return 0;
        }

        auto& widgetComp = registry->get<ECS::Components::UI::Widget>(uiSingleton.focusedEntity);

        lua_pushlightuserdata(state, widgetComp.scriptWidget);

        luaL_getmetatable(state, widgetComp.scriptWidget->metaTableName.c_str());
        lua_setmetatable(state, -2);

        return 1;
    }

    void UIHandler::CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget)
    {
        LuaState ctx(state);

        ctx.GetRawI(LUA_REGISTRYINDEX, eventRef);
        ctx.Push(static_cast<u32>(inputEvent));
        lua_pushlightuserdata(state, widget);

        luaL_getmetatable(state, widget->metaTableName.c_str());
        lua_setmetatable(state, -2);

        ctx.PCall(2);
    }

    void UIHandler::CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, i32 value)
    {
        LuaState ctx(state);

        ctx.GetRawI(LUA_REGISTRYINDEX, eventRef);
        ctx.Push(static_cast<u32>(inputEvent));
        lua_pushlightuserdata(state, widget);

        luaL_getmetatable(state, widget->metaTableName.c_str());
        lua_setmetatable(state, -2);

        ctx.Push(value);
        ctx.PCall(3);
    }

    void UIHandler::CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, f32 value)
    {
        LuaState ctx(state);

        ctx.GetRawI(LUA_REGISTRYINDEX, eventRef);
        ctx.Push(static_cast<u32>(inputEvent));
        lua_pushlightuserdata(state, widget);

        luaL_getmetatable(state, widget->metaTableName.c_str());
        lua_setmetatable(state, -2);

        ctx.Push(value);
        ctx.PCall(3);
    }

    void UIHandler::CallUIInputEvent(lua_State* state, i32 eventRef, UIInputEvent inputEvent, Widget* widget, const vec2& value)
    {
        LuaState ctx(state);

        ctx.GetRawI(LUA_REGISTRYINDEX, eventRef);
        ctx.Push(static_cast<u32>(inputEvent));
        lua_pushlightuserdata(state, widget);

        luaL_getmetatable(state, widget->metaTableName.c_str());
        lua_setmetatable(state, -2);

        ctx.Push(value.x);
        ctx.Push(value.y);
        ctx.PCall(4);
    }

    void UIHandler::CallKeyboardInputEvent(lua_State* state, i32 eventRef, Widget* widget, i32 key, i32 actionMask, i32 modifierMask)
    {
        LuaState ctx(state);

        ctx.GetRawI(LUA_REGISTRYINDEX, eventRef);
        lua_pushlightuserdata(state, widget);

        luaL_getmetatable(state, widget->metaTableName.c_str());
        lua_setmetatable(state, -2);

        ctx.Push(static_cast<i32>(UIKeyboardEvent::Key));
        ctx.Push(key);
        ctx.Push(actionMask);
        ctx.Push(modifierMask);

        ctx.PCall(5);
    }

    void UIHandler::CallKeyboardUnicodeEvent(lua_State* state, i32 eventRef, Widget* widget, u32 unicode)
    {
        LuaState ctx(state);

        ctx.GetRawI(LUA_REGISTRYINDEX, eventRef);
        lua_pushlightuserdata(state, widget);

        luaL_getmetatable(state, widget->metaTableName.c_str());
        lua_setmetatable(state, -2);

        ctx.Push(static_cast<i32>(UIKeyboardEvent::Unicode));
        ctx.Push(unicode);

        ctx.PCall(3);
    }

    void UIHandler::CreateUIInputEventTable(lua_State* state)
    {
        LuaState ctx(state);

        ctx.CreateTableAndPopulate("UIInputEvent", [&]()
        {
            ctx.SetTable("MouseDown", static_cast<u32>(UIInputEvent::MouseDown));
            ctx.SetTable("MouseUp", static_cast<u32>(UIInputEvent::MouseUp));
            ctx.SetTable("MouseHeld", static_cast<u32>(UIInputEvent::MouseHeld));

            ctx.SetTable("HoverBegin", static_cast<u32>(UIInputEvent::HoverBegin));
            ctx.SetTable("HoverEnd", static_cast<u32>(UIInputEvent::HoverEnd));
            ctx.SetTable("HoverHeld", static_cast<u32>(UIInputEvent::HoverHeld));

            ctx.SetTable("FocusBegin", static_cast<u32>(UIInputEvent::FocusBegin));
            ctx.SetTable("FocusEnd", static_cast<u32>(UIInputEvent::FocusEnd));
            ctx.SetTable("FocusHeld", static_cast<u32>(UIInputEvent::FocusHeld));
        });

        ctx.CreateTableAndPopulate("UIKeyboardEvent", [&]()
        {
            ctx.SetTable("Key", static_cast<u32>(UIKeyboardEvent::Key));
            ctx.SetTable("Unicode", static_cast<u32>(UIKeyboardEvent::Unicode));
        });

        // TODO: Move these to something related to input in the future
        ctx.CreateTableAndPopulate("InputAction", [&]()
        {
            ctx.SetTable("Press", static_cast<u32>(KeybindAction::Press));
            ctx.SetTable("Release", static_cast<u32>(KeybindAction::Release));
            ctx.SetTable("Click", static_cast<u32>(KeybindAction::Click));
        });

        ctx.CreateTableAndPopulate("InputModifier", [&]()
        {
            ctx.SetTable("Invalid", static_cast<u32>(KeybindModifier::Invalid));
            ctx.SetTable("None", static_cast<u32>(KeybindModifier::ModNone));
            ctx.SetTable("Shift", static_cast<u32>(KeybindModifier::Shift));
            ctx.SetTable("Ctrl", static_cast<u32>(KeybindModifier::Ctrl));
            ctx.SetTable("Alt", static_cast<u32>(KeybindModifier::Alt));
            ctx.SetTable("Any", static_cast<u32>(KeybindModifier::Any));
        });
    }
}
