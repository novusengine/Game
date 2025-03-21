local UIButton = require("@src/API/UI/Button")

local comboBoxAPI = { }
--[[
usage:
.NewComboBox(panel, 0, -120, 200, 40, 0, {
    options = {"Foo", "Bar", "Fizz", "Buzz", "FizzBuzz"},
        buttonTemplate = {
            panelTemplate = "DefaultButtonPanel",
            textTemplate = "DefaultButtonText",
        },
})
]]--

local function RefreshComboBox(comboBoxTable)
    local index = comboBoxTable.selectedOption;
    comboBoxTable.button:SetText(comboBoxTable.options[index]);

    if (comboBoxTable.prevButton ~= nil) then
        comboBoxTable.prevButton:SetInteractable(index > 1);
        comboBoxTable.nextButton:SetInteractable(index < #comboBoxTable.options);
    end
end

local function SetupComboBoxMethods(comboBoxTable)
    
    comboBoxTable.SelectOption = function(table, index)
        index = math.clamp(index, 1, #table.options);

        if (index ~= table.selectedOption) then
            if (table.onValueChanged ~= nil) then
                local result = table:onValueChanged(index);
                
                if (result == false) then
                    return
                end
            end

            table.selectedOption = index;
            RefreshComboBox(table);
        end
    end

    comboBoxTable.GetOption = function(table)
        return table.options[table.selectedOption];
    end

    comboBoxTable.GetOptionIndex = function(table)
        return table.selectedOption;
    end

    comboBoxTable.Next = function()
        local index = comboBoxTable.selectedOption + 1;
        comboBoxTable:SelectOption(index);
    end

    comboBoxTable.Previous = function()
        local index = comboBoxTable.selectedOption - 1;
        comboBoxTable:SelectOption(index);
    end

    comboBoxTable.SetOnValueChanged = function(table, func)
        table.onValueChanged = func;
    end

    comboBoxTable.ShowSelectionPanel = function()
        comboBoxTable.selectionPanel:SetEnabled(true);
    end
    comboBoxTable.HideSelectionPanel = function()
        comboBoxTable.selectionPanel:SetEnabled(false);
    end

    comboBoxTable.SetAnchor = function(self, x, y)
        self.button:SetAnchor(x, y);
    end
    comboBoxTable.SetRelativePoint = function(self, x, y)
        self.button:SetRelativePoint(x, y);
    end

    return comboBoxTable
end

function comboBoxAPI.NewComboBox(parent, posX, posY, sizeX, sizeY, layer, comboBoxTemplateTable)
    local comboBoxTable = {}
    
    comboBoxTable.onValueChanged = nil;

    local options = comboBoxTemplateTable["options"] or { "None" };
    local buttonTemplate = comboBoxTemplateTable["buttonTemplate"] or {
        panelTemplate = "DefaultComboBoxPanel",
        textTemplate = "YellowText",
    };
    local disableButtons = comboBoxTemplateTable["disableButtons"] or false;
    local prevButtonTemplate = comboBoxTemplateTable["prevButtonTemplate"] or {
        panelTemplate = "DefaultComboBoxPrevPanel",
        textTemplate = "DefaultButtonText",
    };
    local nextButtonTemplate = comboBoxTemplateTable["nextButtonTemplate"] or {
        panelTemplate = "DefaultComboBoxNextPanel",
        textTemplate = "DefaultButtonText",
    };
    comboBoxTable.selectedOption = comboBoxTemplateTable["selectedOption"] or 1;

    local sideButtonsSizeX = if not disableButtons then sizeY else 0; -- Square button
    local middleButtonSizeX = sizeX - (sideButtonsSizeX * 2.0);

    local offsetX = if not disableButtons then sideButtonsSizeX else 0

    -- Background
    comboBoxTable.button = UIButton.NewButton(parent, offsetX + posX, posY, middleButtonSizeX, sizeY, layer, buttonTemplate);
    comboBoxTable.options = options;
    
    comboBoxTable.button:SetText(comboBoxTable.options[comboBoxTable.selectedOption]);

    if (not disableButtons) then
        -- Previous
        comboBoxTable.prevButton = UIButton.NewButton(comboBoxTable.button, 0, 0, sideButtonsSizeX, sizeY, 0, prevButtonTemplate);
        comboBoxTable.prevButton:SetAnchor(0.0, 0.5);
        comboBoxTable.prevButton:SetRelativePoint(1.0, 0.5);
        -- Next
        comboBoxTable.nextButton = UIButton.NewButton(comboBoxTable.button, 0, 0, sideButtonsSizeX, sizeY, 0, nextButtonTemplate);
        comboBoxTable.nextButton:SetAnchor(1.0, 0.5);
        comboBoxTable.nextButton:SetRelativePoint(0.0, 0.5);
    end

    -- Selection panel
    local selectionButtonHeight = comboBoxTemplateTable["selectionButtonHeight"] or 30;

    local selectionPanelPaddingX = comboBoxTemplateTable["selectionPanelPaddingX"] or 10;
    local selectionPanelPaddingY = comboBoxTemplateTable["selectionPanelPaddingY"] or 10;

    local selectionPanelTemplate = comboBoxTemplateTable["selectionPanelTemplate"] or "DialogBox";

    local numOptions = #options;
    local selectionPanelWidth = middleButtonSizeX + selectionPanelPaddingX;
    local selectionPanelHeight = selectionButtonHeight * numOptions + selectionPanelPaddingY;
    comboBoxTable.selectionPanel = comboBoxTable.button:NewPanel(0, 0, selectionPanelWidth, selectionPanelHeight, 0, selectionPanelTemplate);
    comboBoxTable.selectionPanel:SetEnabled(false);
    comboBoxTable.selectionPanel:SetAnchor(0.5, 0.0);
    comboBoxTable.selectionPanel:SetRelativePoint(0.5, 1.0);
    comboBoxTable.selectionPanel:SetAlpha(0.0);

    local yPos = -selectionPanelPaddingY / 2.0;
    comboBoxTable.selectionButtons = {};
    for i=1, numOptions do
        comboBoxTable.selectionButtons[i] = UIButton.NewButton(comboBoxTable.selectionPanel, 0, yPos, middleButtonSizeX, selectionButtonHeight, 0, buttonTemplate);
        comboBoxTable.selectionButtons[i]:SetText(options[i]);

        comboBoxTable.selectionButtons[i]:SetAnchor(0.5, 1.0);
        comboBoxTable.selectionButtons[i]:SetRelativePoint(0.5, 1.0);

        comboBoxTable.selectionButtons[i]:SetOnMouseUp(function(eventID, widget, ...)
            comboBoxTable:SelectOption(i);
            comboBoxTable.HideSelectionPanel();
        end);

        yPos -= selectionButtonHeight;
    end

    comboBoxTable.button:SetOnMouseUp(function(eventID, widget, ...)
        comboBoxTable.ShowSelectionPanel();
    end);

    comboBoxTable = SetupComboBoxMethods(comboBoxTable);

    if (not disableButtons) then
        comboBoxTable.prevButton:SetOnMouseUp(comboBoxTable.Previous);
        comboBoxTable.nextButton:SetOnMouseUp(comboBoxTable.Next);
    end
    RefreshComboBox(comboBoxTable);

    return comboBoxTable;
end

return comboBoxAPI;