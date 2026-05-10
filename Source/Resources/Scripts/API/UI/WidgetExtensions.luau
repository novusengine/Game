--[[
WidgetExtensions — adds Lua-defined methods to widget metatables in place.

luaL_sandbox seals globals and the string metatable but does NOT iterate
LUA_REGISTRYINDEX, so the per-type *MetaTable entries that LuaMetaTable<T>::
Register stores in the registry stay writable post-sandbox. We grab them via
getmetatable() on a probe instance and write the methods directly. After
Install(), every Panel — including ones created later — has the methods.

Currently Panel only; Text/Widget have separate metatables.

Methods on Panel:
    panel:MakeDraggable(handle?)
        Wire up drag behavior. The handle drives the drag; the panel updates
        its own SetPos every frame while held. Omit `handle` to use the panel
        itself. Engine implicitly captures the mouse — Held keeps firing past
        the handle's edges until MouseUp.
]]

local M = {}

local function MakeDraggable(self, handle)
    handle = handle or self
    local mx0, my0 = 0, 0
    local wx0, wy0 = 0, 0

    handle:SetOnMouseDown(function()
        mx0, my0 = UI.GetMousePos()
        wx0, wy0 = self:GetPosX(), self:GetPosY()
    end)
    handle:SetOnMouseHeld(function()
        local mx, my = UI.GetMousePos()
        self:SetPos(wx0 + (mx - mx0), wy0 + (my - my0))
    end)
end

M.MakeDraggable = MakeDraggable

local function ExtendMetatable(probe, methods)
    local mt = getmetatable(probe)
    if mt == nil then
        error("WidgetExtensions: probe widget has no metatable; sandbox or registration changed?", 2)
    end
    for name, fn in methods do
        mt[name] = fn
    end
end

local installed = false
function M.Install()
    if installed then return end
    installed = true

    -- Probe to grab the Panel metatable. Canvas leaks (no Lua API to destroy
    -- a canvas) but is 1x1 and off-screen; the probe panel is destroyed below.
    local probeCanvas = UI.GetCanvas("__widget_ext_probe", 0, 0, 1, 1, false)
    local probePanel = probeCanvas:NewPanel(0, 0, 1, 1, 0, "DebugDarkGrey")
    ExtendMetatable(probePanel, { MakeDraggable = MakeDraggable })
    UI.DestroyWidget(probePanel)
end

M.Install()

return M
