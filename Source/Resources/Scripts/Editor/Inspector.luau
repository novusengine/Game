local EditorWindow = require("@src/API/Editor/UI/Window")
--[[
Inspector — Lua port of the C++ ImGui Inspector. Shows the selected entity's
components (Name, Transform, Scene Node, AABB, Model, Decal, Unit) and lets you
edit them. Selection is shared with the Hierarchy and viewport picking through
EditorSelection.

Panels are rebuilt from scratch on each selection change: the whole content
subtree is destroyed and recreated, which keeps the per-component layout simple
(no incremental diffing). Numeric fields use DragInput (drag to scrub, click to
type); booleans use toggle buttons.

Layer 1100 sits above the dev top bar (1000).
]]

local DragInput = require("@src/API/UI/DragInput")
local Button = require("@src/API/UI/Button")
local EditorRegistry = require("@src/API/Editor/Registry")
local InputBox = require("@src/API/UI/InputBox")
local ScrollBox = require("@src/API/UI/ScrollBox")
local Selection = require("@src/API/Editor/Selection")


local PANEL_W      = 320
local TITLE_H      = 16
local ROW_H        = 18
local HEADER_H     = 18
local FIELD_H      = 16
local SIDE_PAD     = 6
local TOP_PAD      = 4
local BOTTOM_PAD   = 6
local LABEL_W      = 70
local LAYER        = 1100i
local BTN_TEXT_TPL = "DevToolsText"

local WINDOW_BG_COLOR = vec.New(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA = 0.96
local BORDER_COLOR    = vec.New(0.30, 0.30, 0.35)
local BORDER_SIZE     = 1
local HEADER_TINT     = vec.New(0.55, 0.75, 1.00)
local LABEL_TINT      = vec.New(0.82, 0.82, 0.88)

local INNER_W = PANEL_W - SIDE_PAD * 2

local function MakeBtnOpts(text)
    return {
        text = text,
        textTemplate = BTN_TEXT_TPL,
        borderColor = BORDER_COLOR,
        borderSize  = BORDER_SIZE,
    }
end

local canvas = UI.GetCanvas("InspectorEditor", vec.New(0, 0), vec.New(1920, 1080), false)

local editorWindow = EditorWindow.New(canvas, vec.New(950, -80), vec.New(PANEL_W, TITLE_H), LAYER, { title = "Inspector" })
local windowPanel = editorWindow.root
local titleBar = editorWindow.titleBar
local titleText = editorWindow.title
local closeBtn = editorWindow.closeButton

-- ---- gizmo control bar (persistent, below the title bar) ----
local GIZMO_BAR_H = 18

local gizmoBar = windowPanel:NewPanel(vec.New(0, -TITLE_H), vec.New(PANEL_W, GIZMO_BAR_H), 1i, "DebugDarkGrey")
gizmoBar:SetAnchor(vec.New(0.0, 1.0))
gizmoBar:SetRelativePoint(vec.New(0.0, 1.0))
gizmoBar:SetColor(vec.New(0.08, 0.08, 0.10))
gizmoBar:SetAlpha(WINDOW_BG_ALPHA)

local gizmoOp = 0i   -- 0 = translate, 1 = rotate, 2 = scale (matches GizmoOperation in C++)
local gizmoMode = 1i -- 0 = local, 1 = world (matches GizmoMode in C++)
local opButtons = {}
local modeButtons = {}

local function RefreshGizmoButtons()
    for i, b in opButtons do b:SetHighlighted(integer.create(i - 1) == gizmoOp) end
    for i, b in modeButtons do b:SetHighlighted(integer.create(i - 1) == gizmoMode) end
end

local function MakeBarButton(x, w, text)
    local b = Button.New(gizmoBar, vec.New(x, 0), vec.New(w, GIZMO_BAR_H - 2), 1i, MakeBtnOpts(text))
    b:SetAnchor(vec.New(0.0, 0.5))
    b:SetRelativePoint(vec.New(0.0, 0.5))
    return b
end

local OP_W = 46
local opLabels = { "Move", "Rotate", "Scale" }
local barX = SIDE_PAD
for i = 1, 3 do
    local index = integer.create(i - 1)
    local b = MakeBarButton(barX, OP_W, opLabels[i])
    b:SetOnMouseUp(function()
        gizmoOp = index
        Editor.SetGizmoOperation(index)
        RefreshGizmoButtons()
    end)
    opButtons[i] = b
    barX += OP_W + 2
end

barX += 8
local MODE_W = 42
local modeLabels = { "Local", "World" }
for i = 1, 2 do
    local index = integer.create(i - 1)
    local b = MakeBarButton(barX, MODE_W, modeLabels[i])
    b:SetOnMouseUp(function()
        gizmoMode = index
        Editor.SetGizmoMode(index)
        RefreshGizmoButtons()
    end)
    modeButtons[i] = b
    barX += MODE_W + 2
end

RefreshGizmoButtons()

-- contentRoot holds everything below the gizmo bar; Rebuild destroys and recreates it.
local CONTENT_TOP = TITLE_H + GIZMO_BAR_H
local contentRoot = nil

-- The current Transform panel's x/y/z rows, so a gizmo drag can refresh them without a full rebuild.
local transformRows = nil

-- ---- small layout helpers (operate on a cursor { y } and a parent) ----

local function AddText(parent, x, y, text, tint)
    local t = parent:NewText(text, vec.New(x, y), 1i, BTN_TEXT_TPL)
    t:SetAnchor(vec.New(0.0, 1.0))
    t:SetRelativePoint(vec.New(0.0, 1.0))
    t:SetInteractable(false)
    if tint then t:SetColor(tint) end
    return t
end

local function AddHeader(parent, cur, text)
    cur.y -= 2
    AddText(parent, SIDE_PAD, cur.y, text, HEADER_TINT)
    cur.y -= HEADER_H
end

-- a label on the left and a single DragInput on the right
local function AddNumberRow(parent, cur, label, value, opts, onChanged)
    AddText(parent, SIDE_PAD, cur.y, label, LABEL_TINT)
    local fieldX = SIDE_PAD + LABEL_W
    local fieldW = INNER_W - LABEL_W
    local field = DragInput.New(parent, vec.New(fieldX, cur.y), vec.New(fieldW, FIELD_H), 1i, {
        value = value,
        speed = opts.speed,
        format = opts.format,
        min = opts.min,
        max = opts.max,
        onValueChanged = onChanged,
    })
    field:SetAnchor(vec.New(0.0, 1.0))
    field:SetRelativePoint(vec.New(0.0, 1.0))
    cur.y -= ROW_H
    return field
end

-- a label line followed by an x/y/z row on the next line
local function AddVec3Row(parent, cur, label, value, opts, onChanged)
    AddText(parent, SIDE_PAD, cur.y, label, LABEL_TINT)
    cur.y -= FIELD_H
    local row = DragInput.NewVec3(parent, vec.New(SIDE_PAD, cur.y), vec.New(INNER_W, FIELD_H), 1i, {
        value = value,
        speed = opts.speed,
        format = opts.format,
        min = opts.min,
        max = opts.max,
        onChanged = onChanged,
    })
    cur.y -= ROW_H
    return row
end

local function AddToggle(parent, cur, label, state, onToggle)
    local btn = Button.New(parent, vec.New(SIDE_PAD, cur.y), vec.New(INNER_W, FIELD_H), 1i, {
        text = label .. ": " .. (state and "ON" or "OFF"),
        textTemplate = BTN_TEXT_TPL,
        textAlign = "left",
        textPadX = 4,
    })
    btn:SetAnchor(vec.New(0.0, 1.0))
    btn:SetRelativePoint(vec.New(0.0, 1.0))
    local current = state
    btn:SetOnMouseUp(function()
        current = not current
        btn:SetText(label .. ": " .. (current and "ON" or "OFF"))
        onToggle(current)
    end)
    cur.y -= ROW_H
    return btn
end

-- ---- per-component section builders ----

local function BuildName(parent, cur, entityID, name)
    AddHeader(parent, cur, "Name")
    local input = InputBox.New(parent, vec.New(SIDE_PAD, cur.y), vec.New(INNER_W, FIELD_H), 1i, {
        panelTemplate = "DebugDarkGrey",
        textTemplate = BTN_TEXT_TPL,
        defaultText = name,
    })
    input:SetAnchor(vec.New(0.0, 1.0))
    input:SetRelativePoint(vec.New(0.0, 1.0))
    input:SetOnSubmit(function(t)
        Scene.SetName(entityID, t.text)
        return true
    end)
    cur.y -= ROW_H
end

local function BuildTransform(parent, cur, entityID, transform)
    AddHeader(parent, cur, "Transform")

    local posRow, rotRow, scaleRow
    local function Apply()
        Scene.SetTransform(entityID, posRow:GetValue(), rotRow:GetValue(), scaleRow:GetValue())
    end

    posRow = AddVec3Row(parent, cur, "Position", transform.position, { speed = 0.05, format = "%.2f" }, Apply)
    rotRow = AddVec3Row(parent, cur, "Rotation", transform.rotation, { speed = 0.5, format = "%.1f" }, Apply)
    scaleRow = AddVec3Row(parent, cur, "Scale", transform.scale, { speed = 0.01, format = "%.2f" }, Apply)

    transformRows = { pos = posRow, rot = rotRow, scale = scaleRow }
end

local function BuildSceneNode(parent, cur, sceneNode)
    AddHeader(parent, cur, "Scene Node")

    if sceneNode.parent ~= nil then
        local parentBtn = Button.New(parent, vec.New(SIDE_PAD, cur.y), vec.New(INNER_W, FIELD_H), 1i, {
            text = "Parent: " .. sceneNode.parent.name,
            textTemplate = BTN_TEXT_TPL,
            textAlign = "left",
            textPadX = 4,
            borderColor = BORDER_COLOR,
            borderSize = BORDER_SIZE,
        })
        parentBtn:SetAnchor(vec.New(0.0, 1.0))
        parentBtn:SetRelativePoint(vec.New(0.0, 1.0))
        local parentID = sceneNode.parent.id
        parentBtn:SetOnMouseUp(function() Selection.Set(parentID) end)
        cur.y -= ROW_H
    else
        AddText(parent, SIDE_PAD, cur.y, "Parent: none", LABEL_TINT)
        cur.y -= ROW_H
    end

    local children = sceneNode.children
    local childCount = #children
    AddText(parent, SIDE_PAD, cur.y, childCount .. " children", LABEL_TINT)
    cur.y -= FIELD_H

    if childCount > 0 then
        local listH = math.min(120, childCount * FIELD_H + 2)
        local list = ScrollBox.New(parent, vec.New(SIDE_PAD, cur.y), vec.New(INNER_W, listH), 1i, {
            panelTemplate = "DebugDarkGrey",
            contentSizeX = INNER_W,
            contentSizeY = listH,
            verticalScrollBar = true,
            scrollBarWidth = 10,
            alpha = 0.0,
        })
        list:SetAnchor(vec.New(0.0, 1.0))
        list:SetRelativePoint(vec.New(0.0, 1.0))

        for i, child in children do
            local childBtn = Button.New(list.content, vec.New(2, -((i - 1) * FIELD_H) - 1), vec.New(INNER_W - 14, FIELD_H), 1i, {
                text = child.name,
                textTemplate = BTN_TEXT_TPL,
                textAlign = "left",
                textPadX = 4,
            })
            childBtn:SetAnchor(vec.New(0.0, 1.0))
            childBtn:SetRelativePoint(vec.New(0.0, 1.0))
            local childID = child.id
            childBtn:SetOnMouseUp(function() Selection.Set(childID) end)
        end
        list:SetContentHeight(math.max(listH, childCount * FIELD_H + 2))
        cur.y -= listH + 4
    end
end

local function BuildAABB(parent, cur, entityID, aabb)
    AddHeader(parent, cur, "AABB")

    local centerRow, extentsRow
    local function Apply()
        Scene.SetAABB(entityID, centerRow:GetValue(), extentsRow:GetValue())
    end

    centerRow = AddVec3Row(parent, cur, "Center", aabb.center, { speed = 0.05, format = "%.2f" }, Apply)
    extentsRow = AddVec3Row(parent, cur, "Extents", aabb.extents, { speed = 0.05, format = "%.2f" }, Apply)
end

local function BuildModel(parent, cur, entityID, model)
    AddHeader(parent, cur, "Model")

    AddText(parent, SIDE_PAD, cur.y, string.format("ModelID: %d  Instance: %d", model.modelID, model.instanceID), LABEL_TINT)
    cur.y -= FIELD_H
    AddText(parent, SIDE_PAD, cur.y, string.format("ModelHash: %d", model.modelHash), LABEL_TINT)
    cur.y -= FIELD_H + 2

    AddToggle(parent, cur, "Visible", model.visible, function(state)
        Scene.SetModelVisible(entityID, state)
    end)

    local opacityField
    local forcedTransparency = model.forcedTransparency
    AddToggle(parent, cur, "Forced Transparency", forcedTransparency, function(state)
        forcedTransparency = state
        Scene.SetModelTransparent(entityID, state, opacityField:GetValue())
    end)

    opacityField = AddNumberRow(parent, cur, "Opacity", model.opacity, { speed = 0.01, format = "%.2f", min = 0.0, max = 1.0 }, function(v)
        Scene.SetModelTransparent(entityID, forcedTransparency, v)
    end)
end

local function BuildDecal(parent, cur, entityID, decal)
    AddHeader(parent, cur, "Decal")

    -- Gathers the current field values into the table SetDecal expects.
    local pathInput, colorRow, thrMin, thrMax, minU, minV, maxU, maxV, flagsInput
    local function Apply()
        Scene.SetDecal(entityID, {
            texturePath = pathInput:GetText(),
            color = colorRow:GetValue(),
            thresholdMin = thrMin:GetValue(),
            thresholdMax = thrMax:GetValue(),
            minU = minU:GetValue(),
            minV = minV:GetValue(),
            maxU = maxU:GetValue(),
            maxV = maxV:GetValue(),
            flags = integer.create(math.floor(
                tonumber(flagsInput:GetText()) or integer.tonumber(decal.flags)
            )),
        })
    end

    AddText(parent, SIDE_PAD, cur.y, "Texture Path", LABEL_TINT)
    cur.y -= FIELD_H
    pathInput = InputBox.New(parent, vec.New(SIDE_PAD, cur.y), vec.New(INNER_W, FIELD_H), 1i, {
        panelTemplate = "DebugDarkGrey",
        textTemplate = BTN_TEXT_TPL,
        defaultText = decal.texturePath,
    })
    pathInput:SetAnchor(vec.New(0.0, 1.0))
    pathInput:SetRelativePoint(vec.New(0.0, 1.0))
    pathInput:SetOnSubmit(function() Apply() return true end)
    cur.y -= ROW_H

    colorRow = AddVec3Row(parent, cur, "Color (RGB)", decal.color, { speed = 0.01, format = "%.2f", min = 0.0, max = 1.0 }, Apply)

    thrMin = AddNumberRow(parent, cur, "Threshold-", decal.thresholdMin, { speed = 0.01, format = "%.2f", min = -1.0, max = 1.0 }, Apply)
    thrMax = AddNumberRow(parent, cur, "Threshold+", decal.thresholdMax, { speed = 0.01, format = "%.2f", min = -1.0, max = 1.0 }, Apply)
    minU = AddNumberRow(parent, cur, "Min U", decal.minU, { speed = 0.01, format = "%.2f", min = 0.0, max = 1.0 }, Apply)
    minV = AddNumberRow(parent, cur, "Min V", decal.minV, { speed = 0.01, format = "%.2f", min = 0.0, max = 1.0 }, Apply)
    maxU = AddNumberRow(parent, cur, "Max U", decal.maxU, { speed = 0.01, format = "%.2f", min = 0.0, max = 1.0 }, Apply)
    maxV = AddNumberRow(parent, cur, "Max V", decal.maxV, { speed = 0.01, format = "%.2f", min = 0.0, max = 1.0 }, Apply)

    AddText(parent, SIDE_PAD, cur.y, "Flags", LABEL_TINT)
    local flagsX = SIDE_PAD + LABEL_W
    flagsInput = InputBox.New(parent, vec.New(flagsX, cur.y), vec.New(INNER_W - LABEL_W, FIELD_H), 1i, {
        panelTemplate = "DebugDarkGrey",
        textTemplate = BTN_TEXT_TPL,
        defaultText = tostring(decal.flags),
    })
    flagsInput:SetAnchor(vec.New(0.0, 1.0))
    flagsInput:SetRelativePoint(vec.New(0.0, 1.0))
    flagsInput:SetOnSubmit(function() Apply() return true end)
    cur.y -= ROW_H
end

local function BuildUnit(parent, cur, unit)
    AddHeader(parent, cur, "Unit")
    AddText(parent, SIDE_PAD, cur.y, "Body ID: " .. unit.bodyID, LABEL_TINT)
    cur.y -= ROW_H
end

local function BuildDebug(parent, cur, entityID)
    AddHeader(parent, cur, "Debug Draw")
    AddToggle(parent, cur, "Draw OBB", false, function(state) Editor.SetDrawOBB(state) end)
    AddToggle(parent, cur, "Draw World AABB", false, function(state) Editor.SetDrawWorldAABB(state) end)
end

local function Rebuild()
    if contentRoot ~= nil then
        UI.DestroyWidget(contentRoot)
        contentRoot = nil
    end
    transformRows = nil

    contentRoot = windowPanel:NewPanel(vec.New(0, -CONTENT_TOP), vec.New(PANEL_W, 2000), 1i, "DebugDarkGrey")
    contentRoot:SetAnchor(vec.New(0.0, 1.0))
    contentRoot:SetRelativePoint(vec.New(0.0, 1.0))
    contentRoot:SetAlpha(0.0)

    local cur = { y = -TOP_PAD }

    local entityID = Selection.Get()
    if entityID == nil then
        AddText(contentRoot, SIDE_PAD, cur.y, "No entity selected.", LABEL_TINT)
        cur.y -= ROW_H
        AddText(contentRoot, SIDE_PAD, cur.y, "Pick one in the world or", LABEL_TINT)
        cur.y -= FIELD_H
        AddText(contentRoot, SIDE_PAD, cur.y, "the Hierarchy.", LABEL_TINT)
        cur.y -= ROW_H
    else
        AddText(contentRoot, SIDE_PAD, cur.y, "Entity: " .. entityID, LABEL_TINT)
        cur.y -= ROW_H

        local name = Scene.GetName(entityID)
        if name ~= nil then BuildName(contentRoot, cur, entityID, name) end

        local transform = Scene.GetTransform(entityID)
        if transform ~= nil then BuildTransform(contentRoot, cur, entityID, transform) end

        local sceneNode = Scene.GetSceneNode(entityID)
        if sceneNode ~= nil then BuildSceneNode(contentRoot, cur, sceneNode) end

        local aabb = Scene.GetAABB(entityID)
        if aabb ~= nil then BuildAABB(contentRoot, cur, entityID, aabb) end

        local model = Scene.GetModel(entityID)
        if model ~= nil then BuildModel(contentRoot, cur, entityID, model) end

        local decal = Scene.GetDecal(entityID)
        if decal ~= nil then BuildDecal(contentRoot, cur, entityID, decal) end

        local unit = Scene.GetUnit(entityID)
        if unit ~= nil then BuildUnit(contentRoot, cur, unit) end

        BuildDebug(contentRoot, cur, entityID)
    end

    local consumed = -cur.y
    windowPanel:SetSize(vec.New(PANEL_W, CONTENT_TOP + consumed + BOTTOM_PAD))
end

Selection.Subscribe(function()
    if windowPanel:IsVisible() then
        Rebuild()
    end
end)

-- Live-update the Transform fields while the gizmo drags the object (no full rebuild, so focus and
-- the other panels are untouched).
local function RefreshTransformFields()
    if transformRows == nil then return end
    local entityID = Selection.Get()
    if entityID == nil then return end
    local t = Scene.GetTransform(entityID)
    if t == nil then return end
    transformRows.pos:SetValue(t.position)
    transformRows.rot:SetValue(t.rotation)
    transformRows.scale:SetValue(t.scale)
end

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    Editor.SetPickingEnabled(true)
    Editor.SetGizmoEnabled(true)
    Editor.SetGizmoOperation(gizmoOp)
    Editor.SetGizmoMode(gizmoMode)
    Editor.SetOnGizmoChanged(RefreshTransformFields)
    Rebuild()
end

function methods:Hide()
    windowPanel:SetVisible(false)
    Editor.SetPickingEnabled(false)
    Editor.SetGizmoEnabled(false)
    Editor.SetOnGizmoChanged(nil)
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("Inspector", methods)
