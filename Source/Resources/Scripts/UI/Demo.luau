local UIStack = require("@src/API/UI/UIStack")
local UIButton = require("@src/API/UI/Button")
local UILoadingScreen = require("@src/API/UI/LoadingScreen")

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
    local canvasH = 1080
    local topMargin = 28 -- room for title bar above the cells
    local cellW, cellH = 618, 330
    local padX, padY = 16, 16

    local function makeCell(col, row, label)
        local x = padX + col * (cellW + padX)
        local y = (canvasH - topMargin) - (cellH + padY) * (row + 1)

        -- Cell background frame
        local cell = canvas:NewPanel(x, y, cellW, cellH, 0, "SortDemoCellBG")
        cell:SetAnchor(0.0, 0.0)
        cell:SetRelativePoint(0.0, 0.0)

        -- Cell title (top-left corner of cell)
        local titleWidget = cell:NewText(label, 10, -8, 0, "DefaultButtonText")
        titleWidget:SetAnchor(0.0, 1.0)
        titleWidget:SetRelativePoint(0.0, 1.0)

        return cell
    end

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

local function CreateClippingDemo()
    local canvas = UI.GetCanvas("ClippingDemo", 0, 0, 1920, 1080);

    -- Create a panel
    local clippingParent = canvas:NewPanel(215, 0, 400, 400, 0, "DebugRed");
    clippingParent:SetAnchor(0.5, 0.5);
    clippingParent:SetRelativePoint(0.5, 0.5);

    clippingParent:SetClipChildren(true);
    clippingParent:SetClipRect(0, 0, 1, 1);
    clippingParent:SetClipMaskTexture("Data/Texture/interface/masks/circlemaskscalable.dds");

    --clippingParent:SetClipMaskTexture(nil);
    --clippingParent:SetClipChildren(false);

    local clippingChild = clippingParent:NewPanel(0, 0, 300, 400, 0, "DebugBlue");
    clippingChild:SetAnchor(0.5, 0.0);
    clippingChild:SetRelativePoint(0.5, 0.5);

    -- Create a text
    local clippingText = clippingParent:NewText("Test text", 100, 30, 0, "DefaultButtonText");
    --clippingText:SetClipRect(0, 0, 1, 0.3);
    --clippingText:SetClipMaskTexture("Data/Texture/interface/masks/circlemaskscalable.dds");
end

local function OnGameLoaded(eventID : number, data : any)
    local stack = UIStack.Get("Menu")

    --CreateClippingDemo();
    --Demo();
    --CreateSortingDemo();
    --CreateGameMenu(stack);
    --CreateOptionsMenu(stack);

    --UILoadingScreen.SetLoadingScreen(nil)
    --print('Hello from Demo.luau');
end
RegisterEvent(GameEvent.Loaded, OnGameLoaded)