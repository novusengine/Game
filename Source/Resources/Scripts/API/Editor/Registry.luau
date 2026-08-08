--[[
EditorRegistry — registry of Lua editor windows.

    local Editor = require("@src/API/Editor/Registry")
    Editor.Register("Clock", { Show = ..., Hide = ..., IsVisible = ... })
    Editor.Toggle("Clock")

In user mode, Editor/-folder scripts don't load so nothing registers, and
Toggle/IsVisible naturally no-op on the empty registry.
]]

export type Editor =
{
    Show : (self : Editor) -> (),
    Hide : (self : Editor) -> (),
    IsVisible : (self : Editor) -> boolean,
}
export type RegistryAPI =
{
    Register : (name : string, editor : Editor) -> (),
    Unregister : (name : string) -> (),
    IsVisible : (name : string) -> boolean,
    Toggle : (name : string) -> boolean,
    Get : (name : string) -> Editor?,
    GetEditors : () -> { string },
}
local registryAPI : RegistryAPI = {}

local registered = {}   -- name -> { Show = fn, Hide = fn, IsVisible = fn }

function registryAPI.Register(name, methods)
    registered[name] = methods
end

function registryAPI.Unregister(name)
    registered[name] = nil
end

function registryAPI.IsVisible(name)
    local m = registered[name]
    return m ~= nil and m:IsVisible()
end

function registryAPI.Toggle(name)
    local m = registered[name]
    if not m then return false end
    if m:IsVisible() then
        m:Hide()
    else
        m:Show()
    end
    return m:IsVisible()
end

function registryAPI.Get(name)
    return registered[name]
end

function registryAPI.GetEditors()
    local out = {}
    for k in registered do
        table.insert(out, k)
    end
    return out
end

return registryAPI
