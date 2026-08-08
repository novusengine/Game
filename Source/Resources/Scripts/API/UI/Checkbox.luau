local RootedWidget = require("@src/API/UI/RootedWidget")

export type Options =
{
    backgroundTemplate : string,
    fillTemplate : string,
    fillInset : number?,
    isChecked : boolean?,
}

export type Checkbox =
{
    root : Panel,
    fill : Panel,
    IsChecked : (self : Checkbox) -> boolean,
    SetChecked : (self : Checkbox, state : boolean) -> (),
    SetOnValueChanged : (self : Checkbox, callback : (Checkbox, boolean) -> boolean?) -> (),
    SetOnMouseDown : (self : Checkbox, callback : (...any) -> ()) -> (),
    SetOnMouseUp : (self : Checkbox, callback : (...any) -> ()) -> (),
}

export type CheckboxAPI =
{
    New : (parent : Widget, position : vec, size : vec, layer : integer, options : Options) -> Checkbox,
}

local checkboxAPI : CheckboxAPI = { }

--[[
usage:
.New(panel, vec.New(0, 0), vec.New(200, 25), 0, {
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
        table.root:SetOnMouseDown(func)
    end

    checkboxTable.SetOnMouseUp = function(table, func)
        table.root:SetOnMouseUp(func)
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

function checkboxAPI.New(parent : Widget, position : vec, size : vec, layer : integer, checkboxTemplateTable : Options)
    local checkboxTable = {}

    local backgroundTemplate = checkboxTemplateTable["backgroundTemplate"];
    local fillTemplate = checkboxTemplateTable["fillTemplate"];
    local fillInset = checkboxTemplateTable["fillInset"] or 0;
    local fillWidth = math.max(size.x - (fillInset * 2), 1);
    local fillHeight = math.max(size.y - (fillInset * 2), 1);

    checkboxTable.root = parent:NewPanel(position, size, layer, backgroundTemplate);
    RootedWidget.Attach(checkboxTable)
    checkboxTable.isChecked = false
    checkboxTable.onValueChanged = nil

    checkboxTable.fill = checkboxTable.root:NewPanel(vec.New(0, 0), vec.New(fillWidth, fillHeight), 0i, fillTemplate);
    checkboxTable.fill:SetAnchor(vec.New(0.5, 0.5))
    checkboxTable.fill:SetRelativePoint(vec.New(0.5, 0.5))
    checkboxTable.fill:SetVisible(false)
    checkboxTable = SetupCheckboxMethods(checkboxTable);

    checkboxTable:SetChecked(checkboxTemplateTable["isChecked"] or false);

    return checkboxTable;
end

return checkboxAPI
