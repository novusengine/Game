--[[
DragInput — numeric field for dev tools, the Lua stand-in for ImGui's DragFloat.
Two interactions on one widget:
  - drag left/right on the field to scrub the value (live),
  - click without dragging to focus and type a value, committed on Enter.

NewVec3Row lays out three of these as an x/y/z row and reports the combined vector.

Built on InputBox so typing/cursor/clipping come for free; drag-scrub is layered on
top via the panel mouse events and UI.GetMousePos (UI reference space, same units as
widget sizes).
]]

local InputBox = require("@src/API/UI/InputBox")

local dragInputAPI = {}

local DRAG_THRESHOLD = 3        -- pixels of motion before a press counts as a drag, not a click
local DEFAULT_SPEED  = 0.1
local DEFAULT_FORMAT = "%.3f"
local DEFAULT_PANEL  = "DebugDarkGrey"
local DEFAULT_TEXT   = "DevToolsText"

function dragInputAPI.NewDragInput(parent, posX, posY, sizeX, sizeY, layer, opts)
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

    local input = InputBox.NewInputBox(parent, posX, posY, sizeX, sizeY, layer, {
        panelTemplate = opts.panelTemplate or DEFAULT_PANEL,
        textTemplate  = opts.textTemplate or DEFAULT_TEXT,
        defaultText   = Format(self.value),
    })
    self.input = input

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
        if button ~= 0 then return end
        dragging = true
        moved = false
        startX = mouseX
        startValue = self.value
    end)

    input:SetOnMouseHeld(function()
        if not dragging then return end
        local mouseX = UI.GetMousePos()
        local dx = mouseX - startX
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

    function self:SetAnchor(x, y)        input:SetAnchor(x, y) end
    function self:SetRelativePoint(x, y) input:SetRelativePoint(x, y) end
    function self:SetPos(x, y)           input:SetPos(x, y) end
    function self:SetPosX(x)             input:SetPosX(x) end
    function self:SetPosY(y)             input:SetPosY(y) end

    return self
end

-- A horizontal x/y/z row of DragInputs. opts.onChanged receives a vector(x, y, z).
function dragInputAPI.NewVec3Row(parent, posX, posY, sizeX, sizeY, layer, opts)
    opts = opts or {}
    local gap = opts.gap or 3
    local fieldW = (sizeX - gap * 2) / 3

    local value = opts.value or vector.create(0, 0, 0)
    local comps = { value.x, value.y, value.z }

    local self = { fields = {} }

    local function Combined()
        return vector.create(self.fields[1]:GetValue(), self.fields[2]:GetValue(), self.fields[3]:GetValue())
    end

    for i = 1, 3 do
        local fieldX = posX + (i - 1) * (fieldW + gap)
        local field = dragInputAPI.NewDragInput(parent, fieldX, posY, fieldW, sizeY, layer, {
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
        field:SetAnchor(0.0, 1.0)
        field:SetRelativePoint(0.0, 1.0)
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
        for i = 1, 3 do
            self.fields[i]:SetPosY(y)
        end
    end

    return self
end

return dragInputAPI
