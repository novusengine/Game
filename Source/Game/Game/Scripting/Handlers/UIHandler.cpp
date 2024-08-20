#include "UIHandler.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Singletons/UISingleton.h"
#include "Game/ECS/Util/Transform2D.h"
#include "Game/ECS/Util/UIUtil.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Rendering/Canvas/CanvasRenderer.h"
#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Scripting/Systems/LuaSystemBase.h"
#include "Game/Scripting/UI/Button.h"
#include "Game/Scripting/UI/Box.h"
#include "Game/Scripting/UI/Canvas.h"
#include "Game/Scripting/UI/Panel.h"
#include "Game/Scripting/UI/Text.h"
#include "Game/UI/Box.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting::UI
{
    static LuaMethod uiMethods[] =
    {
        { "RegisterButtonTemplate", UIHandler::RegisterButtonTemplate },
        { "RegisterPanelTemplate", UIHandler::RegisterPanelTemplate },
        { "RegisterTextTemplate", UIHandler::RegisterTextTemplate },

        { "GetCanvas", UIHandler::GetCanvas },

        // Utils
        { "PixelsToTexCoord", UIHandler::PixelsToTexCoord },

        { nullptr, nullptr }
    };

    void UIHandler::Register(lua_State* state)
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        registry->ctx().emplace<ECS::Singletons::UISingleton>();

        // UI
        LuaMethodTable::Set(state, uiMethods, "UI");

        // Widgets
        Button::Register(state);
        Canvas::Register(state);
        Panel::Register(state);
        Text::Register(state);

        // Utils
        Box::Register(state);
    }

    void UIHandler::Clear()
    {
        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        registry->clear();

        ECS::Transform2DSystem::Get(*registry).Clear();
        ServiceLocator::GetGameRenderer()->GetCanvasRenderer()->Clear();
    }

    // UI
    i32 UIHandler::RegisterButtonTemplate(lua_State* state)
    {
        LuaState ctx(state);

        const char* templateName = ctx.Get(nullptr, 1);

        const char* panelTemplate = nullptr;
        if (ctx.GetTableField("panelTemplate", 2))
        {
            panelTemplate = ctx.Get(nullptr, 3);
            ctx.Pop(1);
        }

        const char* textTemplate = nullptr;
        if (ctx.GetTableField("textTemplate", 2))
        {
            textTemplate = ctx.Get(nullptr, 3);
            ctx.Pop(1);
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 buttonTemplateIndex = static_cast<u32>(uiSingleton.buttonTemplates.size());
        auto& buttonTemplate = uiSingleton.buttonTemplates.emplace_back();
        buttonTemplate.panelTemplate = panelTemplate;
        buttonTemplate.textTemplate = textTemplate;

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        uiSingleton.templateHashToButtonTemplateIndex[templateNameHash] = buttonTemplateIndex;

        return 0;
    }

    i32 UIHandler::RegisterPanelTemplate(lua_State* state)
    {
        LuaState ctx(state);

        const char* templateName = ctx.Get(nullptr, 1);

        const char* background = "";
        if (ctx.GetTableField("background", 2))
        {
            background = ctx.Get("", 3);
            ctx.Pop(1);
        }

        vec3 color = vec3(1.0f, 1.0f, 1.0f);
        if (ctx.GetTableField("color", 2))
        {
            color = ctx.Get(vec3(1, 1, 1), 3);
            ctx.Pop(1);
        }

        f32 cornerRadius = 0.0f;
        if (ctx.GetTableField("cornerRadius", 2))
        {
            cornerRadius = ctx.Get(0.0f, 3);
            ctx.Pop(1);
        }

        ::UI::Box texCoords;
        if (ctx.GetTableField("texCoords", 2))
        {
            ::UI::Box* box = ctx.GetUserData<::UI::Box>(nullptr, 3);
            ctx.Pop(1);

            if (box)
            {
                texCoords = *box;
            }
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 panelTemplateIndex = static_cast<u32>(uiSingleton.panelTemplates.size());
        auto& panelTemplate = uiSingleton.panelTemplates.emplace_back();
        panelTemplate.background = background;
        panelTemplate.color = Color(color.x, color.y, color.z);
        panelTemplate.cornerRadius = cornerRadius;
        panelTemplate.texCoords = texCoords;

        u32 templateNameHash = StringUtils::fnv1a_32(templateName, strlen(templateName));
        uiSingleton.templateHashToPanelTemplateIndex[templateNameHash] = panelTemplateIndex;

        return 0;
    }

    i32 UIHandler::RegisterTextTemplate(lua_State* state)
    {
        LuaState ctx(state);

        const char* templateName = ctx.Get(nullptr, 1);

        const char* font = nullptr;
        if (ctx.GetTableField("font", 2))
        {
            font = ctx.Get(nullptr, 3);
            ctx.Pop(1);
        }

        f32 size = 0.0f;
        if (ctx.GetTableField("size", 2))
        {
            size = ctx.Get(0.0f, 3);
            ctx.Pop(1);
        }

        vec3 color = vec3(1.0f, 1.0f, 1.0f);
        if (ctx.GetTableField("color", 2))
        {
            color = ctx.Get(vec3(1, 1, 1), 3);
            ctx.Pop(1);
        }

        f32 borderSize = 0.0f;
        if (ctx.GetTableField("borderSize", 2))
        {
            borderSize = ctx.Get(0.0f, 3);
            ctx.Pop(1);
        }

        f32 borderFade = 0.5f;
        if (ctx.GetTableField("borderFade", 2))
        {
            borderFade = ctx.Get(0.5f, 3);
            ctx.Pop(1);
        }

        vec3 borderColor = vec3(1.0f, 1.0f, 1.0f);
        if (ctx.GetTableField("borderColor", 2))
        {
            borderColor = ctx.Get(vec3(1, 1, 1), 3);
            ctx.Pop(1);
        }

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        ECS::Singletons::UISingleton& uiSingleton = registry->ctx().get<ECS::Singletons::UISingleton>();

        u32 textTemplateIndex = static_cast<u32>(uiSingleton.textTemplates.size());
        auto& textTemplate = uiSingleton.textTemplates.emplace_back();
        textTemplate.font = font;
        textTemplate.size = size;
        textTemplate.color = Color(color.x, color.y, color.z);
        textTemplate.borderSize = borderSize;
        textTemplate.borderFade = borderFade;
        textTemplate.borderColor = Color(borderColor.x, borderColor.y, borderColor.z);

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

        entt::registry* registry = ServiceLocator::GetEnttRegistries()->uiRegistry;
        entt::entity entity = ECS::Util::UI::GetOrEmplaceCanvas(registry, canvasIdentifier, vec2(posX, posY), ivec2(sizeX, sizeY));

        Widget* canvas = ctx.PushUserData<Widget>([](void* x)
        {
            // Very sad canvas is gone now :(
        });
        canvas->type = WidgetType::Canvas;
        canvas->entity = entity;

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

        ctx.Pop(4);

        vec2 texCoord = vec2(static_cast<f32>(posX) / static_cast<f32>(sizeX), static_cast<f32>(posY) / static_cast<f32>(sizeY));

        u32 top = ctx.GetTop();

        ctx.Push(texCoord.x);
        ctx.Push(texCoord.y);

        top = ctx.GetTop();

        return 2;
    }
}
