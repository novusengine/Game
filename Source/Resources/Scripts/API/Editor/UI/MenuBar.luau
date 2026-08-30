local Button = require("@src/API/UI/Button");
local ContextMenu = require("@src/API/UI/ContextMenu");
local RootedWidget = require("@src/API/UI/RootedWidget");

export type Item =
{
    label : string,
    enabled : boolean?,
    onClick : (() -> ())?,
}

export type Menu =
{
    label : string,
    items : { Item },
}

export type Options =
{
    menus : { Menu },
    paddingLeft : number?,
    spacing : number?,
    buttonWidth : number?,
}

export type MenuBar = RootedWidget.RootedPanel<
{
    root : Panel,
    Close : (self : MenuBar) -> (),
}>

export type MenuBarAPI = { New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options) -> MenuBar }
local menuBarAPI : MenuBarAPI = { };
local BAR_COLOR = vec.New(0.10, 0.10, 0.12);
local DEFAULT_BUTTON_WIDTH = 56;

function menuBarAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : Options) : MenuBar
    local root = parent:NewPanel(position, size, layer, "DebugDarkGrey");
    root:SetColor(BAR_COLOR);
    root:SetAlpha(0.92);

    local self = {
        root = root,
        _buttons = { },
        _menus = { },
        _openIndex = nil,
    } :: any;
    RootedWidget.Attach(self);

    function self:Close()
        if self._openIndex == nil then return; end
        local index = self._openIndex;
        self._openIndex = nil;
        self._buttons[index]:SetHighlighted(false);
        self._menus[index]:Close();
    end

    local function Open(index : number)
        if self._openIndex == index then
            self:Close();
            return;
        end
        self:Close();
        self._openIndex = index;
        self._buttons[index]:SetHighlighted(true);
        self._menus[index]:OpenBelow(self._buttons[index].root);
    end

    local cursorX = options.paddingLeft or 4;
    local spacing = options.spacing or 0;
    local buttonWidth = options.buttonWidth or DEFAULT_BUTTON_WIDTH;
    for index, menu in options.menus do
        local button = Button.New(root, vec.New(cursorX, 0), vec.New(buttonWidth, size.y), 1i, {
            text = menu.label,
            textTemplate = "DevToolsText",
            color = BAR_COLOR,
            alpha = 0.0,
            hoverColor = vec.New(0.30, 0.55, 0.85),
            hoverAlpha = 0.45,
        });
        button:SetAnchor(vec.New(0.0, 0.0));
        button:SetRelativePoint(vec.New(0.0, 0.0));
        button:SetOnMouseUp(function() Open(index); end);
        button:SetOnHoverBegin(function()
            if self._openIndex ~= index then Open(index); end
        end);
        self._buttons[index] = button;
        self._menus[index] = ContextMenu.New(parent, {
            items = menu.items,
            layer = integer.add(layer, 2i),
            inputPassthrough = root,
            closeOnHoverOutside = true,
            onClose = function()
                if self._openIndex ~= index then return; end
                self._openIndex = nil;
                self._buttons[index]:SetHighlighted(false);
            end,
        });
        cursorX += buttonWidth + spacing;
    end

    return self :: MenuBar;
end

return menuBarAPI;
