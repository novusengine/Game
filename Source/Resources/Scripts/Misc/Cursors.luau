local DragContext = require("@src/API/Game/UI/DragContext")

local Cursors =
{
    { "attack", "texture/cursor/32x32/attack.dds" },
    { "point", "texture/cursor/32x32/pointer.dds" }
}

local softwareCursor : Panel? = nil
local cursorRevision = 0i
local softwareCursorEnabled = false

local function UpdateSoftwareCursor()
    if softwareCursor == nil then
        return
    end

    local enabled = Input.IsSoftwareCursorEnabled()
    if enabled ~= softwareCursorEnabled then
        softwareCursorEnabled = enabled
        softwareCursor:SetEnabled(enabled)
    end

    if not enabled then
        return
    end

    local newRevision, texturePath = Input.GetCursorState(cursorRevision)
    if newRevision ~= nil and texturePath ~= nil then
        cursorRevision = newRevision
        softwareCursor:SetBackground(texturePath)
    end
end

local function RegisterCursors()
    for k, v in Cursors do
        local name = v[1]
        local path = v[2]
        
        -- Ignoring Return Value on purpose
        AddCursor(name, path)
    end
end

local function OnGameLoaded(eventID : integer, data : any)
    RegisterCursors()
    SetCursor("point")

    local cursorCanvas = UI.GetCanvas("CursorCanvas", vec.New(0, 0), vec.New(1920, 1080))
    softwareCursor = cursorCanvas:NewPanel(vec.New(0, 0), vec.New(48, 48), 0i)
    softwareCursor:SetEnabled(false)
    softwareCursor:SetAnchor(vec.New(0.0, 1.0))
    softwareCursor:SetRelativePoint(vec.New(0.0, 1.0))
    UpdateSoftwareCursor()
end

local currentWorldCursor = "point"
local function OnGameUpdated(eventID : integer, data : any)
    UpdateSoftwareCursor()

    if DragContext:HasOperation() then
        currentWorldCursor = ""
        return
    end

    local hoveredUnit = Unit.GetHovered()
    local canAttackUnit = false
    if hoveredUnit ~= INVALID_UNIT_ID then
        local health = Unit.GetHealth(hoveredUnit)
        canAttackUnit = health > 0 and Unit.CanAttack(hoveredUnit)
    end

    local desiredCursor = if canAttackUnit then "attack" else "point"
    if desiredCursor == currentWorldCursor then
        return
    end

    if SetCursor(desiredCursor) then
        currentWorldCursor = desiredCursor
    end
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded)
RegisterEvent(GameEvent.Updated, OnGameUpdated)
