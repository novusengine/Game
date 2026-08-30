local Button = require("@src/API/UI/Button");
local Draggable = require("@src/API/UI/Draggable");
local RootedWidget = require("@src/API/UI/RootedWidget");
local Theme = require("@src/API/Editor/UI/Theme");

export type Options =
{
    title : string,
    titleHeight : number?,
    padding : number?,
    windowColor : vec?,
    titleColor : vec?,
    alpha : number?,
    closeSize : vec?,
}

export type Window = RootedWidget.RootedPanel<
{
    root : Panel,
    titleBar : Panel,
    title : TextWidget,
    closeButton : Button.Button,
}>

export type WindowAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options) -> Window,
}

local windowAPI : WindowAPI = { };

function windowAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : Options)
    local titleHeight = options.titleHeight or 16;
    local padding = options.padding or 6;
    local root = parent:NewPanel(position, size, layer, "DebugDarkGrey");
    root:SetAnchor(vec.New(0.0, 1.0));
    root:SetRelativePoint(vec.New(0.0, 1.0));
    root:SetVisible(false);
    root:SetColor(options.windowColor or Theme.windowColor);
    root:SetAlpha(options.alpha or Theme.windowAlpha);
    root:SetBorderColor(Theme.borderColor);
    root:SetBorderSize(Theme.borderSize);

    local titleBar = root:NewPanel(vec.New(0, 0), vec.New(size.x, titleHeight), 1i, "DebugDarkGrey");
    titleBar:SetAnchor(vec.New(0.0, 1.0));
    titleBar:SetRelativePoint(vec.New(0.0, 1.0));
    titleBar:SetColor(options.titleColor or Theme.titleColor);
    titleBar:SetAlpha(options.alpha or Theme.windowAlpha);
    local title = titleBar:NewText(options.title, vec.New(padding, 0), 1i, Theme.textTemplate);
    title:SetAnchor(vec.New(0.0, 0.5));
    title:SetRelativePoint(vec.New(0.0, 0.5));
    title:SetInteractable(false);

    local closeSize = options.closeSize or vec.New(12, 12);
    local closeButton = Button.New(titleBar, vec.New(-2, 0), closeSize, 1i, {
        text = "X",
        textTemplate = Theme.textTemplate,
        borderColor = Theme.borderColor,
        borderSize = Theme.borderSize,
    });
    closeButton:SetAnchor(vec.New(1.0, 0.5));
    closeButton:SetRelativePoint(vec.New(1.0, 0.5));
    closeButton.text:SetPosX(1);
    Draggable.Attach(root, titleBar);
    local window = { root = root, titleBar = titleBar, title = title, closeButton = closeButton };
    RootedWidget.Attach(window);
    return window :: Window;
end

return windowAPI;
