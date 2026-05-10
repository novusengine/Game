--[[
LinearLayout — shared implementation backing VerticalLayout and HorizontalLayout.

Owns positioning of its direct children along a single axis (main = y for V, x for H),
with axis-absolute alignment (alignH/alignV), padding, spacing, fixed-or-flex per-child
sizing with weight + min/max + opt-in aspectRatio, and bottomUp/rightToLeft via `reverse`.

Container sizing modes (per axis: widthMode / heightMode):
    fixed   — declared sizeX/sizeY at construction; ignores content and parent.
    shrink  — sized to content's intrinsic min; ignores parent allowance.
    stretch — NOT IMPLEMENTED (errors at construction). Reserved for future
              parent-driven stretch once we thread an "available space" parameter.
    auto    — max(content_intrinsic, current_panel_size). Fills, grows past if content demands.

Child sizing modes (per axis: modeX / modeY):
    fixed — sizeX/sizeY (or intrinsic if omitted). weight/min/max ignored.
    flex  — participates in slack distribution by weightX/Y; basis = sizeX/sizeY (or 0);
            clamped by minX/maxX/minY/maxY. On the cross axis there's no sibling
            competition so weight is silently inert and `flex` means "stretch to fill".

Cycle rule: container shrink-on-an-axis with a same-axis flex child collapses the
child to its intrinsic size and warns. Cross-axis flex inside a main-axis shrink
container is fine — the rule is per-axis.

aspectRatio (child opt-in): when set, cross = main / aspectRatio, clamped by cross
min/max. Overrides crossMode for that child.

defaultChildOpts (container-level): a table of per-child opts merged into each
child's opts when the child does not override them. Useful for "default cross-fill"
patterns: e.g. `defaultChildOpts = { modeX = "flex" }` on a V layout makes every
child stretch its width.

Children created via layout:NewPanel/NewText/NewWidget are wrapped in a Lua proxy
(see LayoutCommon.WrapChild) that forwards everything to the raw widget userdata via
__index but intercepts size-affecting setters to call layout:Invalidate(). Refresh is
event-driven via RegisterLayoutRefresh on the container's panel — the C++ pre-render
hook fires a registered closure for any panel tagged DirtyLayoutTag.

Coordinate system: anchor/relativePoint use (0,0)=bottom-left, (1,1)=top-right.
SetPos offsets are in screen-pixel units where +X is right and +Y is UP. So a
top-anchored child needs SetPosY(-N) to move down by N pixels.
]]

local LayoutCommon = require("@src/API/UI/LayoutCommon")

export type SizeMode = "fixed" | "shrink" | "stretch" | "auto"
export type ChildMode = "fixed" | "flex"
export type AlignKind = "start" | "center" | "end"

export type LinearChildOpts =
{
    modeX : ChildMode?,
    modeY : ChildMode?,
    sizeX : number?,
    sizeY : number?,
    weightX : number?,
    weightY : number?,
    minX : number?, maxX : number?,
    minY : number?, maxY : number?,

    aspectRatio : number?,                                      -- cross = main / aspectRatio (overrides crossMode)
    alignH : AlignKind?,                                        -- per-child cross-axis position override (V layout only)
    alignV : AlignKind?,                                        -- per-child cross-axis position override (H layout only)
    skip : boolean?,
}

export type LinearLayoutOpts =
{
    axis : ("x" | "y")?,
    spacing : number?,
    padding : number?,
    paddingTop : number?, paddingBottom : number?, paddingLeft : number?, paddingRight : number?,
    reverse : boolean?,
    panelTemplate : string?,
    alpha : number?,

    widthMode : SizeMode?,                                      -- default "fixed"
    heightMode : SizeMode?,                                     -- default "fixed"

    alignH : AlignKind?,                                        -- horizontal positioning (default "start")
    alignV : AlignKind?,                                        -- vertical positioning (default "start")

    defaultChildOpts : LinearChildOpts?,                        -- per-child opt defaults; child opts override
}

local linearLayoutAPI = {}

-- Run the layout pass. Idempotent given the same inputs; called via the
-- C++ pre-render hook (RegisterLayoutRefresh) and via outer-layout RefreshLocal.
local function DoRefresh(self)
    self._dirty = false

    local opts = self._opts
    local mainAxisIsY = (opts.axis == "y")
    local reverse = opts.reverse or false
    local spacing = opts.spacing or 0

    local pad = opts.padding or 0
    local pt = opts.paddingTop    or pad
    local pb = opts.paddingBottom or pad
    local pl = opts.paddingLeft   or pad
    local pr = opts.paddingRight  or pad

    -- Leading/trailing pad along main axis (depends on direction).
    local leadPad, trailPad
    if mainAxisIsY then
        leadPad  = if reverse then pb else pt
        trailPad = if reverse then pt else pb
    else
        leadPad  = if reverse then pr else pl
        trailPad = if reverse then pl else pr
    end
    local crossLeadPad  = if mainAxisIsY then pl else pt
    local crossTrailPad = if mainAxisIsY then pr else pb

    -- Translate axis-absolute opts to main/cross.
    local mainMode  : SizeMode = if mainAxisIsY then (opts.heightMode or "fixed") else (opts.widthMode  or "fixed")
    local crossMode : SizeMode = if mainAxisIsY then (opts.widthMode  or "fixed") else (opts.heightMode or "fixed")
    if mainMode == "stretch" or crossMode == "stretch" then
        -- Surface the unimplemented mode rather than silently behaving like "fixed".
        error("LinearLayout: 'stretch' container size mode is not implemented; use 'fixed', 'shrink', or 'auto'", 2)
    end
    local mainAlign  : AlignKind = (if mainAxisIsY then opts.alignV else opts.alignH) or "start"
    local crossAlign : AlignKind = (if mainAxisIsY then opts.alignH else opts.alignV) or "start"

    local defaults = opts.defaultChildOpts

    -- Phase A: enumerate active items + measure intrinsics.
    -- If a child is itself a layout container, refresh it first so we measure its
    -- post-refresh size. Invisible/disabled widgets are skipped.
    -- A bare Widget (created via :NewWidget) has no GetSize/SetWidth/SetHeight; treat its
    -- intrinsic size as 0 and skip size mutations later.
    -- Auto-prune: if `raw:IsVisible()` errors (destroyed widget), drop the item from
    -- _items / _itemByProxy after the pass. Explicit `:Remove` is preferred but this
    -- keeps a forgotten DestroyWidget from crashing the layout.
    local activeItems = {}
    local toPrune = nil   -- list of indices into self._items to remove after iteration
    for i, item in self._items do
        local ok, visible = pcall(function() return item.raw:IsVisible() end)
        if not ok then
            toPrune = toPrune or {}
            table.insert(toPrune, i)
            continue
        end

        -- Lazy-merge defaultChildOpts under per-child opts; cache on the item so we don't
        -- allocate a fresh table each refresh. SetChildOpts / Add nil out _mergedOpts.
        local co = item._mergedOpts
        if co == nil then
            co = LayoutCommon.MergeOpts(defaults, item.opts)
            item._mergedOpts = co
        end

        if co and co.skip then continue end
        if not visible then continue end

        if item.proxy and item.proxy._innerLayout then
            item.proxy._innerLayout:Refresh()
        end

        local hasSize = (type(item.raw.GetSize) == "function")
        local ix, iy = 0, 0
        if hasSize then ix, iy = item.raw:GetSize() end

        table.insert(activeItems, {
            raw = item.raw, proxy = item.proxy, opts = co,
            intrinsicX = ix, intrinsicY = iy, hasSize = hasSize,
        })
    end

    if toPrune then
        -- Remove from highest index down so shifts don't invalidate the lower indices.
        for j = #toPrune, 1, -1 do
            local idx = toPrune[j]
            local proxy = self._items[idx].proxy
            self._itemByProxy[proxy] = nil
            table.remove(self._items, idx)
        end
        -- Reindex everything after the lowest prune point.
        for i = toPrune[1], #self._items do
            self._itemByProxy[self._items[i].proxy] = i
        end
    end

    -- Phase B: container size resolve (shrink/auto write back to self.panel).
    local intrinsicMainSum = 0
    local maxIntrinsicCross = 0
    for _, it in activeItems do
        local im = if mainAxisIsY then it.intrinsicY else it.intrinsicX
        local ic = if mainAxisIsY then it.intrinsicX else it.intrinsicY
        intrinsicMainSum += im
        if ic > maxIntrinsicCross then maxIntrinsicCross = ic end
    end
    local mainGap = if #activeItems > 1 then spacing * (#activeItems - 1) else 0
    local intrinsicMainExtent  = leadPad + trailPad + intrinsicMainSum + mainGap
    local intrinsicCrossExtent = crossLeadPad + crossTrailPad + maxIntrinsicCross

    local containerW, containerH = self.panel:GetSize()
    local containerMain  = if mainAxisIsY then containerH else containerW
    local containerCross = if mainAxisIsY then containerW else containerH

    if mainMode == "shrink" then
        containerMain = intrinsicMainExtent
    elseif mainMode == "auto" then
        if intrinsicMainExtent > containerMain then containerMain = intrinsicMainExtent end
    end

    if crossMode == "shrink" then
        containerCross = intrinsicCrossExtent
    elseif crossMode == "auto" then
        if intrinsicCrossExtent > containerCross then containerCross = intrinsicCrossExtent end
    end

    -- Persist resolved container size to the panel widget. Calling Set on the panel
    -- (which may be a proxy if our parent is a layout) propagates Invalidate upward;
    -- the parent's Invalidate guard early-outs while the parent itself is mid-refresh.
    local newW = if mainAxisIsY then containerCross else containerMain
    local newH = if mainAxisIsY then containerMain  else containerCross
    if math.abs(newW - containerW) > 0.5 or math.abs(newH - containerH) > 0.5 then
        self.panel:SetSize(newW, newH)
    end

    local innerMain  = containerMain - leadPad - trailPad
    local innerCross = containerCross - crossLeadPad - crossTrailPad

    -- Cycle rule: shrink-on-main + flex children → coerce flex to fixed (intrinsic).
    local containerShrinksOnMain = (mainMode == "shrink")

    -- Phase C: resolve per-child main-axis sizes.
    local fixedSum = 0
    local flexItems = {}
    local warnedShrinkFlex = false
    for _, it in activeItems do
        local co = it.opts or {}
        local modeMain : ChildMode = (if mainAxisIsY then co.modeY else co.modeX) or "fixed"
        local sizeMain = if mainAxisIsY then co.sizeY else co.sizeX
        local intrinsicMain = if mainAxisIsY then it.intrinsicY else it.intrinsicX

        if containerShrinksOnMain and modeMain == "flex" then
            if not warnedShrinkFlex then
                warn("LinearLayout: flex children require a bounded main axis; container is shrink, falling back to intrinsic")
                warnedShrinkFlex = true
            end
            modeMain = "fixed"
        end

        if modeMain == "flex" then
            it.modeMain = "flex"
            it.basisMain = sizeMain or 0
            it.weight = (if mainAxisIsY then co.weightY else co.weightX) or 1
            it.minMain = if mainAxisIsY then co.minY else co.minX
            it.maxMain = if mainAxisIsY then co.maxY else co.maxX
            it.resolvedMain = it.basisMain
            table.insert(flexItems, it)
        else
            it.modeMain = "fixed"
            it.resolvedMain = sizeMain or intrinsicMain
            fixedSum += it.resolvedMain
        end
    end

    -- Distribute slack to flex children — weight ratio with CSS-flex-style clamp loop.
    if #flexItems > 0 then
        local availableMain = innerMain - fixedSum - mainGap
        local sumBasis = 0
        local sumWeight = 0
        for _, it in flexItems do
            sumBasis += it.basisMain
            sumWeight += it.weight
        end
        local slack = availableMain - sumBasis
        for _, it in flexItems do
            local share = if sumWeight > 0 then slack * it.weight / sumWeight else 0
            it.resolvedMain = it.basisMain + share
            it._unclamped = true
        end

        -- Iterate: each pass clamps min/max violations and redistributes the
        -- freed/owed space to remaining unclamped flex items by weight.
        for iter = 1, #flexItems + 2 do
            local toRedistribute = 0
            local anyClampedThisPass = false
            for _, it in flexItems do
                if it._unclamped then
                    if it.minMain and it.resolvedMain < it.minMain then
                        toRedistribute -= (it.minMain - it.resolvedMain)
                        it.resolvedMain = it.minMain
                        it._unclamped = false
                        anyClampedThisPass = true
                    elseif it.maxMain and it.resolvedMain > it.maxMain then
                        toRedistribute += (it.resolvedMain - it.maxMain)
                        it.resolvedMain = it.maxMain
                        it._unclamped = false
                        anyClampedThisPass = true
                    end
                end
            end
            if not anyClampedThisPass then break end
            local sumW = 0
            for _, it in flexItems do
                if it._unclamped then sumW += it.weight end
            end
            if sumW <= 0 then break end
            for _, it in flexItems do
                if it._unclamped then
                    it.resolvedMain += toRedistribute * it.weight / sumW
                end
            end
        end

        for _, it in flexItems do
            it.resolvedMain = math.floor(math.max(0, it.resolvedMain))
            it._unclamped = nil
        end
    end

    -- Phase D: resolve per-child cross-axis sizes.
    for _, it in activeItems do
        local co = it.opts or {}
        local crossModeChild : ChildMode = (if mainAxisIsY then co.modeX else co.modeY) or "fixed"
        local sizeCross = if mainAxisIsY then co.sizeX else co.sizeY
        local intrinsicCross = if mainAxisIsY then it.intrinsicX else it.intrinsicY
        local minCross = if mainAxisIsY then co.minX else co.minY
        local maxCross = if mainAxisIsY then co.maxX else co.maxY

        local crossSize
        if co.aspectRatio and co.aspectRatio > 0 then
            crossSize = it.resolvedMain / co.aspectRatio
        elseif crossModeChild == "flex" then
            crossSize = innerCross
        else
            crossSize = sizeCross or intrinsicCross
        end

        if minCross and crossSize < minCross then crossSize = minCross end
        if maxCross and crossSize > maxCross then crossSize = maxCross end
        it.resolvedCross = math.floor(math.max(0, crossSize))
        it.crossModeResolved = crossModeChild
    end

    -- Apply size mutations to raw children (only when changed, to avoid extra
    -- DirtyWidgetData churn). We mutate the RAW widget — proxy interception
    -- targets the layout itself, not the raw, so this doesn't re-Invalidate.
    for _, it in activeItems do
        if it.hasSize then
            -- Only resize an axis when the resolved size actually differs from
            -- intrinsic — avoids redundant DirtyWidgetData churn and avoids
            -- calling SetWidth/SetHeight on widgets that don't support them
            -- (e.g. Text exposes GetSize but not the size setters; we leave
            -- such widgets at their intrinsic size).
            local intrinsicMain  = if mainAxisIsY then it.intrinsicY else it.intrinsicX
            local intrinsicCross = if mainAxisIsY then it.intrinsicX else it.intrinsicY
            local mainSetterName  = if mainAxisIsY then "SetHeight" else "SetWidth"
            local crossSetterName = if mainAxisIsY then "SetWidth"  else "SetHeight"

            if math.abs(it.resolvedMain - intrinsicMain) > 0.5 then
                local setter = it.raw[mainSetterName]
                if type(setter) == "function" then setter(it.raw, it.resolvedMain) end
            end
            if math.abs(it.resolvedCross - intrinsicCross) > 0.5 then
                local setter = it.raw[crossSetterName]
                if type(setter) == "function" then setter(it.raw, it.resolvedCross) end
            end
        end

        -- If this child is itself a layout, the raw resize bypassed its
        -- layout-aware proxy and didn't invalidate it. Re-position its
        -- grandchildren via RefreshLocal (skips the upward-propagation that
        -- would re-mark our own _dirty mid-refresh).
        if it.proxy and it.proxy._innerLayout then
            it.proxy._innerLayout:RefreshLocal()
        end
    end

    -- Phase E: place children.
    -- Anchor placed at the leading corner along main, and at start cross edge.
    --   V topDown:  (0, 1)   V bottomUp: (0, 0)   H L->R: (0, 1)   H R->L: (1, 1)
    local anchorMain  = if mainAxisIsY then (if reverse then 0 else 1) else (if reverse then 1 else 0)
    local anchorCross = if mainAxisIsY then 0 else 1
    local anchorX = if mainAxisIsY then anchorCross else anchorMain
    local anchorY = if mainAxisIsY then anchorMain  else anchorCross

    local cursor = leadPad
    -- mainAlign shifts the starting cursor when content is shorter than the container
    -- and there are no flex children (flex would consume all leftover otherwise).
    if (mainAlign == "center" or mainAlign == "end") and #flexItems == 0 then
        local content = 0
        for _, it in activeItems do content += it.resolvedMain end
        content += mainGap
        local leftover = math.max(0, innerMain - content)
        cursor += if mainAlign == "center" then math.floor(leftover / 2) else leftover
    end

    local maxChildCross = 0

    for _, it in activeItems do
        it.raw:SetAnchor(anchorX, anchorY)
        it.raw:SetRelativePoint(anchorX, anchorY)

        local co = it.opts or {}
        local childCrossAlignOpt : AlignKind? = if mainAxisIsY then co.alignH else co.alignV
        local childCrossAlign = childCrossAlignOpt or crossAlign

        local crossDist
        if it.crossModeResolved == "flex" and not co.aspectRatio then
            -- Cross-axis flex fills the cross extent — sit at the cross start edge.
            crossDist = crossLeadPad
        elseif childCrossAlign == "center" then
            crossDist = crossLeadPad + math.floor((innerCross - it.resolvedCross) / 2)
        elseif childCrossAlign == "end" then
            crossDist = containerCross - crossTrailPad - it.resolvedCross
        else  -- "start"
            crossDist = crossLeadPad
        end

        -- Convert (cursor, crossDist) → SetPos(x, y).
        --   V (anchor.x=0): setX = +crossDist;     H (anchor.y=1): setY = -crossDist
        --   V topDown (ay=1): setY = -cursor;      V bottomUp (ay=0): setY = +cursor
        --   H L->R (ax=0):    setX = +cursor;      H R->L (ax=1):    setX = -cursor
        local setX, setY
        if mainAxisIsY then
            setX = crossDist
            setY = if reverse then cursor else -cursor
        else
            setX = if reverse then -cursor else cursor
            setY = -crossDist
        end
        it.raw:SetPos(setX, setY)

        cursor += it.resolvedMain + spacing
        if it.resolvedCross > maxChildCross then maxChildCross = it.resolvedCross end
    end

    -- Phase F: measured extents.
    local mainExtent = leadPad + trailPad
    if #activeItems > 0 then
        local sum = 0
        for _, it in activeItems do sum += it.resolvedMain end
        mainExtent += sum + spacing * (#activeItems - 1)
    end
    local crossExtent = crossLeadPad + maxChildCross + crossTrailPad

    self._measuredMain  = mainExtent
    self._measuredCross = crossExtent
end

function linearLayoutAPI.NewLinearLayout(parent, posX, posY, sizeX, sizeY, layer, opts)
    opts = opts or {}
    if opts.axis ~= "x" and opts.axis ~= "y" then opts.axis = "y" end

    local panelTemplate = opts.panelTemplate or "DialogBox"
    local panel = parent:NewPanel(posX, posY, sizeX, sizeY, layer or 0, panelTemplate)
    if opts.alpha ~= nil then panel:SetAlpha(opts.alpha) end

    local layoutTable = LayoutCommon.MakeBaseLayout(panel, opts, DoRefresh)
    layoutTable._measuredMain = 0
    layoutTable._measuredCross = 0

    function layoutTable.GetMeasuredHeight(sf)
        sf:Refresh()
        return if sf._opts.axis == "y" then sf._measuredMain else sf._measuredCross
    end
    function layoutTable.GetMeasuredWidth(sf)
        sf:Refresh()
        return if sf._opts.axis == "y" then sf._measuredCross else sf._measuredMain
    end

    -- If our container panel is itself a wrapped proxy (i.e. parent was a layout that just
    -- wrapped us in PushItem), hook the upward propagation chain so a deep mutation invalidates
    -- every enclosing layout.
    if type(panel) == "table" and panel._raw ~= nil and panel._layout ~= nil then
        layoutTable._parentLayout = panel._layout
        -- Tag the proxy with a back-reference so the outer layout's DoRefresh can refresh
        -- us before measuring (handles shrink-driven inner panel sizes correctly).
        panel._innerLayout = layoutTable
    end

    return layoutTable
end

return linearLayoutAPI
