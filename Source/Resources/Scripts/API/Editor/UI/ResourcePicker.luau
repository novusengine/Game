local Button = require("@src/API/UI/Button");
local InputField = require("@src/API/Editor/UI/InputField");
local Modal = require("@src/API/UI/Modal");
local TextUtil = require("@src/API/UI/TextUtil");
local VirtualList = require("@src/API/UI/VirtualList");

export type Item =
{
    value : integer,
    primary : string,
    secondary : string?,
    search : string?,
    trailing : string?,
}

export type Options =
{
    name : string,
    title : string,
    searchPlaceholder : string,
    footerText : string,
    width : number?,
    height : number?,
}

export type OpenOptions =
{
    title : string?,
    items : { Item },
    onSelect : (integer) -> (),
}

export type ResourcePicker =
{
    root : Panel,
    Open : (self : ResourcePicker, options : OpenOptions) -> (),
    Close : (self : ResourcePicker) -> (),
    IsOpen : (self : ResourcePicker) -> boolean,
}

export type ResourcePickerAPI =
{
    New : (parent : (Canvas | Widget), layer : integer, options : Options) -> ResourcePicker,
}

local resourcePickerAPI : ResourcePickerAPI = { };
local ROW_COLOR = vec.New(0.065, 0.078, 0.088);
local ROW_HOVER_COLOR = vec.New(0.10, 0.16, 0.21);
local BORDER_COLOR = vec.New(0.22, 0.29, 0.34);
local MUTED_COLOR = vec.New(0.55, 0.61, 0.68);

function resourcePickerAPI.New(parent : (Canvas | Widget), layer : integer, options : Options) : ResourcePicker
    local modalWidth = options.width or 560;
    local modalHeight = options.height or 590;
    local contentWidth = modalWidth - 28;
    local listHeight = modalHeight - 132;
    local modal = Modal.New(parent, vec.New(0, 0), vec.New(modalWidth, modalHeight), layer, { name = options.name, title = options.title, showActions = false });
    local self : any = { root = modal.root, modal = modal, items = { }, onSelect = nil };

    self.search = InputField.New(modal.dialog, vec.New(14, -48), vec.New(contentWidth, 30), 2i, { placeholderText = options.searchPlaceholder, defaultTextFontSize = 10 });
    self.search:SetAnchor(vec.New(0, 1));
    self.search:SetRelativePoint(vec.New(0, 1));
    self.search:SetTextVerticallyCentered();

    self.list = VirtualList.New(modal.dialog, vec.New(14, -86), vec.New(contentWidth, listHeight), 2i, {
        rowHeight = 46,
        scrollBox = { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = contentWidth - 10, contentSizeY = listHeight, verticalScrollBar = true, scrollBarWidth = 10, alpha = 0 },
        createRow = function(rowParent)
            local row = Button.New(rowParent, vec.New(0, 0), vec.New(contentWidth - 12, 44), 1i, { text = "", panelTemplate = "DebugDarkGrey", color = ROW_COLOR, hoverColor = ROW_HOVER_COLOR, borderColor = BORDER_COLOR, borderSize = 1 });
            row:SetAnchor(vec.New(0, 1));
            row:SetRelativePoint(vec.New(0, 1));
            row.root:SetClipChildren(true);
            row.bound = nil;

            row.nameText = row.root:NewText("", vec.New(9, -8), 2i, "DefaultButtonText");
            row.nameText:SetAnchor(vec.New(0, 1));
            row.nameText:SetRelativePoint(vec.New(0, 1));
            row.nameText:SetFontSize(10);
            row.nameText:SetInteractable(false);

            row.detailText = row.root:NewText("", vec.New(9, -27), 2i, "DefaultButtonText");
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
            return row;
        end,
        bindRow = function(row, _, item)
            row.bound = item;
            row.nameText:SetText(TextUtil.TruncateToWidth(item.primary, "DefaultButtonText", contentWidth - 100));
            row.detailText:SetText(item.secondary or "");
            row.idText:SetText(item.trailing or tostring(item.value));
        end,
    });
    self.list:SetAnchor(vec.New(0, 1));
    self.list:SetRelativePoint(vec.New(0, 1));

    local footer = modal.dialog:NewText(options.footerText, vec.New(14, 18), 2i, "DefaultButtonText");
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
            if query == "" or string.find(string.lower(item.search or item.primary), query, 1, true) ~= nil then table.insert(filtered, item); end
        end
        self.list:SetItems(filtered, false);
    end

    self.search:SetOnTextChanged(RefreshFilter);
    function self:Open(openOptions : OpenOptions)
        self.items = openOptions.items;
        self.onSelect = openOptions.onSelect;
        self.search:SetText("");
        RefreshFilter();
        modal:SetTitle(openOptions.title or options.title);
        modal:Open();
        self.search:Focus();
    end

    function self:Close()
        modal:Close();
    end

    function self:IsOpen() : boolean
        return modal:IsOpen();
    end

    return self :: ResourcePicker;
end

return resourcePickerAPI;
