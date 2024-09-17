local UITemplates = require("API/UI/Templates")
local UIStack = require("API/UI/UIStack")
local UISlider = require("API/UI/Slider")
local UICheckbox = require("API/UI/Checkbox")
local UICombobox = require("API/UI/Combobox")

-- TODO: Borders :(

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

local function CreateGameMenu(stack)
    -- Create Canvas
    local canvas = UI.GetCanvas("EscapeMenu", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here
    
    -- Create Panel
    local panel = canvas:NewPanel(0, 0, 300, 480, 0, "DialogBox");
    panel:SetEnabled(false);
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);
    
    -- Create Header
    local header = panel:NewPanel(0, 30, 200, 60, 0, "DialogBoxHeader");
    header:SetAnchor(0.5, 1.0);
    header:SetRelativePoint(0.5, 1.0);

    local headerText = header:NewText("Game Menu", 0, 0, 0, "YellowText");
    headerText:SetAnchor(0.5, 0.5);
    headerText:SetRelativePoint(0.5, 0.5);

    -- Create buttons
    local supportButton = panel:NewButton(0, -70, 250, 40, 0, "DefaultButton");
    supportButton:SetAnchor(0.5, 1.0);
    supportButton:SetRelativePoint(0.5, 0.5);
    supportButton:SetText("Support");

    local shopButton = panel:NewButton(0, -115, 250, 40, 0, "DefaultButton");
    shopButton:SetAnchor(0.5, 1.0);
    shopButton:SetRelativePoint(0.5, 0.5);
    shopButton:SetText("Shop");
    shopButton:SetInteractable(false);

    local optionsButton = panel:NewButton(0, -185, 250, 40, 0, "DefaultButton");
    optionsButton:SetAnchor(0.5, 1.0);
    optionsButton:SetRelativePoint(0.5, 0.5);
    optionsButton:SetText("Options");
    optionsButton:SetOnMouseUp(function(eventID, widget, ...)
        local stack = UIStack.Get("Menu");
        stack:Push("OptionsMenu");
    end)

    local macrosButton = panel:NewButton(0, -230, 250, 40, 0, "DefaultButton");
    macrosButton:SetAnchor(0.5, 1.0);
    macrosButton:SetRelativePoint(0.5, 0.5);
    macrosButton:SetText("Macros");

    local logoutButton = panel:NewButton(0, -300, 250, 40, 0, "DefaultButton");
    logoutButton:SetAnchor(0.5, 1.0);
    logoutButton:SetRelativePoint(0.5, 0.5);
    logoutButton:SetText("Logout");

    local exitButton = panel:NewButton(0, -345, 250, 40, 0, "DefaultButton");
    exitButton:SetAnchor(0.5, 1.0);
    exitButton:SetRelativePoint(0.5, 0.5);
    exitButton:SetText("Exit Game");

    local returnButton = panel:NewButton(0, -415, 250, 40, 0, "DefaultButton");
    returnButton:SetAnchor(0.5, 1.0);
    returnButton:SetRelativePoint(0.5, 0.5);
    returnButton:SetText("Return to Game");
    returnButton:SetOnMouseUp(function(eventID, widget, ...)
        local stack = UIStack.Get("Menu");
        stack:Pop();
    end)

    stack:Register("GameMenu", panel, 0); -- Name, widget, slot, onPush, onPop
    stack:Push("GameMenu")

    -- Stack should have max amount of open widgets
    -- If we push more than max, pop previous
    
end

function CreateOptionsMenu(stack)
    -- Create Canvas
    local canvas = UI.GetCanvas("OptionsMenu", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here
    
    -- Create Panel
    local panel = canvas:NewPanel(0, 0, 1000, 800, 0, "DialogBox");
    panel:SetEnabled(false);
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);

    local headerText = panel:NewText("Options", 0, 0, 0, "YellowText");
    headerText:SetAnchor(0.5, 1.0);
    headerText:SetRelativePoint(0.5, 1.0);

    -- Create buttons
    local exitButton = panel:NewButton(2, 2, 24, 24, 0, "DefaultButtonMinimize");
    exitButton:SetAnchor(1.0, 1.0);
    exitButton:SetRelativePoint(1.0, 1.0);
    exitButton:SetOnMouseUp(function(eventID, widget, ...)
        local stack = UIStack.Get("Menu");
        stack:Push("GameMenu");
    end);

    local volumeSlider = UISlider.NewSlider(panel, 0, -40, 200, 25, 0, {
        backgroundTemplate = "DefaultSliderBackground",
        fillTemplate = "DefaultSliderFill",
    });
    volumeSlider.slider:SetAnchor(0.5, 1.0);
    volumeSlider.background:SetRelativePoint(0.5, 1.0);
    volumeSlider:SetProgress(0.5);

    -- TODO: This
    --local volumeSliderText = volumeSlider:AddText("%cPct / %mPct", "DefaultButtonText");
    --volumeSliderText:SetAnchor(0.0, 0.5);
    --volumeSliderText:SetRelativePoint(1.0, 0.5);
    --volumeSliderText:SetPos(-20, 0);

    local testCheckbox = UICheckbox.NewCheckbox(panel, 0, -80, 24, 24, 0,
    {
        backgroundTemplate = "DefaultCheckboxBackground",
        fillTemplate = "DefaultCheckboxFill",
    });
    testCheckbox.checkbox:SetAnchor(0.5, 1.0);
    testCheckbox.background:SetRelativePoint(0.5, 1.0);
    
    testCheckbox:SetOnValueChanged(function(checkboxTable, state)
        return true
    end)

    local testCombobox = UICombobox.NewCombobox(panel, 0, -120, 200, 40, 0, {
        options = {"Foo", "Bar", "Fizz", "Buzz", "FizzBuzz"},
        backgroundTemplate = "DefaultComboboxPanel",
        textTemplate = "YellowText"
    });
    testCombobox.combobox:SetAnchor(0.5, 1.0);
    testCombobox.background:SetRelativePoint(0.5, 1.0);
    testCombobox:SetOnValueChanged(function(table, value)
        print(table.options[value]);
    end);
   
    stack:Register("OptionsMenu", panel, 0); -- Name, widget, slot, onPush, onPop
    stack:Push("OptionsMenu")
end

local function OnGameLoaded(eventID : number, motd : string)
    UITemplates.RegisterTemplates();

    local stack = UIStack.Get("Menu")

    --Demo();
    CreateGameMenu(stack);
    CreateOptionsMenu(stack);
    print('Hello from Demo.luau');
end

local function OnGameUpdated(eventID : number, motd : string)
    
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)
RegisterGameEvent(GameEvent.Updated, OnGameUpdated)