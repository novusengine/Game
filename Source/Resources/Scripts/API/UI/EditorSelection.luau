--[[
EditorSelection — shared selection state for the Lua editor tools.

The C++ "Editor" handler holds a single selection-changed callback, but both the
Hierarchy and the Inspector need to react. This module owns that one callback and
fans it out to any number of Lua listeners, so the tools stay decoupled.

    local Selection = require("@src/API/UI/EditorSelection")
    Selection.Subscribe(function() ... end)
    Selection.Set(entityID)   -- entityID or nil
    local id = Selection.Get()
]]

local M = {}

local listeners = {}

local function Dispatch()
    for _, fn in listeners do
        fn()
    end
end

function M.Subscribe(fn)
    table.insert(listeners, fn)
end

function M.Get()
    return Editor.GetSelected()
end

function M.Set(entityID)
    -- Routes through C++, which fires the selection-changed callback (Dispatch) only
    -- when the selection actually changes.
    Editor.SetSelected(entityID)
end

-- Editor is a dev-only table; this module is only required by Editor/-folder tools, which
-- only load in developer mode. Guard anyway so a stray require can't hard-error.
if Editor then
    Editor.SetOnSelectionChanged(Dispatch)
end

return M
