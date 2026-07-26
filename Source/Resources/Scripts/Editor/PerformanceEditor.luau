--[[
PerformanceEditor -- Lua port of the C++ ImGui "Performance" panel. Header shows CPU/GPU + averaged frame
timings; the body is a scroll list of collapsible sections: a 5-series frame-time graph (+ legend), frame
times, render-pass timings, and per-subsystem culling counts (instances + triangles). Each section is a
Collapsible; when collapsed its header shows a one-line summary (Total ms, survived/total, ...) so the key
number stays visible without expanding.

Perf: everything updates EVERY frame but stays cheap and uniform (no spikes) -- expanded sections reuse a
per-section widget pool updated in place (skip-when-unchanged), collapsed sections only re-title their
header (also skip-when-unchanged), SetBodyContentHeight/relayout run only when a row count changes, and the
graph is immediate-mode debug lines (drawn only while the graph section is expanded and scrolled into view,
since debug lines aren't clipped by the scroll box). Data + graph draw go through the dev-only Perf handler.
]]

local EditorButton = require("@src/API/UI/EditorButton")
local EditorRegistry = require("@src/API/UI/EditorRegistry")
local ScrollBox = require("@src/API/UI/ScrollBox")
local Collapsible = require("@src/API/UI/Collapsible")

require("@src/API/UI/MenuBar")

local PANEL_W      = 460
local WINDOW_H     = 520
local TITLE_H      = 16
local ROW_H        = 14
local SIDE_PAD     = 6
local VALUE_X      = 200          -- left edge of the value column inside a section body
local LAYER        = 1100
local BTN_TEXT_TPL = "DevToolsText"

local HEADER_ROWS  = 3            -- CPU / GPU / FPS lines in the fixed header
local GRAPH_H      = 72
local STRIP_H      = 15           -- legend strip below the graph (inside the graph section body)
local SWATCH       = 9            -- legend color-block size
local SCROLLBAR_W  = 10
local SECTION_GAP  = 2
local COLLAPSIBLE_HEADER_H = 16   -- must match Collapsible's HEADER_H

local WINDOW_BG_COLOR = vector.create(0.10, 0.10, 0.12)
local WINDOW_BG_ALPHA = 0.96
local BORDER_COLOR    = vector.create(0.30, 0.30, 0.35)
local BORDER_SIZE     = 1
local LABEL_TINT      = vector.create(0.82, 0.82, 0.88)
local VALUE_TINT      = vector.create(0.70, 0.90, 0.75)
local HEADER_TINT     = vector.create(0.65, 0.78, 1.00)
local AXIS_TINT       = vector.create(0.55, 0.55, 0.62)
local LEGEND_TINT     = vector.create(0.82, 0.82, 0.88)

local HEADER_H   = TITLE_H + 2 + HEADER_ROWS * ROW_H
local SCROLL_TOP = HEADER_H + 2
local LIST_W     = PANEL_W - SIDE_PAD * 2
local SCROLL_H   = WINDOW_H - SCROLL_TOP - SIDE_PAD
local CONTENT_W  = LIST_W - SCROLLBAR_W
local SECTION_W  = CONTENT_W - 2
local GRAPH_W    = SECTION_W - 6

local canvas = UI.GetCanvas("PerformanceEditor", 0, 0, 1920, 1080, false)

local windowPanel = canvas:NewPanel(740, -180, PANEL_W, WINDOW_H, LAYER, "DebugDarkGrey")
windowPanel:SetAnchor(0.0, 1.0)
windowPanel:SetRelativePoint(0.0, 1.0)
windowPanel:SetVisible(false)
windowPanel:SetColor(WINDOW_BG_COLOR)
windowPanel:SetAlpha(WINDOW_BG_ALPHA)
windowPanel:SetBorderColor(BORDER_COLOR)
windowPanel:SetBorderSize(BORDER_SIZE)

local titleBar = windowPanel:NewPanel(0, 0, PANEL_W, TITLE_H, 1, "DebugDarkGrey")
titleBar:SetAnchor(0.0, 1.0)
titleBar:SetRelativePoint(0.0, 1.0)
titleBar:SetColor(vector.create(0.06, 0.06, 0.08))
titleBar:SetAlpha(WINDOW_BG_ALPHA)

local titleText = titleBar:NewText("Performance", SIDE_PAD, 0, 1, BTN_TEXT_TPL)
titleText:SetAnchor(0.0, 0.5)
titleText:SetRelativePoint(0.0, 0.5)
titleText:SetInteractable(false)

local closeBtn = EditorButton.NewEditorButton(titleBar, -2, 0, 12, 12, 1, {
    text = "X", textTemplate = BTN_TEXT_TPL, borderColor = BORDER_COLOR, borderSize = BORDER_SIZE,
})
closeBtn:SetAnchor(1.0, 0.5)
closeBtn:SetRelativePoint(1.0, 0.5)
closeBtn.text:SetPosX(1)

windowPanel:MakeDraggable(titleBar)

-- Skip-when-unchanged text/color (keyed by widget) so per-frame updates don't re-bake unchanged glyphs.
local lastText = {}
local function SetLabelText(label, text)
    if lastText[label] == text then return end
    lastText[label] = text
    label:SetText(text)
end
local lastColor = {}
local function SetLabelColor(label, color)
    if lastColor[label] == color then return end
    lastColor[label] = color
    label:SetColor(color)
end

local function MakeHeaderLabel(row)
    local label = windowPanel:NewText("", SIDE_PAD, -(TITLE_H + 2 + row * ROW_H), 1, BTN_TEXT_TPL)
    label:SetAnchor(0.0, 1.0)
    label:SetRelativePoint(0.0, 1.0)
    label:SetColor(LABEL_TINT)
    label:SetInteractable(false)
    return label
end

local cpuLabel = MakeHeaderLabel(0)
local gpuLabel = MakeHeaderLabel(1)
local fpsLabel = MakeHeaderLabel(2)

local function fmt(n) return string.format("%.2f", n or 0) end

local function thousands(n)
    local s = tostring(math.floor((n or 0) + 0.5))
    local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse()
    if out:sub(1, 1) == "," then out = out:sub(2) end
    return out
end

local function svt(survived, total)
    local pct = (total > 0) and (survived / total * 100) or 0
    return string.format("%s / %s (%.0f%%)", thousands(survived), thousands(total), pct)
end

-- ---- scrollable section list (each section is a Collapsible stacked in the scroll content) ----
local statsList = ScrollBox.NewScrollBox(windowPanel, SIDE_PAD, -SCROLL_TOP, LIST_W, SCROLL_H, 1, {
    panelTemplate = "DebugDarkGrey",
    contentSizeX = CONTENT_W,
    contentSizeY = SCROLL_H,
    verticalScrollBar = true,
    scrollBarWidth = SCROLLBAR_W,
    alpha = 0.0,
})
statsList:SetAnchor(0.0, 1.0)
statsList:SetRelativePoint(0.0, 1.0)

local sections = {}
local graphBodyTopLocal = COLLAPSIBLE_HEADER_H  -- graph is section 1, so its body starts one header down
local lastContentH = nil

-- Stack every section top-to-bottom at its current (collapsed/expanded) height, and size the scroll
-- content to the total. Called on any toggle (via onToggle) and whenever a section's row count changes.
local function RelayoutSections()
    local y = 0
    for _, sec in sections do
        sec:SetPosY(-y)
        y += sec:GetTotalHeight() + SECTION_GAP
    end
    local contentH = math.max(SCROLL_H, y)
    if contentH ~= lastContentH then
        lastContentH = contentH
        statsList:SetContentHeight(contentH)
    end
end

-- A data section: a Collapsible whose body holds a reused pool of {left label, right value} rows.
local function CreateDataSection(title, expanded)
    local sec = Collapsible.NewCollapsible(statsList.content, 0, 0, SECTION_W, 1, {
        title = title, expanded = expanded, bodyPadding = 3, onToggle = RelayoutSections,
    })
    local pool = {}
    sec._pool = pool
    sec._getRow = function(i)
        local row = pool[i]
        if not row then
            local y = -((i - 1) * ROW_H) - 1
            local left = sec.body:NewText("", 4, y, 1, BTN_TEXT_TPL)
            left:SetAnchor(0.0, 1.0)
            left:SetRelativePoint(0.0, 1.0)
            left:SetInteractable(false)
            local right = sec.body:NewText("", VALUE_X, y, 1, BTN_TEXT_TPL)
            right:SetAnchor(0.0, 1.0)
            right:SetRelativePoint(0.0, 1.0)
            right:SetColor(VALUE_TINT)
            right:SetInteractable(false)
            row = { left = left, right = right }
            pool[i] = row
        end
        return row
    end
    return sec
end

-- ---- Graph section: graph background + 5-series legend live in its body (built once) ----
local graphSection = Collapsible.NewCollapsible(statsList.content, 0, 0, SECTION_W, 1, {
    title = "Frame Graph", expanded = true, bodyPadding = 3, onToggle = RelayoutSections,
})

local GRAPH_PAD = 2
local graphBg = graphSection.body:NewPanel(GRAPH_PAD, -GRAPH_PAD, GRAPH_W, GRAPH_H, 1, "DebugDarkGrey")
graphBg:SetAnchor(0.0, 1.0)
graphBg:SetRelativePoint(0.0, 1.0)
graphBg:SetColor(vector.create(0.05, 0.05, 0.07))
graphBg:SetAlpha(0.6)
graphBg:SetInteractable(false)

local function axisLabel(text, x, y)
    local lbl = graphBg:NewText(text, x, y, 2, BTN_TEXT_TPL)
    lbl:SetAnchor(0.0, 1.0)
    lbl:SetRelativePoint(0.0, 1.0)
    lbl:SetColor(AXIS_TINT)
    lbl:SetInteractable(false)
end
axisLabel("33 ms", GRAPH_W - 44, -2)
axisLabel("16.7 ms", GRAPH_W - 52, -(GRAPH_H * 0.5 - 4))
axisLabel("0 ms", GRAPH_W - 34, -(GRAPH_H - 13))
axisLabel("120 frames", 4, -(GRAPH_H - 13))

-- Legend below the graph: a colored panel swatch + neutral label per series (the swatch carries the color).
local STRIP_Y = GRAPH_PAD + GRAPH_H + 4
local slotX = { 6, 96, 186, 276, 366 }
for i, series in Perf.GetGraphSeries() do
    local x = slotX[i] or (6 + (i - 1) * 90)
    local swatch = graphSection.body:NewPanel(x, -(STRIP_Y + 2), SWATCH, SWATCH, 1, "DebugDarkGrey")
    swatch:SetAnchor(0.0, 1.0)
    swatch:SetRelativePoint(0.0, 1.0)
    swatch:SetColor(vector.create(series.r, series.g, series.b))
    swatch:SetAlpha(1.0)
    swatch:SetInteractable(false)
    local lbl = graphSection.body:NewText(series.label, x + SWATCH + 3, -STRIP_Y, 1, BTN_TEXT_TPL)
    lbl:SetAnchor(0.0, 1.0)
    lbl:SetRelativePoint(0.0, 1.0)
    lbl:SetColor(LEGEND_TINT)
    lbl:SetInteractable(false)
end

graphSection:SetBodyContentHeight(STRIP_Y + STRIP_H)

-- ---- Data sections ----
local frameTimesSection = CreateDataSection("Frame Times (ms)", true)
local renderPassSection = CreateDataSection("Render Passes (GPU ms)", false)
local instancesSection  = CreateDataSection("Instances (survived / total)", false)
local trianglesSection  = CreateDataSection("Triangles (survived / total)", false)

sections[1] = graphSection
sections[2] = frameTimesSection
sections[3] = renderPassSection
sections[4] = instancesSection
sections[5] = trianglesSection

-- Fill an expanded section's rows in place; hide surplus pooled rows; resize + relayout only if the row
-- count changed. `fillFn(put)` calls put(leftText, rightText?, leftTint?) once per row, top to bottom.
local sectionRowCount = {}
local function FillSection(sec, fillFn)
    local index = 0
    local function put(leftText, rightText, tint)
        index += 1
        local row = sec._getRow(index)
        SetLabelText(row.left, leftText)
        SetLabelColor(row.left, tint or LABEL_TINT)
        SetLabelText(row.right, rightText or "")
        row.left:SetVisible(true)
        row.right:SetVisible(rightText ~= nil)
    end
    fillFn(put)
    for i = index + 1, #sec._pool do
        sec._pool[i].left:SetVisible(false)
        sec._pool[i].right:SetVisible(false)
    end
    if sectionRowCount[sec] ~= index then
        sectionRowCount[sec] = index
        sec:SetBodyContentHeight(index * ROW_H + 2)
        RelayoutSections()
    end
end

-- Expanded: plain title + populated rows. Collapsed: "title   <summary>" (or just the title when
-- summaryFn is nil, for sections whose value is already shown elsewhere).
local function UpdateSection(sec, baseTitle, summaryFn, fillFn)
    if sec:IsExpanded() then
        sec:SetTitle(baseTitle)
        FillSection(sec, fillFn)
    elseif summaryFn then
        sec:SetTitle(baseTitle .. "   " .. summaryFn())
    else
        sec:SetTitle(baseTitle)
    end
end

local function Refresh()
    local s = Perf.GetFrameStats()
    SetLabelText(fpsLabel, string.format("FPS %.1f   Frame %s ms   Shadow cascades: %d", s.fps, fmt(s.deltaMs), Perf.GetShadowCascadeNum()))

    -- Graph section: draw only while expanded AND scrolled into view (debug lines aren't clipped). No
    -- collapsed summary -- FPS/frame time is already shown in the fixed header just above this section.
    if graphSection:IsExpanded() and statsList.scrollPosY <= graphBodyTopLocal then
        local wx, wy = graphBg:GetWorldPos()
        Perf.DrawFrameGraph(wx, wy, GRAPH_W, GRAPH_H)
    end

    -- Frame times (indent encodes the Total > Update/Render CPU > wait hierarchy, like the ImGui table).
    UpdateSection(frameTimesSection, "Frame Times (ms)", nil,
        function(put)
            put("Total", fmt(s.deltaMs) .. " ms")
            put("   Update", fmt(s.simMs) .. " ms")
            put("   Render CPU", fmt(s.renderMs) .. " ms")
            put("      CPU wait for GPU", fmt(s.waitMs) .. " ms")
            put("GPU frame time", fmt(s.gpuMs) .. " ms")
        end)

    -- Render passes. GetRenderPasses() includes a "-- Total --" entry (the whole-graph GPU time) alongside
    -- the individual passes, so the collapsed summary uses that entry -- summing every row would count the
    -- total twice.
    local passes = Perf.GetRenderPasses()
    UpdateSection(renderPassSection, "Render Passes (GPU ms)",
        function()
            for _, p in passes do
                if p.name == "-- Total --" then return fmt(p.ms) .. "ms" end
            end
            return "-"
        end,
        function(put)
            for _, p in passes do put(p.name, fmt(p.ms) .. " ms") end
        end)

    -- Culling: per-subsystem instances + triangles, plus grand totals (used by both rows and summaries).
    local culling = Perf.GetCullingStats()
    local dcSurv, dcTotal, triSurv, triTotal = 0, 0, 0, 0
    for _, c in culling do
        dcSurv += c.dcGeometry
        dcTotal += c.dcTotal
        triSurv += c.triGeometry
        triTotal += c.triTotal
    end

    UpdateSection(instancesSection, "Instances (survived / total)",
        function() return svt(dcSurv, dcTotal) end,
        function(put)
            for _, c in culling do
                if c.showOccluder then put(c.name .. " Occluders", svt(c.dcOccluder, c.dcTotal)) end
                put(c.name .. " Geometry", svt(c.dcGeometry, c.dcTotal))
            end
            put("Total", svt(dcSurv, dcTotal), HEADER_TINT)
        end)

    UpdateSection(trianglesSection, "Triangles (survived / total)",
        function() return svt(triSurv, triTotal) end,
        function(put)
            for _, c in culling do
                if c.showOccluder then put(c.name .. " Occluders", svt(c.triOccluder, c.triTotal)) end
                put(c.name .. " Geometry", svt(c.triGeometry, c.triTotal))
            end
            put("Total", svt(triSurv, triTotal), HEADER_TINT)
        end)
end

RelayoutSections()

RegisterEvent(GameEvent.Updated, function()
    if not windowPanel:IsVisible() then return end
    Refresh()
end)

local methods = {}

function methods:Show()
    windowPanel:SetVisible(true)
    SetLabelText(cpuLabel, "CPU: " .. (Perf.GetCPUName() or "-"))
    SetLabelText(gpuLabel, "GPU: " .. (Perf.GetGPUName() or "-"))
    Refresh()
end

function methods:Hide()
    windowPanel:SetVisible(false)
end

function methods:IsVisible()
    return windowPanel:IsVisible()
end

closeBtn:SetOnMouseUp(function() methods:Hide() end)

EditorRegistry.Register("Performance", methods)
