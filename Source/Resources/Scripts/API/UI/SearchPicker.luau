local Button = require("@src/API/UI/Button");
local InputBox = require("@src/API/UI/InputBox");
local PopupLayer = require("@src/API/UI/PopupLayer");
local RootedWidget = require("@src/API/UI/RootedWidget");
local TextUtil = require("@src/API/UI/TextUtil");
local VirtualList = require("@src/API/UI/VirtualList");

export type Item =
{
    value : any,
    primary : string,
    secondary : string?,
    search : string?,
}

export type Options =
{
    items : { Item }?,
    value : any?,
    placeholder : string?,
    onValueChanged : ((any, Item?) -> ())?,
    matchesSearch : ((Item, string) -> boolean)?,
    popupWidth : number?,
    popupHeight : number?,
    rowHeight : number?,
    popupLayer : integer?,
    button : Button.Options?,
    input : InputBox.Options?,
}

export type SearchPicker = RootedWidget.RootedPanel<
{
    root : Panel,
    button : Button.Button,
    Open : (self : SearchPicker) -> (),
    Close : (self : SearchPicker) -> (),
    SetItems : (self : SearchPicker, items : { Item }) -> (),
    SetValue : (self : SearchPicker, value : any, notify : boolean?) -> (),
    GetValue : (self : SearchPicker) -> any,
    SetOnValueChanged : (self : SearchPicker, callback : ((any, Item?) -> ())?) -> (),
}>

export type SearchPickerAPI =
{
    New : (parent : Widget, popupParent : Widget, position : vec, size : vec, layer : integer, options : Options?) -> SearchPicker,
}

local searchPickerAPI : SearchPickerAPI = { };

local DEFAULT_POPUP_HEIGHT = 300;
local DEFAULT_ROW_HEIGHT = 40;
local DEFAULT_POPUP_LAYER_OFFSET = 100i;
local POPUP_PADDING = 8;
local SEARCH_HEIGHT = 28;
local SEARCH_GAP = 6;
local ROW_INSET = 2;
local TEXT_INSET = 8;
local SCROLLBAR_WIDTH = 10;
local PRIMARY_FONT_SIZE = 11;
local SECONDARY_FONT_SIZE = 10;
local TRIGGER_FONT_SIZE = 11;
local MUTED_COLOR = vec.New(0.58, 0.61, 0.67);
local ROW_COLOR = vec.New(0.095, 0.105, 0.12);
local ROW_HOVER_COLOR = vec.New(0.13, 0.20, 0.29);
local ROW_SELECTED_COLOR = vec.New(0.24, 0.40, 0.62);

local function CopyInputOptions(source : InputBox.Options?) : InputBox.Options
    local result : { [string] : any } = { };
    if source ~= nil then
        for key, value in source do result[key] = value; end
    end
    return result :: InputBox.Options;
end

local function DefaultMatches(item : Item, query : string) : boolean
    if query == "" then return true; end
    if TextUtil.ContainsIgnoreCase(item.primary, query) then return true; end
    if item.secondary ~= nil and TextUtil.ContainsIgnoreCase(item.secondary, query) then return true; end
    return item.search ~= nil and TextUtil.ContainsIgnoreCase(item.search, query);
end

function searchPickerAPI.New(parent : Widget, popupParent : Widget, position : vec, size : vec, layer : integer, options : Options?) : SearchPicker
    options = options or { };

    local rowHeight = math.max(ROW_INSET + 2, options.rowHeight or DEFAULT_ROW_HEIGHT);
    local popupWidth = math.max(POPUP_PADDING * 2 + TEXT_INSET * 2 + 1, options.popupWidth or size.x);
    local minimumPopupHeight = POPUP_PADDING * 2 + SEARCH_HEIGHT + SEARCH_GAP + rowHeight * 2;
    local popupHeight = math.max(minimumPopupHeight, options.popupHeight or DEFAULT_POPUP_HEIGHT);
    local self : any = {
        items = options.items or { },
        value = options.value,
        placeholder = options.placeholder or "Select...",
        onValueChanged = options.onValueChanged,
        matchesSearch = options.matchesSearch,
        filteredItems = {},
        popupWidth = popupWidth,
        popupHeight = popupHeight,
        rowHeight = rowHeight,
        popupLayer = options.popupLayer or integer.add(layer, DEFAULT_POPUP_LAYER_OFFSET),
        popup = nil,
        popupPanel = nil,
        searchInput = nil,
        popupList = nil,
    };

    self.button = Button.New(parent, position, size, layer, options.button or { });
    self.button:SetFontSize(TRIGGER_FONT_SIZE);
    self.root = self.button.root;
    RootedWidget.Attach(self);
    self:SetAnchor(vec.New(0.0, 1.0));
    self:SetRelativePoint(vec.New(0.0, 1.0));

    function self:GetSelectedItem() : Item?
        for _, item in self.items do
            if item.value == self.value then return item; end
        end
        return nil;
    end

    function self:RefreshButton()
        local selected = self:GetSelectedItem();
        self.button:SetText(selected and selected.primary or self.placeholder);
    end

    function self:RefreshFilter(query : string, preserveScroll : boolean?)
        local filtered = { };
        for _, item in self.items do
            local matches = if self.matchesSearch ~= nil then self.matchesSearch(item, query) else DefaultMatches(item, query);
            if matches then table.insert(filtered, item); end
        end
        self.filteredItems = filtered;
        if self.popupList ~= nil then self.popupList:SetItems(filtered, preserveScroll); end
    end

    function self:ScrollToSelected()
        if self.popupList == nil then return; end
        for index, item in self.filteredItems do
            if item.value == self.value then
                self.popupList:ScrollToIndex(index, "center");
                return;
            end
        end
    end

    function self:ClearPopupReferences()
        self.popup = nil;
        self.popupPanel = nil;
        self.searchInput = nil;
        self.popupList = nil;
    end

    function self:Close()
        local popup = self.popup;
        if popup ~= nil then
            popup:Close();
        else
            self:ClearPopupReferences();
        end
    end

    function self:Open()
        if self.popup ~= nil then self:Close(); end

        local popupWidth = self.popupWidth;
        local rowHeight = self.rowHeight;
        local listWidth = popupWidth - POPUP_PADDING * 2;
        local listContentWidth = listWidth - SCROLLBAR_WIDTH;
        local chromeHeight = POPUP_PADDING * 2 + SEARCH_HEIGHT + SEARCH_GAP;
        local maximumListHeight = math.max(rowHeight * 2, self.popupHeight - chromeHeight);
        local visibleRowCount = math.max(2, math.floor(maximumListHeight / rowHeight));
        local listHeight = visibleRowCount * rowHeight;
        local popupHeight = chromeHeight + listHeight;
        local triggerPosition = self.root:GetWorldPos();

        self.popup = PopupLayer.Open(popupParent, integer.sub(self.popupLayer, 1i), function()
            self:ClearPopupReferences();
        end, self.root);

        local popupPanel = popupParent:NewPanel(vec.New(triggerPosition.x, triggerPosition.y), vec.New(popupWidth, popupHeight), self.popupLayer, "DialogBox");
        popupPanel:SetAnchor(vec.New(0.0, 0.0));
        popupPanel:SetRelativePoint(vec.New(0.0, 1.0));
        popupPanel:SetClipChildren(true);
        self.popupPanel = popupPanel;
        self.popup:SetContent(popupPanel);

        local inputOptions = CopyInputOptions(options.input);
        if inputOptions.placeholderText == nil then inputOptions.placeholderText = "Search..."; end
        inputOptions.defaultText = "";
        if inputOptions.defaultTextFontSize == nil then inputOptions.defaultTextFontSize = TRIGGER_FONT_SIZE; end
        local searchInput = InputBox.New(popupPanel, vec.New(POPUP_PADDING, -POPUP_PADDING), vec.New(listWidth, SEARCH_HEIGHT), 1i, inputOptions);
        searchInput:SetAnchor(vec.New(0.0, 1.0));
        searchInput:SetRelativePoint(vec.New(0.0, 1.0));
        searchInput:SetTextVerticallyCentered();
        self.searchInput = searchInput;

        local list = VirtualList.New(popupPanel, vec.New(POPUP_PADDING, -(POPUP_PADDING + SEARCH_HEIGHT + SEARCH_GAP)), vec.New(listWidth, listHeight), 1i, {
            rowHeight = rowHeight,
            scrollBox = {
                panelTemplate = "DebugDarkGrey",
                contentPanelTemplate = "DebugDarkGrey",
                contentSizeX = listContentWidth,
                contentSizeY = listHeight,
                verticalScrollBar = true,
                scrollBarWidth = SCROLLBAR_WIDTH,
                alpha = 0.0,
            },
            createRow = function(rowParent)
                local rowWidth = listContentWidth - ROW_INSET * 2;
                local row = Button.New(rowParent, vec.New(ROW_INSET, 0), vec.New(rowWidth, rowHeight - ROW_INSET), 1i, {
                    text = "",
                    panelTemplate = "DebugDarkGrey",
                    textTemplate = "DefaultButtonText",
                    textAlign = "left",
                    textPadX = TEXT_INSET,
                    color = ROW_COLOR,
                    hoverColor = ROW_HOVER_COLOR,
                    highlightColor = ROW_SELECTED_COLOR,
                });
                row:SetFontSize(PRIMARY_FONT_SIZE);
                row:SetAnchor(vec.New(0.0, 1.0));
                row:SetRelativePoint(vec.New(0.0, 1.0));
                row.text:SetAnchor(vec.New(0.0, 1.0));
                row.text:SetRelativePoint(vec.New(0.0, 1.0));
                row.text:SetPos(vec.New(TEXT_INSET, -5));
                local secondaryText = row.root:NewText("", vec.New(TEXT_INSET, -21), 2i, "DefaultButtonText");
                secondaryText:SetFontSize(SECONDARY_FONT_SIZE);
                secondaryText:SetAnchor(vec.New(0.0, 1.0));
                secondaryText:SetRelativePoint(vec.New(0.0, 1.0));
                secondaryText:SetColor(MUTED_COLOR);
                secondaryText:SetInteractable(false);
                row.secondaryText = secondaryText;
                row.boundItem = nil;
                row:SetOnMouseUp(function(_, _, mouseButton)
                    if mouseButton ~= 0i or row.boundItem == nil then return; end
                    local item = row.boundItem;
                    self:SetValue(item.value, true);
                    self:Close();
                end);
                return row;
            end,
            bindRow = function(row, _, item)
                row.boundItem = item;
                row:SetText(TextUtil.TruncateToWidth(item.primary, "DefaultButtonText", listContentWidth - TEXT_INSET * 2));
                local secondary = item.secondary or "";
                row.secondaryText:SetText(TextUtil.TruncateToWidth(secondary, "DefaultButtonText", listContentWidth - TEXT_INSET * 2));
                if secondary == "" then
                    row.text:SetAnchor(vec.New(0.0, 0.5));
                    row.text:SetRelativePoint(vec.New(0.0, 0.5));
                    row.text:SetPos(vec.New(TEXT_INSET, 0));
                    row.secondaryText:SetVisible(false);
                else
                    local rowContentHeight = math.max(1, rowHeight - ROW_INSET);
                    local blockHeight = PRIMARY_FONT_SIZE + SECONDARY_FONT_SIZE;
                    local topInset = math.floor(math.max(0, rowContentHeight - blockHeight) / 2);
                    row.text:SetAnchor(vec.New(0.0, 1.0));
                    row.text:SetRelativePoint(vec.New(0.0, 1.0));
                    row.text:SetPos(vec.New(TEXT_INSET, -topInset));
                    row.secondaryText:SetAnchor(vec.New(0.0, 1.0));
                    row.secondaryText:SetRelativePoint(vec.New(0.0, 1.0));
                    row.secondaryText:SetPos(vec.New(TEXT_INSET, -topInset - PRIMARY_FONT_SIZE));
                    row.secondaryText:SetVisible(true);
                end
                row:SetHighlighted(item.value == self.value);
            end,
        });
        list:SetAnchor(vec.New(0.0, 1.0));
        list:SetRelativePoint(vec.New(0.0, 1.0));
        self.popupList = list;
        searchInput:SetOnTextChanged(function(input)
            self:RefreshFilter(input:GetText(), false);
        end);
        self:RefreshFilter("", false);
        self:ScrollToSelected();
        searchInput:Focus();
    end

    function self:SetItems(items : { Item })
        self.items = items or { };
        self:RefreshButton();
        self:RefreshFilter(self.searchInput and self.searchInput:GetText() or "", true);
    end

    function self:SetValue(value, notify)
        if value == self.value then
            if self.popupList ~= nil then self:ScrollToSelected(); end
            return;
        end
        self.value = value;
        self:RefreshButton();
        if self.popupList ~= nil then
            self.popupList:Refresh();
            self:ScrollToSelected();
        end
        if notify and self.onValueChanged ~= nil then self.onValueChanged(value, self:GetSelectedItem()); end
    end

    function self:GetValue() return self.value; end
    function self:SetOnValueChanged(callback) self.onValueChanged = callback; end

    function self:SetVisible(visible)
        self.button:SetVisible(visible);
        if not visible then self:Close(); end
    end

    self.button:SetOnMouseUp(function(_, _, mouseButton)
        if mouseButton ~= 0i then return; end
        if self.popup ~= nil then self:Close(); else self:Open(); end
    end);
    self:RefreshButton();
    self:RefreshFilter("", false);
    return self :: SearchPicker;
end

return searchPickerAPI;
