local UIStack = require("@src/API/UI/UIStack")
local UIButton = require("@src/API/UI/Button")
local UILoadingScreen = require("@src/API/UI/LoadingScreen")
local UIVerticalLayout   = require("@src/API/UI/VerticalLayout")
local UIHorizontalLayout = require("@src/API/UI/HorizontalLayout")
local UIGridLayout       = require("@src/API/UI/GridLayout")

-- TODO: Borders :(

local function OnButtonEvent(button : Button, eventID : ButtonEvent, ...)
    if (eventID == ButtonEvent.Hovered) then
        print("Hovered!");
    end
end

local function CreateSingleLineDemo()
    local canvas : Canvas = UI.GetCanvas("SingleLineDemo", 0, 0, 400, 400, true);

    -- Create a panel
    local panel = canvas:NewPanel(0, 0, 400, 400, 0, "DialogBox");
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);

    local bottomLeft = panel:NewText("Bottom Left", 0, 0, 0, "DefaultButtonText");
    bottomLeft:SetAnchor(0.0, 0.0);
    bottomLeft:SetRelativePoint(0.0, 0.0);

    local bottomRight = panel:NewText("Bottom Right", 0, 0, 0, "DefaultButtonText");
    bottomRight:SetAnchor(1.0, 0.0);
    bottomRight:SetRelativePoint(1.0, 0.0);

    local topLeft = panel:NewText("Top Left", 0, 0, 0, "DefaultButtonText");
    topLeft:SetAnchor(0.0, 1.0);
    topLeft:SetRelativePoint(0.0, 1.0);

    local topRight = panel:NewText("Top Right", 0, 0, 0, "DefaultButtonText");
    topRight:SetAnchor(1.0, 1.0);
    topRight:SetRelativePoint(1.0, 1.0);

    local center = panel:NewText("Center", 0, 0, 0, "DefaultButtonText");
    center:SetAnchor(0.5, 0.5);
    center:SetRelativePoint(0.5, 0.5);

    -- Test button
    local supportButton = UIButton.NewButton(panel, 0, 70, 250, 40, 0, {
        text = "Test"
    });
    supportButton:SetAnchor(0.5, 0.5);
    supportButton:SetRelativePoint(0.5, 0.5);
    supportButton:SetOnMouseUp(function(eventID, widget, ...)
        print("SingleLineButton");
    end);

    return canvas;
end

local function CreateMultiLineDemo(rtCanvas : Canvas)
    local canvas : Canvas = UI.GetCanvas("MultiLineDemo", 0, 0, 1920, 1080);

    -- Create a panel displaying the single line demo
    --local singleLinePanel = canvas:NewPanel(0, 0, 400, 400, 0, "DialogBox");
    --singleLinePanel:SetAnchor(0.5, 0.5);
    --singleLinePanel:SetRelativePoint(0.5, 0.5);
    --singleLinePanel:SetBackground(rtCanvas);
    --singleLinePanel:DebugSetWorldTransformIndex(0);

    -- Create a panel
    local panel = canvas:NewPanel(0, 0, 400, 400, 0, "DialogBox");
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);

    --panel:SetAnchor(0.0, 0.0);
    --panel:SetRelativePoint(1.0, 0.0);
    --panel:DebugSetWorldTransformIndex(0);

    local middlePanel = panel:NewPanel(0, 0, 50, 50, 0, "DialogBox");
    middlePanel:SetAnchor(0.5, 0.5);
    middlePanel:SetRelativePoint(0.5, 0.5);

    local bottomLeft = panel:NewText("Bottom Left\nLeft Bottom", 0, 0, 0, "DefaultButtonText");
    bottomLeft:SetAnchor(0.0, 0.0);
    bottomLeft:SetRelativePoint(0.0, 0.0);

    local bottomRight = panel:NewText("Bottom Right\nRight Bottom", 0, 0, 0, "DefaultButtonText");
    bottomRight:SetAnchor(1.0, 0.0);
    bottomRight:SetRelativePoint(1.0, 0.0);

    local topLeft = panel:NewText("Top Left\nLeft Top\nLeft Left", 0, 0, 0, "DefaultButtonText");
    topLeft:SetAnchor(0.0, 1.0);
    topLeft:SetRelativePoint(0.0, 1.0);

    local topRight = panel:NewText("Top Right\nRight Top", 0, 0, 0, "DefaultButtonText");
    topRight:SetAnchor(1.0, 1.0);
    topRight:SetRelativePoint(1.0, 1.0);

    local center = panel:NewText("-Center\nCenter-", 0, 0, 0, "DefaultButtonText");
    center:SetAnchor(0.5, 0.5);
    center:SetRelativePoint(0.5, 0.5);

    -- Test button
    --local supportButton = UIButton.NewButton(panel, 0, 70, 250, 40, 0, {
    --    text = "Test"
    --});
    --supportButton:SetAnchor(0.5, 0.5);
    --supportButton:SetRelativePoint(0.5, 0.5);
    --supportButton:SetOnMouseUp(function(eventID, widget, ...)
    --    print("MultiLineButton");
    --end);
end

local function Demo()
    local rtCanvas = CreateSingleLineDemo()
    CreateMultiLineDemo(rtCanvas)
end

-- 3x3 demo cell grid shared by SortingDemo and LayoutsDemo. Cells read
-- left-to-right, top-to-bottom; row 0 is the top row.
local DEMO_CANVAS_H = 1080
local DEMO_TOP_MARGIN = 28
local DEMO_CELL_W, DEMO_CELL_H = 618, 330
local DEMO_PAD_X, DEMO_PAD_Y = 16, 16

local function MakeDemoCell(canvas, col, row, label, bgTemplate)
    local x = DEMO_PAD_X + col * (DEMO_CELL_W + DEMO_PAD_X)
    local y = (DEMO_CANVAS_H - DEMO_TOP_MARGIN) - (DEMO_CELL_H + DEMO_PAD_Y) * (row + 1)

    local cell = canvas:NewPanel(x, y, DEMO_CELL_W, DEMO_CELL_H, 0, bgTemplate)
    cell:SetAnchor(0.0, 0.0); cell:SetRelativePoint(0.0, 0.0)
    cell:SetBorderColor(vector.create(0.75, 0.75, 0.75))
    cell:SetBorderSize(2)

    local titleWidget = cell:NewText(label, 10, -8, 0, "DefaultButtonText")
    titleWidget:SetAnchor(0.0, 1.0); titleWidget:SetRelativePoint(0.0, 1.0)

    return cell
end

-- =====================================================================
-- SortingDemo
--
-- Visual regression case for the upcoming GPU-driven UI renderer (merged
-- Widget pipeline + GPU sort + indirect draw). Run this BEFORE the
-- refactor and screenshot it as a reference, then run it AFTER and
-- compare. Every test must look pixel-identical, modulo deliberate new
-- behaviour: tests 7 and 8 start exercising the layer parameter once
-- GPU sort honors it.
--
-- Sibling order: siblings are drawn in INSERTION ORDER (first inserted
-- on the bottom, last inserted on top). Several tests use 4+ siblings
-- to verify this directly.
-- =====================================================================
local function CreateSortingDemo()
    -- Local debug colors so we don't pollute Templates.luau. Re-registering an
    -- existing template name is a no-op (or overwrite, either way harmless).
    UI.RegisterPanelTemplate("SortDemoOrange",  { cornerRadius = 0.0, color = vector.create(1.00, 0.55, 0.00) });
    UI.RegisterPanelTemplate("SortDemoYellow",  { cornerRadius = 0.0, color = vector.create(1.00, 0.95, 0.00) });
    UI.RegisterPanelTemplate("SortDemoCyan",    { cornerRadius = 0.0, color = vector.create(0.00, 0.85, 0.95) });
    UI.RegisterPanelTemplate("SortDemoMagenta", { cornerRadius = 0.0, color = vector.create(1.00, 0.00, 0.80) });
    UI.RegisterPanelTemplate("SortDemoCellBG",  { cornerRadius = 0.0, color = vector.create(0.10, 0.10, 0.13) });

    -- =======================================================================
    -- Test 9 extras: two top-level canvases that overlap inside cell 9's
    -- screen-space area. entt's view<Canvas> iterates LAST-CREATED first, so
    -- the first-created canvas ends up drawn LAST (on top). To get the
    -- desired stack from bottom to top:
    --     SortingDemo  (bottom, cell 9 background)
    --         CanvasA  (middle)
    --             CanvasB (top)
    -- ...we must create them in REVERSE of that order here:
    --     CanvasB first, CanvasA next, SortingDemo last.
    --
    -- Cell 9 screen bounds (col=2, row=2): x 1284..1902, y 14..344.
    -- Panels are placed in y 130..280 so they don't overlap the cell title
    -- at ~y 336 or the multi-line hint at ~y 22..110.
    -- =======================================================================
    do
        -- CanvasB (created FIRST => drawn LAST => on top)
        local canvasB : Canvas = UI.GetCanvas("SortingDemo_CanvasB", 0, 0, 1920, 1080)
        local bPanel = canvasB:NewPanel(1490, 130, 200, 130, 0, "DebugGreen")
        bPanel:SetAnchor(0.0, 0.0); bPanel:SetRelativePoint(0.0, 0.0)
        local bChild = bPanel:NewPanel(0, 0, 140, 80, 0, "SortDemoYellow")
        bChild:SetAnchor(0.5, 0.5); bChild:SetRelativePoint(0.5, 0.5)
        local bText = bPanel:NewText("CanvasB", 10, 10, 0, "DefaultButtonText")
        bText:SetAnchor(0.0, 0.0); bText:SetRelativePoint(0.0, 0.0)

        -- CanvasA (created SECOND => drawn in the middle)
        local canvasA : Canvas = UI.GetCanvas("SortingDemo_CanvasA", 0, 0, 1920, 1080)
        local aPanel = canvasA:NewPanel(1340, 150, 200, 130, 0, "DebugRed")
        aPanel:SetAnchor(0.0, 0.0); aPanel:SetRelativePoint(0.0, 0.0)
        local aChild = aPanel:NewPanel(0, 0, 140, 80, 0, "DebugBlue")
        aChild:SetAnchor(0.5, 0.5); aChild:SetRelativePoint(0.5, 0.5)
        local aText = aPanel:NewText("CanvasA", 10, 10, 0, "DefaultButtonText")
        aText:SetAnchor(0.0, 0.0); aText:SetRelativePoint(0.0, 0.0)
    end

    -- Main SortingDemo canvas. Created LAST of the three so it's iterated
    -- first and drawn on the bottom, letting CanvasA and CanvasB show through
    -- over cell 9's background.
    local canvas : Canvas = UI.GetCanvas("SortingDemo", 0, 0, 1920, 1080);

    -- Top title bar
    local title = canvas:NewText("SortingDemo - validate before/after GPU-sort refactor", 0, -6, 0, "DefaultButtonText");
    title:SetAnchor(0.5, 1.0);
    title:SetRelativePoint(0.5, 1.0);

    -- 3 columns x 3 rows. row 0 = TOP, row 2 = bottom. col 0 = left, col 2 = right.
    -- Numbering reads left-to-right, top-to-bottom (row*3 + col + 1).
    local function makeCell(col, row, label) return MakeDemoCell(canvas, col, row, label, "SortDemoCellBG") end

    -- =========================================================
    -- Test 1 [top-left]: Sibling panels - 4-step staircase
    -- 4 overlapping panels inserted in order R, G, B, Yellow.
    -- Verifies the fixed insertion-order iteration with >2 siblings.
    -- =========================================================
    do
        local cell = makeCell(0, 0, "1: Sibling Panels (x4)")

        local templates = { "DebugRed", "DebugGreen", "DebugBlue", "SortDemoYellow" }
        local size = 130
        local step = 18
        local startOffset = -((#templates - 1) * step) / 2
        for i = 1, #templates do
            local off = startOffset + (i - 1) * step
            local p = cell:NewPanel(off, -off, size, size, 0, templates[i])
            p:SetAnchor(0.5, 0.5); p:SetRelativePoint(0.5, 0.5)
        end

        local hintText = [[
cell:
    Red
    Green
    Blue
    Yellow   <- on top]]
        local hint = cell:NewText(hintText, 10, 8, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 0.0); hint:SetRelativePoint(0.0, 0.0)
    end

    -- =========================================================
    -- Test 2 [top-mid]: Sibling text - 4-step insertion order
    -- Y step is half the X step so consecutive labels actually
    -- overlap vertically (text height ~22px, Y step = 9px).
    -- =========================================================
    do
        local cell = makeCell(1, 0, "2: Sibling Text (x4)")

        local labels = { "FIRST", "SECOND", "THIRD", "FOURTH" }
        local stepX = 18
        local stepY = 9
        local startX = -((#labels - 1) * stepX) / 2
        local startY = -((#labels - 1) * stepY) / 2
        for i = 1, #labels do
            local offX = startX + (i - 1) * stepX
            local offY = startY + (i - 1) * stepY
            local t = cell:NewText(labels[i], offX, -offY, 0, "DefaultButtonText")
            t:SetAnchor(0.5, 0.5); t:SetRelativePoint(0.5, 0.5)
        end

        local hintText = [[
cell:
    FIRST
    SECOND
    THIRD
    FOURTH   <- on top]]
        local hint = cell:NewText(hintText, 10, 8, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 0.0); hint:SetRelativePoint(0.0, 0.0)
    end

    -- =========================================================
    -- Test 3 [top-right]: Mixed Panel + Text (sibling, both directions)
    -- Two pairs in one cell, each isolated from the other:
    --   Pair A (left side):  Panel inserted FIRST, Text inserted SECOND.
    --                        Expected: text draws ON TOP of the panel.
    --   Pair B (right side): Text inserted FIRST, Panel inserted SECOND.
    --                        Expected: opaque panel HIDES the text.
    -- This is the critical test for the merged Widget pipeline - the
    -- old two-pipeline path could not interleave panel and text draws
    -- in arbitrary insertion order.
    -- =========================================================
    do
        local cell = makeCell(2, 0, "3: Mixed P+T sibling (both ways)")

        -- Pair A: panel first, text second (text expected on top)
        local pA = cell:NewPanel(-130, 25, 200, 70, 0, "DebugBlue")
        pA:SetAnchor(0.5, 0.5); pA:SetRelativePoint(0.5, 0.5)
        local tA = cell:NewText("Text over Blue", -130, 25, 0, "DefaultButtonText")
        tA:SetAnchor(0.5, 0.5); tA:SetRelativePoint(0.5, 0.5)

        -- Pair B: text first, panel second (panel expected to hide text)
        local tB = cell:NewText("Hidden by Red", 130, -25, 0, "DefaultButtonText")
        tB:SetAnchor(0.5, 0.5); tB:SetRelativePoint(0.5, 0.5)
        local pB = cell:NewPanel(130, -25, 200, 70, 0, "DebugRed")
        pB:SetAnchor(0.5, 0.5); pB:SetRelativePoint(0.5, 0.5)

        local hintText = [[
cell:
    Blue panel
    "Text over Blue" <- VISIBLE

    "Hidden by Red"  <- INVISIBLE
    Red panel]]
        local hint = cell:NewText(hintText, 10, 8, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 0.0); hint:SetRelativePoint(0.0, 0.0)
    end

    -- =========================================================
    -- Test 4 [mid-left]: Hierarchy depth (single-child chain)
    -- Single-child means there's no sibling order at all; each
    -- inner level cleanly draws on top of its parent. Tests
    -- depth-first traversal of a deep chain.
    -- =========================================================
    do
        local cell = makeCell(0, 1, "4: Hierarchy Depth")

        local p1 = cell:NewPanel(0, 0, 180, 180, 0, "DebugRed")
        p1:SetAnchor(0.5, 0.5); p1:SetRelativePoint(0.5, 0.5)

        local p2 = p1:NewPanel(0, 0, 130, 130, 0, "DebugGreen")
        p2:SetAnchor(0.5, 0.5); p2:SetRelativePoint(0.5, 0.5)

        local p3 = p2:NewPanel(0, 0, 90, 90, 0, "DebugBlue")
        p3:SetAnchor(0.5, 0.5); p3:SetRelativePoint(0.5, 0.5)

        local p4 = p3:NewPanel(0, 0, 50, 50, 0, "SortDemoYellow")
        p4:SetAnchor(0.5, 0.5); p4:SetRelativePoint(0.5, 0.5)

        local label = p4:NewText("DEEP", 0, 0, 0, "DefaultDebugText")
        label:SetAnchor(0.5, 0.5); label:SetRelativePoint(0.5, 0.5)

        local hintText = [[
cell:
    Red
        Green
            Blue
                Yellow
                    "DEEP"]]
        local hint = cell:NewText(hintText, 10, 8, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 0.0); hint:SetRelativePoint(0.0, 0.0)
    end

    -- =========================================================
    -- Test 5 [mid-mid]: Branching hierarchy
    -- A single parent panel with two non-overlapping child subtrees.
    -- Each child has its own grandchild text. Verifies depth-first
    -- traversal across MULTIPLE BRANCHES at depth >= 2:
    --   parent -> leftChild -> leftText -> rightChild -> rightText
    -- =========================================================
    do
        local cell = makeCell(1, 1, "5: Branching Hierarchy")

        local parent = cell:NewPanel(0, 0, 414, 153, 0, "DebugDarkGrey")
        parent:SetAnchor(0.5, 0.5); parent:SetRelativePoint(0.5, 0.5)

        local leftChild = parent:NewPanel(-105, 0, 180, 120, 0, "DebugRed")
        leftChild:SetAnchor(0.5, 0.5); leftChild:SetRelativePoint(0.5, 0.5)
        local leftText = leftChild:NewText("LEFT", 0, 0, 0, "DefaultButtonText")
        leftText:SetAnchor(0.5, 0.5); leftText:SetRelativePoint(0.5, 0.5)

        local rightChild = parent:NewPanel(105, 0, 180, 120, 0, "DebugBlue")
        rightChild:SetAnchor(0.5, 0.5); rightChild:SetRelativePoint(0.5, 0.5)
        local rightText = rightChild:NewText("RIGHT", 0, 0, 0, "DefaultButtonText")
        rightText:SetAnchor(0.5, 0.5); rightText:SetRelativePoint(0.5, 0.5)

        local hintText = [[
cell:
    DarkGrey parent
        Red leftChild
            "LEFT"
        Blue rightChild
            "RIGHT"]]
        local hint = cell:NewText(hintText, 10, 8, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 0.0); hint:SetRelativePoint(0.0, 0.0)
    end

    -- =========================================================
    -- Test 6 [mid-right]: Two top-level subtrees that overlap
    -- Two parent panels (A then B) inserted as siblings of the
    -- cell, each containing a child panel and a child text. Verifies
    -- that the entire B subtree draws over the entire A subtree, not
    -- interleaved at the leaves.
    -- =========================================================
    do
        local cell = makeCell(2, 1, "6: Two-Parent Subtrees")

        -- Parent A (DarkGrey base) inserted first
        local parentA = cell:NewPanel(-50, -15, 240, 160, 0, "DebugDarkGrey")
        parentA:SetAnchor(0.5, 0.5); parentA:SetRelativePoint(0.5, 0.5)
        local aChildPanel = parentA:NewPanel(0, 0, 170, 110, 0, "DebugRed")
        aChildPanel:SetAnchor(0.5, 0.5); aChildPanel:SetRelativePoint(0.5, 0.5)
        local aChildText = parentA:NewText("A.text", 0, 0, 0, "DefaultButtonText")
        aChildText:SetAnchor(0.5, 0.5); aChildText:SetRelativePoint(0.5, 0.5)

        -- Parent B (Cyan tinted) inserted second, overlapping parent A
        local parentB = cell:NewPanel(50, 15, 240, 160, 0, "SortDemoCyan")
        parentB:SetAnchor(0.5, 0.5); parentB:SetRelativePoint(0.5, 0.5)
        local bChildPanel = parentB:NewPanel(0, 0, 170, 110, 0, "DebugBlue")
        bChildPanel:SetAnchor(0.5, 0.5); bChildPanel:SetRelativePoint(0.5, 0.5)
        local bChildText = parentB:NewText("B.text", 0, 0, 0, "DefaultButtonText")
        bChildText:SetAnchor(0.5, 0.5); bChildText:SetRelativePoint(0.5, 0.5)

        local hintText = [[
cell:
    DarkGrey parentA
        Red child
        "A.text"
    Cyan parentB
        Blue child
        "B.text"]]
        local hint = cell:NewText(hintText, 10, 8, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 0.0); hint:SetRelativePoint(0.0, 0.0)
    end

    -- =========================================================
    -- Test 7 [bot-left]: Z-index (layer) - 3 panels, gradient sort
    -- Three sibling panels with three DIFFERENT layer values to
    -- verify the post-refactor layer sort handles a gradient (not
    -- just a binary high/low). The MIDDLE panel has the highest
    -- layer, so post-refactor it should be on top despite being
    -- inserted second.
    -- This is the ONE test whose visual is expected to change after
    -- the refactor.
    -- =========================================================
    do
        local cell = makeCell(0, 2, "7: Z-index (3 layers)")

        local pRed   = cell:NewPanel(-50,  20, 160, 160, 50,  "DebugRed")
        pRed:SetAnchor(0.5, 0.5);   pRed:SetRelativePoint(0.5, 0.5)

        local pGreen = cell:NewPanel(  0,   0, 160, 160, 100, "DebugGreen")
        pGreen:SetAnchor(0.5, 0.5); pGreen:SetRelativePoint(0.5, 0.5)
        pGreen:SetAlpha(0.5) -- semi-transparent so we can see whether Red or Blue is stacked under it

        local pBlue  = cell:NewPanel( 50, -20, 160, 160, 25,  "DebugBlue")
        pBlue:SetAnchor(0.5, 0.5);  pBlue:SetRelativePoint(0.5, 0.5)

        local hintText = [[
cell:
    Red    (layer 50,  1st)
    Green  (layer 100, 2nd)
    Blue   (layer 25,  3rd)

BEFORE: Blue on top (insertion order)
AFTER : Green on top, Red middle, Blue bottom
        (sorted by layer descending)]]
        local hint = cell:NewText(hintText, 10, 8, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 0.0); hint:SetRelativePoint(0.0, 0.0)
    end

    -- =========================================================
    -- Test 8 [bot-mid]: Combined nesting + siblings + layers
    -- A parent panel with two child subtrees, where the FIRST
    -- subtree is given a high layer. Pre-refactor: insertion order
    -- means the second subtree covers the first. Post-refactor:
    -- the layer-elevated subtree should rise above the second.
    -- Forces the sort design to commit to "does layer cross
    -- subtree boundaries?" - if yes, the whole Red subtree (panel
    -- + text) should appear on top, including its child text.
    -- =========================================================
    do
        local cell = makeCell(1, 2, "8: Nest + Siblings + Layers")

        local parent = cell:NewPanel(0, 10, 460, 170, 0, "DebugDarkGrey")
        parent:SetAnchor(0.5, 0.5); parent:SetRelativePoint(0.5, 0.5)

        -- Red subtree, inserted FIRST, layer 100
        local redChild = parent:NewPanel(-70, 0, 200, 110, 100, "DebugRed")
        redChild:SetAnchor(0.5, 0.5); redChild:SetRelativePoint(0.5, 0.5)
        local redText = redChild:NewText("L", 0, 0, 0, "DefaultButtonText")
        redText:SetAnchor(0.5, 0.5); redText:SetRelativePoint(0.5, 0.5)

        -- Green subtree, inserted SECOND, layer 0, OVERLAPS Red
        local greenChild = parent:NewPanel(70, 0, 200, 110, 0, "DebugGreen")
        greenChild:SetAnchor(0.5, 0.5); greenChild:SetRelativePoint(0.5, 0.5)
        local greenText = greenChild:NewText("R", 0, 0, 0, "DefaultButtonText")
        greenText:SetAnchor(0.5, 0.5); greenText:SetRelativePoint(0.5, 0.5)

        local hintText = [[
cell:
    DarkGrey parent
        Red   (layer 100, 1st)
            "L"
        Green (layer 0,   2nd)
            "R"

BEFORE: Green's subtree covers Red in overlap
AFTER : Red's subtree on top (layer 100)]]
        local hint = cell:NewText(hintText, 10, 8, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 0.0); hint:SetRelativePoint(0.0, 0.0)
    end

    -- =========================================================
    -- Test 9 [bot-right]: Two overlapping canvases
    -- Cell 9 itself just contains an explanatory label. The actual
    -- test is two extra non-RT canvases (CanvasA and CanvasB) created
    -- AFTER this cell at the top level - they overlap each other in
    -- screen space within the bot-right cell area.
    --
    -- Demonstrates that canvases render WHOLE one at a time:
    -- everything in CanvasB draws above everything in CanvasA in the
    -- overlap zone, never interleaved at the leaf level. (Once
    -- per-canvas Z-index lands, that priority will determine which
    -- canvas wins; today it's strictly creation order.)
    -- =========================================================
    do
        local cell = makeCell(2, 2, "9: Two Canvases (overlap)")

        local hintText = [[
Two extra non-RT canvases overlap inside
this cell area. CanvasA was created first,
CanvasB second.

CanvasB's entire contents draw on top of
CanvasA in the overlap zone - nothing from
the two canvases interleaves at the leaves.]]
        local hint = cell:NewText(hintText, 10, 8, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 0.0); hint:SetRelativePoint(0.0, 0.0)
    end

end

-- =====================================================================
-- LayoutsDemo
--
-- Visual reference / regression case for Vertical/Horizontal/GridLayout.
-- Each cell is a focused feature combination; the hint text on the right
-- describes what a correct render should look like. If you change any
-- LinearLayout / GridLayout code, run this and eyeball every cell.
--
-- 3x3 grid (618x330 cells), reading left-to-right, top-to-bottom.
-- =====================================================================
local function CreateLayoutsDemo()
    UI.RegisterPanelTemplate("LayoutDemoCellBG",   { cornerRadius = 0.0, color = vector.create(0.10, 0.10, 0.13) })
    UI.RegisterPanelTemplate("LayoutDemoFrame",    { cornerRadius = 0.0, color = vector.create(0.20, 0.20, 0.26) })
    UI.RegisterPanelTemplate("LayoutDemoChild",    { cornerRadius = 0.0, color = vector.create(0.30, 0.55, 0.85) })
    UI.RegisterPanelTemplate("LayoutDemoChildAlt", { cornerRadius = 0.0, color = vector.create(0.85, 0.55, 0.30) })

    local canvas : Canvas = UI.GetCanvas("LayoutsDemo", 0, 0, 1920, 1080)

    local title = canvas:NewText("LayoutsDemo - V/H/Grid layout features", 0, -6, 0, "DefaultButtonText")
    title:SetAnchor(0.5, 1.0); title:SetRelativePoint(0.5, 1.0)

    local function makeCell(col, row, label) return MakeDemoCell(canvas, col, row, label, "LayoutDemoCellBG") end

    -- Add a child panel to a layout with an optional centered label.
    local function addChild(layout, w, h, label, template)
        template = template or "LayoutDemoChild"
        local p = layout:NewPanel(0, 0, w, h, 0, template)
        if label then
            local t = p:NewText(label, 0, 0, 0, "DefaultButtonText")
            t:SetAnchor(0.5, 0.5); t:SetRelativePoint(0.5, 0.5)
        end
        return p
    end

    -- Hint text in the right half of the cell, top-down.
    local function makeHint(cell, text)
        local hint = cell:NewText(text, 310, -30, 0, "DefaultDebugText")
        hint:SetAnchor(0.0, 1.0); hint:SetRelativePoint(0.0, 1.0)
    end

    -- =========================================================
    -- Test 1 [top-left]: V layout - spacing + padding + alignH=center
    -- 3 children of differing widths stacked top-down. alignH=center
    -- centers each child horizontally regardless of own width.
    -- padding leaves a uniform 12px frame inside.
    -- =========================================================
    do
        local cell = makeCell(0, 0, "1: V spacing + padding + alignH=center")

        local v = UIVerticalLayout.NewVerticalLayout(cell, 10, 30, 290, 260, 0, {
            panelTemplate = "LayoutDemoFrame",
            spacing = 8, padding = 12, alignH = "center",
        })
        v:SetAnchor(0.0, 0.0); v:SetRelativePoint(0.0, 0.0)
        addChild(v, 80,  40, "A")
        addChild(v, 200, 40, "B (wide)")
        addChild(v, 120, 40, "C")

        makeHint(cell, [[
V layout (medium-grey frame):
  spacing = 8
  padding = 12
  alignH  = center

Children top-down:
  A         (80w)   X-centered
  B (wide)  (200w)  X-centered
  C         (120w)  X-centered

Frame leaves a uniform 12px border;
gaps between children are 8px.]])
    end

    -- =========================================================
    -- Test 2 [top-mid]: H layout - mixed flex weight + intrinsic
    -- Two intrinsic-sized children (60w each) bracket two flex
    -- children (weight 1, 2). After fixed sums + spacing, the
    -- remaining width is split 1:2 between the flex ones.
    -- defaultChildOpts.modeY=flex stretches children to full row height.
    -- =========================================================
    do
        local cell = makeCell(1, 0, "2: H flex weight + fixed (modeY=flex)")

        local h = UIHorizontalLayout.NewHorizontalLayout(cell, 10, 30, 290, 60, 0, {
            panelTemplate = "LayoutDemoFrame",
            spacing = 6, padding = 6,
            defaultChildOpts = { modeY = "flex" },
        })
        h:SetAnchor(0.0, 0.0); h:SetRelativePoint(0.0, 0.0)

        addChild(h, 60, 0, "60")
        local w1 = addChild(h, 0, 0, "w=1", "LayoutDemoChildAlt")
        local w2 = addChild(h, 0, 0, "w=2", "LayoutDemoChildAlt")
        addChild(h, 60, 0, "60")
        h:SetChildOpts(w1, { modeX = "flex", weightX = 1 })
        h:SetChildOpts(w2, { modeX = "flex", weightX = 2 })

        makeHint(cell, [[
H layout, 4 children:
  [ 60 | w=1 | w=2 | 60 ]
  spacing = 6, padding = 6
  defaultChildOpts: modeY=flex
    (children stretch row height)

Inner = 290 - 12 - 3*6 = 260
Fixed total = 120, remainder = 140
  w=1 -> ~46    w=2 -> ~93

Orange = flex children
Blue   = fixed children]])
    end

    -- =========================================================
    -- Test 3 [top-right]: V alignV (main-axis position) start/center/end
    -- Three V samples side by side inside an outer H. Each sample
    -- has identical content but a different alignV, so the same
    -- content block sits at top / middle / bottom of its column.
    -- Also exercises nesting.
    -- =========================================================
    do
        local cell = makeCell(2, 0, "3: V alignV (start/center/end)")

        local h = UIHorizontalLayout.NewHorizontalLayout(cell, 10, 30, 290, 260, 0, {
            panelTemplate = "LayoutDemoCellBG", alpha = 0.0,
            spacing = 8,
        })
        h:SetAnchor(0.0, 0.0); h:SetRelativePoint(0.0, 0.0)

        local sampleW = 91 -- (290 - 2*8) / 3
        local function makeSample(alignV)
            local v = UIVerticalLayout.NewVerticalLayout(h, 0, 0, sampleW, 260, 0, {
                panelTemplate = "LayoutDemoFrame",
                spacing = 6, padding = 6,
                alignH = "center", alignV = alignV,
            })
            v:NewText(alignV, 0, 0, 0, "DefaultDebugText")
            addChild(v, 50, 30)
            addChild(v, 50, 30)
        end

        makeSample("start")
        makeSample("center")
        makeSample("end")

        makeHint(cell, [[
3 vertical samples (91w each, fixed)
in an outer H. Outer H spacing=8.

Same content per column:
  text label + 2 small panels.

alignV positions the block on the
main (vertical) axis:
  start  - block flush to top
  center - block vertically centered
  end    - block flush to bottom

Outer H exercises layout nesting.]])
    end

    -- =========================================================
    -- Test 4 [mid-left]: reverse on H and on V
    -- Left half: H with reverse=true. Insertion order 1..4 lays
    -- out RIGHT to LEFT, so "1" sits at the right edge.
    -- Right half: V with reverse=true. Insertion order 1..4
    -- lays out BOTTOM to TOP, so "1" sits at the bottom.
    -- =========================================================
    do
        local cell = makeCell(0, 1, "4: reverse (RTL + bottom-up)")

        local outer = UIHorizontalLayout.NewHorizontalLayout(cell, 10, 30, 290, 260, 0, {
            panelTemplate = "LayoutDemoCellBG", alpha = 0.0,
            spacing = 10,
        })
        outer:SetAnchor(0.0, 0.0); outer:SetRelativePoint(0.0, 0.0)

        -- Inner layouts get explicit sizes (see Test 3 for why).
        local halfW = 140 -- (290 - 10) / 2
        local hRev = UIHorizontalLayout.NewHorizontalLayout(outer, 0, 0, halfW, 260, 0, {
            panelTemplate = "LayoutDemoFrame",
            spacing = 4, padding = 6, reverse = true, alignV = "center",
        })
        for i = 1, 4 do addChild(hRev, 26, 26, tostring(i)) end

        local vRev = UIVerticalLayout.NewVerticalLayout(outer, 0, 0, halfW, 260, 0, {
            panelTemplate = "LayoutDemoFrame",
            spacing = 4, padding = 6, reverse = true, alignH = "center",
        })
        for i = 1, 4 do addChild(vRev, 50, 26, tostring(i)) end

        makeHint(cell, [[
Left half:  H reverse=true
  insertion order 1,2,3,4 lays out
  RIGHT to LEFT  ->  "1" at right.

Right half: V reverse=true
  insertion order 1,2,3,4 lays out
  BOTTOM to TOP  ->  "1" at bottom.

Outer H holds two equal-width
columns (140w each, spacing=10).]])
    end

    -- =========================================================
    -- Test 5 [mid-mid]: per-child cross-axis position overrides
    -- Layout default is alignH=start, but each child overrides
    -- with its own alignH (start / center / end). The last child
    -- sets modeX=flex to stretch its width to the container.
    -- =========================================================
    do
        local cell = makeCell(1, 1, "5: per-child alignH overrides + modeX=flex")

        local v = UIVerticalLayout.NewVerticalLayout(cell, 10, 30, 290, 260, 0, {
            panelTemplate = "LayoutDemoFrame",
            spacing = 8, padding = 8, alignH = "start",
        })
        v:SetAnchor(0.0, 0.0); v:SetRelativePoint(0.0, 0.0)

        local cs = addChild(v, 80, 30, "start");                          v:SetChildOpts(cs,  { alignH = "start"  })
        local cc = addChild(v, 80, 30, "center");                         v:SetChildOpts(cc,  { alignH = "center" })
        local ce = addChild(v, 80, 30, "end");                            v:SetChildOpts(ce,  { alignH = "end"    })
        local css = addChild(v, 80, 30, "stretch", "LayoutDemoChildAlt"); v:SetChildOpts(css, { modeX = "flex"    })

        makeHint(cell, [[
V alignH=start (default); each child
overrides cross-axis position:

  alignH=start   -> flush left
  alignH=center  -> X-centered
  alignH=end     -> flush right
  modeX=flex     -> width = inner
                    (orange to highlight)

Stretched child's intrinsic 80w is
overridden by the layout to 274w.]])
    end

    -- =========================================================
    -- Test 6 [mid-right]: Grid columns + per-cell cellAlignH
    -- 4-column grid, 8 children. Top row uses cellAlignH=start
    -- (children flush to left of each cell). Bottom row uses
    -- cellAlignH=stretch (children resized to cell width).
    -- =========================================================
    do
        local cell = makeCell(2, 1, "6: Grid columns + per-cell cellAlignH")

        local grid = UIGridLayout.NewGridLayout(cell, 10, 30, 290, 260, 0, {
            panelTemplate = "LayoutDemoFrame",
            columns = 4, spacing = 6, padding = 8,
            cellAlign = "center",
        })
        grid:SetAnchor(0.0, 0.0); grid:SetRelativePoint(0.0, 0.0)

        for i = 1, 4 do
            local p = addChild(grid, 30, 30, tostring(i))
            grid:SetChildOpts(p, { cellAlignH = "start" })
        end
        for i = 5, 8 do
            local p = addChild(grid, 30, 30, tostring(i), "LayoutDemoChildAlt")
            grid:SetChildOpts(p, { cellAlignH = "stretch" })
        end

        makeHint(cell, [[
Grid: columns=4, 8 children.
Layout default cellAlign=center.

Row 1 (1..4): cellAlignH=start
  30w panel pushed to LEFT of cell.

Row 2 (5..8): cellAlignH=stretch
  panel resized to cell width
  (orange to highlight).

Y axis still uses default center
on both rows.]])
    end

    -- =========================================================
    -- Test 7 [bot-left]: Grid columnMajor + asymmetric spacing
    -- rows=3 + direction=columnMajor means fill goes down then
    -- right: column 0 is 1,2,3 top-bottom; column 1 is 4,5,6.
    -- spacingX=24 vs spacingY=4 makes the column gap visibly
    -- larger than the row gap.
    -- =========================================================
    do
        local cell = makeCell(0, 2, "7: Grid columnMajor + asym spacing")

        local grid = UIGridLayout.NewGridLayout(cell, 10, 30, 290, 260, 0, {
            panelTemplate = "LayoutDemoFrame",
            rows = 3, direction = "columnMajor",
            spacingX = 24, spacingY = 4,
            padding = 8,
            cellAlign = "center",
        })
        grid:SetAnchor(0.0, 0.0); grid:SetRelativePoint(0.0, 0.0)

        for i = 1, 6 do addChild(grid, 60, 60, tostring(i)) end

        makeHint(cell, [[
Grid: rows=3, direction=columnMajor.
6 children fill DOWN then RIGHT:
  col0 (top-bottom) = 1, 2, 3
  col1 (top-bottom) = 4, 5, 6

spacingX=24, spacingY=4
  -> the gap BETWEEN COLUMNS is
     much wider than the gap
     between rows (visually clear).

Children are 60x60 in cells of
~125x82 (cellAlign=center).]])
    end

    -- =========================================================
    -- Test 8 [bot-mid]: Grid cellSize + gridAlignH/V + cellAlign=stretch
    -- Explicit cellSize=50x50, 6 children in 3 columns. The
    -- 162x106 group is much smaller than the inner area, so
    -- gridAlignH / gridAlignV center the whole group.
    -- cellAlign=stretch forces children to fill each cell.
    -- =========================================================
    do
        local cell = makeCell(1, 2, "8: Grid cellSize + gridAlignH/V=center")

        local grid = UIGridLayout.NewGridLayout(cell, 10, 30, 290, 260, 0, {
            panelTemplate = "LayoutDemoFrame",
            columns = 3, cellSizeX = 50, cellSizeY = 50,
            spacing = 6, padding = 12,
            gridAlignH = "center", gridAlignV = "center",
            cellAlign = "stretch",
        })
        grid:SetAnchor(0.0, 0.0); grid:SetRelativePoint(0.0, 0.0)

        for i = 1, 6 do addChild(grid, 0, 0, tostring(i)) end

        makeHint(cell, [[
Grid: explicit cellSizeX/Y=50.
columns=3, 6 children, padding=12.

Group size = 3*50 + 2*6 = 162 wide
            2*50 + 1*6 = 106 tall
Inner area = 266 x 236 (lots extra).

gridAlignH=center
  -> group X-centered in container.
gridAlignV=center
  -> group Y-centered in container.

cellAlign=stretch fills each cell.]])
    end

    -- =========================================================
    -- Test 9 [bot-right]: NewWidget spacer (flex) + skip
    -- Header / subheader at top, footer at bottom, separated
    -- by an invisible NewWidget spacer with modeY=flex weightY=1
    -- that consumes all remaining vertical space. A skipped child
    -- between spacer and footer is removed from layout entirely.
    -- defaultChildOpts.modeX=flex stretches each child's width.
    -- =========================================================
    do
        local cell = makeCell(2, 2, "9: NewWidget flex spacer + skip")

        local v = UIVerticalLayout.NewVerticalLayout(cell, 10, 30, 290, 260, 0, {
            panelTemplate = "LayoutDemoFrame",
            spacing = 6, padding = 8,
            defaultChildOpts = { modeX = "flex" },
        })
        v:SetAnchor(0.0, 0.0); v:SetRelativePoint(0.0, 0.0)

        addChild(v, 0, 30, "header")
        addChild(v, 0, 30, "subheader")

        local spacer = v:NewWidget(0, 0, 0)
        v:SetChildOpts(spacer, { modeY = "flex", weightY = 1 })

        local skipped = addChild(v, 0, 30, "SKIPPED", "LayoutDemoChildAlt")
        v:SetChildOpts(skipped, { skip = true })

        addChild(v, 0, 30, "footer")

        makeHint(cell, [[
V padding=8, spacing=6.
defaultChildOpts: modeX=flex
  (each child fills width)

Children in insertion order:
  header     panel (visible)
  subheader  panel (visible)
  spacer     NewWidget flex weight=1
             (no size, fills slot)
  SKIPPED    panel skip=true
             (orange, INVISIBLE,
              removed from layout)
  footer     panel (visible)

Result: header and subheader at top,
footer pushed to bottom by spacer's
expanded slot. Skipped contributes
zero space.]])
    end
end

local function CreateClippingDemo()
    local canvas = UI.GetCanvas("ClippingDemo", 0, 0, 1920, 1080);

    -- Create a panel
    local clippingParent = canvas:NewPanel(215, 0, 400, 400, 0, "DebugRed");
    clippingParent:SetAnchor(0.5, 0.5);
    clippingParent:SetRelativePoint(0.5, 0.5);

    clippingParent:SetClipChildren(true);
    clippingParent:SetClipRect(0, 0, 1, 1);
    clippingParent:SetClipMaskTexture("texture/interface/masks/circlemaskscalable.dds");

    --clippingParent:SetClipMaskTexture(nil);
    --clippingParent:SetClipChildren(false);

    local clippingChild = clippingParent:NewPanel(0, 0, 300, 400, 0, "DebugBlue");
    clippingChild:SetAnchor(0.5, 0.0);
    clippingChild:SetRelativePoint(0.5, 0.5);

    -- Create a text
    local clippingText = clippingParent:NewText("Test text", 100, 30, 0, "DefaultButtonText");
    --clippingText:SetClipRect(0, 0, 1, 0.3);
    --clippingText:SetClipMaskTexture("texture/interface/masks/circlemaskscalable.dds");
end

local function OnGameLoaded(eventID : number, data : any)
    local stack = UIStack.Get("Menu")

    --CreateClippingDemo();
    --Demo();
    --CreateSortingDemo();
    --CreateLayoutsDemo();
    --CreateGameMenu(stack);
    --CreateOptionsMenu(stack);

    --UILoadingScreen.SetLoadingScreen(nil)
    --print('Hello from Demo.luau');
end
RegisterEvent(GameEvent.Loaded, OnGameLoaded)