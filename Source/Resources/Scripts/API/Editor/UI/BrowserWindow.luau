local EditorWindow = require("@src/API/Editor/UI/Window");
local HorizontalLayout = require("@src/API/UI/HorizontalLayout");
local RootedWidget = require("@src/API/UI/RootedWidget");
local VerticalLayout = require("@src/API/UI/VerticalLayout");

export type Options =
{
    title : string,
    titleHeight : number?,
    toolbarHeight : number?,
    browserWidth : number?,
}

export type BrowserWindow = RootedWidget.RootedPanel<
{
    root : Panel,
    window : EditorWindow.Window,
    toolbar : any,
    body : any,
    browser : any,
    inspector : any,
    bodyHeight : number,
    browserWidth : number,
    inspectorWidth : number,
}>

export type BrowserWindowAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options) -> BrowserWindow,
}

local browserWindowAPI : BrowserWindowAPI = { };

local CONTENT_PADDING = 8;
local CONTENT_SPACING = 6;
local SPLIT_SPACING = 8;
local BROWSER_PADDING = 8;
local BROWSER_SPACING = 6;
local INSPECTOR_PADDING = 8;
local INSPECTOR_SPACING = 7;

function browserWindowAPI.New(parent : Widget, position : vec, size : vec, layer : integer, options : Options) : BrowserWindow
    local titleHeight = options.titleHeight or 18;
    local toolbarHeight = options.toolbarHeight or 26;
    local browserWidth = options.browserWidth or 320;
    local contentWidth = size.x - CONTENT_PADDING * 2;
    local bodyHeight = size.y - titleHeight - CONTENT_PADDING * 2 - toolbarHeight - CONTENT_SPACING;
    local inspectorWidth = contentWidth - browserWidth - SPLIT_SPACING;

    if bodyHeight <= 0 then error("BrowserWindow requires enough height for its title, toolbar, and content padding", 2); end
    if inspectorWidth <= 0 then error("BrowserWindow browser width leaves no room for the inspector", 2); end

    local window = EditorWindow.New(parent, position, size, layer, {
        title = options.title,
        titleHeight = titleHeight,
    });

    local content = VerticalLayout.New(window.root, vec.New(0, -titleHeight), vec.New(size.x, size.y - titleHeight), 1i, {
        panelTemplate = "DebugDarkGrey",
        alpha = 0.0,
        padding = CONTENT_PADDING,
        spacing = CONTENT_SPACING,
        defaultChildOpts = { modeX = "flex" },
    });
    content:SetAnchor(vec.New(0.0, 1.0));
    content:SetRelativePoint(vec.New(0.0, 1.0));

    local toolbar = HorizontalLayout.New(content, vec.New(0, 0), vec.New(contentWidth, toolbarHeight), 1i, {
        panelTemplate = "DebugDarkGrey",
        alpha = 0.0,
        spacing = 6,
        alignV = "center",
    });
    content:SetChildOpts(toolbar, { modeX = "flex", sizeY = toolbarHeight });

    local body = HorizontalLayout.New(content, vec.New(0, 0), vec.New(contentWidth, bodyHeight), 1i, {
        panelTemplate = "DebugDarkGrey",
        alpha = 0.0,
        spacing = SPLIT_SPACING,
    });
    content:SetChildOpts(body, { modeX = "flex", sizeY = bodyHeight });

    local browser = VerticalLayout.New(body, vec.New(0, 0), vec.New(browserWidth, bodyHeight), 1i, {
        panelTemplate = "DebugDarkGrey",
        padding = BROWSER_PADDING,
        spacing = BROWSER_SPACING,
        defaultChildOpts = { modeX = "flex" },
    });
    local inspector = VerticalLayout.New(body, vec.New(0, 0), vec.New(inspectorWidth, bodyHeight), 1i, {
        panelTemplate = "DebugDarkGrey",
        padding = INSPECTOR_PADDING,
        spacing = INSPECTOR_SPACING,
        defaultChildOpts = { modeX = "flex" },
    });
    body:SetChildOpts(browser, { sizeX = browserWidth, modeY = "flex" });
    body:SetChildOpts(inspector, { modeX = "flex", modeY = "flex" });

    local result = {
        root = window.root,
        window = window,
        toolbar = toolbar,
        body = body,
        browser = browser,
        inspector = inspector,
        bodyHeight = bodyHeight,
        browserWidth = browserWidth,
        inspectorWidth = inspectorWidth,
    };
    RootedWidget.Attach(result);
    return result :: BrowserWindow;
end

return browserWindowAPI;
