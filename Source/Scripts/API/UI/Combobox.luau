local Module = { }

--[[
usage:
.NewCombobox(panel, 0, -120, 200, 40, 0, {
    options = {"Foo", "Bar", "Fizz", "Buzz", "FizzBuzz"},
    backgroundTemplate = "DefaultComboboxPanel",
    textTemplate = "YellowText"
})
]]--

local function RefreshCombobox(comboboxTable)
    local index = comboboxTable.selectedOption;
    comboboxTable.text:SetText(comboboxTable.options[index]);

    comboboxTable.prevButton:SetInteractable(index > 1);
    comboboxTable.nextButton:SetInteractable(index < #comboboxTable.options);
end

local function SetupComboboxMethods(comboboxTable)
    
    comboboxTable.SelectOption = function(table, index)
        index = math.clamp(index, 1, #table.options);

        if (index ~= table.selectedOption) then
            table.selectedOption = index;
            RefreshCombobox(table);
            if (table.OnValueChanged ~= nil) then
                table:OnValueChanged(index);
            end
        end
    end

    comboboxTable.Next = function()
        local index = comboboxTable.selectedOption + 1;
        comboboxTable:SelectOption(index);
    end

    comboboxTable.Previous = function()
        local index = comboboxTable.selectedOption - 1;
        comboboxTable:SelectOption(index);
    end

    comboboxTable.OnValueChanged = nil;
    comboboxTable.SetOnValueChanged = function(table, func)
        table.OnValueChanged = func;
    end

    return comboboxTable
end

function Module.NewCombobox(parent, posX, posY, sizeX, sizeY, layer, comboboxTemplateTable)
    local comboboxTable = {}
    
    comboboxTable.combobox = parent:NewWidget(posX, posY, layer);

    local options = comboboxTemplateTable["options"];
    local backgroundTemplate = comboboxTemplateTable["backgroundTemplate"];
    local textTemplate = comboboxTemplateTable["textTemplate"];

    local sideButtonsSizeX = sizeY; -- Square button
    local middleButtonSizeX = sizeX - (sideButtonsSizeX * 2.0);

    local sideButtonPosXOffset = (middleButtonSizeX / 2.0);

    -- Background
    comboboxTable.background = comboboxTable.combobox:NewPanel(0, 0, middleButtonSizeX, sizeY, 0, backgroundTemplate);
    
    comboboxTable.options = options;
    comboboxTable.selectedOption = 1;

    comboboxTable.text = comboboxTable.background:NewText(comboboxTable.options[comboboxTable.selectedOption], 0, 0, 0, textTemplate);
    comboboxTable.text:SetAnchor(0.5, 0.5);
    comboboxTable.text:SetRelativePoint(0.5, 0.5);

    -- Previous
    comboboxTable.prevButton = comboboxTable.combobox:NewButton(-sideButtonPosXOffset, 0, sideButtonsSizeX, sizeY, 0, "DefaultComboboxPrevButton");
    comboboxTable.prevButton:SetAnchor(0.5, 0.0);
    comboboxTable.prevButton:SetRelativePoint(1.0, 1.0);
    -- Next
    comboboxTable.nextButton = comboboxTable.combobox:NewButton(sideButtonPosXOffset, 0, sideButtonsSizeX, sizeY, 0, "DefaultComboboxNextButton");
    comboboxTable.nextButton:SetAnchor(0.5, 0.0);
    comboboxTable.nextButton:SetRelativePoint(0.0, 1.0);

    comboboxTable = SetupComboboxMethods(comboboxTable);

    comboboxTable.prevButton:SetOnMouseUp(comboboxTable.Previous);
    comboboxTable.nextButton:SetOnMouseUp(comboboxTable.Next);
    RefreshCombobox(comboboxTable);

    return comboboxTable;
end

return Module