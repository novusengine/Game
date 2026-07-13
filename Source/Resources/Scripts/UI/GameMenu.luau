local UIStack = require("@src/API/UI/UIStack")
local UIButton = require("@src/API/UI/Button")
local UIScrollBox = require("@src/API/UI/ScrollBox")
local UISlider = require("@src/API/UI/Slider")
local UICheckbox = require("@src/API/UI/Checkbox")
local UIComboBox = require("@src/API/UI/ComboBox")
local UIInputBox = require("@src/API/UI/InputBox")
local UIVerticalLayout = require("@src/API/UI/VerticalLayout")
local UIHorizontalLayout = require("@src/API/UI/HorizontalLayout")
local OptionsContext = require("@src/API/OptionsContext")

local function CreateOption(parent : any, option)
    local optionType = option.type;
    local optionName = option.name;
    local optionValue = option.value;

    -- Each option is a horizontal row: label first, then control(s). Row height = 40 to fit
    -- the tallest control (input box / combo box). alignV="center" vertically centers everything.
    local rowHeight = 40;
    local row = UIHorizontalLayout.NewHorizontalLayout(parent, 0, 0, parent:GetWidth(), rowHeight, 0, {
        alignV = "center", spacing = 5,
    });
    parent:SetChildOpts(row, { sizeY = rowHeight, modeX = "flex" });

    row:NewText(optionName, 0, 0, 0, "DefaultButtonText");

    if (optionType == OptionType.InputBox) then
        local inputBox = UIInputBox.NewInputBox(row, 0, 0, 200, 40, 0, {
            defaultText = optionValue,
        });
        row:SetChildOpts(inputBox, { sizeX = 200 });

        inputBox:SetOnSubmit(function(inputBoxTable)
            local previousValue = option.value;
            option.value = inputBoxTable.text;
            local result = option:InvokeOnValueChanged(inputBoxTable.text);
            if (not result) then
                option.value = previousValue;
                return false;
            end
            return true;
        end)
    elseif (optionType == OptionType.Checkbox) then
        local checkbox = UICheckbox.NewCheckbox(row, 0, 0, 24, 24, 0, {
            backgroundTemplate = "DefaultCheckboxBackground",
            fillTemplate = "DefaultCheckboxFill",
            isChecked = optionValue,
        });
        row:SetChildOpts(checkbox, { sizeX = 24 });

        checkbox:SetOnValueChanged(function(checkboxTable, state)
            local previousState = option.value;
            option.value = state;
            local result = option:InvokeOnValueChanged(state);
            if (not result) then
                option.value = previousState;
                return false;
            end
            return true;
        end)
    elseif (optionType == OptionType.Slider) then
        local valueText    = row:NewText(optionValue, 0, 0, 0, "DefaultButtonText");
        local minValueText = row:NewText(option.settings.Min, 0, 0, 0, "DefaultButtonText");
        local slider = UISlider.NewSlider(row, 0, 0, 200, 25, 0, {
            backgroundTemplate = "DefaultSliderBackground",
            fillTemplate = "DefaultSliderFill",
            Min = option.settings.Min,
            Max = option.settings.Max,
            Step = option.settings.Step,
        });
        slider:SetValue(optionValue);
        local maxValueText = row:NewText(option.settings.Max, 0, 0, 0, "DefaultButtonText");
        row:SetChildOpts(slider, { sizeX = 200 });

        slider:SetOnValueChanged(function(sliderTable, value)
            local previousValue = option.value;
            option.value = value;
            local result = option:InvokeOnValueChanged(value);
            if (not result) then
                option.value = previousValue;
                return false;
            end
            valueText:SetText(value);
            return true;
        end)
    elseif (optionType == OptionType.ComboBox) then
        local comboBox = UIComboBox.NewComboBox(row, 0, 0, 200, 40, 0, {
            options = option.settings.options,
            disableButtons = true,
        });
        row:SetChildOpts(comboBox, { sizeX = 200 });
        comboBox:SelectOption(optionValue);

        comboBox:SetOnValueChanged(function(comboBoxTable, index)
            local previousValue = option.value;
            option.value = index;
            local result = option:InvokeOnValueChanged(index);
            if (not result) then
                option.value = previousValue;
                return false;
            end
            return true;
        end)
    end
end

local function UpdateMainPanel(optionsMenu, category, group, section)
    local title = category.name;
    if (group) then title = title .. "/" .. group.name end
    if (section) then title = title .. "/" .. section.name end
    optionsMenu.mainPanelTitle:SetText(title);

    -- Clear previous content (rebuilds the V layout from scratch each navigation).
    for i, child in optionsMenu.mainPanelContent.content:GetChildrenRecursive() do
        UI.DestroyWidget(child);
    end

    local content = optionsMenu.mainPanelContent.content;
    local layout = UIVerticalLayout.NewVerticalLayout(content, 0, 0, content:GetWidth(), 0, 0, {
        heightMode = "shrink", spacing = 5, paddingTop = 5,
    });
    layout:SetAnchor(0.0, 1.0);
    layout:SetRelativePoint(0.0, 1.0);

    for i, option in section.options.list do
        CreateOption(layout, option);
    end

    -- Implicit Refresh via GetMeasuredHeight feeds the scrollbox.
    local currentHeight = layout:GetMeasuredHeight();
    optionsMenu.mainPanelContent:SetVerticalScrollBarVisible(currentHeight > optionsMenu.mainPanelContent.content:GetHeight());
    optionsMenu.mainPanelContent:SetContentHeight(currentHeight + 5);
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
    logoutButton:SetOnMouseUp(function(eventID, widget, ...)
        Logout();
    end)

    local exitButton = UIButton.NewButton(panel, 0, -345, 250, 40, 0, {
        text = "Exit Game"
    });
    exitButton:SetAnchor(0.5, 1.0);
    exitButton:SetRelativePoint(0.5, 0.5);
    exitButton:SetOnMouseUp(function(eventID, widget, ...)
        ExitGame();
    end)

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

    -- Tree: categories at root indent, groups indented +15, sections indented +15 again.
    -- Nested V layouts let each level's paddingLeft compose naturally instead of every line
    -- carrying its own absolute X offset.
    local treeLayout = UIVerticalLayout.NewVerticalLayout(optionsMenu.sidePanel.content, 0, 0, width, 0, 0, {
        heightMode = "shrink", spacing = 5,
    });
    treeLayout:SetAnchor(0.0, 1.0);
    treeLayout:SetRelativePoint(0.0, 1.0);

    local categories = OptionsContext:GetCategories();

    for i, category in categories do
        treeLayout:NewText(category.name, 0, 0, 0, "DefaultButtonText");

        local groupsContainer = UIVerticalLayout.NewVerticalLayout(treeLayout, 0, 0, width, 0, 0, {
            heightMode = "shrink", paddingLeft = 15, spacing = 5,
        });

        for j, group in category.groups.list do
            groupsContainer:NewText(group.name, 0, 0, 0, "DefaultButtonText");

            local sectionsContainer = UIVerticalLayout.NewVerticalLayout(groupsContainer, 0, 0, width, 0, 0, {
                heightMode = "shrink", paddingLeft = 15, spacing = 5,
            });

            for k, section in group.sections.list do
                local sectionText = sectionsContainer:NewText(section.name, 0, 0, 0, "DefaultButtonText");
                sectionText:SetOnMouseUp(function(eventID, widget, ...)
                    UpdateMainPanel(optionsMenu, category, group, section);
                end)
            end
        end
    end

    local currentHeight = treeLayout:GetMeasuredHeight();
    optionsMenu.sidePanel:SetVerticalScrollBarVisible(currentHeight > height);
    optionsMenu.sidePanel:SetContentHeight(currentHeight + 5);

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

local function OnGameLoaded(eventID : number, data : any)
    local stack = UIStack.Get("Menu");

    CreateGameMenu(stack);
    CreateOptionsMenu(stack);
end
RegisterEvent(GameEvent.Loaded, OnGameLoaded)
