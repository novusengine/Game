local UIStack = require("@src/API/UI/UIStack")
local UIButton = require("@src/API/UI/Button")
local UIScrollBox = require("@src/API/UI/ScrollBox")
local UISlider = require("@src/API/UI/Slider")
local UICheckbox = require("@src/API/UI/Checkbox")
local UIComboBox = require("@src/API/UI/ComboBox")
local UIInputBox = require("@src/API/UI/InputBox")
local OptionsContext = require("@src/API/OptionsContext")

local function CreateOption(optionsMenu, currentY, option)
    local optionType = option.type;
    local optionName = option.name;
    local optionValue = option.value;
    local optionDefaultValue = option.defaultValue;

    local currentX = 0;

    local optionText = optionsMenu.mainPanelContent.content:NewText(optionName, currentX, currentY, 0, "DefaultButtonText");
    optionText:SetRelativePoint(0.0, 1.0);
    currentX += select(1, optionText:GetSize()) + 5;

    if (optionType == OptionType.InputBox) then
        local inputBox = UIInputBox.NewInputBox(optionsMenu.mainPanelContent.content, currentX, currentY, 200, 40, 0, {
            defaultText = optionValue,
        });
        inputBox.button:SetRelativePoint(0.0, 1.0);

        inputBox:SetOnSubmit(function(inputBoxTable)
            local previousValue = option.value;

            option.value = inputBoxTable.text;

            local result = option:InvokeOnValueChanged(inputBoxTable.text);
            if (not result) then
                option.value = previousValue; -- Revert to previous state
                return false; -- Returning false will prevent the value from changing
            end

            return true; -- Returning false will prevent the value from changing
        end)
    elseif (optionType == OptionType.Checkbox) then
        local checkbox = UICheckbox.NewCheckbox(optionsMenu.mainPanelContent.content, currentX, currentY, 24, 24, 0,
        {
            backgroundTemplate = "DefaultCheckboxBackground",
            fillTemplate = "DefaultCheckboxFill",
            isChecked = optionValue,
        });
        checkbox.background:SetRelativePoint(0.0, 1.0);

        checkbox:SetOnValueChanged(function(checkboxTable, state)
            local previousState = option.value;

            option.value = state;

            local result = option:InvokeOnValueChanged(state);
            if (not result) then
                option.value = previousState; -- Revert to previous state
                return false; -- Returning false will prevent the value from changing
            end

            return true; -- Returning false will prevent the value from changing
        end)
    elseif (optionType == OptionType.Slider) then
        local valueText = optionsMenu.mainPanelContent.content:NewText(optionValue, currentX, currentY, 0, "DefaultButtonText");
        valueText:SetRelativePoint(0.0, 1.0);
        currentX += select(1, valueText:GetSize()) + 20;

        local minValueText = optionsMenu.mainPanelContent.content:NewText(option.settings.Min, currentX, currentY, 0, "DefaultButtonText");
        minValueText:SetRelativePoint(0.0, 1.0);
        currentX += select(1, minValueText:GetSize()) + 5;

        local slider = UISlider.NewSlider(optionsMenu.mainPanelContent.content, currentX, currentY, 200, 25, 0, {
            backgroundTemplate = "DefaultSliderBackground",
            fillTemplate = "DefaultSliderFill",
            Min = option.settings.Min,
            Max = option.settings.Max,
            Step = option.settings.Step,
        });
        slider.background:SetRelativePoint(0.0, 1.0);
        slider:SetValue(optionValue);

        local maxValueText = optionsMenu.mainPanelContent.content:NewText(option.settings.Max, currentX + 200, currentY, 0, "DefaultButtonText");
        maxValueText:SetRelativePoint(0.0, 1.0);
        currentX += select(1, maxValueText:GetSize()) + 5;

        --slider:SetProgress(optionValue);
        --slider:SetMinMax(option.settings.Min, option.settings.Max, option.settings.Step);

        slider:SetOnValueChanged(function(sliderTable, value)
            local previousValue = option.value;

            option.value = value;

            local result = option:InvokeOnValueChanged(value);
            if (not result) then
                option.value = previousValue; -- Revert to previous state
                return false; -- Returning false will prevent the value from changing
            end

            valueText:SetText(value); -- Update text with new value
            return true; -- Returning false will prevent the value from changing
        end)
    elseif (optionType == OptionType.ComboBox) then
        local comboBox = UIComboBox.NewComboBox(optionsMenu.mainPanelContent.content, currentX, currentY, 200, 40, 0, {
            options = option.settings.options,
            disableButtons = true,
        });
        comboBox.button:SetRelativePoint(0.0, 1.0);
        comboBox:SelectOption(optionValue);

        comboBox:SetOnValueChanged(function(comboBoxTable, index)
            local previousValue = option.value;

            option.value = index;

            local result = option:InvokeOnValueChanged(index);
            if (not result) then
                option.value = previousValue; -- Revert to previous state
                return false; -- Returning false will prevent the value from changing
            end

            return true; -- Returning false will prevent the value from changing
        end)
    end

    currentY -= select(2, optionText:GetSize()) + 5;
    return currentY;
end

local function UpdateMainPanel(optionsMenu, category, group, section)
    -- Set title
    local title = category.name;
    if (group) then
        title = title .. "/" .. group.name;
    end
    if (section) then
        title = title .. "/" .. section.name;
    end
    optionsMenu.mainPanelTitle:SetText(title);

    -- Clear content
    for i, child in optionsMenu.mainPanelContent.content:GetChildrenRecursive() do
        UI.DestroyWidget(child);
    end

    -- Create new content
    local currentY = 0;

    for i, option in section.options.list do
        currentY = CreateOption(optionsMenu, currentY, option);
    end

    -- Update scrollbox
    local currentHeight = math.abs(currentY);
    if (currentHeight > optionsMenu.mainPanelContent.content:GetHeight()) then
        optionsMenu.mainPanelContent:SetVerticalScrollBarVisible(true);
    else
        optionsMenu.mainPanelContent:SetVerticalScrollBarVisible(false);
    end
    optionsMenu.mainPanelContent:SetContentHeight(currentHeight+5);
end

function ToggleEscapeMenu(panelWrapper)
    panelWrapper.isOpen = not panelWrapper.isOpen;
    local stack = UIStack.Get("Menu");
    
    if (panelWrapper.isOpen) then
        stack:Push("GameMenu")
    else
        stack:Pop();
    end

    return true; -- Consume the event
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
    --stack:Push("GameMenu")

    -- Stack should have max amount of open widgets
    -- If we push more than max, pop previous

    local panelWrapper = {
        panel = panel,
        isOpen = false,
    }

    -- Set up global escape key handler
    UI.AddOnKeyboard(function(eventID, ...)
        if (eventID == UIKeyboardEvent.Key) then
            local keyID, action, modifierMask = ...;

            local keyIDEscape = 256;
            -- actionMask is up, down etc
            -- modifierMask is ctrl, shift etc

            if (action ~= InputAction.Press or keyID ~= keyIDEscape) then
                return false;
            end

            return ToggleEscapeMenu(panelWrapper);
        end
        return false;
    end);
end

local function CreateMainPanel(parent : Panel, x : number, optionsMenu)
    local padding = 5;

    local width, height = parent:GetSize();
    width -= x + (padding * 2);
    height -= padding;
    x += padding;

    optionsMenu.mainPanel = parent:NewPanel(x, 0, width, height, 0, "DialogBox");
    optionsMenu.mainPanel:SetAnchor(0.0, 1.0);
    optionsMenu.mainPanel:SetRelativePoint(0.0, 1.0);

    optionsMenu.mainPanelTitle = optionsMenu.mainPanel:NewText("General", 0, -5, 0, "DefaultButtonText");
    optionsMenu.mainPanelTitle:SetAnchor(0.5, 1.0);
    optionsMenu.mainPanelTitle:SetRelativePoint(0.5, 1.0);
    local titleWidth, titleHeight = optionsMenu.mainPanelTitle:GetSize();

    optionsMenu.mainPanelContent = UIScrollBox.NewScrollBox(optionsMenu.mainPanel, 0, 0, width, height - titleHeight - 10, 0, {
        verticalScrollBar = true,
    });
    optionsMenu.mainPanelContent.content:SetRelativePoint(0.0, 0.0);
end

local function CreateSidePanel(parent : Panel, width : number, optionsMenu)
    local padding = 5;
    local height = parent:GetHeight();
    width -= padding;
    height -= padding;

    optionsMenu.sidePanel = UIScrollBox.NewScrollBox(parent, padding, 0, width, height, 0, {
        verticalScrollBar = true,
    });
    optionsMenu.sidePanel:SetAnchor(0.0, 1.0);
    optionsMenu.sidePanel:SetRelativePoint(0.0, 1.0);
    optionsMenu.sidePanel.scrollBox:SetAlpha(1.0);

    -- Categories
    local categories = OptionsContext:GetCategories();
    local currentY = 0;

    for i, category in categories do
        local categoryText = optionsMenu.sidePanel.content:NewText(category.name, 0, currentY, 0, "DefaultButtonText");
        categoryText:SetAnchor(0.0, 1.0);
        categoryText:SetRelativePoint(0.0, 1.0);
        --categoryText:SetOnMouseUp(function(eventID, widget, ...)
        --    UpdateMainPanel(optionsMenu, category, nil, nil);
        --end)

        currentY -= select(2, categoryText:GetSize()) + 5;

        for j, group in category.groups.list do
            local groupText = optionsMenu.sidePanel.content:NewText(group.name, 15, currentY, 0, "DefaultButtonText");
            groupText:SetAnchor(0.0, 1.0);
            groupText:SetRelativePoint(0.0, 1.0);
            --groupText:SetOnMouseUp(function(eventID, widget, ...)
            --    UpdateMainPanel(optionsMenu, category, group, nil);
            --end)

            currentY -= select(2, groupText:GetSize()) + 5;

            for k, section in group.sections.list do
                local sectionText = optionsMenu.sidePanel.content:NewText(section.name, 30, currentY, 0, "DefaultButtonText");
                sectionText:SetAnchor(0.0, 1.0);
                sectionText:SetRelativePoint(0.0, 1.0);
                sectionText:SetOnMouseUp(function(eventID, widget, ...)
                    UpdateMainPanel(optionsMenu, category, group, section);
                end)

                currentY -= select(2, sectionText:GetSize()) + 5;
            end
        end
    end

    local currentHeight = math.abs(currentY);
    if (currentHeight > height) then
        optionsMenu.sidePanel:SetVerticalScrollBarVisible(true);
    else
        optionsMenu.sidePanel:SetVerticalScrollBarVisible(false);
    end
    optionsMenu.sidePanel:SetContentHeight(currentHeight+5);

    return optionsMenu;
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

    local optionsMenu = {};

    -- Left Side panel
    local sidePanelWidth = 250;
    CreateSidePanel(contentPanel, sidePanelWidth, optionsMenu);

    -- Main panel
    CreateMainPanel(contentPanel, sidePanelWidth, optionsMenu);

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
    --stack:Push("OptionsMenu")
end

local function OnGameLoaded(eventID : number, motd : string)
    local stack = UIStack.Get("Menu");

    CreateGameMenu(stack);
    CreateOptionsMenu(stack);
end
RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)