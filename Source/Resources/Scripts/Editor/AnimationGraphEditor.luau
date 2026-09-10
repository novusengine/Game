--[[
Animation Graph - live playback and an independent editable graph canvas.

Topology is fetched once for each preview binding. Runtime state is sampled while the
window is visible and only affects highlights, transition progress, and blend-space data.
]]

local Button = require("@src/API/UI/Button")
local InputBox = require("@src/API/UI/InputBox")
local EditorRegistry = require("@src/API/Editor/Registry")
local PreviewSession = require("@src/API/Editor/AnimationPreviewSession")
local EditorWindow = require("@src/API/Editor/UI/Window")
local ShapeSurfaceUtil = require("@src/API/UI/ShapeSurfaceUtil")

local WINDOW_SIZE = vec.New(820, 678)
local VIEWPORT_SIZE = vec.New(568, 518)
local INSPECTOR_WIDTH = 224
local NODE_SIZE = vec.New(184, 96)
local NODE_HEADER_HEIGHT = 32
local LAYER = 1090i
local TEXT_TEMPLATE = "DevToolsText"

local colors = {
    window = vec.New(0.055, 0.063, 0.078),
    viewport = vec.New(0.035, 0.043, 0.055),
    panel = vec.New(0.075, 0.086, 0.105),
    node = vec.New(0.075, 0.09, 0.115),
    nodeHeader = vec.New(0.075, 0.09, 0.115),
    border = vec.New(0.22, 0.27, 0.34),
    edge = vec.New(0.31, 0.40, 0.51),
    current = vec.New(0.20, 0.88, 0.48),
    target = vec.New(1.00, 0.66, 0.20),
    selected = vec.New(0.30, 0.70, 1.00),
    muted = vec.New(0.58, 0.64, 0.72),
    text = vec.New(0.90, 0.93, 0.98),
    grid = vec.New(0.11, 0.14, 0.18),
}

local comparisonNames = { "==", "!=", "<", "<=", ">", ">=" }
local kindNames = { "Clip", "Blend Space 1D", "Blend Space 2D" }
local curveNames = { "Linear", "Smooth Step" }

local canvas = UI.GetCanvas("AnimationGraphEditor", vec.New(0, 0), vec.New(1920, 1080), false)
local window = EditorWindow.New(canvas, vec.New(1080, -72), WINDOW_SIZE, LAYER, {
    title = "Animation State Graph",
    titleHeight = 20,
    padding = 8,
    windowColor = colors.window,
    titleColor = vec.New(0.035, 0.04, 0.055),
    alpha = 0.98,
})
local root = window.root

local function ConfigureTopLeft(widget)
    widget:SetAnchor(vec.New(0.0, 1.0))
    widget:SetRelativePoint(vec.New(0.0, 1.0))
end

local function NewText(parent, text, position, size, color, fontSize, layer)
    local widget = parent:NewText(text, position, layer or 2i, TEXT_TEMPLATE)
    ConfigureTopLeft(widget)
    widget:SetColor(color or colors.text)
    widget:SetFontSize(fontSize or 10)
    widget:SetInteractable(false)
    return widget
end

local toolbar = root:NewPanel(vec.New(8, -26), vec.New(WINDOW_SIZE.x - 16, 44), 2i, "DebugDarkGrey")
ConfigureTopLeft(toolbar)
toolbar:SetColor(colors.panel)
toolbar:SetAlpha(0.9)
toolbar:SetBorderSize(0)

local graphNameText = NewText(toolbar, "No controller attached", vec.New(8, -5), vec.New(360, 20), colors.text, 11)
local graphStatusText = NewText(toolbar, "Open Animation Debug and spawn a controller", vec.New(8, -20), vec.New(420, 16), colors.muted, 8)
local liveParameterText = NewText(toolbar, "", vec.New(8, -34), vec.New(500, 16), colors.selected, 9)

local fitButton = Button.New(toolbar, vec.New(518, -3), vec.New(70, 24), 2i, { text = "Fit", textTemplate = TEXT_TEMPLATE, borderColor = colors.border, borderSize = 1 })
local followButton = Button.New(toolbar, vec.New(592, -3), vec.New(96, 24), 2i, { text = "Follow Active", textTemplate = TEXT_TEMPLATE, borderColor = colors.border, borderSize = 1 })
local pauseButton = Button.New(toolbar, vec.New(692, -3), vec.New(90, 24), 2i, { text = "Pause", textTemplate = TEXT_TEMPLATE, borderColor = colors.border, borderSize = 1 })
for _, button in { fitButton, followButton, pauseButton } do ConfigureTopLeft(button) end

local viewport = root:NewPanel(vec.New(8, -74), VIEWPORT_SIZE, 2i, "DebugDarkGrey")
ConfigureTopLeft(viewport)
viewport:SetColor(colors.viewport)
viewport:SetAlpha(0.98)
viewport:SetBorderColor(colors.border)
viewport:SetBorderSize(1)
viewport:SetClipChildren(true)

local edgePanel = viewport:NewPanel(vec.New(0, 0), VIEWPORT_SIZE, 1i, "TexturePanel")
edgePanel:SetInteractable(false)
local edgeSurface = UI.GetShapeSurface("AnimationGraphEdges", VIEWPORT_SIZE)
edgeSurface:Clear()
edgePanel:SetBackground(edgeSurface)

local overlayPanel = viewport:NewPanel(vec.New(0, 0), VIEWPORT_SIZE, 9i, "TexturePanel")
overlayPanel:SetInteractable(false)
local overlaySurface = UI.GetShapeSurface("AnimationGraphOverlay", VIEWPORT_SIZE)
overlaySurface:Clear()
overlayPanel:SetBackground(overlaySurface)

local inspector = root:NewPanel(vec.New(584, -74), vec.New(228, VIEWPORT_SIZE.y), 2i, "DebugDarkGrey")
ConfigureTopLeft(inspector)
inspector:SetColor(colors.panel)
inspector:SetAlpha(0.96)
inspector:SetBorderColor(colors.border)
inspector:SetBorderSize(1)

local inspectorTitle = NewText(inspector, "NO SELECTION", vec.New(10, -12), vec.New(204, 22), colors.selected, 12)
local inspectorKind = NewText(inspector, "", vec.New(10, -38), vec.New(204, 18), colors.muted, 9)
local inspectorLine1 = NewText(inspector, "", vec.New(10, -68), vec.New(204, 18), colors.text, 9)
local inspectorLine2 = NewText(inspector, "", vec.New(10, -90), vec.New(204, 18), colors.text, 9)
local inspectorLine3 = NewText(inspector, "", vec.New(10, -112), vec.New(204, 18), colors.text, 9)
local inspectorLine4 = NewText(inspector, "", vec.New(10, -134), vec.New(204, 18), colors.text, 9)
local inspectorLine5 = NewText(inspector, "", vec.New(10, -156), vec.New(204, 18), colors.text, 9)
local blendTitle = NewText(inspector, "LIVE BLEND SPACE", vec.New(10, -198), vec.New(204, 18), colors.muted, 9)
local blendPanel = inspector:NewPanel(vec.New(9, -224), vec.New(208, 208), 2i, "TexturePanel")
ConfigureTopLeft(blendPanel)
blendPanel:SetBorderColor(colors.border)
blendPanel:SetBorderSize(1)
local blendSurface = UI.GetShapeSurface("AnimationGraphBlendSpace", vec.New(208, 208))
blendSurface:Clear()
blendPanel:SetBackground(blendSurface)
local blendAxisX = NewText(blendPanel, "", vec.New(72, -190), vec.New(128, 14), colors.muted, 7, 4i)
local blendAxisY = NewText(blendPanel, "", vec.New(7, -7), vec.New(128, 14), colors.muted, 7, 4i)
local blendSampleLabels = {}
for index = 1, 6 do
    blendSampleLabels[index] = NewText(blendPanel, "", vec.New(0, 0), vec.New(140, 14), colors.text, 7, 4i)
    blendSampleLabels[index]:SetVisible(false)
end
local blendSampleLegend = NewText(inspector, "SAMPLE", vec.New(10, -442), vec.New(64, 14), colors.selected, 7)
local blendInputLegend = NewText(inspector, "LIVE INPUT", vec.New(76, -442), vec.New(86, 14), colors.current, 7)
local blendInfo = NewText(inspector, "Select a blend-space state", vec.New(10, -462), vec.New(204, 50), colors.muted, 8)

local graph = nil
local editingDraft = false
local connectSource = nil
local draggedNode = nil
local blendMapping = nil
local draggedSample = nil
local runtime = nil
local attachedEntity = nil
local sessionRevision = -1i
local nodeWidgets = {}
local transitionWidgets = {}
local selectedState = nil
local selectedTransition = nil
local followActive = true
local paused = false
local zoom = 1.0
local pan = vec.New(0, 0)
local panActive = false
local panMouseStart = vec.New(0, 0)
local panOffsetStart = vec.New(0, 0)

local editButton = Button.New(toolbar, vec.New(438,-3), vec.New(70,24), 2i, {text="Edit",textTemplate=TEXT_TEMPLATE,borderColor=colors.border,borderSize=1})
ConfigureTopLeft(editButton)
editButton:SetOnMouseUp(function()
    local author=EditorRegistry.Get("AnimationAuthoring")
    if not author then return end
    author:Show()
    if selectedTransition ~= nil then author:Select("transitions",integer.tonumber(selectedTransition))
    elseif selectedState ~= nil then author:Select("states",integer.tonumber(selectedState)) end
end)

local function StateByIndex(index)
    if graph == nil then return nil end
    return graph.states[integer.tonumber(index) + 1]
end

local function TransitionLabel(transition)
    local comparison = comparisonNames[integer.tonumber(transition.comparison) + 1] or "?"
    return string.format("%s  %s  %.2f", transition.parameter, comparison, transition.threshold)
end

local function TransitionExpression(transition)
    local comparison = comparisonNames[integer.tonumber(transition.comparison) + 1] or "?"
    local value = runtime ~= nil and runtime.parameters ~= nil and runtime.parameters[transition.parameter] or nil
    return value ~= nil and string.format("%s  %.2f  %s  %.2f", transition.parameter, value, comparison, transition.threshold) or TransitionLabel(transition)
end

local function TransitionConditionPasses(transition)
    local value = runtime ~= nil and runtime.parameters ~= nil and runtime.parameters[transition.parameter] or nil
    if value == nil then return false end
    local comparison = integer.tonumber(transition.comparison)
    if comparison == 0 then return value == transition.threshold end
    if comparison == 1 then return value ~= transition.threshold end
    if comparison == 2 then return value < transition.threshold end
    if comparison == 3 then return value <= transition.threshold end
    if comparison == 4 then return value > transition.threshold end
    return value >= transition.threshold
end

local function GraphToLocal(position)
    return VIEWPORT_SIZE / 2 + pan + vec.New(position.x, position.y) * zoom
end

local function NodeCenter(state)
    return GraphToLocal(vec.New(state.positionX, state.positionY))
end

local function NodeSocketPoints(transition)
    local source = StateByIndex(transition.sourceState)
    local target = StateByIndex(transition.targetState)
    if source == nil or target == nil then return nil, nil end
    local sourceCenter = NodeCenter(source)
    local targetCenter = NodeCenter(target)
    local forward = targetCenter.x >= sourceCenter.x
    local verticalOffset = if forward then 13 else -13
    local sourcePoint = sourceCenter + vec.New(if forward then NODE_SIZE.x * zoom / 2 else -NODE_SIZE.x * zoom / 2, verticalOffset)
    local targetPoint = targetCenter + vec.New(if forward then -NODE_SIZE.x * zoom / 2 else NODE_SIZE.x * zoom / 2, verticalOffset)
    return sourcePoint, targetPoint
end

local function DestroyGraphWidgets()
    for _, widget in nodeWidgets do UI.DestroyWidget(widget.panel) end
    for _, widget in transitionWidgets do
        UI.DestroyWidget(widget.root)
    end
    nodeWidgets = {}
    transitionWidgets = {}
end

local function DrawGrid()
    edgeSurface:Clear()
    local spacing = 32 * zoom
    if spacing < 12 then return end
    local offsetX = pan.x % spacing
    local offsetY = pan.y % spacing
    local x = offsetX
    while x < VIEWPORT_SIZE.x do
        ShapeSurfaceUtil.DrawLine(edgeSurface, vec.New(x, 0), vec.New(x, VIEWPORT_SIZE.y), { width = 1.0, color = colors.grid, alpha = 0.65 })
        x += spacing
    end
    local y = offsetY
    while y < VIEWPORT_SIZE.y do
        ShapeSurfaceUtil.DrawLine(edgeSurface, vec.New(0, y), vec.New(VIEWPORT_SIZE.x, y), { width = 1.0, color = colors.grid, alpha = 0.65 })
        y += spacing
    end
end

local function DrawEdges()
    DrawGrid()
    overlaySurface:Clear()
    if graph == nil then return end
    for _, transition in graph.transitions do
        local fromPoint, toPoint = NodeSocketPoints(transition)
        if fromPoint == nil or toPoint == nil then continue end
        ShapeSurfaceUtil.DrawWire(edgeSurface, fromPoint, toPoint, { width = 2.0, color = colors.edge, alpha = 0.9 })
        local direction = if toPoint.x >= fromPoint.x then 1 else -1
        ShapeSurfaceUtil.DrawArrow(edgeSurface, toPoint - vec.New(direction * 15, 0), toPoint, { width = 2.0, color = colors.edge, alpha = 0.9 }, 6.0)
        edgeSurface:DrawCircle(fromPoint, 4.0, colors.viewport, 1.0)
        edgeSurface:DrawCircleOutline(fromPoint, 4.0, 1.5, colors.edge, 1.0)
        edgeSurface:DrawCircle(toPoint, 4.0, colors.viewport, 1.0)
        edgeSurface:DrawCircleOutline(toPoint, 4.0, 1.5, colors.edge, 1.0)
        local active = runtime ~= nil and runtime.transitioning and runtime.currentState == transition.sourceState and runtime.transitionTarget == transition.targetState
        if active then
            local duration = integer.tonumber(runtime.transitionDurationUS)
            local progress = if duration > 0 then math.clamp(integer.tonumber(runtime.transitionTimeUS) / duration, 0.0, 1.0) else 1.0
            ShapeSurfaceUtil.DrawWire(overlaySurface, fromPoint, toPoint, { width = 5.0, color = colors.target, alpha = 0.95, dashLength = 16.0, gapLength = 7.0, dashOffset = -progress * 90.0 })
            overlaySurface:DrawCircle(vec.lerp(fromPoint, toPoint, progress), 5.5, colors.target, 1.0)
        elseif selectedTransition == transition.index then
            ShapeSurfaceUtil.DrawWire(overlaySurface, fromPoint, toPoint, { width = 4.0, color = colors.selected, alpha = 0.75 })
        end
    end
end

local function PositionTransitionWidget(transition, widget)
    local fromPoint, toPoint = NodeSocketPoints(transition)
    if widget == nil or fromPoint == nil or toPoint == nil then return end
    local midpoint = (fromPoint + toPoint) / 2
    local offset = if integer.tonumber(transition.targetState) > integer.tonumber(transition.sourceState) then 24 else -24
    local labelWidth = widget.text:GetWidth()
    widget:SetPos(midpoint + vec.New(-math.max(54, labelWidth / 2 + 8), offset - 6))
    widget:SetSize(vec.New(math.max(108, labelWidth + 16), 22))
end

local function ApplyWidgetPositions()
    if graph == nil then return end
    for _, state in graph.states do
        local widget = nodeWidgets[state.index]
        if widget ~= nil then
            local size = NODE_SIZE * zoom
            widget.panel:SetSize(size)
            widget.panel:SetPos(NodeCenter(state) - size / 2)
            widget.header:SetPos(vec.New(1, -1) * zoom)
            widget.header:SetSize(vec.New(NODE_SIZE.x - 2, NODE_HEADER_HEIGHT) * zoom)
            widget.accent:SetPos(vec.New(1, -1) * zoom)
            widget.accent:SetSize(vec.New(4, NODE_SIZE.y - 2) * zoom)
            widget.divider:SetPos(vec.New(5, -(NODE_HEADER_HEIGHT + 1)) * zoom)
            widget.divider:SetSize(vec.New(NODE_SIZE.x - 6, 1) * zoom)
            widget.title:SetPos(vec.New(13, -6) * zoom)
            widget.subtitle:SetPos(vec.New(13, -43) * zoom)
            widget.badge:SetPos(vec.New(13, -69) * zoom)
            widget.title:SetFontSize(math.max(8, 12 * zoom))
            widget.subtitle:SetFontSize(math.max(7, 9 * zoom))
            widget.badge:SetFontSize(math.max(7, 8 * zoom))
        end
    end
    for _, transition in graph.transitions do
        PositionTransitionWidget(transition, transitionWidgets[transition.index])
    end
    DrawEdges()
end

local function RefreshInspector()
    blendSurface:Clear()
    blendPanel:SetVisible(false)
    blendTitle:SetVisible(false)
    blendInfo:SetVisible(false)
    blendAxisX:SetVisible(false)
    blendAxisY:SetVisible(false)
    blendSampleLegend:SetVisible(false)
    blendInputLegend:SetVisible(false)
    for _, label in blendSampleLabels do label:SetVisible(false) end
    if graph == nil then
        inspectorTitle:SetText("NO CONTROLLER")
        inspectorKind:SetText("Spawn one from Animation Debug")
        for _, line in { inspectorLine1, inspectorLine2, inspectorLine3, inspectorLine4, inspectorLine5 } do line:SetText("") end
        return
    end
    if selectedTransition ~= nil then
        local transition = graph.transitions[integer.tonumber(selectedTransition) + 1]
        if transition ~= nil then
            inspectorTitle:SetText("TRANSITION")
            local from,to=StateByIndex(transition.sourceState),StateByIndex(transition.targetState)
            inspectorKind:SetText((from and from.name or "Missing state") .. "  ->  " .. (to and to.name or "Missing state"))
            inspectorLine1:SetText("Condition  " .. TransitionExpression(transition))
            inspectorLine2:SetText(string.format("Duration   %.0f ms", integer.tonumber(transition.durationUS) / 1000))
            inspectorLine3:SetText("Curve      " .. (curveNames[integer.tonumber(transition.curve) + 1] or "Unknown"))
            local active = runtime ~= nil and runtime.transitioning and runtime.currentState == transition.sourceState and runtime.transitionTarget == transition.targetState
            inspectorLine4:SetText(active and string.format("Progress   %.1f%%", 100 * integer.tonumber(runtime.transitionTimeUS) / math.max(1, integer.tonumber(runtime.transitionDurationUS))) or "Inactive")
            inspectorLine5:SetText(string.format("GUIDs      %s -> %s", tostring(transition.sourceGUID), tostring(transition.targetGUID)))
            return
        end
    end
    local state = if selectedState ~= nil then StateByIndex(selectedState) else nil
    if state == nil then
        inspectorTitle:SetText("GRAPH")
        inspectorKind:SetText(graph.name)
        inspectorLine1:SetText(string.format("States       %d", #graph.states))
        inspectorLine2:SetText(string.format("Transitions  %d", #graph.transitions))
        inspectorLine3:SetText(string.format("Parameters   %d", #graph.parameters))
        inspectorLine4:SetText(string.format("IK operations %d", graph.ikOperations ~= nil and #graph.ikOperations or 0))
        inspectorLine5:SetText("Select a state or condition")
        return
    end
    inspectorTitle:SetText(string.upper(state.name))
    inspectorKind:SetText(kindNames[integer.tonumber(state.kind) + 1] or "Unknown State")
    inspectorLine1:SetText(string.format("State index  %d", state.index))
    inspectorLine2:SetText(string.format("Source GUID  %s", tostring(state.sourceGUID)))
    inspectorLine3:SetText(string.format("Duration     %.2f s", integer.tonumber(state.durationUS) / 1000000))
    inspectorLine4:SetText(state.inputX ~= "" and "Input X      " .. state.inputX or string.format("Slot         %s", tostring(state.clipSlotSemanticID)))
    inspectorLine5:SetText(state.inputY ~= "" and "Input Y      " .. state.inputY or (runtime ~= nil and runtime.currentState == state.index and string.format("State time   %.2f s", integer.tonumber(runtime.stateTimeUS) / 1000000) or "Inactive"))
    if state.kind == 0i then return end

    blendPanel:SetVisible(true)
    blendTitle:SetVisible(true)
    blendInfo:SetVisible(true)
    blendAxisX:SetVisible(true)
    blendAxisY:SetVisible(state.kind == 2i)
    blendSampleLegend:SetVisible(true)
    blendInputLegend:SetVisible(true)
    blendAxisX:SetText("X  " .. string.upper(state.inputX) .. "  (+)")
    blendAxisY:SetText("Y  " .. string.upper(state.inputY) .. "  (+)")
    for grid = 0, 4 do
        local coordinate = 18 + grid * 43
        ShapeSurfaceUtil.DrawLine(blendSurface, vec.New(coordinate, 18), vec.New(coordinate, 190), { width = 1.0, color = colors.grid })
        ShapeSurfaceUtil.DrawLine(blendSurface, vec.New(18, coordinate), vec.New(190, coordinate), { width = 1.0, color = colors.grid })
    end
    local minimumX, maximumX, minimumY, maximumY = math.huge, -math.huge, math.huge, -math.huge
    for _, sample in state.samples do
        minimumX = math.min(minimumX, sample.x); maximumX = math.max(maximumX, sample.x)
        minimumY = math.min(minimumY, sample.y); maximumY = math.max(maximumY, sample.y)
    end
    if #state.samples==0 then minimumX=-1; maximumX=1; minimumY=-1; maximumY=1 end
    if minimumX == maximumX then minimumX -= 1; maximumX += 1 end
    if minimumY == maximumY then minimumY -= 1; maximumY += 1 end
    blendMapping={state=state,minX=minimumX,maxX=maximumX,minY=minimumY,maxY=maximumY}
    local function SamplePoint(sample)
        return vec.New(18 + (sample.x - minimumX) / (maximumX - minimumX) * 172, 18 + (sample.y - minimumY) / (maximumY - minimumY) * 172)
    end
    for _, triangle in state.triangles do
        local a, b, c = state.samples[integer.tonumber(triangle.a) + 1], state.samples[integer.tonumber(triangle.b) + 1], state.samples[integer.tonumber(triangle.c) + 1]
        if a ~= nil and b ~= nil and c ~= nil then
            ShapeSurfaceUtil.DrawPolyline(blendSurface, { SamplePoint(a), SamplePoint(b), SamplePoint(c), SamplePoint(a) }, { width = 1.5, color = colors.edge, alpha = 0.8 })
        end
    end
    for _, sample in state.samples do
        blendSurface:DrawCircle(SamplePoint(sample), 5.0, colors.selected, 1.0)
    end
    for index, sample in state.samples do
        local label = blendSampleLabels[index]
        if label == nil then break end
        local point = SamplePoint(sample)
        local labelText = if state.kind == 1i then string.format("%s  %.1f", sample.name, sample.x) else sample.name
        label:SetText(labelText)
        label:SetVisible(true)
        local labelX = if point.x > 104 then point.x - label:GetWidth() - 8 else point.x + 8
        local screenY = 208 - point.y
        local labelY = if screenY > 104 then screenY - 14 else screenY + 5
        label:SetPos(vec.New(math.clamp(labelX, 5, 203 - label:GetWidth()), -math.clamp(labelY, 5, 195)))
    end
    local blendPoint = nil
    if runtime ~= nil and runtime.currentState == state.index and runtime.parameters ~= nil then
        local inputX = runtime.parameters[state.inputX]
        local inputY = if state.kind == 2i then runtime.parameters[state.inputY] else 0.0
        if inputX ~= nil and inputY ~= nil then
            blendPoint = SamplePoint({ x = math.clamp(inputX, minimumX, maximumX), y = math.clamp(inputY, minimumY, maximumY) })
        end
    end
    if blendPoint ~= nil then
        blendSurface:DrawCircle(blendPoint, 11.0, colors.current, 0.22)
        blendSurface:DrawCircleOutline(blendPoint, 7.0, 3.0, colors.current, 1.0)
    end
    if runtime == nil then
        blendInfo:SetText(editingDraft and "Drag a sample to edit its input position.\nApply validates the blend topology." or "Waiting for runtime state")
    elseif state.kind == 1i then
        blendInfo:SetText(string.format("Blend weights  %.2f  /  %.2f\n%s", runtime.blendWeight0, runtime.blendWeight1, runtime.synchronizationFallback and "Synchronization fallback" or "Marker synchronized"))
    else
        blendInfo:SetText(string.format("Blend weights  %.2f  /  %.2f  /  %.2f\n%s", runtime.blendWeight0, runtime.blendWeight1, runtime.blendWeight2, runtime.synchronizationFallback and "Synchronization fallback" or "Marker synchronized"))
    end
end

local function RefreshNodeStyles()
    if graph == nil then return end
    for _, state in graph.states do
        local widget = nodeWidgets[state.index]
        if widget == nil then continue end
        local isCurrent = runtime ~= nil and runtime.currentState == state.index
        local isTarget = runtime ~= nil and runtime.transitioning and runtime.transitionTarget == state.index
        local isSelected = selectedState == state.index
        widget.panel:SetColor(colors.node)
        widget.panel:SetBorderColor(isCurrent and colors.current or (isTarget and colors.target or (isSelected and colors.selected or colors.border)))
        widget.panel:SetBorderSize((isCurrent or isTarget or isSelected) and 2 or 1)
        widget.header:SetColor(colors.node)
        widget.accent:SetColor(isCurrent and colors.current or (isTarget and colors.target or (isSelected and colors.selected or colors.border)))
        widget.badge:SetText(isCurrent and (isTarget and "ACTIVE  /  TARGET" or "ACTIVE") or (isTarget and "TARGET" or (graph.entryState == state.index and "ENTRY STATE" or "")))
        widget.badge:SetColor(isCurrent and colors.current or (isTarget and colors.target or colors.muted))
    end
end

local function RefreshTransitionStyles()
    if graph == nil then return end
    for _, transition in graph.transitions do
        local widget = transitionWidgets[transition.index]
        if widget == nil then continue end
        local active = runtime ~= nil and runtime.transitioning and runtime.currentState == transition.sourceState and runtime.transitionTarget == transition.targetState
        local eligible = runtime ~= nil and runtime.currentState == transition.sourceState and TransitionConditionPasses(transition)
        widget:SetText(TransitionLabel(transition))
        widget.text:SetColor(active and colors.target or (eligible and colors.current or (selectedTransition == transition.index and colors.selected or colors.muted)))
        PositionTransitionWidget(transition, widget)
    end
end

local function BuildGraphWidgets()
    DestroyGraphWidgets()
    if graph == nil then DrawEdges(); RefreshInspector(); return end
    for _, state in graph.states do
        local panel = viewport:NewPanel(vec.New(0, 0), NODE_SIZE, 5i, "DebugDarkGrey")
        panel:SetColor(colors.node)
        panel:SetAlpha(0.98)
        panel:SetBorderColor(colors.border)
        panel:SetBorderSize(1)
        local header = panel:NewPanel(vec.New(1, -1), vec.New(NODE_SIZE.x - 2, NODE_HEADER_HEIGHT), 1i, "DebugDarkGrey")
        ConfigureTopLeft(header)
        header:SetColor(colors.nodeHeader)
        header:SetBorderSize(0)
        local accent = panel:NewPanel(vec.New(1, -1), vec.New(4, NODE_SIZE.y - 2), 3i, "DebugDarkGrey")
        ConfigureTopLeft(accent)
        accent:SetColor(colors.border)
        accent:SetBorderSize(0)
        accent:SetInteractable(false)
        local divider = panel:NewPanel(vec.New(5, -(NODE_HEADER_HEIGHT + 1)), vec.New(NODE_SIZE.x - 6, 1), 2i, "DebugDarkGrey")
        ConfigureTopLeft(divider)
        divider:SetColor(colors.border)
        divider:SetAlpha(0.65)
        divider:SetBorderSize(0)
        divider:SetInteractable(false)
        local title = NewText(panel, state.name, vec.New(13, -6), vec.New(NODE_SIZE.x - 24, 20), colors.text, 12)
        local subtitle = NewText(panel, kindNames[integer.tonumber(state.kind) + 1] or "Unknown", vec.New(13, -43), vec.New(NODE_SIZE.x - 24, 18), colors.muted, 9)
        local badge = NewText(panel, "", vec.New(13, -69), vec.New(NODE_SIZE.x - 24, 16), colors.muted, 8)
        panel:SetOnMouseUp(function(_, _, button)
            if button ~= 0i then return end
            if editingDraft and connectSource ~= nil then
                local ok,error=Asset.AnimationGraphCommand(attachedEntity,"connect",connectSource,state.index)
                connectSource=nil
                graphStatusText:SetText(ok and "Transition added to draft; Apply to publish" or error)
                if ok then graph=Asset.GetAnimationControllerGraph(attachedEntity,true); BuildGraphWidgets() end
                return
            end
            selectedState = state.index
            selectedTransition = nil
            followActive = false
            followButton:SetHighlighted(false)
            RefreshNodeStyles()
            RefreshTransitionStyles()
            RefreshInspector()
        end)
        panel:SetOnMouseDown(function(_,_,button,x,y)
            if editingDraft and button==0i then draggedNode={state=state,mouse=UI.GetMousePos(),position=vec.New(state.positionX,state.positionY)} end
        end)
        panel:SetOnMouseHeld(function(_,_,x,y)
            if not draggedNode or draggedNode.state~=state then return end
            local delta=UI.GetMousePos()-draggedNode.mouse
            state.positionX=draggedNode.position.x+delta.x/zoom
            state.positionY=draggedNode.position.y-delta.y/zoom
            ApplyWidgetPositions(); DrawEdges()
        end)
        panel:SetOnMouseRelease(function(_,_,button)
            if button~=0i or not draggedNode or draggedNode.state~=state then return end
            Asset.AnimationGraphEditField(attachedEntity,"states",state.index,"editorPosition",string.format("[%f,%f]",state.positionX,state.positionY))
            draggedNode=nil
        end)
        nodeWidgets[state.index] = { panel = panel, header = header, accent = accent, divider = divider, title = title, subtitle = subtitle, badge = badge }
    end
    for _, transition in graph.transitions do
        local label = Button.New(viewport, vec.New(0, 0), vec.New(118, 22), 7i, { text = TransitionLabel(transition), panelTemplate = "DebugDarkGrey", textTemplate = TEXT_TEMPLATE, color = colors.viewport, hoverColor = colors.viewport, highlightColor = colors.viewport, alpha = 0.0, hoverAlpha = 0.0, highlightAlpha = 0.0, borderSize = 0 })
        label:SetFontSize(8)
        label.text:SetColor(colors.muted)
        label:SetOnHoverBegin(function() label.text:SetColor(colors.text) end)
        label:SetOnHoverEnd(function()
            local active = runtime ~= nil and runtime.transitioning and runtime.currentState == transition.sourceState and runtime.transitionTarget == transition.targetState
            local eligible = runtime ~= nil and runtime.currentState == transition.sourceState and TransitionConditionPasses(transition)
            label.text:SetColor(active and colors.target or (eligible and colors.current or (selectedTransition == transition.index and colors.selected or colors.muted)))
        end)
        label:SetOnMouseUp(function()
            selectedState = nil
            selectedTransition = transition.index
            followActive = false
            followButton:SetHighlighted(false)
            RefreshNodeStyles()
            RefreshTransitionStyles()
            RefreshInspector()
            DrawEdges()
        end)
        transitionWidgets[transition.index] = label
    end
    ApplyWidgetPositions()
    RefreshNodeStyles()
    RefreshTransitionStyles()
    RefreshInspector()
end

local function FitGraph()
    if graph == nil or #graph.states == 0 then zoom = 1.0; pan = vec.New(0, 0); ApplyWidgetPositions(); return end
    local minimum = vec.New(math.huge, math.huge)
    local maximum = vec.New(-math.huge, -math.huge)
    for _, state in graph.states do
        minimum = vec.min(minimum, vec.New(state.positionX, state.positionY) - NODE_SIZE / 2)
        maximum = vec.max(maximum, vec.New(state.positionX, state.positionY) + NODE_SIZE / 2)
    end
    local extent = maximum - minimum + vec.New(100, 120)
    zoom = math.clamp(math.min(VIEWPORT_SIZE.x / extent.x, VIEWPORT_SIZE.y / extent.y), 0.55, 1.35)
    pan = -(minimum + maximum) / 2 * zoom
    ApplyWidgetPositions()
end

local function AttachFromSession(force)
    local session = PreviewSession.GetSnapshot()
    if not force and session.revision == sessionRevision then return end
    sessionRevision = session.revision
    local sameEntity=attachedEntity==session.entity
    attachedEntity = session.entity
    runtime = nil
    graph = if attachedEntity ~= nil then Asset.GetAnimationControllerGraph(attachedEntity,editingDraft) else nil
    if not sameEntity then selectedState=nil; selectedTransition=nil end
    if graph ~= nil then
        graphNameText:SetText(graph.name)
        graphStatusText:SetText(string.format("Entity %d  |  %d states  |  %d transitions  |  %d IK  |  live", attachedEntity, #graph.states, #graph.transitions, graph.ikOperations ~= nil and #graph.ikOperations or 0))
        liveParameterText:SetText("")
        if editingDraft then graphStatusText:SetText(graph.validDraft and "DRAFT | Ready to compile and publish" or "DRAFT | Invalid fields; running graph unchanged. Open Inspector for errors.") end
    else
        graphNameText:SetText("No controller attached")
        graphStatusText:SetText("Open Animation Debug and spawn States, Blend 1D, or Blend 2D")
        liveParameterText:SetText("")
    end
    BuildGraphWidgets()
    FitGraph()
end

local function RefreshRuntime()
    if editingDraft then return end
    if paused or attachedEntity == nil or graph == nil then return end
    runtime = Asset.GetAnimationControllerState(attachedEntity)
    if runtime == nil then
        graphStatusText:SetText("Controller is no longer available")
        PreviewSession.Clear(attachedEntity)
        AttachFromSession(true)
        return
    end
    if runtime.graphRevision ~= graph.revision then
        graph=Asset.GetAnimationControllerGraph(attachedEntity)
        BuildGraphWidgets()
    end
    local parameterParts = {}
    for _, parameter in graph.parameters do
        local value = runtime.parameters ~= nil and runtime.parameters[parameter.name] or nil
        if typeof(value) == "table" then
            table.insert(parameterParts, string.format("%s (%.2f, %.2f, %.2f)", parameter.name, value.x, value.y, value.z))
        else
            table.insert(parameterParts, value ~= nil and string.format("%s %.2f", parameter.name, value) or parameter.name .. " --")
        end
    end
    liveParameterText:SetText("PARAMETERS   " .. table.concat(parameterParts, "     "))
    graphStatusText:SetText(string.format("Entity %d  |  %d states  |  %d transitions  |  layers %d  |  IK %d  |  live", attachedEntity, #graph.states, #graph.transitions, integer.tonumber(runtime.activeLayers), integer.tonumber(runtime.activeIKOperations)))
    if followActive then
        selectedState = runtime.transitioning and runtime.transitionTarget or runtime.currentState
        selectedTransition = nil
    end
    RefreshNodeStyles()
    RefreshTransitionStyles()
    DrawEdges()
    RefreshInspector()
end

fitButton:SetOnMouseUp(FitGraph)
blendPanel:SetOnMouseDown(function(_,_,button,x,y)
    if not editingDraft or button~=0i or not blendMapping then return end
    local point=ShapeSurfaceUtil.ScreenToSurface(blendPanel,blendSurface,UI.GetMousePos())
    local mapping=blendMapping
    local nearest,distance=nil,14
    for i,sample in mapping.state.samples do
        local p=vec.New(18+(sample.x-mapping.minX)/(mapping.maxX-mapping.minX)*172,18+(sample.y-mapping.minY)/(mapping.maxY-mapping.minY)*172)
        local d=vec.magnitude(p-point); if d<distance then nearest=i; distance=d end
    end
    if nearest then draggedSample={mapping=mapping,index=nearest} end
end)
blendPanel:SetOnMouseHeld(function(_,_,x,y)
    if not draggedSample then return end
    local m=draggedSample.mapping
    local point=ShapeSurfaceUtil.ScreenToSurface(blendPanel,blendSurface,UI.GetMousePos())
    local sample=m.state.samples[draggedSample.index]
    sample.x=m.minX+(point.x-18)/172*(m.maxX-m.minX)
    if m.state.kind==2i then sample.y=m.minY+(point.y-18)/172*(m.maxY-m.minY) end
    RefreshInspector()
end)
blendPanel:SetOnMouseRelease(function(_,_,button)
    if button~=0i or not draggedSample then return end
    local m=draggedSample.mapping
    local sample=m.state.samples[draggedSample.index]
    local ok,error=Asset.AnimationGraphEditField(attachedEntity,"states",m.state.index,"blendSamples/"..tostring(draggedSample.index-1).."/position",string.format("[%f,%f]",sample.x,sample.y))
    graphStatusText:SetText(ok and "Blend sample position edited; Apply to publish" or error)
    draggedSample=nil
end)
local function CanvasButton(text,x,width,fn,y)
    local button=Button.New(root,vec.New(x,y or -607),vec.New(width,26),2i,{text=text,textTemplate=TEXT_TEMPLATE,borderColor=colors.border,borderSize=1})
    ConfigureTopLeft(button); button:SetOnMouseUp(fn); return button
end
local draftButton
draftButton=CanvasButton("Edit draft",8,90,function()
    editingDraft=not editingDraft; draftButton:SetText(editingDraft and "Show live" or "Edit draft")
    followActive=false; connectSource=nil; AttachFromSession(true)
    graphStatusText:SetText(editingDraft and "DRAFT | Drag nodes. Connect selects the next target. Apply publishes." or "LIVE")
end)
local function EditCanvas(command)
    if not attachedEntity then return end
    local section=selectedTransition~=nil and "transitions" or "states"
    local index=selectedTransition or selectedState or 0i
    local ok,error=Asset.AnimationGraphCommand(attachedEntity,command,section,index)
    if ok then editingDraft=true; draftButton:SetText("Show live"); AttachFromSession(true) end
    graphStatusText:SetText(ok and "Draft updated; Apply to publish" or error)
end
CanvasButton("Clone node",104,94,function() EditCanvas("duplicate") end)
CanvasButton("Connect",204,80,function()
    if editingDraft and selectedState~=nil then connectSource=selectedState; graphStatusText:SetText("Select the target state") end
end)
CanvasButton("Delete",290,64,function() EditCanvas("remove") end)
CanvasButton("Undo",360,62,function() EditCanvas("undo") end)
CanvasButton("Redo",428,62,function() EditCanvas("redo") end)
local sampleInput=InputBox.New(root,vec.New(8,-642),vec.New(40,26),2i,{panelTemplate="DebugDarkGrey",textTemplate=TEXT_TEMPLATE,defaultText="0"})
ConfigureTopLeft(sampleInput); sampleInput:SetTextVerticallyCentered()
local function EditSample(command)
    if not attachedEntity or selectedState==nil then return false end
    local index=math.max(0,math.floor(tonumber(sampleInput:GetText()) or 0))
    local section="states/"..tostring(selectedState).."/blendSamples"
    local ok,error=Asset.AnimationGraphCommand(attachedEntity,command,section,integer.create(index))
    if ok then editingDraft=true; draftButton:SetText("Show live"); AttachFromSession(true) end
    graphStatusText:SetText(ok and "Sample draft updated. Set its position and clip slot in the inspector." or error)
    local inspector=EditorRegistry.Get("AnimationAuthoring")
    if inspector then inspector:Show(); inspector:Select("states",tonumber(tostring(selectedState))) end
    return ok
end
CanvasButton("Add sample",54,100,function() EditSample("add") end,-642)
CanvasButton("Remove sample",160,120,function() EditSample("remove") end,-642)
local triangleInput=InputBox.New(root,vec.New(286,-642),vec.New(100,26),2i,{panelTemplate="DebugDarkGrey",textTemplate=TEXT_TEMPLATE,defaultText="0,1,2"})
ConfigureTopLeft(triangleInput); triangleInput:SetTextVerticallyCentered()
CanvasButton("Add triangle",392,106,function()
    if not attachedEntity or selectedState==nil then return end
    local a,b,c=string.match(triangleInput:GetText(),"^(%d+),%s*(%d+),%s*(%d+)$")
    if not a then graphStatusText:SetText("Enter three sample indices: 0,1,2"); return end
    local section="states/"..tostring(selectedState).."/blendTriangles"
    local doc=Asset.AnimationGraphDocument(attachedEntity)
    local count=0
    for _,field in doc.fields do if field.section=="states" and field.index==selectedState and string.match(field.name,"^blendTriangles/%d+/samples$") then count+=1 end end
    local ok,error=Asset.AnimationGraphCommand(attachedEntity,"add",section)
    if ok then ok,error=Asset.AnimationGraphEditField(attachedEntity,"states",selectedState,"blendTriangles/"..tostring(count).."/samples",string.format("[%s,%s,%s]",a,b,c)) end
    graphStatusText:SetText(ok and "Triangle added to draft" or error)
    if ok then editingDraft=true; AttachFromSession(true) end
end,-642)
CanvasButton("Inspector",504,80,function() local inspector=EditorRegistry.Get("AnimationAuthoring"); if inspector then inspector:Show(); inspector:Select("states",tonumber(tostring(selectedState or 0i))) end end,-642)
CanvasButton("Remove triangle",590,132,function()
    if not attachedEntity or selectedState==nil then return end
    local index=math.max(0,math.floor(tonumber(sampleInput:GetText()) or 0))
    local ok,error=Asset.AnimationGraphCommand(attachedEntity,"remove","states/"..tostring(selectedState).."/blendTriangles",integer.create(index))
    if ok then editingDraft=true; draftButton:SetText("Show live"); AttachFromSession(true) end
    graphStatusText:SetText(ok and "Triangle removed (index from sample/index field)" or error)
end,-642)
CanvasButton("Apply",496,74,function()
    if not attachedEntity then return end
    local ok,error=Asset.AnimationGraphCommand(attachedEntity,"apply")
    graphStatusText:SetText(ok and "Publication queued" or error)
    if ok then Scheduler.AfterFrames(2,function()
        local preview=EditorRegistry.Get("AnimationDebug"); if preview then preview:RefreshParameterBindings() end
        editingDraft=false; draftButton:SetText("Edit draft"); PreviewSession.NotifyGraphChanged(); AttachFromSession(true)
    end) end
end)
followButton:SetOnMouseUp(function()
    followActive = not followActive
    followButton:SetHighlighted(followActive)
    RefreshRuntime()
end)
pauseButton:SetOnMouseUp(function()
    paused = not paused
    pauseButton:SetText(paused and "Resume" or "Pause")
    pauseButton:SetHighlighted(paused)
end)

viewport:SetOnMouseDown(function(_, _, button, mouseX, mouseY)
    if button ~= 2i then return end
    panActive = true
    panMouseStart = vec.New(mouseX, mouseY)
    panOffsetStart = pan
end)
viewport:SetOnMouseHeld(function(_, _, mouseX, mouseY)
    if not panActive then return end
    pan = panOffsetStart + vec.New(mouseX, mouseY) - panMouseStart
    ApplyWidgetPositions()
end)
viewport:SetOnMouseRelease(function(_, _, button) if button == 2i then panActive = false end end)
viewport:SetOnMouseScroll(function(_, _, _, scrollY)
    if scrollY == 0 then return end
    zoom = math.clamp(zoom * (if scrollY > 0 then 1.15 else 1 / 1.15), 0.45, 1.8)
    ApplyWidgetPositions()
end)

local refreshGeneration = 0
local function BeginRefresh()
    refreshGeneration += 1
    local generation = refreshGeneration
    local function Tick()
        if generation ~= refreshGeneration or not root:IsVisible() then return end
        AttachFromSession(false)
        RefreshRuntime()
        Scheduler.AfterFrames(3, Tick)
    end
    Tick()
end

local methods = {}

function methods:Show()
    root:SetVisible(true)
    followButton:SetHighlighted(followActive)
    AttachFromSession(true)
    BeginRefresh()
end

function methods:Hide()
    root:SetVisible(false)
    refreshGeneration += 1
end

function methods:IsVisible()
    return root:IsVisible()
end

function methods:Fit()
    FitGraph()
end

function methods:EditMode(enabled)
    editingDraft=enabled; draftButton:SetText(enabled and "Show live" or "Edit draft"); AttachFromSession(true)
end
function methods:SelectState(index)
    selectedState=integer.create(index); selectedTransition=nil; followActive=false; RefreshInspector(); RefreshNodeStyles()
end
function methods:EditSelection(command) EditCanvas(command) end
function methods:EditBlendSample(command,index) sampleInput:SetText(tostring(index or 0)); return EditSample(command) end
function methods:Refresh()
    AttachFromSession(true)
    local inspector=EditorRegistry.Get("AnimationAuthoring")
    if inspector then inspector:Refresh() end
end

window.closeButton:SetOnMouseUp(function() methods:Hide() end)
EditorRegistry.Register("AnimationGraph", methods)
