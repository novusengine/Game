local UIStack = require("@src/API/UI/UIStack")
local UIButton = require("@src/API/UI/Button")
local UISlider = require("@src/API/UI/Slider")
local UICheckbox = require("@src/API/UI/Checkbox")
local UIComboBox = require("@src/API/UI/ComboBox")
local UIInputBox = require("@src/API/UI/InputBox")
local OptionsContext = require("@src/API/OptionsContext")

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

local function CreateSidePanel(parent : Panel, width : number)
    local height = parent:GetHeight();

    local sidePanel = parent:NewPanel(0, 0, width, height, 0, "DialogBox");
    sidePanel:SetAnchor(0.0, 0.5);
    sidePanel:SetRelativePoint(0.0, 0.5);
    --sidePanel:SetAlpha(0.0);

    -- Categories
    local categories = OptionsContext:GetCategories();
    local currentY = 0;

    for i, category in categories do
        local categoryText = sidePanel:NewText(category.name, 0, currentY, 0, "DefaultButtonText");
        categoryText:SetAnchor(0.0, 1.0);
        categoryText:SetRelativePoint(0.0, 1.0);

        currentY -= select(2, categoryText:GetSize()) + 5;

        for j, group in category.groups.list do
            local groupText = sidePanel:NewText(group.name, 10, currentY, 0, "DefaultButtonText");
            groupText:SetAnchor(0.0, 1.0);
            groupText:SetRelativePoint(0.0, 1.0);

            currentY -= select(2, groupText:GetSize()) + 5;

            for k, section in group.sections.list do
                local sectionText = sidePanel:NewText(section.name, 20, currentY, 0, "DefaultButtonText");
                sectionText:SetAnchor(0.0, 1.0);
                sectionText:SetRelativePoint(0.0, 1.0);

                currentY -= select(2, sectionText:GetSize()) + 5;
            end
        end
    end

    return sidePanel;
end

function CreateOptionsMenu(stack)
    -- Create Canvas
    local canvas = UI.GetCanvas("OptionsMenu", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here
    
    local width = 1000;
    local height = 800;

    -- Create Panel
    local panel = canvas:NewPanel(0, 0, width, height, 0, "DialogBox");
    panel:SetEnabled(false);
    panel:SetAnchor(0.5, 0.5);
    panel:SetRelativePoint(0.5, 0.5);

    local headerText = panel:NewText("Options", 0, -5, 0, "YellowText");
    headerText:SetAnchor(0.5, 1.0);
    headerText:SetRelativePoint(0.5, 1.0);

    -- Exit button
    local exitButton = UIButton.NewButton(panel, 2, 2, 24, 24, 0, {
        panelTemplate = "DefaultButtonMinimizePanel",
    });
    exitButton:SetAnchor(1.0, 1.0);
    exitButton:SetRelativePoint(1.0, 1.0);
    exitButton:SetOnMouseUp(function(eventID, widget, ...)
        local stack = UIStack.Get("Menu");
        stack:Push("GameMenu");
    end);

    -- Content panel
    local contentHeight = height - select(2, headerText:GetSize()) - 15;
    local contentPanel = panel:NewPanel(0, 0, width, contentHeight, 0, "DebugBlue");
    contentPanel:SetAnchor(0.5, 0.0);
    contentPanel:SetRelativePoint(0.5, 0.0);
    contentPanel:SetAlpha(0.0);

    -- Left Side panel
    local sidePanelWidth = 250;
    CreateSidePanel(contentPanel, sidePanelWidth);

    -- Options
    --local optionGroups = OptionsContext:GetGroups();
    --for i, group in optionGroups do
    --    local groupText = panel:NewText(group.name, 0, -50 - (i * 30), 0, "DefaultButtonText");
    --    groupText:SetAnchor(0.0, 1.0);
    --    groupText:SetRelativePoint(0.0, 1.0);
    --end

    --[[
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
        options = {"Foo", "Bar", "Fizz", "Buzz", "FizzBuzz"},
        disableButtons = true,
    });
    --testComboBox.comboBox:SetAnchor(0.5, 1.0);
    --testComboBox.button:SetRelativePoint(0.5, 1.0);
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
    end);]]
   
    stack:Register("OptionsMenu", panel, 0); -- Name, widget, slot, onPush, onPop
    stack:Push("OptionsMenu")
end

local function OnGameLoaded(eventID : number, motd : string)
    local stack = UIStack.Get("Menu")

    CreateGameMenu(stack);
    CreateOptionsMenu(stack);
end
RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)