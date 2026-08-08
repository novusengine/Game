local RootedWidget = require("@src/API/UI/RootedWidget");

export type ScrollBoxAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : ScrollBoxTemplate?) -> ScrollBox,
}

export type ScrollBoxTemplate =
{
    panelTemplate : string?,
    contentPanelTemplate : string?,
    contentSizeX : number?,
    contentSizeY : number?,
    alpha : number?,

    verticalScrollBar : boolean?,
    horizontalScrollBar : boolean?,
    scrollBarWidth : number?,
    autoScrollToStart : boolean?, -- NEW: Auto-scroll to start when content height changes
    fillFromBottom : boolean?, -- NEW: Option to fill from bottom instead of top

    sensitivity : number?,

    scrollBarBackgroundTemplate : string?,
    scrollBarBackgroundColor : vec?,
    scrollBarBackgroundAlpha : number?,

    scrollBarTemplate : string?,
    scrollBarColor : vec?,
    scrollBarAlpha : number?,
}

export type ScrollBox =
{
    root : Panel,
    content : Panel,
    Scroll : (self : ScrollBox, event : any, x : number, y : number) -> (),
    ScrollToStart : (self : ScrollBox) -> (), -- Scrolls to the "start" (top for top-fill, bottom for bottom-fill)
    ScrollToEnd : (self : ScrollBox) -> (), -- Scrolls to the "end" (bottom for top-fill, top for bottom-fill)
    MoveContent : (self : ScrollBox, position : vec) -> (),
    MoveScrollBar : (self : ScrollBox, y : number) -> (),
    SetAnchor : (self : ScrollBox, anchor : vec) -> (),
    SetRelativePoint : (self : ScrollBox, point : vec) -> (),
    SetContentHeight : (self : ScrollBox, height : number) -> (),
    GetSize : (self : ScrollBox) -> vec,
    IsVerticalScrollBarVisible : (self : ScrollBox) -> boolean,
    SetVerticalScrollBarVisible : (self : ScrollBox, visible : boolean) -> (),
    RefreshScrollbar : (self : ScrollBox) -> (),
    RefreshScrollbarHandleSize : (self : ScrollBox) -> (),
    SetOnScroll : (self : ScrollBox, fn : () -> ()) -> (),
    GetOffset : (self : ScrollBox) -> vec,
    SetOffset : (self : ScrollBox, offset : vec) -> (),
    GetViewportSize : (self : ScrollBox) -> vec,
}

local scrollBoxAPI : ScrollBoxAPI = {};

local function SetupScrollBoxMethods(scrollBoxTable : ScrollBox)

    scrollBoxTable.Scroll = function(self : ScrollBox, event : any, x : number, y : number)
        scrollBoxTable.scrollPosX = scrollBoxTable.scrollPosX + x * scrollBoxTable.scrollSensitivity;
        scrollBoxTable.scrollPosY = scrollBoxTable.scrollPosY - y * scrollBoxTable.scrollSensitivity;
    
        local viewportSize = scrollBoxTable._viewport:GetSize();
        local contentSize = scrollBoxTable.content:GetSize();
        local width, height = viewportSize.x, viewportSize.y;
        local contentWidth, contentHeight = contentSize.x, contentSize.y;
    
        -- For X, we still clamp between math.min(0, width - contentWidth) and 0.
        local lowerBoundX = math.min(0, width - contentWidth);
    
        -- Y bounds depend on fill direction
        local upperBoundY, lowerBoundY;
        if scrollBoxTable.fillFromBottom then
            -- Bottom-fill: content grows upward, scroll range is negative
            upperBoundY = 0; -- Start at bottom
            lowerBoundY = math.min(0, height - contentHeight); -- Can scroll up if content is taller
        else
            -- Top-fill: content grows downward, scroll range is positive
            upperBoundY = math.max(0, contentHeight - height);
            lowerBoundY = 0;
        end

        scrollBoxTable.scrollPosX = math.max(lowerBoundX, math.min(0, scrollBoxTable.scrollPosX));
        scrollBoxTable.scrollPosY = math.min(upperBoundY, math.max(lowerBoundY, scrollBoxTable.scrollPosY));
    
        scrollBoxTable:MoveContent(vec.New(scrollBoxTable.scrollPosX, scrollBoxTable.scrollPosY));

        if (scrollBoxTable.canHaveVerticalScrollBar) then
            -- Calculate the proper position of the scrollbar.
            local scrollBarBackgroundHeight = scrollBoxTable.scrollBarBackground:GetHeight();
            local scrollBarHeight = scrollBoxTable.scrollBar:GetHeight();

            local scrollHeight = scrollBarBackgroundHeight - scrollBarHeight;
            local scrollBarY;
            
            if scrollBoxTable.fillFromBottom then
                -- For bottom-fill, scrollbar position calculation
                local scrollRange = height - contentHeight;
                if scrollRange < 0 then
                    -- Handle is anchored to bottom, so we need to calculate position from bottom
                    -- When scrollPosY = 0 (newest messages), we want scrollBarY = 0 (at bottom)
                    -- When scrollPosY = scrollRange (oldest messages), we want scrollBarY = -scrollHeight (at top)
                    scrollBarY = (scrollBoxTable.scrollPosY / scrollRange) * scrollHeight;
                else
                    scrollBarY = 0; -- At bottom when no scrolling needed
                end
            else
                -- Original top-fill calculation
                scrollBarY = upperBoundY > 0 and -(scrollBoxTable.scrollPosY / upperBoundY) * scrollHeight or 0;
            end

            scrollBoxTable:MoveScrollBar(scrollBarY);
        end

        if scrollBoxTable.onScroll then scrollBoxTable.onScroll() end
    end

    scrollBoxTable.ScrollToStart = function(self : ScrollBox)
        scrollBoxTable.scrollPosX = 0;
        
        if scrollBoxTable.fillFromBottom then
            -- For bottom-fill, "start" means the natural position (newest content visible)
            scrollBoxTable.scrollPosY = 0;
        else
            -- For top-fill, "start" means the top
            scrollBoxTable.scrollPosY = 0;
        end
        
        scrollBoxTable:MoveContent(vec.New(scrollBoxTable.scrollPosX, scrollBoxTable.scrollPosY));
        scrollBoxTable:MoveScrollBar(scrollBoxTable.scrollPosY);
    end

    scrollBoxTable.ScrollToEnd = function(self : ScrollBox)
        if (scrollBoxTable.contentSizeY == nil) then
            return;
        end

        scrollBoxTable.scrollPosX = 0;
        
        if scrollBoxTable.fillFromBottom then
            -- For bottom-fill, "end" means scrolling up to see older content
            local height = scrollBoxTable._viewport:GetHeight();
            local contentHeight = scrollBoxTable.content:GetHeight();
            scrollBoxTable.scrollPosY = math.min(0, height - contentHeight);
        else
            -- For top-fill, "end" means the bottom
            scrollBoxTable.scrollPosY = scrollBoxTable.contentSizeY - scrollBoxTable._viewport:GetHeight();
        end
        
        scrollBoxTable:MoveContent(vec.New(scrollBoxTable.scrollPosX, scrollBoxTable.scrollPosY));
        scrollBoxTable:MoveScrollBar(scrollBoxTable.scrollPosY);
    end

    scrollBoxTable.MoveContent = function(self : ScrollBox, position : vec)
        self.content:SetPos(position);
    end

    scrollBoxTable.MoveScrollBar = function(self : ScrollBox, y : number)
        if (self.scrollBar ~= nil) then
            self.scrollBar:SetPos(vec.New(0, y));
        end
    end

    scrollBoxTable.SetAnchor = function(self : ScrollBox, anchor : vec)
        self.root:SetAnchor(anchor);
    end
    scrollBoxTable.SetRelativePoint = function(self : ScrollBox, point : vec)
        self.root:SetRelativePoint(point);
    end

    scrollBoxTable.SetContentHeight = function(self : ScrollBox, height : number)
        self.contentSizeY = height;
        self.content:SetHeight(height);

        if self.autoScrollToStart then
            self:ScrollToStart();
        else
            local viewportHeight = self._viewport:GetHeight();
            if self.fillFromBottom then
                self.scrollPosY = math.clamp(self.scrollPosY, math.min(0, viewportHeight - height), 0);
            else
                self.scrollPosY = math.clamp(self.scrollPosY, 0, math.max(0, height - viewportHeight));
            end
            self:MoveContent(vec.New(self.scrollPosX, self.scrollPosY));
        end

        if self.canHaveVerticalScrollBar then
            self:RefreshScrollbar();
            self:RefreshScrollbarHandleSize();

            local viewportHeight = self._viewport:GetHeight();
            local scrollRange = if self.fillFromBottom
                then math.min(0, viewportHeight - height)
                else math.max(0, height - viewportHeight);
            local scrollTrackHeight = self.scrollBarBackground:GetHeight() - self.scrollBar:GetHeight();
            local scrollBarY = 0;
            if scrollRange ~= 0 then
                scrollBarY = if self.fillFromBottom
                    then (self.scrollPosY / scrollRange) * scrollTrackHeight
                    else -(self.scrollPosY / scrollRange) * scrollTrackHeight;
            end
            self:MoveScrollBar(scrollBarY);
        end
    end

    scrollBoxTable.GetSize = function(self : ScrollBox)
        local visible = self:IsVerticalScrollBarVisible();
        local width = if (visible) then self.widthWithScrollBar else self.width;
        local height = self.root:GetHeight();

        return vec.New(width, height);
    end

    scrollBoxTable.IsVerticalScrollBarVisible = function(self : ScrollBox)
        -- Determine if the scrollbar should be visible
        local visible = self.canHaveVerticalScrollBar;
        if (visible) then
            local height = self._viewport:GetHeight();
            local contentHeight = self.content:GetHeight();

            if (contentHeight <= height) then
                visible = false;
            end
        end

        return visible;
    end

    scrollBoxTable.SetVerticalScrollBarVisible = function(self : ScrollBox, visible : boolean)
        self.scrollBarBackground:SetVisible(visible);
        local width = if (visible) then self.widthWithScrollBar else self.width;
        self.root:SetWidth(width);
        self._viewport:SetWidth(width);
    end

    scrollBoxTable.RefreshScrollbar = function(self : ScrollBox)
        local visible = self:IsVerticalScrollBarVisible();
        self:SetVerticalScrollBarVisible(visible);
    end

    scrollBoxTable.RefreshScrollbarHandleSize = function(self : ScrollBox)
        local viewportSize = scrollBoxTable._viewport:GetSize();
        local contentSize = scrollBoxTable.content:GetSize();
        local width, height = viewportSize.x, viewportSize.y;
        local contentWidth, contentHeight = contentSize.x, contentSize.y;

        -- Resize the scrollbar handle based on the visible area ratio.
        local scrollBarBackgroundHeight = scrollBoxTable.scrollBarBackground:GetHeight();
        -- Calculate handle height: ratio of visible height to total content height
        local newHandleHeight = scrollBarBackgroundHeight * (height / contentHeight);
        -- Optionally, enforce a minimum handle height (e.g., 20 pixels)
        newHandleHeight = math.max(newHandleHeight, 20);
        scrollBoxTable.scrollBar:SetHeight(newHandleHeight);
    end

    -- Notified after scrollPosY changes (wheel or scrollbar drag). Used by virtualized lists to
    -- rebind the visible window.
    scrollBoxTable.SetOnScroll = function(self : ScrollBox, fn)
        self.onScroll = fn;
    end

    scrollBoxTable.GetOffset = function(self : ScrollBox) : vec
        return vec.New(self.scrollPosX, self.scrollPosY);
    end
    scrollBoxTable.SetOffset = function(self : ScrollBox, offset : vec)
        local viewportHeight = self._viewport:GetHeight();
        local contentHeight = self.content:GetHeight();
        self.scrollPosX = offset.x;
        self.scrollPosY = if self.fillFromBottom
            then math.clamp(offset.y, math.min(0, viewportHeight - contentHeight), 0)
            else math.clamp(offset.y, 0, math.max(0, contentHeight - viewportHeight));
        self:MoveContent(vec.New(self.scrollPosX, self.scrollPosY));
        if self.onScroll then self.onScroll(); end
    end
    scrollBoxTable.GetViewportSize = function(self : ScrollBox) : vec
        return self._viewport:GetSize();
    end

    return scrollBoxTable;
end

function scrollBoxAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : ScrollBoxTemplate?)
    local posX, posY = position.x, position.y;
    local sizeX, sizeY = size.x, size.y;
    local scrollBoxTemplateTable = options or { };
    local scrollBoxTable : ScrollBox = {} :: any;

    local panelTemplate = scrollBoxTemplateTable["panelTemplate"] or "DialogBox";
    local contentSizeX = scrollBoxTemplateTable["contentSizeX"] or sizeX;
    local contentSizeY = scrollBoxTemplateTable["contentSizeY"] or sizeY;
    local alpha = scrollBoxTemplateTable["alpha"] or 0.0;
    scrollBoxTable.canHaveVerticalScrollBar = scrollBoxTemplateTable["verticalScrollBar"] or false;
    scrollBoxTable.canHaveHorizontalScrollBar = scrollBoxTemplateTable["horizontalScrollBar"] or false;
    scrollBoxTable.scrollBarWidth = scrollBoxTemplateTable["scrollBarWidth"] or 20;
    scrollBoxTable.autoScrollToStart = scrollBoxTemplateTable["autoScrollToStart"] == true;
    scrollBoxTable.fillFromBottom = scrollBoxTemplateTable["fillFromBottom"] or false;

    scrollBoxTable.width = sizeX;
    scrollBoxTable.height = sizeY;
    scrollBoxTable.widthWithScrollBar = sizeX - if (scrollBoxTable.canHaveVerticalScrollBar) then scrollBoxTable.scrollBarWidth else 0;

    -- Create a panel wrapping the scrollbox and scrollbar
    scrollBoxTable.root = parent:NewPanel(position, vec.New(scrollBoxTable.widthWithScrollBar, sizeY), layer, "DebugRed");
    RootedWidget.Attach(scrollBoxTable);
    scrollBoxTable.root:SetAlpha(0.0);

    -- Create the scrollbox
    scrollBoxTable._viewport = scrollBoxTable.root:NewPanel(vec.New(0, 0), vec.New(scrollBoxTable.widthWithScrollBar, sizeY), layer, panelTemplate);
    scrollBoxTable._viewport:SetClipChildren(true);
    scrollBoxTable._viewport:SetAlpha(alpha);

    -- Create the content panel with appropriate anchoring
    local contentPanelTemplate = scrollBoxTemplateTable["contentPanelTemplate"] or "DialogBox";
    scrollBoxTable.content = scrollBoxTable._viewport:NewPanel(vec.New(0, 0), vec.New(contentSizeX, contentSizeY), 0i, contentPanelTemplate);
    
    if scrollBoxTable.fillFromBottom then
        -- Bottom-fill: anchor to bottom-left, content grows upward
        scrollBoxTable.content:SetAnchor(vec.New(0.0, 0.0));
        scrollBoxTable.content:SetRelativePoint(vec.New(0.0, 0.0));
    else
        -- Top-fill: anchor to top-left, content grows downward (original behavior)
        scrollBoxTable.content:SetAnchor(vec.New(0.0, 1.0));
        scrollBoxTable.content:SetRelativePoint(vec.New(0.0, 1.0));
    end
    
    scrollBoxTable.content:SetAlpha(0.0);

    -- Create the scrollbar
    if (scrollBoxTable.canHaveVerticalScrollBar) then
        local scrollBarBackgroundTemplate = scrollBoxTemplateTable["scrollBarBackgroundTemplate"];
        local scrollBarBackgroundColor = scrollBoxTemplateTable["scrollBarBackgroundColor"] or vec.New(0.4, 0.4, 0.4);
        local scrollBarBackgroundAlpha = scrollBoxTemplateTable["scrollBarBackgroundAlpha"] or 1.0;
        scrollBoxTable.scrollBarBackground = scrollBoxTable.root:NewPanel(vec.New(0, 0), vec.New(scrollBoxTable.scrollBarWidth, sizeY), 0i, scrollBarBackgroundTemplate);
        scrollBoxTable.scrollBarBackground:SetAnchor(vec.New(1.0, 0.0));
        scrollBoxTable.scrollBarBackground:SetRelativePoint(vec.New(0.0, 0.0));
        scrollBoxTable.scrollBarBackground:SetColor(scrollBarBackgroundColor, scrollBarBackgroundAlpha);

        local scrollBarTemplate = scrollBoxTemplateTable["scrollBarTemplate"];
        local scrollBarColor = scrollBoxTemplateTable["scrollBarColor"] or vec.New(0.7, 0.7, 0.7);
        local scrollBarAlpha = scrollBoxTemplateTable["scrollBarAlpha"] or 1.0;
        scrollBoxTable.scrollBar = scrollBoxTable.scrollBarBackground:NewPanel(vec.New(0, 0), vec.New(scrollBoxTable.scrollBarWidth, 40), 0i, scrollBarTemplate);
        
        if scrollBoxTable.fillFromBottom then
            -- For bottom-fill, anchor to bottom so positive Y moves down from bottom
            scrollBoxTable.scrollBar:SetAnchor(vec.New(1.0, 0.0));
            scrollBoxTable.scrollBar:SetRelativePoint(vec.New(1.0, 0.0));
        else
            -- For top-fill, anchor to top so positive Y moves down from top
            scrollBoxTable.scrollBar:SetAnchor(vec.New(1.0, 1.0));
            scrollBoxTable.scrollBar:SetRelativePoint(vec.New(1.0, 1.0));
        end
        
        scrollBoxTable.scrollBar:SetColor(scrollBarColor, scrollBarAlpha);
    end

    -- Setup variables
    scrollBoxTable.scrollSensitivity = scrollBoxTemplateTable["sensitivity"] or 15.0;
    scrollBoxTable.scrollPosX = 0;
    scrollBoxTable.scrollPosY = 0;

    -- Setup methods
    scrollBoxTable = SetupScrollBoxMethods(scrollBoxTable);

    scrollBoxTable._viewport:SetOnMouseScroll(scrollBoxTable.Scroll);

    if scrollBoxTable.canHaveVerticalScrollBar then
        local startMouseY = 0
        local startScrollPosY = 0

        scrollBoxTable.scrollBar:SetOnMouseDown(function()
            startMouseY = UI.GetMousePos().y
            startScrollPosY = scrollBoxTable.scrollPosY
        end)

        scrollBoxTable.scrollBar:SetOnMouseHeld(function()
            local mousePosition = UI.GetMousePos()
            local visibleH = scrollBoxTable._viewport:GetHeight()
            local contentH = scrollBoxTable.content:GetHeight()
            local trackH = scrollBoxTable.scrollBarBackground:GetHeight() - scrollBoxTable.scrollBar:GetHeight()
            if trackH <= 0 then return end

            local upperBoundY = scrollBoxTable.fillFromBottom
                and math.min(0, visibleH - contentH)
                or math.max(0, contentH - visibleH)
            if upperBoundY == 0 then return end

            -- World Y is positive-up, so dragging the cursor downward (lower Y) means
            -- (startMouseY - my) > 0, which should advance us toward more-scrolled content.
            local newScrollPosY = startScrollPosY + (startMouseY - mousePosition.y) * (upperBoundY / trackH)

            if scrollBoxTable.fillFromBottom then
                scrollBoxTable.scrollPosY = math.max(upperBoundY, math.min(0, newScrollPosY))
            else
                scrollBoxTable.scrollPosY = math.max(0, math.min(upperBoundY, newScrollPosY))
            end
            scrollBoxTable:MoveContent(vec.New(scrollBoxTable.scrollPosX, scrollBoxTable.scrollPosY))

            local barY
            if scrollBoxTable.fillFromBottom then
                barY = (scrollBoxTable.scrollPosY / upperBoundY) * trackH
            else
                barY = -(scrollBoxTable.scrollPosY / upperBoundY) * trackH
            end
            scrollBoxTable:MoveScrollBar(barY)

            if scrollBoxTable.onScroll then scrollBoxTable.onScroll() end
        end)
    end

    return scrollBoxTable;
end

return scrollBoxAPI;
