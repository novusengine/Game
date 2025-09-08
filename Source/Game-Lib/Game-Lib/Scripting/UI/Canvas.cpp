#include "Canvas.h"

#include "Game-Lib/Scripting/UI/Panel.h"
#include "Game-Lib/Scripting/UI/Text.h"

#include <Scripting/Zenith.h>

namespace Scripting::UI
{
    void Canvas::Register(Zenith* zenith)
    {
        LuaMetaTable<Canvas>::Register(zenith, "CanvasMetaTable");

        LuaMetaTable<Canvas>::Set(zenith, widgetCreationMethods);
    }

    namespace CanvasMethods
    {
        
    }
}