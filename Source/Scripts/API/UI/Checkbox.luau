local Module = { }

--[[
usage:
.NewCheckbox(panel, 0, 0, 200, 25, 0, {
    backgroundTemplate = "DefaultCheckboxBackground",
    fillTemplate = "DefaultCheckboxFill",
})
]]--

local function SetupCheckboxMethods(checkboxTable)
    checkboxTable.IsChecked = function(table)
        return table.isChecked
    end

    checkboxTable.SetChecked = function(table, state)
        if (checkboxTable.isChecked == state) then
            return
        end

        local callback = table.onValueChanged
        if (callback ~= nil) then
            local result = callback(table, state)
            if (result == false) then
                return
            end
        end

        checkboxTable.isChecked = state
        checkboxTable.fill:SetVisible(state)
    end

    checkboxTable.SetOnMouseDown = function(table, func)
        table.background:SetOnMouseDown(func)
        table.fill:SetOnMouseDown(func)
    end

    checkboxTable.SetOnMouseUp = function(table, func)
        table.background:SetOnMouseUp(func)
        table.fill:SetOnMouseUp(func)
    end

    checkboxTable.Click = function(...)
        local isChecked = not checkboxTable.isChecked
        checkboxTable:SetChecked(isChecked)
    end

    checkboxTable.SetOnValueChanged = function(table, callback)
        table.onValueChanged = callback
    end

    checkboxTable:SetOnMouseUp(checkboxTable.Click)

    return checkboxTable
end

function Module.NewCheckbox(parent, posX, posY, sizeX, sizeY, layer, checkboxTemplateTable)
    local checkboxTable = {}
    
    checkboxTable.checkbox = parent:NewWidget(posX, posY, layer);
    checkboxTable.isChecked = false
    checkboxTable.onValueChanged = nil

    local backgroundTemplate = checkboxTemplateTable["backgroundTemplate"];
    local fillTemplate = checkboxTemplateTable["fillTemplate"];

    checkboxTable.background = checkboxTable.checkbox:NewPanel(0, 0, sizeX, sizeY, 0, backgroundTemplate);
    checkboxTable.fill = checkboxTable.checkbox:NewPanel(0, -2, sizeX, sizeY, 0, fillTemplate);
    checkboxTable.fill:SetAnchor(0.5, 0.5)
    checkboxTable.fill:SetRelativePoint(0.5, 1.0)
    checkboxTable.fill:SetVisible(false)
    checkboxTable = SetupCheckboxMethods(checkboxTable);

    return checkboxTable;
end

return Module