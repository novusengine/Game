--[[
EditorButton — flat-panel button for dev tool UI. Default look is "invisible
until hovered" (base alpha 0, hover alpha 0.45 with a light-blue tint).
Returned table mirrors the slice of Button.luau used by MenuBar/Clock plus a
SetHighlighted(bool) for "menu open" states.
]]

local editorButtonAPI = {}

local DEFAULT_BASE_COLOR              = vector.create(0.30, 0.30, 0.35)
local DEFAULT_HOVER_COLOR             = vector.create(0.30, 0.55, 0.85)
local DEFAULT_BASE_ALPHA_INVISIBLE    = 0.0   -- "invisible until hovered" — see docblock
local DEFAULT_HOVER_ALPHA             = 0.45

function editorButtonAPI.NewEditorButton(parent, posX, posY, sizeX, sizeY, layer, opts)
    opts = opts or {}

    local label         = opts.text or ""
    local textTemplate  = opts.textTemplate or "DefaultDebugText"
    local textAlign     = opts.textAlign or "center"   -- "left", "center", "right"
    local textPadX      = opts.textPadX or 6           -- inset for left/right alignment
    local baseColor     = opts.color or DEFAULT_BASE_COLOR
    local hoverColor    = opts.hoverColor or DEFAULT_HOVER_COLOR
    local baseAlpha     = opts.alpha or DEFAULT_BASE_ALPHA_INVISIBLE
    local hoverAlpha    = opts.hoverAlpha or DEFAULT_HOVER_ALPHA

    local panel = parent:NewPanel(posX, posY, sizeX, sizeY, layer or 0, "DebugDarkGrey")
    panel:SetColor(baseColor)
    panel:SetAlpha(baseAlpha)

    if opts.borderColor ~= nil and opts.borderSize ~= nil then
        panel:SetBorderColor(opts.borderColor)
        panel:SetBorderSize(opts.borderSize)
    end

    local text = panel:NewText(label, 0, 0, 0, textTemplate)
    if textAlign == "left" then
        text:SetAnchor(0.0, 0.5)
        text:SetRelativePoint(0.0, 0.5)
        text:SetPosX(textPadX)
    elseif textAlign == "right" then
        text:SetAnchor(1.0, 0.5)
        text:SetRelativePoint(1.0, 0.5)
        text:SetPosX(-textPadX)
    else
        text:SetAnchor(0.5, 0.5)
        text:SetRelativePoint(0.5, 0.5)
    end

    local self = {
        panel = panel,
        text = text,
        _baseColor = baseColor,
        _hoverColor = hoverColor,
        _baseAlpha = baseAlpha,
        _hoverAlpha = hoverAlpha,
        _highlighted = false,
        _hovered = false,
    }

    local function ApplyVisualState()
        if self._highlighted or self._hovered then
            self.panel:SetColor(self._hoverColor)
            self.panel:SetAlpha(self._hoverAlpha)
        else
            self.panel:SetColor(self._baseColor)
            self.panel:SetAlpha(self._baseAlpha)
        end
    end

    panel:SetOnHoverBegin(function()
        self._hovered = true
        ApplyVisualState()
    end)
    panel:SetOnHoverEnd(function()
        self._hovered = false
        ApplyVisualState()
    end)

    -- Lock the hover-style appearance on regardless of cursor state.
    function self:SetHighlighted(on)
        self._highlighted = on and true or false
        ApplyVisualState()
    end

    function self:SetText(t)             self.text:SetText(t) end
    function self:SetOnMouseUp(fn)       self.panel:SetOnMouseUp(fn) end
    function self:SetOnMouseDown(fn)     self.panel:SetOnMouseDown(fn) end
    function self:SetOnHoverBegin(fn)
        -- Wrap so the visual state still updates alongside the user's fn.
        self.panel:SetOnHoverBegin(function()
            self._hovered = true
            ApplyVisualState()
            if fn then fn() end
        end)
    end
    function self:SetOnHoverEnd(fn)
        self.panel:SetOnHoverEnd(function()
            self._hovered = false
            ApplyVisualState()
            if fn then fn() end
        end)
    end
    function self:SetAnchor(x, y)        self.panel:SetAnchor(x, y) end
    function self:SetRelativePoint(x, y) self.panel:SetRelativePoint(x, y) end
    function self:SetPos(x, y)           self.panel:SetPos(x, y) end
    function self:SetPosX(x)             self.panel:SetPosX(x) end
    function self:SetPosY(y)             self.panel:SetPosY(y) end
    function self:GetPosX()              return self.panel:GetPosX() end
    function self:GetPosY()              return self.panel:GetPosY() end
    function self:SetVisible(v)          self.panel:SetVisible(v) end
    function self:SetInteractable(v)     self.panel:SetInteractable(v) end
    function self:SetSize(w, h)          self.panel:SetSize(w, h) end

    return self
end

return editorButtonAPI
