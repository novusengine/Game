local Module = { }

--[[
usage:
.NewCombobox(panel, 0, -120, 200, 40, 0, {
    options = {"Foo", "Bar", "Fizz", "Buzz", "FizzBuzz"},
        buttonTemplate = "DefaultComboboxButton",
})
]]--

local function RefreshCombobox(comboboxTable)
    local index = comboboxTable.selectedOption;
    comboboxTable.button:SetText(comboboxTable.options[index]);

    comboboxTable.prevButton:SetInteractable(index > 1);
    comboboxTable.nextButton:SetInteractable(index < #comboboxTable.options);
end

local function SetupComboboxMethods(comboboxTable)
    
    comboboxTable.SelectOption = function(table, index)
        index = math.clamp(index, 1, #table.options);

        if (index ~= table.selectedOption) then
            if (table.onValueChanged ~= nil) then
                local result = table:onValueChanged(index);
                
                if (result == false) then
                    return
                end
            end

            table.selectedOption = index;
            RefreshCombobox(table);
        end
    end

    comboboxTable.GetOption = function(table)
        return table.options[table.selectedOption];
    end

    comboboxTable.GetOptionIndex = function(table)
        return table.selectedOption;
    end

    comboboxTable.Next = function()
        local index = comboboxTable.selectedOption + 1;
        comboboxTable:SelectOption(index);
    end

    comboboxTable.Previous = function()
        local index = comboboxTable.selectedOption - 1;
        comboboxTable:SelectOption(index);
    end

    comboboxTable.SetOnValueChanged = function(table, func)
        table.onValueChanged = func;
    end

    comboboxTable.ShowSelectionPanel = function()
        comboboxTable.selectionPanel:SetEnabled(true);
    end
    comboboxTable.HideSelectionPanel = function()
        comboboxTable.selectionPanel:SetEnabled(false);
    end

    return comboboxTable
end

function Module.NewCombobox(parent, posX, posY, sizeX, sizeY, layer, comboboxTemplateTable)
    local comboboxTable = {}
    
    comboboxTable.combobox = parent:NewWidget(posX, posY, layer);
    comboboxTable.onValueChanged = nil;

    local options = comboboxTemplateTable["options"] or { "None" };
    local buttonTemplate = comboboxTemplateTable["buttonTemplate"] or "DefaultComboboxButton";
    local prevButtonTemplate = comboboxTemplateTable["prevButtonTemplate"] or "DefaultComboboxPrevButton";
    local nextButtonTemplate = comboboxTemplateTable["nextButtonTemplate"] or "DefaultComboboxNextButton";
    comboboxTable.selectedOption = comboboxTemplateTable["selectedOption"] or 1;

    local sideButtonsSizeX = sizeY; -- Square button
    local middleButtonSizeX = sizeX - (sideButtonsSizeX * 2.0);

    local sideButtonPosXOffset = (middleButtonSizeX / 2.0);

    -- Background
    comboboxTable.button = comboboxTable.combobox:NewButton(0, 0, middleButtonSizeX, sizeY, 0, buttonTemplate);
    
    comboboxTable.options = options;
    
    comboboxTable.button:SetText(comboboxTable.options[comboboxTable.selectedOption]);

    -- Previous
    comboboxTable.prevButton = comboboxTable.combobox:NewButton(-sideButtonPosXOffset, 0, sideButtonsSizeX, sizeY, 0, prevButtonTemplate);
    comboboxTable.prevButton:SetAnchor(0.5, 0.0);
    comboboxTable.prevButton:SetRelativePoint(1.0, 1.0);
    -- Next
    comboboxTable.nextButton = comboboxTable.combobox:NewButton(sideButtonPosXOffset, 0, sideButtonsSizeX, sizeY, 0, nextButtonTemplate);
    comboboxTable.nextButton:SetAnchor(0.5, 0.0);
    comboboxTable.nextButton:SetRelativePoint(0.0, 1.0);

    -- Selection panel
    local selectionButtonHeight = comboboxTemplateTable["selectionButtonHeight"] or 30;

    local selectionPanelPaddingX = comboboxTemplateTable["selectionPanelPaddingX"] or 10;
    local selectionPanelPaddingY = comboboxTemplateTable["selectionPanelPaddingY"] or 10;

    local selectionPanelTemplate = comboboxTemplateTable["selectionPanelTemplate"] or "DialogBox";

    local numOptions = #options;
    local selectionPanelWidth = middleButtonSizeX + selectionPanelPaddingX;
    local selectionPanelHeight = selectionButtonHeight * numOptions + selectionPanelPaddingY;
    comboboxTable.selectionPanel = comboboxTable.button:NewPanel(0, 0, selectionPanelWidth, selectionPanelHeight, 0, selectionPanelTemplate);
    comboboxTable.selectionPanel:SetEnabled(false);
    comboboxTable.selectionPanel:SetAnchor(0.5, 0.0);
    comboboxTable.selectionPanel:SetRelativePoint(0.5, 1.0);

    local yPos = -selectionPanelPaddingY / 2.0;
    comboboxTable.selectionButtons = {};
    for i=1, numOptions do
        comboboxTable.selectionButtons[i] = comboboxTable.selectionPanel:NewButton(0, yPos, middleButtonSizeX, selectionButtonHeight, 0, buttonTemplate);
        comboboxTable.selectionButtons[i]:SetText(options[i]);

        comboboxTable.selectionButtons[i]:SetAnchor(0.5, 1.0);
        comboboxTable.selectionButtons[i]:SetRelativePoint(0.5, 1.0);

        comboboxTable.selectionButtons[i]:SetOnMouseUp(function(eventID, widget, ...)
            comboboxTable:SelectOption(i);
            comboboxTable.HideSelectionPanel();
        end);

        yPos -= selectionButtonHeight;
    end

    comboboxTable.button:SetOnMouseUp(function(eventID, widget, ...)
        comboboxTable.ShowSelectionPanel();
    end);

    comboboxTable = SetupComboboxMethods(comboboxTable);

    comboboxTable.prevButton:SetOnMouseUp(comboboxTable.Previous);
    comboboxTable.nextButton:SetOnMouseUp(comboboxTable.Next);
    RefreshCombobox(comboboxTable);

    return comboboxTable;
end

return Module