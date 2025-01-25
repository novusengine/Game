local UIStack = require("API/UI/UIStack")
local UIButton = require("API/UI/Button")
local UISlider = require("API/UI/Slider")
local UICheckbox = require("API/UI/Checkbox")
local UIComboBox = require("API/UI/ComboBox")
local UIInputBox = require("API/UI/InputBox")
local UILoadingScreen = require("API/UI/LoadingScreen")

-- TODO: Borders :(

local function OnButtonEvent(button : Button, eventID : ButtonEvent, ...)
    if (eventID == ButtonEvent.Hovered) then

    end
end

local function Demo()
    local canvas = UI.GetCanvas("EscapeMenu", 0, 0, 1920, 1080);

    -- Create a panel
    local panel = canvas:NewPanel(0, 0, 800, 800, 0, "DialogBox");
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
    local supportButton = UIButton.NewButton(panel, 0, -70, 250, 40, 0, {
        text = "Support"
    });
    supportButton:SetAnchor(0.5, 1.0);
    supportButton:SetRelativePoint(0.5, 0.5);

    local shopButton = UIButton.NewButton(panel, 0, -115, 250, 40, 0, {
        text = "Shop"
    });
    shopButton:SetAnchor(0.5, 1.0);
    shopButton:SetRelativePoint(0.5, 0.5);
    shopButton:SetInteractable(false);

    local optionsButton = UIButton.NewButton(panel, 0, -185, 250, 40, 0, {
        text = "Options"
    });
    optionsButton:SetAnchor(0.5, 1.0);
    optionsButton:SetRelativePoint(0.5, 0.5);
    optionsButton:SetOnMouseUp(function(eventID, widget, ...)
        local stack = UIStack.Get("Menu");
        stack:Push("OptionsMenu");
    end)

    local macrosButton = UIButton.NewButton(panel, 0, -230, 250, 40, 0, {
        text = "Macros"
    });
    macrosButton:SetAnchor(0.5, 1.0);
    macrosButton:SetRelativePoint(0.5, 0.5);

    local logoutButton = UIButton.NewButton(panel, 0, -300, 250, 40, 0, {
        text = "Logout"
    });
    logoutButton:SetAnchor(0.5, 1.0);
    logoutButton:SetRelativePoint(0.5, 0.5);

    local exitButton = UIButton.NewButton(panel, 0, -345, 250, 40, 0, {
        text = "Exit Game"
    });
    exitButton:SetAnchor(0.5, 1.0);
    exitButton:SetRelativePoint(0.5, 0.5);

    local returnButton = UIButton.NewButton(panel, 0, -415, 250, 40, 0, {
        text = "Return to Game"
    });
    returnButton:SetAnchor(0.5, 1.0);
    returnButton:SetRelativePoint(0.5, 0.5);
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

    local headerText = panel:NewText("Options", 0, -5, 0, "YellowText");
    headerText:SetAnchor(0.5, 1.0);
    headerText:SetRelativePoint(0.5, 1.0);

    -- Create buttons
    local exitButton = UIButton.NewButton(panel, 2, 2, 24, 24, 0, {
        panelTemplate = "DefaultButtonMinimizePanel",
    });
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
        return true; -- Returning false will prevent the value from changing
    end)

    local testComboBox = UIComboBox.NewComboBox(panel, 0, -120, 200, 40, 0, {
        options = {"Foo", "Bar", "Fizz", "Buzz", "FizzBuzz"}
    });
    testComboBox.comboBox:SetAnchor(0.5, 1.0);
    testComboBox.button:SetRelativePoint(0.5, 1.0);
    testComboBox:SetOnValueChanged(function(table, index)
        return true; -- Returning false will prevent the value from changing
    end);

    local testInputBox = UIInputBox.NewInputBox(panel, 0, -170, 200, 40, 0, {
        defaultText = "Test",
    });
    testInputBox.inputBox:SetAnchor(0.5, 1.0);
    testInputBox.button:SetRelativePoint(0.5, 1.0);
    testInputBox:SetOnSubmit(function(table)
        print(table.text)
        return true; -- Returning true will unfocus the widget
    end);
   
    stack:Register("OptionsMenu", panel, 0); -- Name, widget, slot, onPush, onPop
    --stack:Push("OptionsMenu")
end

local function OnGameLoaded(eventID : number, motd : string)
    local stack = UIStack.Get("Menu")

    --Demo();
    --CreateGameMenu(stack);
    --CreateOptionsMenu(stack);

    --UILoadingScreen.SetLoadingScreen(nil)
    print('Hello from Demo.luau');
end

local function OnGameUpdated(eventID : number, deltaTime : number)
    
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)
RegisterGameEvent(GameEvent.Updated, OnGameUpdated)