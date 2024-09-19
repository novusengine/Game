local Module = { }

--[[
usage:
.NewSlider(panel, 0, 0, 200, 25, 0, {
    backgroundTemplate = "DefaultSliderBackground",
    fillTemplate = "DefaultSliderFill",
})
]]--

local function SetupSliderMethods(sliderTable) 
    sliderTable.GetProgress = function(table)
        local sizeX = table.fill:GetWidth()
        return math.clamp(sizeX / table.fillSizeX, 0.0, 1.0)
    end

    sliderTable.SetProgress = function(table, progress)
        local sizeX = table.fillSizeX * progress;
        table.fill:SetWidth(sizeX);
    end

    sliderTable.SetOnMouseDown = function(table, func)
        table.background:SetOnMouseDown(func);
        table.fill:SetOnMouseDown(func);
    end

    sliderTable.SetOnMouseUp = function(table, func)
        table.background:SetOnMouseUp(func);
        table.fill:SetOnMouseUp(func);
    end

    sliderTable.SetOnMouseHeld = function(table, func)
        table.background:SetOnMouseHeld(func);
        table.fill:SetOnMouseHeld(func);
    end

    sliderTable:SetOnMouseHeld(function(eventID, widget, mouseX, mouseY)
        local fillPosX = sliderTable.fill:GetWorldPosX();

        local fillDiff = mouseX - fillPosX;
        local fillSize = sliderTable.fillSizeX;
        local progress = fillDiff / fillSize;

        progress = math.clamp(progress, 0.0, 1.0);
        sliderTable:SetProgress(progress);
    end);

    return sliderTable
end

function Module.NewSlider(parent, posX, posY, sizeX, sizeY, layer, sliderTemplateTable)
    local sliderTable = {}
    
    sliderTable.slider = parent:NewWidget(posX, posY, layer);

    sliderTemplateTable = sliderTemplateTable or {};

    local backgroundTemplate = sliderTemplateTable["backgroundTemplate"] or "DefaultSliderBackground";
    local fillTemplate = sliderTemplateTable["fillTemplate"] or "DefaultSliderFill";

    sliderTable.background = sliderTable.slider:NewPanel(0, 0, sizeX, sizeY, 0, backgroundTemplate);
    
    -- Create a fill which grows logically from the left
    sliderTable.fillSizeX = sliderTemplateTable["fillSizeX"] or sizeX-16;
    sliderTable.fillSizeY = sliderTemplateTable["fillSizeY"] or sizeY-16;
    local fillPosX = sliderTemplateTable["fillPosX"] or 8;
    local fillPosY = sliderTemplateTable["fillPosY"] or 0;

    sliderTable.fill = sliderTable.background:NewPanel(fillPosX, fillPosY, sliderTable.fillSizeX, sliderTable.fillSizeY, 1, fillTemplate);
    sliderTable.fill:SetAnchor(0.0, 0.5);
    sliderTable.fill:SetRelativePoint(0.0, 0.5);

    sliderTable = SetupSliderMethods(sliderTable);

    return sliderTable;
end

return Module