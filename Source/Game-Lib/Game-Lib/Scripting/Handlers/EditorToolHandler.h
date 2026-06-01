#pragma once
#include <Base/Types.h>

#include <Scripting/Defines.h>
#include <Scripting/LuaMethodTable.h>

namespace Scripting::Editor
{
    // Backs the dev-only "Editor" Lua table: shared selection plus the gizmo / debug-draw
    // toggles consumed by the C++ editor systems (picking, gizmo, debug draw). State lives
    // in the EditorSelection singleton; this handler only owns the selection-changed callback.
    class EditorToolHandler : public LuaHandlerBase
    {
    public:
        void Register(Zenith* zenith);
        void Clear(Zenith* zenith);

        void PostLoad(Zenith* zenith) {}
        void Update(Zenith* zenith, f32 deltaTime) {}

        static i32 GetSelected(Zenith* zenith);
        static i32 SetSelected(Zenith* zenith);
        static i32 SetOnSelectionChanged(Zenith* zenith);
        static i32 SetOnGizmoChanged(Zenith* zenith);

        static i32 SetPickingEnabled(Zenith* zenith);
        static i32 SetGizmoEnabled(Zenith* zenith);
        static i32 GetGizmoOperation(Zenith* zenith);
        static i32 SetGizmoOperation(Zenith* zenith);
        static i32 GetGizmoMode(Zenith* zenith);
        static i32 SetGizmoMode(Zenith* zenith);

        static i32 SetDrawOBB(Zenith* zenith);
        static i32 SetDrawWorldAABB(Zenith* zenith);

        // Fires the registered selection-changed callback. Called from Lua (after SetSelected)
        // and from C++ (after picking updates the selection).
        void OnSelectionChanged(Zenith* zenith);

        // Fires the registered gizmo-changed callback. Called from C++ after a gizmo drag mutates
        // the selected entity's transform, so the Inspector can refresh its fields.
        void OnGizmoChanged(Zenith* zenith);

    private:
        i32 _onSelectionChangedRef = LUA_NOREF;
        i32 _onGizmoChangedRef = LUA_NOREF;
    };

    static LuaRegister<> editorGlobalMethods[] =
    {
        { "GetSelected",            EditorToolHandler::GetSelected,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetSelected",            EditorToolHandler::SetSelected,           Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetOnSelectionChanged",  EditorToolHandler::SetOnSelectionChanged, Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetOnGizmoChanged",      EditorToolHandler::SetOnGizmoChanged,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetPickingEnabled",      EditorToolHandler::SetPickingEnabled,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetGizmoEnabled",        EditorToolHandler::SetGizmoEnabled,       Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetGizmoOperation",      EditorToolHandler::GetGizmoOperation,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetGizmoOperation",      EditorToolHandler::SetGizmoOperation,     Scripting::LuaMethodFlags::DeveloperOnly },
        { "GetGizmoMode",           EditorToolHandler::GetGizmoMode,          Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetGizmoMode",           EditorToolHandler::SetGizmoMode,          Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetDrawOBB",             EditorToolHandler::SetDrawOBB,            Scripting::LuaMethodFlags::DeveloperOnly },
        { "SetDrawWorldAABB",       EditorToolHandler::SetDrawWorldAABB,      Scripting::LuaMethodFlags::DeveloperOnly },
    };
}
