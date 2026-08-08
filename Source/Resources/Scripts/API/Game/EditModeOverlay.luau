local Draggable = require("@src/API/UI/Draggable")
local Button = require("@src/API/UI/Button")
local EditModeDefaults = require("@src/API/Game/EditModeDefaults")

export type Overlay =
{
    elementID   : EditModeDefaults.ElementID,
    category    : EditModeDefaults.CategoryID,
    root        : Panel,
    header      : Panel,
    body        : Panel,
    resetButton : Button.Button,
    nameLabel   : TextWidget,
    dragTarget  : any,
}

export type EditModeOverlayAPI =
{
    Create      : (parent : Panel, elementID : EditModeDefaults.ElementID, displayName : string, category : EditModeDefaults.CategoryID, position : vec, color : vec, elementSize : vec?, onDragged : ((EditModeDefaults.ElementID, vec) -> ())?) -> Overlay,
    Destroy     : (overlay : Overlay) -> (),
    SetPosition : (overlay : Overlay, position : vec) -> (),
}

local overlayAPI : EditModeOverlayAPI = { }

local HEADER_HEIGHT = 20
local BODY_ALPHA = 0.15
local BORDER_ALPHA = 0.80
local HEADER_ALPHA = 0.85
local RESET_BUTTON_WIDTH = 50
local LABEL_INSET = 6

local COLOR_HEADER_BG = vec.New(0.08, 0.08, 0.10)
local COLOR_RESET_BG = vec.New(0.50, 0.20, 0.20)
local COLOR_RESET_HOVER = vec.New(0.70, 0.30, 0.30)
local COLOR_TEXT = vec.New(0.92, 0.93, 0.95)

function overlayAPI.Create(parent : Panel, elementID : EditModeDefaults.ElementID, displayName : string, category : EditModeDefaults.CategoryID, position : vec, color : vec, elementSize : vec?, onDragged : ((EditModeDefaults.ElementID, vec) -> ())?) : Overlay
    local size = elementSize or vec.New(170, 50)
    local categoryLabel = category

    local catDef = EditModeDefaults.GetCategory(category)
    if catDef ~= nil then
        categoryLabel = catDef.displayName
    end

    -- Root panel: same size as element + header, positioned at element position
    local rootHeight = size.y + HEADER_HEIGHT
    local root = parent:NewPanel(vec.New(position.x, position.y), vec.New(size.x, rootHeight), 1i, "DebugDarkGrey")
    root:SetAlpha(0.0)

    -- Header: drag handle at the top of the root
    local header = root:NewPanel(vec.New(0, 0), vec.New(size.x, HEADER_HEIGHT), 2i, "DebugDarkGrey")
    header:SetAnchor(vec.New(0.0, 1.0))
    header:SetRelativePoint(vec.New(0.0, 1.0))
    header:SetColor(color)
    header:SetAlpha(HEADER_ALPHA)
    header:SetBorderSize(1)
    header:SetBorderColor(color)

    -- Category + element name label
    local labelText = `[ {categoryLabel} ] {displayName}`
    local nameLabel = header:NewText(labelText, vec.New(LABEL_INSET, 0), 1i, "DefaultButtonText")
    nameLabel:SetAnchor(vec.New(0.0, 0.5))
    nameLabel:SetRelativePoint(vec.New(0.0, 0.5))
    nameLabel:SetFontSize(8)
    nameLabel:SetColor(COLOR_TEXT)

    -- Reset button on the right side of the header
    local resetButton = Button.New(header, vec.New(-LABEL_INSET, 0), vec.New(RESET_BUTTON_WIDTH, HEADER_HEIGHT - 4), 1i, {
        text = "Reset",
        panelTemplate = "DebugDarkGrey",
        textTemplate = "DefaultButtonText",
        color = COLOR_RESET_BG,
        hoverColor = COLOR_RESET_HOVER,
        alpha = 0.90,
        hoverAlpha = 1.0,
        borderColor = color,
        borderSize = 1,
    })
    resetButton:SetAnchor(vec.New(1.0, 0.5))
    resetButton:SetRelativePoint(vec.New(1.0, 0.5))
    resetButton:SetFontSize(7)

    -- Body: semi-transparent highlight area covering the element
    local body = root:NewPanel(vec.New(0, 0), vec.New(size.x, size.y), 1i, "DebugDarkGrey")
    body:SetColor(color)
    body:SetAlpha(BODY_ALPHA)
    body:SetBorderSize(1)
    body:SetBorderColor(color)

    -- Attach dragging: dragging anywhere on the overlay moves the element root.
    -- Since the element root is on a different canvas, we use a custom approach:
    -- Draggable moves a proxy position, and we call the onDragged callback.
    local dragTarget = {
        _pos = vec.New(position.x, position.y),
        GetPosX = function(self) return self._pos.x end,
        GetPosY = function(self) return self._pos.y end,
        SetPos = function(self, newPos)
            self._pos = newPos
            -- Move the overlay root to follow
            root:SetPos(newPos)
            -- Notify caller to move the actual element
            if onDragged ~= nil then
                onDragged(elementID, newPos)
            end
        end,
    } :: any

    -- Make both header and body draggable (entire overlay is the drag handle)
    local overlay : Overlay = {
        elementID = elementID,
        category = category,
        root = root,
        header = header,
        body = body,
        resetButton = resetButton,
        nameLabel = nameLabel,
        dragTarget = dragTarget,
    }

    -- Attach drag to the body (the main visual area)
    Draggable.Attach(dragTarget, body)

    -- Also attach drag to the header
    Draggable.Attach(dragTarget, header)

    -- Reset button: snap element back to default position
    resetButton:SetOnMouseUp(function()
        local defaults = EditModeDefaults.GetElement(elementID)
        if defaults ~= nil then
            local defaultPos = vec.New(defaults.defaultPosX, defaults.defaultPosY)
            dragTarget._pos = defaultPos
            root:SetPos(defaultPos)
            if onDragged ~= nil then
                onDragged(elementID, defaultPos)
            end
        end
    end)

    return overlay
end

function overlayAPI.Destroy(overlay : Overlay)
    UI.DestroyWidget(overlay.root)
end

function overlayAPI.SetPosition(overlay : Overlay, position : vec)
    overlay.dragTarget._pos = vec.New(position.x, position.y)
    overlay.root:SetPos(position)
end

return overlayAPI
