local UITemplates = require("UI/Templates")

local function OnButtonEvent(button : Button, eventID : ButtonEvent, ...)
    if (eventID == ButtonEvent.Hovered) then

    end
end

local function Demo()
    local canvas = UI.GetCanvas("EscapeMenu", 0, 0, 100, 100);

    -- Create a panel
    local panel = canvas:NewPanel(100, 100, 800, 800, 0, "DefaultPanel");

    local bottomLeft = panel:NewText("Bottom Left", 0, 0, 0, "MenuButtonText");
    bottomLeft:SetAnchor(0.0, 0.0);
    bottomLeft:SetRelativePoint(0.0, 0.0);

    local bottomRight = panel:NewText("Bottom Right", 0, 0, 0, "MenuButtonText");
    bottomRight:SetAnchor(1.0, 0.0);
    bottomRight:SetRelativePoint(1.0, 0.0);

    local topLeft = panel:NewText("Top Left", 0, 0, 0, "MenuButtonText");
    topLeft:SetAnchor(0.0, 1.0);
    topLeft:SetRelativePoint(0.0, 1.0);

    local topRight = panel:NewText("Top Right", 0, 0, 0, "MenuButtonText");
    topRight:SetAnchor(1.0, 1.0);
    topRight:SetRelativePoint(1.0, 1.0);

    local center = panel:NewText("Center", 0, 0, 0, "MenuButtonText");
    center:SetAnchor(0.5, 0.5);
    center:SetRelativePoint(0.5, 0.5);

    --local testPanel = panel:NewPanel(0, 0, 50, 50, 0, "DefaultPanel2");
    --testPanel:SetAnchor(0.5, 0.5);
    --testPanel:SetRelativePoint(0.5, 0.5);
end

local function CreateEscapeMenu()
    -- Create Canvas
    local canvas = UI.GetCanvas("EscapeMenu", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here
    
    -- Create Panel
    local panel = canvas:NewPanel(0, 0, 300, 480, 0, "MenuPanel");
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);

    -- Create Header
    local header = panel:NewPanel(0, 30, 200, 60, 0, "MenuPanelHeader");
    header:SetAnchor(0.5, 1.0);
    header:SetRelativePoint(0.5, 1.0);

    local headerText = header:NewText("Game Menu", 0, 0, 0, "MenuPanelHeaderText");
    headerText:SetAnchor(0.5, 0.5);
    headerText:SetRelativePoint(0.5, 0.5);

    -- Create buttons
    local supportButton = panel:NewButton(0, -70, 250, 40, 0, "MenuButton");
    supportButton:SetAnchor(0.5, 1.0);
    supportButton:SetRelativePoint(0.5, 0.5);
    supportButton:SetText("Support");

    local shopButton = panel:NewButton(0, -115, 250, 40, 0, "MenuButton");
    shopButton:SetAnchor(0.5, 1.0);
    shopButton:SetRelativePoint(0.5, 0.5);
    shopButton:SetText("Shop");
    shopButton:SetInteractable(false);

    local optionsButton = panel:NewButton(0, -185, 250, 40, 0, "MenuButton");
    optionsButton:SetAnchor(0.5, 1.0);
    optionsButton:SetRelativePoint(0.5, 0.5);
    optionsButton:SetText("Options");

    local macrosButton = panel:NewButton(0, -230, 250, 40, 0, "MenuButton");
    macrosButton:SetAnchor(0.5, 1.0);
    macrosButton:SetRelativePoint(0.5, 0.5);
    macrosButton:SetText("Macros");

    local logoutButton = panel:NewButton(0, -300, 250, 40, 0, "MenuButton");
    logoutButton:SetAnchor(0.5, 1.0);
    logoutButton:SetRelativePoint(0.5, 0.5);
    logoutButton:SetText("Logout");

    local exitButton = panel:NewButton(0, -345, 250, 40, 0, "MenuButton");
    exitButton:SetAnchor(0.5, 1.0);
    exitButton:SetRelativePoint(0.5, 0.5);
    exitButton:SetText("Exit Game");

    local returnButton = panel:NewButton(0, -415, 250, 40, 0, "MenuButton");
    returnButton:SetAnchor(0.5, 1.0);
    returnButton:SetRelativePoint(0.5, 0.5);
    returnButton:SetText("Return to Game");
end

local function OnGameLoaded(eventID : number, motd : string)
    UITemplates.RegisterTemplates();

    --Demo();
    CreateEscapeMenu();
    print('Hello from Demo.luau');
end

local function OnGameUpdated(eventID : number, motd : string)
    
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)
RegisterGameEvent(GameEvent.Updated, OnGameUpdated)