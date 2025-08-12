local buttonAPI = { }

--[[
usage:
.NewButton(parent, 0, 0, 250, 40, 0, {
    text = "Options",
    panelTemplate = "DefaultButtonPanel",
    textTemplate = "DefaultButtonText",
});
]]--

local function SetupButtonMethods(buttonTable)
    -- Button Methods
    buttonTable.SetText = function(table, text)
        table.text:SetText(text);
    end

    -- Widget Methods
    buttonTable.SetEnabled = function(table, val)
        table.panel:SetEnabled(val);
    end
    buttonTable.SetVisible = function(table, val)
        table.panel:SetVisible(val);
    end
    buttonTable.SetInteractable = function(table, val)
        table.panel:SetInteractable(val);
    end
    buttonTable.SetFocusable = function(table, val)
        table.panel:SetFocusable(val);
    end

    buttonTable.NewWidget = function(table, x, y, layer)
        return table.panel:NewWidget(x, y, layer);
    end

    buttonTable.NewPanel = function(table, x, y, w, h, layer, template)
        return table.panel:NewPanel(x, y, w, h, layer, template);
    end

    buttonTable.NewText = function(table, text, x, y, layer, template)
        return table.panel:NewText(text, x, y, layer, template);
    end

    buttonTable.GetAnchor = function(table)
        return table.panel:GetAnchor();
    end
    buttonTable.GetRelativePoint = function(table)
        return table.panel:GetRelativePoint();
    end

    buttonTable.SetAnchor = function(table, x, y)
        table.panel:SetAnchor(x, y);
    end
    buttonTable.SetRelativePoint = function(table, x, y)
        table.panel:SetRelativePoint(x, y);
    end

    buttonTable.GetPos = function(table)
        return table.panel:GetPos();
    end
    buttonTable.GetPosX = function(table)
        return table.panel:GetPosX();
    end
    buttonTable.GetPosY = function(table)
        return table.panel:GetPosY();
    end

    buttonTable.SetPos = function(table, x, y)
        table.panel:SetPos(x, y);
    end
    buttonTable.SetPosX = function(table, x)
        table.panel:SetPosX(x);
    end
    buttonTable.SetPosY = function(table, y)
        table.panel:SetPosY(y);
    end

    buttonTable.GetWorldPos = function(table)
        return table.panel:GetWorldPos();
    end
    buttonTable.GetWorldPosX = function(table)
        return table.panel:GetWorldPosX();
    end
    buttonTable.GetWorldPosY = function(table)
        return table.panel:GetWorldPosY();
    end

    buttonTable.SetWorldPos = function(table, x, y)
        table.panel:SetWorldPos(x, y);
    end
    buttonTable.SetWorldPosX = function(table, x)
        table.panel:SetWorldPosX(x);
    end
    buttonTable.SetWorldPosY = function(table, y)
        table.panel:SetWorldPosY(y);
    end

    -- Widget Input Methods
    buttonTable.SetOnMouseDown = function(table, func)
        table.panel:SetOnMouseDown(func);
    end

    buttonTable.SetOnMouseUp = function(table, func)
        table.panel:SetOnMouseUp(func);
    end

    buttonTable.SetOnMouseHeld = function(table, func)
        table.panel:SetOnMouseHeld(func);
    end

    buttonTable.SetOnHoverBegin = function(table, func)
        table.panel:SetOnFocusBegin(func);
    end

    buttonTable.SetOnHoverEnd = function(table, func)
        table.panel:SetOnFocusEnd(func);
    end

    buttonTable.SetOnHoverHeld = function(table, func)
        table.panel:SetOnHoverHeld(func);
    end

    buttonTable.SetOnFocusBegin = function(table, func)
        table.panel:SetOnFocusBegin(func);
    end

    buttonTable.SetOnFocusEnd = function(table, func)
        table.panel:SetOnFocusEnd(func);
    end

    buttonTable.SetOnFocusHeld = function(table, func)
        table.panel:SetOnFocusHeld(func);
    end

    buttonTable.SetOnKeyboard = function(table, func)
        table.panel:SetOnKeyboard(func);
    end

    buttonTable.ForceRefresh = function(table)
        table.panel:ForceRefresh();
        table.text:ForceRefresh();
    end

    return buttonTable
end

function buttonAPI.NewButton(parent, posX, posY, sizeX, sizeY, layer, buttonTemplateTable)
    local buttonTable = {}

    local text = buttonTemplateTable["text"] or "";
    local panelTemplate = buttonTemplateTable["panelTemplate"] or "DefaultButtonPanel";
    local textTemplate = buttonTemplateTable["textTemplate"] or "DefaultButtonText";

    buttonTable.panel = parent:NewPanel(posX, posY, sizeX, sizeY, 0, panelTemplate);
    buttonTable.text = buttonTable.panel:NewText(text, 0, 3, 0, textTemplate);
    buttonTable.text:SetAnchor(0.5, 0.5);
    buttonTable.text:SetRelativePoint(0.5, 0.5);

    buttonTable = SetupButtonMethods(buttonTable);

    return buttonTable;
end

return buttonAPI