--[[
VirtualList — a scrollable, fixed-row-height list that recycles a fixed pool of row widgets, so
cost is bounded by the viewport rather than the item count. Owns a ScrollBox.

Rows live on the ScrollBox's `content` panel and ride its scroll, so they are positioned ONCE (when
bound to an index) and never repositioned per frame -- `content:SetPos` moves them. They are also
created visible ONCE and never toggled, so the draw bucket's widget set is stable across scrolling.
Recycling is by reassignment: when the window slides, a row whose index left the window is rebound to
an index that entered. Off-window rows sit at their index position outside the viewport (clipped).

Because a pooled row is reused for different items, `bindRow` must set the row's FULL visual state
every call (text, selected/unselected look), and any handler created in `createRow` must read the
row's CURRENTLY bound item (stored on the handle by `bindRow`), never a captured index.

    local list = VirtualList.NewVirtualList(parent, x, y, w, h, layer, {
        rowHeight = 16,
        scrollBox = { panelTemplate = "DebugDarkGrey", contentSizeX = w, verticalScrollBar = true,
                      scrollBarWidth = 10, alpha = 0.0 },
        createRow = function(parent) ... return handle end,  -- called ONCE per pool slot
        bindRow   = function(handle, index, item) ... end,   -- bind on (re)assignment / Refresh
    })
    list:SetItems(items)  -- array; index passed to bindRow is 1-based
    list:Refresh()        -- re-run bindRow for the visible window (e.g. after a selection change)
]]

local ScrollBox = require("@src/API/UI/ScrollBox")

local OFFSCREEN_Y = -100000 -- park unused rows far outside the viewport (clipped)

local virtualListAPI = {}

function virtualListAPI.NewVirtualList(parent, posX, posY, sizeX, sizeY, layer, opts)
    opts = opts or {}

    local self = {
        rowHeight  = opts.rowHeight or 16,
        createRow  = opts.createRow,
        bindRow    = opts.bindRow,
        items      = {},
        realized   = {},   -- [index0] = row
        recyclable = {},   -- rows not mapped to an in-window index (a stack)
        viewportH  = sizeY,
        lastFirst  = nil,
    }

    self.scrollBox = ScrollBox.NewScrollBox(parent, posX, posY, sizeX, sizeY, layer, opts.scrollBox or {})
    self.rowParent = self.scrollBox.content -- rows ride the content scroll
    self.poolSize = math.ceil(sizeY / self.rowHeight) + 2 -- visible rows + a small buffer

    for _ = 1, self.poolSize do
        local row = self.createRow(self.rowParent)
        row:SetVisible(true)
        row:SetPosY(OFFSCREEN_Y)
        table.insert(self.recyclable, row)
    end

    function self:Rebind()
        local count = #self.items
        local scrollPosY = self.scrollBox.scrollPosY or 0
        local first = math.floor(scrollPosY / self.rowHeight)
        if first < 0 then first = 0 end

        -- Window membership only changes when `first` does; sub-row scroll is handled by content move.
        if first == self.lastFirst then return end
        self.lastFirst = first

        local last = math.min(count - 1, first + self.poolSize - 1)

        for index0, row in self.realized do
            if index0 < first or index0 > last then
                self.realized[index0] = nil
                table.insert(self.recyclable, row)
            end
        end

        for index0 = first, last do
            if self.realized[index0] == nil then
                local row = table.remove(self.recyclable)
                self.realized[index0] = row
                self.bindRow(row, index0 + 1, self.items[index0 + 1])
                row:SetPosY(-(index0 * self.rowHeight) - 1)
            end
        end
    end

    function self:SetItems(items)
        for index0, row in self.realized do
            self.realized[index0] = nil
            table.insert(self.recyclable, row)
        end
        self.items = items or {}
        self.lastFirst = nil -- force Rebind past the membership guard
        -- SetContentHeight resets the scroll to the top (autoScrollToStart), so Rebind starts at 0.
        self.scrollBox:SetContentHeight(math.max(self.viewportH, #self.items * self.rowHeight + 2))
        self:Rebind()
        -- Rows left over (list shorter than the pool) hold stale content; park them off-viewport.
        for _, row in self.recyclable do
            row:SetPosY(OFFSCREEN_Y)
        end
    end

    function self:Refresh()
        for index0, row in self.realized do
            self.bindRow(row, index0 + 1, self.items[index0 + 1])
        end
    end

    function self:SetAnchor(x, y)        self.scrollBox:SetAnchor(x, y) end
    function self:SetRelativePoint(x, y) self.scrollBox:SetRelativePoint(x, y) end
    function self:GetScrollBox()         return self.scrollBox end

    self.scrollBox:SetOnScroll(function() self:Rebind() end)

    return self
end

return virtualListAPI
