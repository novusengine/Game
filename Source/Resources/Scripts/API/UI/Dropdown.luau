local ContextMenu = require("@src/API/UI/ContextMenu")
local Button = require("@src/API/UI/Button")
local RootedWidget = require("@src/API/UI/RootedWidget")

-- Generic button-backed dropdown. Options use { label/name, value, enabled? } entries;
-- selection menus are consistently anchored below the owning button.
export type Option = { label : string?, name : string?, value : any, enabled : boolean? }
export type Options =
{
    options : { Option }?,
    value : any?,
    placeholder : string?,
    arrow : string?,
    previewMaxCharacters : number?,
    onValueChanged : ((any, Option?) -> ())?,
    button : Button.Options?,
    menuLayer : integer?,
    menuWidth : number?,
    itemHeight : number?,
}
export type Dropdown =
{
    root : Panel,
    button : Button.Button,
    GetSelectedOption : (self : Dropdown) -> Option?,
    SetValue : (self : Dropdown, value : any, notify : boolean?) -> (),
    SetOptions : (self : Dropdown, options : { Option }) -> (),
    SetOnValueChanged : (self : Dropdown, callback : (any, Option?) -> ()) -> (),
    GetValue : (self : Dropdown) -> any,
    Open : (self : Dropdown) -> (),
}
export type DropdownAPI = { New : (parent : Widget, canvas : Widget, position : vec, size : vec, layer : integer, options : Options?) -> Dropdown }
local dropdownAPI : DropdownAPI = { };

local function GetPreviewLabel(label, maximumCharacters)
    if maximumCharacters == nil or #label <= maximumCharacters then
        return label;
    end
    return string.sub(label, 1, math.max(0, maximumCharacters - 3)) .. "...";
end

function dropdownAPI.New(parent : Widget, canvas : Widget, position : vec, size : vec, layer : integer, options : Options?)
    options = options or { };

    local self = {
        options = options.options or { },
        value = options.value,
        placeholder = options.placeholder or "Select...",
        arrow = options.arrow or "  v",
        previewMaxCharacters = options.previewMaxCharacters,
        onValueChanged = options.onValueChanged,
    };

    self.button = Button.New(parent, position, size, layer, options.button or { });
    self.root = self.button.root;
    RootedWidget.Attach(self);
    self.button:SetAnchor(vec.New(0.0, 1.0));
    self.button:SetRelativePoint(vec.New(0.0, 1.0));

    self.menu = ContextMenu.New(canvas, {
        items = { },
        layer = options.menuLayer,
        minWidth = options.menuWidth or size.x,
        itemHeight = options.itemHeight,
    });

    function self:GetSelectedOption()
        for _, option in self.options do
            if option.value == self.value then
                return option;
            end
        end
        return nil;
    end

    function self:Refresh()
        local selected = self:GetSelectedOption();
        local label = selected and (selected.label or selected.name) or self.placeholder;
        self.button:SetText(`{GetPreviewLabel(tostring(label), self.previewMaxCharacters)}{self.arrow}`);
    end

    function self:SetValue(value, notify)
        if value == self.value then
            return true;
        end

        local previousValue = self.value;
        self.value = value;
        self:Refresh();
        if notify and self.onValueChanged then
            local accepted = self.onValueChanged(value, self:GetSelectedOption());
            if accepted == false then
                self.value = previousValue;
                self:Refresh();
                return false;
            end
        end
        return true;
    end

    function self:SetOptions(newOptions)
        self.options = newOptions or { };
        self:Refresh();
    end

    function self:SetOnValueChanged(callback)
        self.onValueChanged = callback;
    end

    function self:GetValue()
        return self.value;
    end

    function self:Open()
        local items = { };
        local function SelectValue(value)
            return function()
                self:SetValue(value, true);
            end
        end

        for _, option in self.options do
            local optionLabel = option.label or option.name or tostring(option.value);
            local optionEnabled = option.enabled ~= false;
            table.insert(items, {
                label = optionLabel,
                selected = option.value == self.value,
                enabled = optionEnabled,
                onClick = SelectValue(option.value),
            });
        end
        self.menu:SetItems(items);
        self.menu:OpenBelow(self.button.root);
    end

    function self:SetInteractable(interactable)
        self.button:SetInteractable(interactable);
    end

    function self:SetAnchor(anchor)
        self.button:SetAnchor(anchor);
    end

    function self:SetRelativePoint(point)
        self.button:SetRelativePoint(point);
    end

    function self:SetPos(position)
        self.button:SetPos(position);
    end

    function self:SetVisible(visible)
        self.button:SetVisible(visible);
        if not visible then
            self.menu:Close();
        end
    end

    self.button:SetOnMouseUp(function(_, _, mouseButton)
        if mouseButton == 0i then self:Open(); end
    end);
    self:Refresh();
    return self;
end

return dropdownAPI;
