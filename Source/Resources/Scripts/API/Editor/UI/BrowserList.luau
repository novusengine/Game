local Button = require("@src/API/UI/Button");
local RootedWidget = require("@src/API/UI/RootedWidget");
local TextUtil = require("@src/API/UI/TextUtil");
local Theme = require("@src/API/Editor/UI/Theme");
local VirtualList = require("@src/API/UI/VirtualList");

export type Options =
{
    rowHeight : number?,
    textTemplate : string?,
    getPrimaryText : (item : any, index : number) -> string,
    getSecondaryText : (item : any, index : number) -> string,
    getSecondaryColor : ((item : any, index : number) -> vec)?,
    isSelected : ((item : any, index : number) -> boolean)?,
    onActivate : ((item : any, index : number) -> ())?,
}

export type BrowserList = RootedWidget.RootedPanel<
{
    root : Panel,
    SetItems : (self : BrowserList, items : { any }, preserveScroll : boolean?) -> (),
    Refresh : (self : BrowserList) -> (),
}>

export type BrowserListAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options) -> BrowserList,
}

local browserListAPI : BrowserListAPI = { };

local BASE_COLOR = vec.New(0.095, 0.105, 0.12);
local HOVER_COLOR = vec.New(0.13, 0.20, 0.29);
local MUTED_COLOR = vec.New(0.58, 0.61, 0.67);
local SCROLL_BAR_WIDTH = 10;
local ROW_INSET = 2;
local TEXT_INSET = 8;

function browserListAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : Options) : BrowserList
    local rowHeight = options.rowHeight or 40;
    local textTemplate = options.textTemplate or Theme.textTemplate;
    local rowWidth = size.x - SCROLL_BAR_WIDTH - ROW_INSET * 2;
    local textWidth = rowWidth - TEXT_INSET * 2;
    local self : any = { };

    local function CreateRow(rowParent : Widget)
        local row = Button.New(rowParent, vec.New(ROW_INSET, 0), vec.New(rowWidth, rowHeight - 2), 1i, {
            text = "",
            textTemplate = textTemplate,
            textAlign = "left",
            textPadX = TEXT_INSET,
            color = BASE_COLOR,
            hoverColor = HOVER_COLOR,
            highlightColor = Theme.selectedColor,
            alpha = 1.0,
            hoverAlpha = 1.0,
            highlightAlpha = 0.55,
            borderSize = 0,
        });
        row:SetAnchor(vec.New(0.0, 1.0));
        row:SetRelativePoint(vec.New(0.0, 1.0));
        row.text:SetAnchor(vec.New(0.0, 1.0));
        row.text:SetRelativePoint(vec.New(0.0, 1.0));
        row.text:SetPos(vec.New(TEXT_INSET, -5));
        row.secondaryText = row.root:NewText("", vec.New(TEXT_INSET, -21), 2i, textTemplate);
        row.secondaryText:SetAnchor(vec.New(0.0, 1.0));
        row.secondaryText:SetRelativePoint(vec.New(0.0, 1.0));
        row.secondaryText:SetColor(MUTED_COLOR);
        row.secondaryText:SetInteractable(false);
        row.boundItem = nil;
        row.boundIndex = 0;
        row:SetOnMouseUp(function(_, _, button)
            if button ~= 0i or row.boundItem == nil or options.onActivate == nil then return; end
            options.onActivate(row.boundItem, row.boundIndex);
        end);
        return row;
    end

    local function BindRow(row, index : number, item : any)
        row.boundItem = item;
        row.boundIndex = index;
        row:SetText(TextUtil.TruncateToWidth(options.getPrimaryText(item, index), textTemplate, textWidth));
        row.secondaryText:SetText(TextUtil.TruncateToWidth(options.getSecondaryText(item, index), textTemplate, textWidth));
        row.secondaryText:SetColor(if options.getSecondaryColor then options.getSecondaryColor(item, index) else MUTED_COLOR);
        row:SetHighlighted(options.isSelected ~= nil and options.isSelected(item, index));
    end

    self.virtualList = VirtualList.New(parent, position, size, layer, {
        rowHeight = rowHeight,
        scrollBox = {
            panelTemplate = "DebugDarkGrey",
            contentPanelTemplate = "DebugDarkGrey",
            contentSizeX = size.x,
            contentSizeY = size.y,
            verticalScrollBar = true,
            scrollBarWidth = SCROLL_BAR_WIDTH,
            alpha = 0.0,
        },
        createRow = CreateRow,
        bindRow = BindRow,
    });
    self.root = self.virtualList.root;

    function self:SetItems(items : { any }, preserveScroll : boolean?)
        self.virtualList:SetItems(items, preserveScroll);
    end

    function self:Refresh()
        self.virtualList:Refresh();
    end

    RootedWidget.Attach(self);
    return self :: BrowserList;
end

return browserListAPI;
