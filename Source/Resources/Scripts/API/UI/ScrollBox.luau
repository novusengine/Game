
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
    autoScrollToTopWhenChangeContentHeight : boolean?,

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
    ScrollToTop : (self : ScrollBox) -> (),
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
    
        -- For Y, if you want to only move the content upward (making scrollPosY positive),
        -- then the allowed range is from 0 (initial top alignment) to (contentHeight - height)
        local upperBoundY = math.max(0, contentHeight - height);

        scrollBoxTable.scrollPosX = math.max(lowerBoundX, math.min(0, scrollBoxTable.scrollPosX));
        scrollBoxTable.scrollPosY = math.min(upperBoundY, math.max(0, scrollBoxTable.scrollPosY));
    
        scrollBoxTable:MoveContent(scrollBoxTable.scrollPosX, scrollBoxTable.scrollPosY);

        -- Calculate the proper position of the scrollbar.
        local scrollBarBackgroundHeight = scrollBoxTable.scrollBarBackground:GetHeight();
        local scrollBarHeight = scrollBoxTable.scrollBar:GetHeight();

        local scrollHeight = scrollBarBackgroundHeight - scrollBarHeight;
        local scrollBarY = -(scrollBoxTable.scrollPosY / upperBoundY) * scrollHeight;

        scrollBoxTable:MoveScrollBar(scrollBarY);
    end

    scrollBoxTable.ScrollToTop = function(self : ScrollBox)
        scrollBoxTable.scrollPosX = 0;
        scrollBoxTable.scrollPosY = 0;
        scrollBoxTable:MoveContent(scrollBoxTable.scrollPosX, scrollBoxTable.scrollPosY);
        scrollBoxTable:MoveScrollBar(scrollBoxTable.scrollPosY);
    end

    scrollBoxTable.MoveContent = function(self : ScrollBox, x : number, y : number)
        self.content:SetPos(x, y);
    end

    scrollBoxTable.MoveScrollBar = function(self : ScrollBox, y : number)
        self.scrollBar:SetPos(0, y);
    end

    scrollBoxTable.SetAnchor = function(self : ScrollBox, x : number, y : number)
        self.panel:SetAnchor(x, y);
    end
    scrollBoxTable.SetRelativePoint = function(self : ScrollBox, x : number, y : number)
        self.panel:SetRelativePoint(x, y);
    end

    scrollBoxTable.SetContentHeight = function(self : ScrollBox, height : number)
        self.content:SetHeight(height);

        if (self.autoScrollToTopWhenChangeContentHeight) then
            self:ScrollToTop();
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
    scrollBoxTable.canHaveVerticalScrollBar = scrollBoxTemplateTable["verticalScrollBar"] or true;
    scrollBoxTable.canHaveHorizontalScrollBar = scrollBoxTemplateTable["horizontalScrollBar"] or true;
    scrollBoxTable.scrollBarWidth = scrollBoxTemplateTable["scrollBarWidth"] or 20;
    scrollBoxTable.autoScrollToTopWhenChangeContentHeight = scrollBoxTemplateTable["autoScrollToTopWhenChangeContentHeight"] or true;

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

    -- Create the content panel
    scrollBoxTable.content = scrollBoxTable.scrollBox:NewPanel(0, 0, contentSizeX, contentSizeY, 0, "DialogBox");
    scrollBoxTable.content:SetAnchor(0.0, 1.0);
    scrollBoxTable.content:SetRelativePoint(0.0, 1.0);
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
        scrollBoxTable.scrollBar:SetAnchor(1.0, 1.0);
        scrollBoxTable.scrollBar:SetRelativePoint(1.0, 1.0);
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