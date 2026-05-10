--[[
LayoutCommon — shared internals between LinearLayout and GridLayout.

Owns:
  * SIZE_AFFECTING setter list — proxy-intercepted to invalidate the layout.
  * ResolveProxy(child)        — find the layout proxy on a wrapper / userdata / proxy.
  * WrapChild(raw, layout)     — Lua proxy that forwards to the raw widget but invalidates
                                 the layout on size-affecting setters.
  * MergeOpts(defaults, opts)  — shallow merge with `opts` winning. Used when a layout
                                 supplies a `defaultChildOpts` table.
  * MakeBaseLayout(panel, opts, doRefreshFn)
        Returns a layout table preloaded with the everything-but-DoRefresh machinery:
        Invalidate / Refresh / RefreshLocal / GetSize / Set{Size,Width,Height,Pos*,Anchor,
        RelativePoint,Spacing,Padding,ChildOpts} / Remove / Add / NewPanel / NewText /
        NewWidget / __index fall-through to .panel / RegisterLayoutRefresh hookup.
        The caller (LinearLayout/GridLayout) supplies `doRefreshFn(self)` and tacks on
        its own GetMeasuredHeight / GetMeasuredWidth / type-specific bits.
]]

local layoutCommon = {}

local SIZE_AFFECTING = { "SetSize", "SetWidth", "SetHeight", "SetText", "SetWrapWidth", "SetFontSize" }
layoutCommon.SIZE_AFFECTING = SIZE_AFFECTING

-- Try to find the proxy associated with whatever the user handed in.
--   - A proxy itself ({ _raw = ..., _layout = ... }) → return it.
--   - A compound wrapper (UIButton/UIScrollBox/UIInputBox/UIComboBox/etc.) created with a
--     layout as its parent: its first proxy-valued field IS the layout child. Walk fields.
--   - Anything else: return nil.
local function ResolveProxy(child)
    if type(child) ~= "table" then return nil end
    if child._raw ~= nil then return child end
    for _, v in pairs(child) do
        if type(v) == "table" and v._raw ~= nil and v._layout ~= nil then return v end
    end
    return nil
end
layoutCommon.ResolveProxy = ResolveProxy

local function WrapChild(raw, layout)
    local proxy = setmetatable({ _raw = raw, _layout = layout }, {
        __index = function(_, k)
            local v = raw[k]
            if type(v) ~= "function" then return v end
            return function(_, ...) return v(raw, ...) end
        end,
    })
    for _, name in SIZE_AFFECTING do
        local original = raw[name]
        if type(original) == "function" then
            proxy[name] = function(_, ...)
                original(raw, ...)   -- raw:<name>(args), bypassing the proxy to avoid recursion
                layout:Invalidate()
            end
        end
    end
    return proxy
end
layoutCommon.WrapChild = WrapChild

-- Merge `defaults` beneath `itemOpts` (itemOpts wins). Returns nil only if both inputs
-- are nil. Result is a fresh table — DoRefresh callers should cache it on the item to
-- avoid per-frame allocation.
local function MergeOpts(defaults, itemOpts)
    if not defaults then return itemOpts end
    if not itemOpts then return defaults end
    local merged = {}
    for k, v in pairs(defaults) do merged[k] = v end
    for k, v in pairs(itemOpts) do merged[k] = v end
    return merged
end
layoutCommon.MergeOpts = MergeOpts

function layoutCommon.MakeBaseLayout(panel, opts, doRefreshFn)
    local self = {
        panel = panel,
        content = panel,            -- alias for the escape-hatch usage
        _opts = opts,
        _items = {},                -- ordered list of { proxy, raw, opts, _mergedOpts }
        _itemByProxy = {},          -- proxy -> index in _items (for SetChildOpts lookup)
        -- _dirty starts FALSE so the first PushItem's Invalidate is a real false→true
        -- transition that emplaces the C++ DirtyLayoutTag. Initialising to true would
        -- short-circuit the dirty-guard and the C++ tag would never be emplaced, leaving
        -- the layout invisible to the pre-render hook.
        _dirty = false,
        _parentLayout = nil,        -- set when our `panel` is itself a wrapped proxy of another layout
    }

    -- Invalidate is gated on `_dirty`: each dirty cycle emplaces the C++ DirtyLayoutTag
    -- exactly once and propagates upward exactly once. Clearing happens at the top of
    -- DoRefresh (`self._dirty = false`), so the next mutation re-arms the chain.
    function self.Invalidate(sf)
        if sf._dirty then return end
        sf._dirty = true
        sf.panel:InvalidateLayout()
        if sf._parentLayout then sf._parentLayout:Invalidate() end
    end

    function self.Refresh(sf)
        if not sf._dirty then return end
        doRefreshFn(sf)
    end

    -- Force a refresh without going through Invalidate (skips the upward-propagation
    -- that would re-mark our parent _dirty mid-pass). Used by an outer layout's
    -- DoRefresh to re-position grandchildren after raw-resizing us.
    function self.RefreshLocal(sf)
        sf._dirty = true
        doRefreshFn(sf)
    end

    function self.GetSize(sf)
        sf:Refresh()
        return sf.panel:GetSize()
    end

    -- Container size mutators invalidate (children may need repositioning).
    function self.SetSize(sf, w, h) sf.panel:SetSize(w, h); sf:Invalidate() end
    function self.SetWidth(sf, w)   sf.panel:SetWidth(w);   sf:Invalidate() end
    function self.SetHeight(sf, h)  sf.panel:SetHeight(h);  sf:Invalidate() end

    -- Position / anchor mutators of the container itself don't affect the layout pass.
    function self.SetPos(sf, x, y)        sf.panel:SetPos(x, y) end
    function self.SetPosX(sf, x)          sf.panel:SetPosX(x) end
    function self.SetPosY(sf, y)          sf.panel:SetPosY(y) end
    function self.SetAnchor(sf, x, y)     sf.panel:SetAnchor(x, y) end
    function self.SetRelativePoint(sf, x, y) sf.panel:SetRelativePoint(x, y) end

    function self.SetSpacing(sf, s) sf._opts.spacing = s; sf:Invalidate() end
    function self.SetPadding(sf, p) sf._opts.padding = p; sf:Invalidate() end

    function self.SetChildOpts(sf, child, childOpts)
        local proxy = ResolveProxy(child)
        local idx = proxy and sf._itemByProxy[proxy]
        if idx then
            sf._items[idx].opts = childOpts
            sf._items[idx]._mergedOpts = nil
            sf:Invalidate()
        end
    end

    -- Explicit removal — symmetric with Add. Auto-prune in each layout's DoRefresh
    -- (catches "destroyed without :Remove") is the safety net; this is the preferred path.
    function self.Remove(sf, child)
        local proxy = ResolveProxy(child)
        local idx = proxy and sf._itemByProxy[proxy]
        if not idx then return end
        sf._itemByProxy[proxy] = nil
        table.remove(sf._items, idx)
        -- Reindex entries shifted by the table.remove.
        for i = idx, #sf._items do
            sf._itemByProxy[sf._items[i].proxy] = i
        end
        sf:Invalidate()
    end

    local function PushItem(sf, raw, proxy, childOpts)
        table.insert(sf._items, { proxy = proxy, raw = raw, opts = childOpts })
        sf._itemByProxy[proxy] = #sf._items
        sf:Invalidate()
    end

    function self.NewPanel(sf, ...)
        local raw = sf.panel:NewPanel(...)
        local proxy = WrapChild(raw, sf)
        PushItem(sf, raw, proxy, nil)
        return proxy
    end
    function self.NewText(sf, ...)
        local raw = sf.panel:NewText(...)
        local proxy = WrapChild(raw, sf)
        PushItem(sf, raw, proxy, nil)
        return proxy
    end
    function self.NewWidget(sf, ...)
        local raw = sf.panel:NewWidget(...)
        local proxy = WrapChild(raw, sf)
        PushItem(sf, raw, proxy, nil)
        return proxy
    end

    -- Manual register: wrap (or reuse-wrap) a child added externally and append.
    function self.Add(sf, child, childOpts)
        local proxy = ResolveProxy(child)
        if proxy then
            if not sf._itemByProxy[proxy] then
                PushItem(sf, proxy._raw, proxy, childOpts)
            elseif childOpts then
                local item = sf._items[sf._itemByProxy[proxy]]
                item.opts = childOpts
                item._mergedOpts = nil
                sf:Invalidate()
            end
            return proxy
        end
        if type(child) == "userdata" then
            local newProxy = WrapChild(child, sf)
            PushItem(sf, child, newProxy, childOpts)
            return newProxy
        end
        return child
    end

    -- Fall through to .panel for any other widget method (GetWorldPos, SetClipChildren, etc.)
    -- so the layout walks like a Widget when passed as a parent to compound constructors.
    setmetatable(self, {
        __index = function(t, k)
            local p = rawget(t, "panel")
            if p == nil then return nil end
            local v = p[k]
            if type(v) ~= "function" then return v end
            return function(_, ...) return v(p, ...) end
        end,
    })

    -- C++ pre-render hook: CanvasRenderer fires the registered closure for any panel
    -- tagged DirtyLayoutTag. PushItem / SetChildOpts / Set* mutators all call Invalidate
    -- which tags the panel via panel:InvalidateLayout, so the first frame's refresh runs
    -- automatically as soon as a child is added.
    panel:RegisterLayoutRefresh(function() self:Refresh() end)

    return self
end

return layoutCommon
