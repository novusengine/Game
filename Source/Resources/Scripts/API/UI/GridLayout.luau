--[[
GridLayout — uniform-cell grid. Pick one shape mode:
    columns = N        explicit column count, rows derived from child count
    rows = N           explicit row count, columns derived
    cellSizeX/Y = N    auto: derive cols/rows from container size + cell size

Children fill in `direction` order ("rowMajor" default; "columnMajor"). Per-cell
alignment (cellAlignH / cellAlignV) places children smaller than the cell;
"stretch" resizes the child to the cell.

Container sizing modes (widthMode / heightMode):
    fixed   — declared sizeX/sizeY at construction; ignores content.
    shrink  — sized to the cell-grid extent; ignores parent.
    stretch — NOT IMPLEMENTED (errors at construction). Reserved for future
              parent-driven stretch.
    auto    — max(grid_extent, current_panel_size).

Same proxy + Invalidate + pre-render-driven Refresh model as LinearLayout, sharing
the LayoutCommon module's wrap and base-layout machinery.

Coordinate system: anchor (0,0)=bottom-left, (1,1)=top-right. SetPos +Y is up. Grid
uses anchor (0,1) consistently — children are placed relative to top-left.
]]

local LayoutCommon = require("@src/API/UI/LayoutCommon")

export type SizeMode = "fixed" | "shrink" | "stretch" | "auto"
export type CellAlignKind = "start" | "center" | "end" | "stretch"
export type GridAlignKind = "start" | "center" | "end"

export type GridChildOpts =
{
    skip : boolean?,
    -- Per-child cell-alignment overrides.
    cellAlignH : CellAlignKind?,
    cellAlignV : CellAlignKind?,
    aspectRatio : number?,                                      -- shape-lock (cellAlignH/V = stretch only)
}

export type GridLayoutOpts =
{
    spacing : number?, spacingX : number?, spacingY : number?,
    padding : number?,
    paddingTop : number?, paddingBottom : number?, paddingLeft : number?, paddingRight : number?,

    columns : number?, rows : number?,
    cellSizeX : number?, cellSizeY : number?,

    cellAlign  : CellAlignKind?,                                -- default "center" (both axes)
    cellAlignH : CellAlignKind?,                                -- horizontal cell alignment
    cellAlignV : CellAlignKind?,                                -- vertical cell alignment

    -- Where the entire cell group sits within the container, on each axis.
    -- Useful when explicit cellSize fits content smaller than the container's inner area.
    gridAlignH : GridAlignKind?,                                -- default "start"
    gridAlignV : GridAlignKind?,                                -- default "start"

    direction : ("rowMajor" | "columnMajor")?,                  -- default "rowMajor"

    widthMode  : SizeMode?,                                     -- default "fixed"
    heightMode : SizeMode?,                                     -- default "fixed"

    panelTemplate : string?,
    alpha : number?,
}

local gridLayoutAPI = {}

local function trySetWidth(raw, w)
    local fn = raw.SetWidth
    if type(fn) == "function" then fn(raw, w) end
end
local function trySetHeight(raw, h)
    local fn = raw.SetHeight
    if type(fn) == "function" then fn(raw, h) end
end

local function PlaceChild(item, co, originX, originY, cellW, cellH, childW, childH, alignH, alignV)
    -- originX/Y is the cell's top-left in "distance from container's top-left" (Y positive = down).
    local raw = item.raw
    local resized = false
    local opts = co or {}

    local offX
    if alignH == "start" then offX = 0
    elseif alignH == "end" then offX = cellW - childW
    elseif alignH == "stretch" then
        if opts.aspectRatio and opts.aspectRatio > 0 then
            -- Aspect-locked: derive height from width if cellAlignH=stretch.
            local newH = math.floor(cellW / opts.aspectRatio)
            trySetWidth(raw, cellW)
            trySetHeight(raw, newH)
            childW = cellW
            childH = newH
        else
            trySetWidth(raw, cellW)
            childW = cellW
        end
        offX = 0
        resized = true
    else offX = math.floor((cellW - childW) / 2)   -- "center" default
    end

    local offY
    if alignV == "start" then offY = 0
    elseif alignV == "end" then offY = cellH - childH
    elseif alignV == "stretch" then
        if opts.aspectRatio and opts.aspectRatio > 0 and alignH ~= "stretch" then
            local newW = math.floor(cellH * opts.aspectRatio)
            trySetWidth(raw, newW)
            trySetHeight(raw, cellH)
            childW = newW
            childH = cellH
        else
            trySetHeight(raw, cellH)
            childH = cellH
        end
        offY = 0
        resized = true
    else offY = math.floor((cellH - childH) / 2)
    end

    -- Stretch raw-resized a nested layout's panel; re-position its grandchildren
    -- against the new size via RefreshLocal (skips the upward Invalidate propagation).
    if resized and item.proxy and item.proxy._innerLayout then
        item.proxy._innerLayout:RefreshLocal()
    end

    raw:SetAnchor(0.0, 1.0)
    raw:SetRelativePoint(0.0, 1.0)
    raw:SetPos(originX + offX, -(originY + offY))
end

local function DoRefresh(self)
    self._dirty = false
    local opts = self._opts

    local widthMode  : SizeMode = opts.widthMode  or "fixed"
    local heightMode : SizeMode = opts.heightMode or "fixed"
    if widthMode == "stretch" or heightMode == "stretch" then
        -- Surface the unimplemented mode rather than silently behaving like "fixed".
        error("GridLayout: 'stretch' container size mode is not implemented; use 'fixed', 'shrink', or 'auto'", 2)
    end

    local pad = opts.padding or 0
    local pt = opts.paddingTop    or pad
    local pb = opts.paddingBottom or pad
    local pl = opts.paddingLeft   or pad
    local pr = opts.paddingRight  or pad

    local spacing  = opts.spacing  or 0
    local spacingX = opts.spacingX or spacing
    local spacingY = opts.spacingY or spacing

    local defaults = nil  -- Grid has no defaultChildOpts today; passing nil keeps MergeOpts a no-op.

    -- Filter active items, refresh inner layouts, auto-prune destroyed.
    local activeItems = {}
    local toPrune = nil
    for i, item in self._items do
        local ok, visible = pcall(function() return item.raw:IsVisible() end)
        if not ok then
            toPrune = toPrune or {}
            table.insert(toPrune, i)
            continue
        end

        local co = item._mergedOpts
        if co == nil then
            co = LayoutCommon.MergeOpts(defaults, item.opts)
            item._mergedOpts = co
        end

        if not visible then continue end
        if co and co.skip then continue end

        if item.proxy and item.proxy._innerLayout then
            item.proxy._innerLayout:Refresh()
        end
        table.insert(activeItems, { item = item, co = co })
    end

    if toPrune then
        for j = #toPrune, 1, -1 do
            local idx = toPrune[j]
            local proxy = self._items[idx].proxy
            self._itemByProxy[proxy] = nil
            table.remove(self._items, idx)
        end
        for i = toPrune[1], #self._items do
            self._itemByProxy[self._items[i].proxy] = i
        end
    end
    local n = #activeItems

    local containerW, containerH = self.panel:GetSize()
    local innerW = containerW - pl - pr
    local innerH = containerH - pt - pb

    -- Determine shape (cols, rows).
    local cols, rows
    if opts.columns then
        cols = math.max(1, opts.columns)
        rows = math.max(1, math.ceil(n / cols))
    elseif opts.rows then
        rows = math.max(1, opts.rows)
        cols = math.max(1, math.ceil(n / rows))
    elseif opts.cellSizeX and opts.cellSizeY then
        cols = math.max(1, math.floor((innerW + spacingX) / (opts.cellSizeX + spacingX)))
        rows = math.max(1, math.ceil(n / cols))
    else
        -- Fallback: square-ish from sqrt(N).
        cols = math.max(1, math.ceil(math.sqrt(math.max(1, n))))
        rows = math.max(1, math.ceil(n / cols))
    end

    -- Cell size: explicit overrides; otherwise distribute container space.
    local cellW = opts.cellSizeX or math.floor((innerW - spacingX * (cols - 1)) / cols)
    local cellH = opts.cellSizeY or math.floor((innerH - spacingY * (rows - 1)) / rows)

    -- Container size resolve (shrink/auto) — apply BEFORE per-child placement so
    -- cell grouping uses the resolved container extents.
    local gridW = pl + pr + (if cols > 0 then cols * cellW + (cols - 1) * spacingX else 0)
    local gridH = pt + pb + (if rows > 0 then rows * cellH + (rows - 1) * spacingY else 0)
    local newW, newH = containerW, containerH
    if widthMode == "shrink" then newW = gridW
    elseif widthMode == "auto" and gridW > containerW then newW = gridW end
    if heightMode == "shrink" then newH = gridH
    elseif heightMode == "auto" and gridH > containerH then newH = gridH end
    if math.abs(newW - containerW) > 0.5 or math.abs(newH - containerH) > 0.5 then
        self.panel:SetSize(newW, newH)
        containerW, containerH = newW, newH
        innerW = containerW - pl - pr
        innerH = containerH - pt - pb
        -- Re-derive cellW/H if they were size-driven (no explicit cellSize).
        if not opts.cellSizeX then cellW = math.floor((innerW - spacingX * (cols - 1)) / cols) end
        if not opts.cellSizeY then cellH = math.floor((innerH - spacingY * (rows - 1)) / rows) end
    end

    local alignH = opts.cellAlignH or opts.cellAlign or "center"
    local alignV = opts.cellAlignV or opts.cellAlign or "center"
    local rowMajor = (opts.direction or "rowMajor") == "rowMajor"

    -- gridAlignH / gridAlignV: shift the entire cell group within the container
    -- when the group is smaller than the inner area.
    local groupW = cols * cellW + (cols - 1) * spacingX
    local groupH = rows * cellH + (rows - 1) * spacingY
    local extraX = math.max(0, innerW - groupW)
    local extraY = math.max(0, innerH - groupH)
    local groupOffsetX = if opts.gridAlignH == "center" then math.floor(extraX / 2)
                         elseif opts.gridAlignH == "end" then extraX
                         else 0
    local groupOffsetY = if opts.gridAlignV == "center" then math.floor(extraY / 2)
                         elseif opts.gridAlignV == "end" then extraY
                         else 0

    for i, entry in activeItems do
        local item, co = entry.item, entry.co
        local zeroIdx = i - 1
        local col, row
        if rowMajor then
            col = zeroIdx % cols
            row = math.floor(zeroIdx / cols)
        else
            row = zeroIdx % rows
            col = math.floor(zeroIdx / rows)
        end
        local originX = pl + groupOffsetX + col * (cellW + spacingX)
        local originY = pt + groupOffsetY + row * (cellH + spacingY)

        local childW, childH = item.raw:GetSize()
        local thisAlignH = (co and co.cellAlignH) or alignH
        local thisAlignV = (co and co.cellAlignV) or alignV
        PlaceChild(item, co, originX, originY, cellW, cellH, childW, childH, thisAlignH, thisAlignV)
    end

    self._measuredWidth  = gridW
    self._measuredHeight = gridH
end

function gridLayoutAPI.NewGridLayout(parent, posX, posY, sizeX, sizeY, layer, opts)
    opts = opts or {}

    local panelTemplate = opts.panelTemplate or "DialogBox"
    local panel = parent:NewPanel(posX, posY, sizeX, sizeY, layer or 0, panelTemplate)
    if opts.alpha ~= nil then panel:SetAlpha(opts.alpha) end

    local layoutTable = LayoutCommon.MakeBaseLayout(panel, opts, DoRefresh)
    layoutTable._measuredWidth = 0
    layoutTable._measuredHeight = 0

    function layoutTable.GetMeasuredHeight(sf) sf:Refresh(); return sf._measuredHeight end
    function layoutTable.GetMeasuredWidth(sf)  sf:Refresh(); return sf._measuredWidth  end

    if type(panel) == "table" and panel._raw ~= nil and panel._layout ~= nil then
        layoutTable._parentLayout = panel._layout
        panel._innerLayout = layoutTable
    end

    return layoutTable
end

return gridLayoutAPI
