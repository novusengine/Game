local UITemplates = require("UI/Templates")

local function OnButtonEvent(button : Button, eventID : ButtonEvent, ...)
    if (eventID == ButtonEvent.Hovered) then

    end
end

local function Demo()
    local canvas = UI.GetCanvas("EscapeMenu", 0, 0, 100, 100);

    -- Create a panel
    local panel = canvas:NewPanel(100, 100, 800, 800, 0, "DefaultPanel");

    local bottomLeft = panel:NewText("Bottom Left", 0, 0, 0, "DefaultText");
    bottomLeft:SetAnchor(0.0, 0.0);
    bottomLeft:SetRelativePoint(0.0, 0.0);

    local bottomRight = panel:NewText("Bottom Right", 0, 0, 0, "DefaultText");
    bottomRight:SetAnchor(1.0, 0.0);
    bottomRight:SetRelativePoint(1.0, 0.0);

    local topLeft = panel:NewText("Top Left", 0, 0, 0, "DefaultText");
    topLeft:SetAnchor(0.0, 1.0);
    topLeft:SetRelativePoint(0.0, 1.0);

    local topRight = panel:NewText("Top Right", 0, 0, 0, "DefaultText");
    topRight:SetAnchor(1.0, 1.0);
    topRight:SetRelativePoint(1.0, 1.0);

    local center = panel:NewText("Center", 0, 0, 0, "DefaultText");
    center:SetAnchor(0.5, 0.5);
    center:SetRelativePoint(0.5, 0.5);

    local testPanel = panel:NewPanel(0, 0, 50, 50, 0, "DefaultPanel2");
    testPanel:SetAnchor(0.5, 0.5);
    testPanel:SetRelativePoint(0.5, 0.5);
end

local function CreateEscapeMenu()
    -- Create Canvas
    local canvas = UI.GetCanvas("EscapeMenu", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here
    
    -- Create Panel
    local panel = canvas:NewPanel(0, 0, 400, 600, 0, "MenuPanel");
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);

    -- Create Header
    local header = panel:NewPanel(0, 30, 400, 100, 0, "MenuPanelHeader");
    header:SetAnchor(0.5, 1.0);
    header:SetRelativePoint(0.5, 1.0);

    local headerText = header:NewText("Game Menu", 0, -25, 0, "MenuPanelHeaderText");
    headerText:SetAnchor(0.5, 1.0);
    headerText:SetRelativePoint(0.5, 0.5);

    -- Create buttons
    local optionsButton = panel:NewButton(0, -100, 100, 50, 0, "MenuButton");
    optionsButton:SetAnchor(0.5, 1.0);
    optionsButton:SetRelativePoint(0.5, 0.5);
    optionsButton:SetText("Options");

    local creditsButton = panel:NewButton(0, -150, 100, 50, 0, "MenuButton");
    creditsButton:SetAnchor(0.5, 1.0);
    creditsButton:SetRelativePoint(0.5, 0.5);
    creditsButton:SetText("Credits");
end

local function OnGameLoaded(eventID : number, motd : string)
    UITemplates.RegisterTemplates();

    CreateEscapeMenu();
    print('Hello from Demo.luau');
end

local function OnGameUpdated(eventID : number, motd : string)
    
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)
RegisterGameEvent(GameEvent.Updated, OnGameUpdated)