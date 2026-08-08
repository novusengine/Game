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
        NewWidget / __index fall-through to .root / RegisterLayoutRefresh hookup.
        The caller (LinearLayout/GridLayout) supplies `doRefreshFn(self)` and tacks on
        its own GetMeasuredHeight / GetMeasuredWidth / type-specific bits.
]]

export type LayoutCommonAPI =
{
    SIZE_AFFECTING : { string },
    ResolveProxy : (child : any) -> any?,
    WrapChild : (raw : Widget, layout : any) -> any,
    MergeOpts : (defaults : { [string] : any }?, options : { [string] : any }?) -> { [string] : any },
    MakeBaseLayout : (panel : Panel, options : { [string] : any }, doRefresh : (any) -> ()) -> any,
}
local layoutCommonAPI : LayoutCommonAPI = {} :: any
local RootedWidget = require("@src/API/UI/RootedWidget")

local SIZE_AFFECTING = { "SetSize", "SetWidth", "SetHeight", "SetText", "SetWrapWidth", "SetFontSize" }
layoutCommonAPI.SIZE_AFFECTING = SIZE_AFFECTING

-- Try to find the proxy associated with whatever the user handed in.
--   - A proxy itself ({ _raw = ..., _layout = ... }) → return it.
--   - A compound wrapper created with a layout as its parent: its explicit `root` is the child.
--   - Anything else: return nil.
local function ResolveProxy(child)
    if type(child) ~= "table" then return nil end
    if child._raw ~= nil then return child end
    local root = child.root
    if type(root) == "table" and root._raw ~= nil and root._layout ~= nil then return root end
    return nil
end
layoutCommonAPI.ResolveProxy = ResolveProxy

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
layoutCommonAPI.WrapChild = WrapChild

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
layoutCommonAPI.MergeOpts = MergeOpts

function layoutCommonAPI.MakeBaseLayout(panel, opts, doRefreshFn)
    local self = {
        root = panel,
        content = panel,
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
        sf.root:InvalidateLayout()
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
        return sf.root:GetSize()
    end

    -- Container size mutators invalidate (children may need repositioning).
    function self.SetSize(sf, size) sf.root:SetSize(size); sf:Invalidate() end
    function self.SetWidth(sf, w)   sf.root:SetWidth(w);   sf:Invalidate() end
    function self.SetHeight(sf, h)  sf.root:SetHeight(h);  sf:Invalidate() end

    -- Position / anchor mutators of the container itself don't affect the layout pass.
    function self.SetPos(sf, position)    sf.root:SetPos(position) end
    function self.SetPosX(sf, x)          sf.root:SetPosX(x) end
    function self.SetPosY(sf, y)          sf.root:SetPosY(y) end
    function self.SetAnchor(sf, anchor)   sf.root:SetAnchor(anchor) end
    function self.SetRelativePoint(sf, point) sf.root:SetRelativePoint(point) end

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
        local raw = sf.root:NewPanel(...)
        local proxy = WrapChild(raw, sf)
        PushItem(sf, raw, proxy, nil)
        return proxy
    end
    function self.NewText(sf, ...)
        local raw = sf.root:NewText(...)
        local proxy = WrapChild(raw, sf)
        PushItem(sf, raw, proxy, nil)
        return proxy
    end
    function self.NewWidget(sf, ...)
        local raw = sf.root:NewWidget(...)
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

    -- Fall through to .root for native widget methods not explicitly wrapped here,
    -- so the layout walks like a Widget when passed as a parent to compound constructors.
    RootedWidget.Attach(self)

    -- C++ pre-render hook: CanvasRenderer fires the registered closure for any panel
    -- tagged DirtyLayoutTag. PushItem / SetChildOpts / Set* mutators all call Invalidate
    -- which tags the panel via panel:InvalidateLayout, so the first frame's refresh runs
    -- automatically as soon as a child is added.
    panel:RegisterLayoutRefresh(function() self:Refresh() end)

    return self
end

return layoutCommonAPI
