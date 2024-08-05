#include "CanvasHandler.h"
#include "Game/Application/EnttRegistries.h"
#include "Game/ECS/Util/MapUtil.h"
#include "Game/ECS/Singletons/MapDB.h"
#include "Game/Gameplay/MapLoader.h"
#include "Game/Rendering/GameRenderer.h"
#include "Game/Scripting/LuaState.h"
#include "Game/Scripting/LuaManager.h"
#include "Game/Scripting/Systems/LuaSystemBase.h"
#include "Game/Util/ServiceLocator.h"

#include <Base/Util/StringUtils.h>

#include <entt/entt.hpp>
#include <lualib.h>

namespace Scripting
{
    struct Button
    {
        i32 posX;
        i32 posY;
        i32 sizeX;
        i32 sizeY;
        u32 layer;

        u32 templateIndex;
    };

    void CanvasHandler::Register(lua_State* state)
    {
        // UI
        LuaMethodTable::Set(state, uiMethods, "UI");

        // Canvas
        LuaMetaTable<Canvas>::Register(state, "CanvasMetaTable");
        LuaMetaTable<Canvas>::Set(state, canvasMethods);
    }

    i32 CanvasHandler::RegisterButtonTemplate(lua_State* state)
    {
        LuaState ctx(state);

        const char* templateName = ctx.Get(nullptr, 1);

        const char* background = nullptr;
        if (ctx.GetTableField("background", 2))
        {
            background = ctx.Get(nullptr, 3);
            ctx.Pop(1);
        }

        vec3 color = vec3(1.0f, 1.0f, 1.0f);
        if (ctx.GetTableField("color", 2))
        {
            color = ctx.Get(vec3(1,1,1), 3);
            ctx.Pop(1);
        }

        f32 cornerRadius = 0.0f;
        if (ctx.GetTableField("cornerRadius", 2))
        {
            cornerRadius = ctx.Get(0.0f, 3);
            ctx.Pop(1);
        }

        return 0;
    }

    i32 CanvasHandler::RegisterPanelTemplate(lua_State* state)
    {
        LuaState ctx(state);

        const char* templateName = ctx.Get(nullptr, 1);

        const char* background = nullptr;
        if (ctx.GetTableField("background", 2))
        {
            background = ctx.Get(nullptr, 3);
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

        return 0;
    }

    i32 CanvasHandler::RegisterTextTemplate(lua_State* state)
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

        f32 border = 0.0f;
        if (ctx.GetTableField("border", 2))
        {
            border = ctx.Get(0.0f, 3);
            ctx.Pop(1);
        }

        vec3 borderColor = vec3(1.0f, 1.0f, 1.0f);
        if (ctx.GetTableField("borderColor", 2))
        {
            borderColor = ctx.Get(vec3(1, 1, 1), 3);
            ctx.Pop(1);
        }

        return 0;
    }

    // UI
    i32 CanvasHandler::GetCanvas(lua_State* state)
    {
        LuaState ctx(state);

        const char* canvasIdentifier = ctx.Get(nullptr, 1);
        if (canvasIdentifier == nullptr)
        {
            ctx.Push();
            return 1;
        }

        i32 sizeX = ctx.Get(100, 2);
        i32 sizeY = ctx.Get(100, 3);

        Canvas* canvas = ctx.PushUserData<Canvas>([](void* x)
        {
            // Very sad canvas is gone now :(
        });

        canvas->nameHash = StringUtils::fnv1a_32(canvasIdentifier, strlen(canvasIdentifier));
        canvas->sizeX = sizeX;
        canvas->sizeY = sizeY;

        luaL_getmetatable(state, "CanvasMetaTable");
        lua_setmetatable(state, -2);

        return 1;
    }

    i32 CanvasHandler::CreatePanel(lua_State* state)
    {
        LuaState ctx(state);

        i32 posX = ctx.Get(0, 1);
        i32 posY = ctx.Get(0, 2);

        i32 sizeX = ctx.Get(100, 3);
        i32 sizeY = ctx.Get(100, 4);

        i32 layer = ctx.Get(0, 5);

        const char* templateName = ctx.Get(nullptr, 6);
        if (templateName == nullptr)
        {
            ctx.Push();
            return 1;
        }

        Panel* panel = ctx.PushUserData<Panel>([](void* x)
        {

        });

        panel->posX = posX;
        panel->posY = posY;
        panel->sizeX = sizeX;
        panel->sizeY = sizeY;
        panel->layer = layer;

        // TODO: templateIndex

        luaL_getmetatable(state, "PanelMetaTable");
        lua_setmetatable(state, -2);

        return 1;
    }

    i32 CanvasHandler::CreateText(lua_State* state)
    {
        LuaState ctx(state);

        const char* str = ctx.Get("", 1);
        i32 posX = ctx.Get(0, 2);
        i32 posY = ctx.Get(0, 3);

        i32 layer = ctx.Get(0, 4);

        const char* templateName = ctx.Get(nullptr, 5);
        if (templateName == nullptr)
        {
            ctx.Push();
            return 1;
        }

        Text* text = ctx.PushUserData<Text>([](void* x)
        {

        });
        text->text = str;
        text->posX = posX;
        text->posY = posY;
        text->layer = layer;

        // TODO: templateIndex

        //luaL_getmetatable(state, "TextMetaTable");
        //lua_setmetatable(state, -2);

        return 1;
    }
}
