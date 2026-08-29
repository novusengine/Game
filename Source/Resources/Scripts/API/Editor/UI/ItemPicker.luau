local Button = require("@src/API/UI/Button");
local GameTooltip = require("@src/API/Game/UI/GameTooltip");
local InputField = require("@src/API/Editor/UI/InputField");
local ItemUtil = require("@src/API/Game/ItemUtil");
local Modal = require("@src/API/UI/Modal");
local TextUtil = require("@src/API/UI/TextUtil");
local VirtualList = require("@src/API/UI/VirtualList");

export type Item =
{
    value : integer,
    primary : string,
    secondary : string?,
    search : string?,
    icon : string?,
    rarity : integer?,
    trailing : string?,
}

export type Options =
{
    name : string,
    title : string?,
    width : number?,
    height : number?,
    searchPlaceholder : string?,
}

export type OpenOptions =
{
    title : string?,
    items : { Item },
    onSelect : (integer) -> (),
}

export type ItemPicker =
{
    root : Panel,
    Open : (self : ItemPicker, options : OpenOptions) -> (),
    Close : (self : ItemPicker) -> (),
    IsOpen : (self : ItemPicker) -> boolean,
}

export type ItemPickerAPI =
{
    New : (parent : (Canvas | Widget), layer : integer, options : Options) -> ItemPicker,
}

local itemPickerAPI : ItemPickerAPI = { };
local UNKNOWN_ITEM_ICON = "texture/ui/icons/question.png";
local ROW_COLOR = vec.New(0.065, 0.078, 0.088);
local ROW_HOVER_COLOR = vec.New(0.10, 0.16, 0.21);
local BORDER_COLOR = vec.New(0.22, 0.29, 0.34);
local MUTED_COLOR = vec.New(0.55, 0.61, 0.68);

function itemPickerAPI.New(parent : (Canvas | Widget), layer : integer, options : Options) : ItemPicker
    local modalWidth = options.width or 620;
    local modalHeight = options.height or 640;
    local contentWidth = modalWidth - 28;
    local listHeight = modalHeight - 132;
    local hideTooltip : () -> () = function() end;
    local modal = Modal.New(parent, vec.New(0, 0), vec.New(modalWidth, modalHeight), layer, { name = options.name, title = options.title or "Select Item", showActions = false, onCancel = function() hideTooltip(); end });
    local self : any = { root = modal.root, modal = modal, items = { }, onSelect = nil };
    self.tooltip = GameTooltip.New(modal.root);
    hideTooltip = function()
        self.tooltip.ClearLines();
        self.tooltip.Hide();
    end;

    self.search = InputField.New(modal.dialog, vec.New(14, -48), vec.New(contentWidth, 30), 2i, { placeholderText = options.searchPlaceholder or "Search by name or item ID...", defaultTextFontSize = 10 });
    self.search:SetAnchor(vec.New(0, 1));
    self.search:SetRelativePoint(vec.New(0, 1));
    self.search:SetTextVerticallyCentered();

    local function PositionTooltip()
        self.tooltip.SetPositionAroundCursor(UI.GetMousePos());
    end

    self.list = VirtualList.New(modal.dialog, vec.New(14, -86), vec.New(contentWidth, listHeight), 2i, {
        rowHeight = 50,
        scrollBox = { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = contentWidth - 10, contentSizeY = listHeight, verticalScrollBar = true, scrollBarWidth = 10, alpha = 0 },
        createRow = function(rowParent)
            local row = Button.New(rowParent, vec.New(0, 0), vec.New(contentWidth - 12, 48), 1i, { text = "", panelTemplate = "DebugDarkGrey", color = ROW_COLOR, hoverColor = ROW_HOVER_COLOR, borderColor = BORDER_COLOR, borderSize = 1 });
            row:SetAnchor(vec.New(0, 1));
            row:SetRelativePoint(vec.New(0, 1));
            row.root:SetClipChildren(true);
            row.bound = nil;

            row.icon = row.root:NewPanel(vec.New(6, 0), vec.New(36, 36), 2i, "DebugDarkGrey");
            row.icon:SetAnchor(vec.New(0, 0.5));
            row.icon:SetRelativePoint(vec.New(0, 0.5));
            row.icon:SetColor(vec.New(1, 1, 1));
            row.icon:SetBorderColor(MUTED_COLOR);
            row.icon:SetBorderSize(1);
            row.icon:SetInteractable(false);

            row.nameText = row.root:NewText("", vec.New(50, -10), 2i, "DefaultButtonText");
            row.nameText:SetAnchor(vec.New(0, 1));
            row.nameText:SetRelativePoint(vec.New(0, 1));
            row.nameText:SetFontSize(10);
            row.nameText:SetInteractable(false);

            row.detailText = row.root:NewText("", vec.New(50, -29), 2i, "DefaultButtonText");
            row.detailText:SetAnchor(vec.New(0, 1));
            row.detailText:SetRelativePoint(vec.New(0, 1));
            row.detailText:SetFontSize(8);
            row.detailText:SetColor(MUTED_COLOR);
            row.detailText:SetInteractable(false);

            row.idText = row.root:NewText("", vec.New(-8, 0), 2i, "DefaultButtonText");
            row.idText:SetAnchor(vec.New(1, 0.5));
            row.idText:SetRelativePoint(vec.New(1, 0.5));
            row.idText:SetFontSize(8);
            row.idText:SetColor(MUTED_COLOR);
            row.idText:SetInteractable(false);
            row:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton ~= 0i or row.bound == nil then return; end
                local onSelect = self.onSelect;
                local value = row.bound.value;
                self:Close();
                if onSelect ~= nil then onSelect(value); end
            end);
            row:SetOnHoverBegin(function()
                if row.bound == nil then return; end
                self.tooltip.SetItem(row.bound.value);
                PositionTooltip();
                self.tooltip.Show();
            end);
            row:SetOnHoverHeld(function()
                if row.bound == nil then return; end
                if not self.tooltip.ContainsItem(row.bound.value) then self.tooltip.SetItem(row.bound.value); end
                PositionTooltip();
                if not self.tooltip.IsShown() then self.tooltip.Show(); end
            end);
            row:SetOnHoverEnd(hideTooltip);
            return row;
        end,
        bindRow = function(row, _, item)
            row.bound = item;
            row.icon:SetBackground(item.icon or UNKNOWN_ITEM_ICON);
            row.nameText:SetText(TextUtil.TruncateToWidth(item.primary, "DefaultButtonText", contentWidth - 150));
            row.nameText:SetColor(if item.rarity ~= nil then ItemUtil.GetRarityColor(item.rarity) else vec.New(1, 1, 1));
            row.detailText:SetText(item.secondary or "");
            row.idText:SetText(item.trailing or tostring(item.value));
        end,
    });
    self.list:SetAnchor(vec.New(0, 1));
    self.list:SetRelativePoint(vec.New(0, 1));

    local footer = modal.dialog:NewText("Click an item to select it.", vec.New(14, 18), 2i, "DefaultButtonText");
    footer:SetAnchor(vec.New(0, 0));
    footer:SetRelativePoint(vec.New(0, 0));
    footer:SetFontSize(9);
    footer:SetColor(MUTED_COLOR);
    footer:SetInteractable(false);

    local closeButton = Button.New(modal.dialog, vec.New(-14, 12), vec.New(76, 28), 2i, { text = "Close", textTemplate = "DefaultButtonText" });
    closeButton:SetAnchor(vec.New(1, 0));
    closeButton:SetRelativePoint(vec.New(1, 0));
    closeButton:SetFontSize(9);
    closeButton:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i then self:Close(); end end);

    local function RefreshFilter()
        local query = string.lower(self.search:GetText());
        local filtered = { };
        for _, item in self.items do
            if query == "" or string.find(string.lower(item.search or item.primary), query, 1, true) ~= nil then
                table.insert(filtered, item);
            end
        end

        self.list:SetItems(filtered, false);
    end

    self.search:SetOnTextChanged(RefreshFilter);
    function self:Open(openOptions : OpenOptions)
        hideTooltip();
        self.items = openOptions.items;
        self.onSelect = openOptions.onSelect;
        self.search:SetText("");
        RefreshFilter();
        modal:SetTitle(openOptions.title or options.title or "Select Item");
        modal:Open();
        self.search:Focus();
    end

    function self:Close()
        hideTooltip();
        modal:Close();
    end

    function self:IsOpen() : boolean
        return modal:IsOpen();
    end

    return self :: ItemPicker;
end

return itemPickerAPI;
