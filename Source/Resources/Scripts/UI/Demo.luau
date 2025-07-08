local UIStack = require("@src/API/UI/UIStack")
local UIButton = require("@src/API/UI/Button")
local UILoadingScreen = require("@src/API/UI/LoadingScreen")

-- TODO: Borders :(

local function OnButtonEvent(button : Button, eventID : ButtonEvent, ...)
    if (eventID == ButtonEvent.Hovered) then
        print("Hovered!");
    end
end

local function CreateSingleLineDemo()
    local canvas : Canvas = UI.GetCanvas("SingleLineDemo", 0, 0, 400, 400, true);

    -- Create a panel
    local panel = canvas:NewPanel(0, 0, 400, 400, 0, "DialogBox");
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);

    local bottomLeft = panel:NewText("Bottom Left", 0, 0, 0, "DefaultButtonText");
    bottomLeft:SetAnchor(0.0, 0.0);
    bottomLeft:SetRelativePoint(0.0, 0.0);

    local bottomRight = panel:NewText("Bottom Right", 0, 0, 0, "DefaultButtonText");
    bottomRight:SetAnchor(1.0, 0.0);
    bottomRight:SetRelativePoint(1.0, 0.0);

    local topLeft = panel:NewText("Top Left", 0, 0, 0, "DefaultButtonText");
    topLeft:SetAnchor(0.0, 1.0);
    topLeft:SetRelativePoint(0.0, 1.0);

    local topRight = panel:NewText("Top Right", 0, 0, 0, "DefaultButtonText");
    topRight:SetAnchor(1.0, 1.0);
    topRight:SetRelativePoint(1.0, 1.0);

    local center = panel:NewText("Center", 0, 0, 0, "DefaultButtonText");
    center:SetAnchor(0.5, 0.5);
    center:SetRelativePoint(0.5, 0.5);

    -- Test button
    local supportButton = UIButton.NewButton(panel, 0, 70, 250, 40, 0, {
        text = "Test"
    });
    supportButton:SetAnchor(0.5, 0.5);
    supportButton:SetRelativePoint(0.5, 0.5);
    supportButton:SetOnMouseUp(function(eventID, widget, ...)
        print("SingleLineButton");
    end);

    return canvas;
end

local function CreateMultiLineDemo(rtCanvas : Canvas)
    local canvas : Canvas = UI.GetCanvas("MultiLineDemo", 0, 0, 1920, 1080);

    -- Create a panel displaying the single line demo
    --local singleLinePanel = canvas:NewPanel(0, 0, 400, 400, 0, "DialogBox");
    --singleLinePanel:SetAnchor(0.5, 0.5);
    --singleLinePanel:SetRelativePoint(0.5, 0.5);
    --singleLinePanel:SetBackground(rtCanvas);
    --singleLinePanel:DebugSetWorldTransformIndex(0);

    -- Create a panel
    local panel = canvas:NewPanel(0, 0, 400, 400, 0, "DialogBox");
    --panel:SetAnchor(0.5, 0.5);
    --panel:SetRelativePoint(0.5, 0.5);

    panel:SetAnchor(0.0, 0.0);
    panel:SetRelativePoint(1.0, 0.0);
    --panel:DebugSetWorldTransformIndex(0);

    local middlePanel = panel:NewPanel(0, 0, 50, 50, 0, "DialogBox");
    middlePanel:SetAnchor(0.0, 0.0);
    middlePanel:SetRelativePoint(0.0, 0.0);

    local bottomLeft = panel:NewText("Bottom Left\nLeft Bottom", 0, 0, 0, "DefaultButtonText");
    bottomLeft:SetAnchor(0.0, 0.0);
    bottomLeft:SetRelativePoint(0.0, 0.0);

    local bottomRight = panel:NewText("Bottom Right\nRight Bottom", 0, 0, 0, "DefaultButtonText");
    bottomRight:SetAnchor(1.0, 0.0);
    bottomRight:SetRelativePoint(1.0, 0.0);

    local topLeft = panel:NewText("Top Left\nLeft Top\nLeft Left", 0, 0, 0, "DefaultButtonText");
    topLeft:SetAnchor(0.0, 1.0);
    topLeft:SetRelativePoint(0.0, 1.0);

    local topRight = panel:NewText("Top Right\nRight Top", 0, 0, 0, "DefaultButtonText");
    topRight:SetAnchor(1.0, 1.0);
    topRight:SetRelativePoint(1.0, 1.0);

    local center = panel:NewText("-Center\nCenter-", 0, 0, 0, "DefaultButtonText");
    center:SetAnchor(0.5, 0.5);
    center:SetRelativePoint(0.5, 0.5);

    -- Test button
    --local supportButton = UIButton.NewButton(panel, 0, 70, 250, 40, 0, {
    --    text = "Test"
    --});
    --supportButton:SetAnchor(0.5, 0.5);
    --supportButton:SetRelativePoint(0.5, 0.5);
    --supportButton:SetOnMouseUp(function(eventID, widget, ...)
    --    print("MultiLineButton");
    --end);
end

local function Demo()
    local rtCanvas = CreateSingleLineDemo()
    CreateMultiLineDemo(rtCanvas)
end

local function CreateClippingDemo()
    local canvas = UI.GetCanvas("ClippingDemo", 0, 0, 1920, 1080);

    -- Create a panel
    local clippingParent = canvas:NewPanel(215, 0, 400, 400, 0, "DebugRed");
    clippingParent:SetAnchor(0.5, 0.5);
    clippingParent:SetRelativePoint(0.5, 0.5);

    clippingParent:SetClipChildren(true);
    clippingParent:SetClipRect(0, 0, 1, 1);
    clippingParent:SetClipMaskTexture("Data/Texture/interface/masks/circlemaskscalable.dds");

    --clippingParent:SetClipMaskTexture(nil);
    --clippingParent:SetClipChildren(false);

    local clippingChild = clippingParent:NewPanel(0, 0, 300, 400, 0, "DebugBlue");
    clippingChild:SetAnchor(0.5, 0.0);
    clippingChild:SetRelativePoint(0.5, 0.5);

    -- Create a text
    local clippingText = clippingParent:NewText("Test text", 100, 30, 0, "DefaultButtonText");
    --clippingText:SetClipRect(0, 0, 1, 0.3);
    --clippingText:SetClipMaskTexture("Data/Texture/interface/masks/circlemaskscalable.dds");
end

local function OnGameLoaded(eventID : number, motd : string)
    local stack = UIStack.Get("Menu")

    --CreateClippingDemo();
    --Demo();
    --CreateGameMenu(stack);
    --CreateOptionsMenu(stack);

    --UILoadingScreen.SetLoadingScreen(nil)
    --print('Hello from Demo.luau');
end
RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)