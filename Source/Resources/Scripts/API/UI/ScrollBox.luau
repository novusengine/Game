type ScrollBoxAPI =
{
    NewScrollBox : (parent : Panel, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, scrollBoxTemplateTable : ScrollBoxTemplate) -> ScrollBox,
}

type ScrollBoxTemplate =
{
    panelTemplate : string?,
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
    scrollBarBackgroundColor : Vector?,
    scrollBarBackgroundAlpha : number?,

    scrollBarTemplate : string?,
    scrollBarColor : Vector?,
    scrollBarAlpha : number?,
}

type ScrollBox =
{
    Scroll : (self : ScrollBox, event : string, x : number, y : number) -> (),
    ScrollToStart : (self : ScrollBox) -> (), -- Scrolls to the "start" (top for top-fill, bottom for bottom-fill)
    ScrollToEnd : (self : ScrollBox) -> (), -- Scrolls to the "end" (bottom for top-fill, top for bottom-fill)
    MoveContent : (self : ScrollBox, x : number, y : number) -> (),
    MoveScrollBar : (self : ScrollBox, y : number) -> (),
    SetAnchor : (self : ScrollBox, x : number, y : number) -> (),
    SetRelativePoint : (self : ScrollBox, x : number, y : number) -> (),
    SetContentHeight : (self : ScrollBox, height : number) -> (),
    GetSize : (self : ScrollBox) -> (number, number),
    IsVerticalScrollBarVisible : (self : ScrollBox) -> boolean,
    SetVerticalScrollBarVisible : (self : ScrollBox, visible : boolean) -> (),
    RefreshScrollbar : (self : ScrollBox) -> (),
    RefreshScrollbarHandleSize : (self : ScrollBox) -> (),
}

local scrollBoxAPI : ScrollBoxAPI = {};

local function SetupScrollBoxMethods(scrollBoxTable : ScrollBox)

    scrollBoxTable.Scroll = function(self : Widget, event : MouseEventCallback, x : number, y : number)
        scrollBoxTable.scrollPosX = scrollBoxTable.scrollPosX + x * scrollBoxTable.scrollSensitivity;
        scrollBoxTable.scrollPosY = scrollBoxTable.scrollPosY - y * scrollBoxTable.scrollSensitivity;
    
        local width, height = scrollBoxTable.scrollBox:GetSize();
        local contentWidth, contentHeight = scrollBoxTable.content:GetSize();
    
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
    
        scrollBoxTable:MoveContent(scrollBoxTable.scrollPosX, scrollBoxTable.scrollPosY);

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
                scrollBarY = -(scrollBoxTable.scrollPosY / upperBoundY) * scrollHeight;
            end

            scrollBoxTable:MoveScrollBar(scrollBarY);
        end
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
        
        scrollBoxTable:MoveContent(scrollBoxTable.scrollPosX, scrollBoxTable.scrollPosY);
        scrollBoxTable:MoveScrollBar(scrollBoxTable.scrollPosY);
    end

    scrollBoxTable.ScrollToEnd = function(self : ScrollBox)
        if (scrollBoxTable.contentSizeY == nil) then
            return;
        end

        scrollBoxTable.scrollPosX = 0;
        
        if scrollBoxTable.fillFromBottom then
            -- For bottom-fill, "end" means scrolling up to see older content
            local height = scrollBoxTable.scrollBox:GetHeight();
            local contentHeight = scrollBoxTable.content:GetHeight();
            scrollBoxTable.scrollPosY = math.min(0, height - contentHeight);
        else
            -- For top-fill, "end" means the bottom
            scrollBoxTable.scrollPosY = scrollBoxTable.contentSizeY - scrollBoxTable.scrollBox:GetHeight();
        end
        
        scrollBoxTable:MoveContent(scrollBoxTable.scrollPosX, scrollBoxTable.scrollPosY);
        scrollBoxTable:MoveScrollBar(scrollBoxTable.scrollPosY);
    end

    scrollBoxTable.MoveContent = function(self : ScrollBox, x : number, y : number)
        self.content:SetPos(x, y);
    end

    scrollBoxTable.MoveScrollBar = function(self : ScrollBox, y : number)
        if (self.scrollBar ~= nil) then
            self.scrollBar:SetPos(0, y);
        end
    end

    scrollBoxTable.SetAnchor = function(self : ScrollBox, x : number, y : number)
        self.panel:SetAnchor(x, y);
    end
    scrollBoxTable.SetRelativePoint = function(self : ScrollBox, x : number, y : number)
        self.panel:SetRelativePoint(x, y);
    end

    scrollBoxTable.SetContentHeight = function(self : ScrollBox, height : number)
        self.contentSizeY = height;
        self.content:SetHeight(height);

        if (self.autoScrollToStart) then
            self:ScrollToStart();
        end

        self:RefreshScrollbarHandleSize();
    end

    scrollBoxTable.GetSize = function(self : ScrollBox)
        local visible = self:IsVerticalScrollBarVisible();
        local width = if (visible) then self.widthWithScrollBar else self.width;
        local height = self.panel:GetHeight();

        return width, height;
    end

    scrollBoxTable.IsVerticalScrollBarVisible = function(self : ScrollBox)
        -- Determine if the scrollbar should be visible
        local visible = self.canHaveVerticalScrollBar;
        if (visible) then
            local height = self.scrollBox:GetHeight();
            local contentHeight = self.content:GetHeight();

            if (contentHeight < height) then
                visible = false;
            end
        end

        return visible;
    end

    scrollBoxTable.SetVerticalScrollBarVisible = function(self : ScrollBox, visible : boolean)
        self.scrollBarBackground:SetVisible(visible);
        local width = if (visible) then self.widthWithScrollBar else self.width;
        self.panel:SetWidth(width);
        self.scrollBox:SetWidth(width);
    end

    scrollBoxTable.RefreshScrollbar = function(self : ScrollBox)
        local visible = self:IsVerticalScrollBarVisible();
        self:SetVerticalScrollBarVisible(visible);
    end

    scrollBoxTable.RefreshScrollbarHandleSize = function(self : ScrollBox)
        local width, height = scrollBoxTable.scrollBox:GetSize();
        local contentWidth, contentHeight = scrollBoxTable.content:GetSize();

        -- Resize the scrollbar handle based on the visible area ratio.
        local scrollBarBackgroundHeight = scrollBoxTable.scrollBarBackground:GetHeight();
        -- Calculate handle height: ratio of visible height to total content height
        local newHandleHeight = scrollBarBackgroundHeight * (height / contentHeight);
        -- Optionally, enforce a minimum handle height (e.g., 20 pixels)
        newHandleHeight = math.max(newHandleHeight, 20);
        scrollBoxTable.scrollBar:SetHeight(newHandleHeight);
    end

    return scrollBoxTable;
end

function scrollBoxAPI.NewScrollBox(parent : Widget, posX : number, posY : number, sizeX : number, sizeY : number, layer : number, scrollBoxTemplateTable : ScrollBoxTemplate)
    local scrollBoxTable : ScrollBoxTemplate = {};

    local panelTemplate = scrollBoxTemplateTable["panelTemplate"] or "DialogBox";
    local contentSizeX = scrollBoxTemplateTable["contentSizeX"] or sizeX;
    local contentSizeY = scrollBoxTemplateTable["contentSizeY"] or sizeY;
    local alpha = scrollBoxTemplateTable["alpha"] or 0.0;
    scrollBoxTable.canHaveVerticalScrollBar = scrollBoxTemplateTable["verticalScrollBar"] or false;
    scrollBoxTable.canHaveHorizontalScrollBar = scrollBoxTemplateTable["horizontalScrollBar"] or false;
    scrollBoxTable.scrollBarWidth = scrollBoxTemplateTable["scrollBarWidth"] or 20;
    scrollBoxTable.autoScrollToStart = scrollBoxTemplateTable["autoScrollToStart"] or true;
    scrollBoxTable.fillFromBottom = scrollBoxTemplateTable["fillFromBottom"] or false; -- NEW: Default to false for backward compatibility

    scrollBoxTable.width = sizeX;
    scrollBoxTable.height = sizeY;
    scrollBoxTable.widthWithScrollBar = sizeX - if (scrollBoxTable.canHaveVerticalScrollBar) then scrollBoxTable.scrollBarWidth else 0;

    -- Create a panel wrapping the scrollbox and scrollbar
    scrollBoxTable.panel = parent:NewPanel(posX, posY, scrollBoxTable.widthWithScrollBar, sizeY, layer, "DebugRed");
    scrollBoxTable.panel:SetAlpha(0.0);

    -- Create the scrollbox
    scrollBoxTable.scrollBox = scrollBoxTable.panel:NewPanel(0, 0, scrollBoxTable.widthWithScrollBar , sizeY, layer, panelTemplate);
    scrollBoxTable.scrollBox:SetClipChildren(true);
    scrollBoxTable.scrollBox:SetAlpha(alpha);

    -- Create the content panel with appropriate anchoring
    scrollBoxTable.content = scrollBoxTable.scrollBox:NewPanel(0, 0, contentSizeX, contentSizeY, 0, "DialogBox");
    
    if scrollBoxTable.fillFromBottom then
        -- Bottom-fill: anchor to bottom-left, content grows upward
        scrollBoxTable.content:SetAnchor(0.0, 0.0);
        scrollBoxTable.content:SetRelativePoint(0.0, 0.0);
    else
        -- Top-fill: anchor to top-left, content grows downward (original behavior)
        scrollBoxTable.content:SetAnchor(0.0, 1.0);
        scrollBoxTable.content:SetRelativePoint(0.0, 1.0);
    end
    
    scrollBoxTable.content:SetAlpha(0.0);

    -- Create the scrollbar
    if (scrollBoxTable.canHaveVerticalScrollBar) then
        local scrollBarBackgroundTemplate = scrollBoxTemplateTable["scrollBarBackgroundTemplate"];
        local scrollBarBackgroundColor = scrollBoxTemplateTable["scrollBarBackgroundColor"] or vector.create(0.4, 0.4, 0.4);
        local scrollBarBackgroundAlpha = scrollBoxTemplateTable["scrollBarBackgroundAlpha"] or 1.0;
        scrollBoxTable.scrollBarBackground = scrollBoxTable.panel:NewPanel(0, 0, scrollBoxTable.scrollBarWidth, sizeY, 0, scrollBarBackgroundTemplate);
        scrollBoxTable.scrollBarBackground:SetAnchor(1.0, 0.0);
        scrollBoxTable.scrollBarBackground:SetRelativePoint(0.0, 0.0);
        scrollBoxTable.scrollBarBackground:SetColor(scrollBarBackgroundColor, scrollBarBackgroundAlpha);

        local scrollBarTemplate = scrollBoxTemplateTable["scrollBarTemplate"];
        local scrollBarColor = scrollBoxTemplateTable["scrollBarColor"] or vector.create(0.7, 0.7, 0.7);
        local scrollBarAlpha = scrollBoxTemplateTable["scrollBarAlpha"] or 1.0;
        scrollBoxTable.scrollBar = scrollBoxTable.scrollBarBackground:NewPanel(0, 0, scrollBoxTable.scrollBarWidth, 40, 0, scrollBarTemplate);
        
        if scrollBoxTable.fillFromBottom then
            -- For bottom-fill, anchor to bottom so positive Y moves down from bottom
            scrollBoxTable.scrollBar:SetAnchor(1.0, 0.0);
            scrollBoxTable.scrollBar:SetRelativePoint(1.0, 0.0);
        else
            -- For top-fill, anchor to top so positive Y moves down from top
            scrollBoxTable.scrollBar:SetAnchor(1.0, 1.0);
            scrollBoxTable.scrollBar:SetRelativePoint(1.0, 1.0);
        end
        
        scrollBoxTable.scrollBar:SetColor(scrollBarColor, scrollBarAlpha);
    end

    -- Setup variables
    scrollBoxTable.scrollSensitivity = scrollBoxTemplateTable["sensitivity"] or 15.0;
    scrollBoxTable.scrollPosX = 0;
    scrollBoxTable.scrollPosY = 0;

    -- Setup methods
    scrollBoxTable = SetupScrollBoxMethods(scrollBoxTable);

    scrollBoxTable.scrollBox:SetOnMouseScroll(scrollBoxTable.Scroll);

    return scrollBoxTable;
end

return scrollBoxAPI;