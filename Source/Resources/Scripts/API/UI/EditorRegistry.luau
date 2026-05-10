--[[
EditorRegistry — registry of Lua editor windows.

    local Editor = require("@src/API/UI/EditorRegistry")
    Editor.Register("Clock", { Show = ..., Hide = ..., IsVisible = ... })
    Editor.Toggle("Clock")

In user mode, Editor/-folder scripts don't load so nothing registers, and
Toggle/IsVisible naturally no-op on the empty registry.
]]

local M = {}

local registered = {}   -- name -> { Show = fn, Hide = fn, IsVisible = fn }

function M.Register(name, methods)
    registered[name] = methods
end

function M.Unregister(name)
    registered[name] = nil
end

function M.IsVisible(name)
    local m = registered[name]
    return m ~= nil and m:IsVisible()
end

function M.Toggle(name)
    local m = registered[name]
    if not m then return false end
    if m:IsVisible() then
        m:Hide()
    else
        m:Show()
    end
    return m:IsVisible()
end

function M.GetEditors()
    local out = {}
    for k in registered do
        table.insert(out, k)
    end
    return out
end

return M
