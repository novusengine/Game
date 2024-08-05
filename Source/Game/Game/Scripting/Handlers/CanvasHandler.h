#pragma once
#include "LuaEventHandlerBase.h"
#include "Game/Scripting/LuaDefines.h"
#include "Game/Scripting/LuaMethodTable.h"

#include <Base/Types.h>

namespace Scripting
{
    struct Canvas
    {
    public:
        u32 nameHash;
        i32 sizeX;
        i32 sizeY;
    };

    struct Panel
    {
    public:
        i32 posX;
        i32 posY;
        i32 sizeX;
        i32 sizeY;
        u32 layer;

        u32 templateIndex;
    };

    struct Text
    {
    public:
        std::string text;
        i32 posX;
        i32 posY;
        u32 layer;

        u32 templateIndex;
    };


    class CanvasHandler : public LuaHandlerBase
    {
    private:
        void Register(lua_State* state);
        void Clear() { }

    public: 
        // Registered Functions
        // Register templates
        static i32 RegisterButtonTemplate(lua_State* state);
        static i32 RegisterPanelTemplate(lua_State* state);
        static i32 RegisterTextTemplate(lua_State* state);
        
        // UI functions
        static i32 GetCanvas(lua_State* state);

        // Canvas functions
        static i32 CreatePanel(lua_State* state);
        static i32 CreateText(lua_State* state);
    };

    static LuaMethod uiMethods[] =
    {
        { "RegisterButtonTemplate", CanvasHandler::RegisterButtonTemplate },
        { "RegisterPanelTemplate", CanvasHandler::RegisterPanelTemplate },
        { "RegisterTextTemplate", CanvasHandler::RegisterTextTemplate },
        
        { "GetCanvas", CanvasHandler::GetCanvas },

        { nullptr, nullptr }
    };

    static LuaMethod canvasMethods[] =
    {
        { "Panel", CanvasHandler::CreatePanel },
        { "Text", CanvasHandler::CreateText },

        { nullptr, nullptr }
    };
}