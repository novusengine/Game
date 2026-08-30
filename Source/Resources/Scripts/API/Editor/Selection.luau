--[[
EditorSelection - shared selection state for the Lua editor tools.

The C++ "Editor" handler holds a single selection-changed callback, but both the
Hierarchy and the Inspector need to react. This module owns that one callback and
fans it out to any number of Lua listeners, so the tools stay decoupled.

    local Selection = require("@src/API/Editor/Selection")
    Selection.Subscribe(function() ... end)
    Selection.Set(entityID)   -- entityID or nil
    local id = Selection.Get()
]]

export type SelectionAPI =
{
    Subscribe : (callback : () -> ()) -> (),
    Get : () -> integer?,
    Set : (entityID : integer?) -> (),
}
local selectionAPI : SelectionAPI = { }
local listeners = {}
local initialized = false

local function Dispatch()
    for _, fn in listeners do
        fn()
    end
end

function selectionAPI.Subscribe(fn)
    if not initialized then
        initialized = true
        Editor.SetOnSelectionChanged(Dispatch)
    end
    table.insert(listeners, fn)
end

function selectionAPI.Get()
    return Editor.GetSelected()
end

function selectionAPI.Set(entityID)
    -- Routes through C++, which fires the selection-changed callback (Dispatch) only
    -- when the selection actually changes.
    Editor.SetSelected(entityID)
end

return selectionAPI
