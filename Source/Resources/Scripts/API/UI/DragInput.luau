--[[
DragInput — numeric field for dev tools, the Lua stand-in for ImGui's DragFloat.
Two interactions on one widget:
  - drag left/right on the field to scrub the value (live),
  - click without dragging to focus and type a value, committed on Enter.

NewVec3Row lays out three of these as an x/y/z row and reports the combined vec.

Built on InputBox so typing/cursor/clipping come for free; drag-scrub is layered on
top via the panel mouse events and UI.GetMousePos (UI reference space, same units as
widget sizes).
]]

local InputBox = require("@src/API/UI/InputBox")
local RootedWidget = require("@src/API/UI/RootedWidget")

export type Options =
{
    value : number?,
    min : number?,
    max : number?,
    speed : number?,
    format : string?,
    panelTemplate : string?,
    textTemplate : string?,
    onValueChanged : ((number) -> ())?,
}

export type Vec3Options =
{
    gap : number?,
    value : vec?,
    min : number?,
    max : number?,
    speed : number?,
    format : string?,
    panelTemplate : string?,
    textTemplate : string?,
    onChanged : ((vec) -> ())?,
}
export type DragInputAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options?) -> any,
    NewVec3 : (parent : Widget, position : vec, size : vec, layer : integer, options : Vec3Options?) -> any,
}

local dragInputAPI : DragInputAPI = {}

local DRAG_THRESHOLD = 3        -- pixels of motion before a press counts as a drag, not a click
local DEFAULT_SPEED  = 0.1
local DEFAULT_FORMAT = "%.3f"
local DEFAULT_PANEL  = "DebugDarkGrey"
local DEFAULT_TEXT   = "DevToolsText"

function dragInputAPI.New(parent : Widget, position : vec, size : vec, layer : integer, opts : Options?)
    opts = opts or {}

    local self = {}
    self.value     = opts.value or 0.0
    self.min       = opts.min
    self.max       = opts.max
    self.speed     = opts.speed or DEFAULT_SPEED
    self.format    = opts.format or DEFAULT_FORMAT
    self.onChanged = opts.onValueChanged

    local function Format(v)
        return string.format(self.format, v)
    end

    local input = InputBox.New(parent, position, size, layer, {
        panelTemplate = opts.panelTemplate or DEFAULT_PANEL,
        textTemplate  = opts.textTemplate or DEFAULT_TEXT,
        defaultText   = Format(self.value),
    })
    self.input = input
    self.root = input.root
    RootedWidget.Attach(self)

    function self:GetValue()
        return self.value
    end

    -- fire=true notifies the owner; we suppress it for external SetValue so refreshing the
    -- panel from engine state doesn't loop back into a write.
    function self:SetValue(v, fire)
        if self.min ~= nil then v = math.max(self.min, v) end
        if self.max ~= nil then v = math.min(self.max, v) end
        self.value = v
        input:SetText(Format(v))
        if fire and self.onChanged then
            self.onChanged(v)
        end
    end

    input:SetOnSubmit(function(t)
        local parsed = tonumber(t.text)
        if parsed ~= nil then
            self:SetValue(parsed, true)
        else
            input:SetText(Format(self.value))
        end
        return true -- unfocus on commit
    end)

    local dragging = false
    local startX = 0
    local startValue = 0
    local moved = false

    input:SetOnMouseDown(function(_, _, button, mouseX)
        if button ~= 0i then return end
        dragging = true
        moved = false
        startX = mouseX
        startValue = self.value
    end)

    input:SetOnMouseHeld(function()
        if not dragging then return end
        local mousePosition = UI.GetMousePos()
        local dx = mousePosition.x - startX
        if math.abs(dx) > DRAG_THRESHOLD then
            moved = true
            self:SetValue(startValue + dx * self.speed, true)
        end
    end)

    input:SetOnMouseUp(function()
        -- A drag shouldn't leave the field in text-edit mode; a plain click should.
        if dragging and moved then
            input:Unfocus()
        end
        dragging = false
    end)

    function self:SetAnchor(anchor)      input:SetAnchor(anchor) end
    function self:SetRelativePoint(point) input:SetRelativePoint(point) end
    function self:SetPos(position)       input:SetPos(position) end
    function self:SetPosX(x)             input:SetPosX(x) end
    function self:SetPosY(y)             input:SetPosY(y) end

    return self
end

-- A horizontal x/y/z row of DragInputs. opts.onChanged receives a vec(x, y, z).
function dragInputAPI.NewVec3(parent : Widget, position : vec, size : vec, layer : integer, opts : Vec3Options?)
    opts = opts or {}
    local gap = opts.gap or 3
    local fieldW = (size.x - gap * 2) / 3

    local value = opts.value or vec.New(0, 0, 0)
    local comps = { value.x, value.y, value.z }

    local root = parent:NewWidget(position, layer)
    local self = { root = root, fields = {} }
    RootedWidget.Attach(self)

    local function Combined()
        return vec.New(self.fields[1]:GetValue(), self.fields[2]:GetValue(), self.fields[3]:GetValue())
    end

    for i = 1, 3 do
        local fieldX = (i - 1) * (fieldW + gap)
        local field = dragInputAPI.New(root, vec.New(fieldX, 0), vec.New(fieldW, size.y), 0i, {
            value         = comps[i],
            speed         = opts.speed,
            format        = opts.format,
            min           = opts.min,
            max           = opts.max,
            panelTemplate = opts.panelTemplate,
            textTemplate  = opts.textTemplate,
            onValueChanged = function()
                if opts.onChanged then opts.onChanged(Combined()) end
            end,
        })
        field:SetAnchor(vec.New(0.0, 1.0))
        field:SetRelativePoint(vec.New(0.0, 1.0))
        self.fields[i] = field
    end

    function self:GetValue()
        return Combined()
    end

    function self:SetValue(vec)
        self.fields[1]:SetValue(vec.x, false)
        self.fields[2]:SetValue(vec.y, false)
        self.fields[3]:SetValue(vec.z, false)
    end

    function self:SetPosY(y)
        self.root:SetPosY(y)
    end

    return self
end

return dragInputAPI
