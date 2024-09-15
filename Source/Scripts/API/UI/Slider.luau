local Module = { }

--[[
usage:
.NewSlider(panel, 0, 0, 200, 25, 0, {
    backgroundTemplate = "DefaultSliderBackground",
    fillTemplate = "DefaultSliderFill",
})
]]--

local function SetupSliderMethods(slider)

    slider.SetProgress = function(table, progress)
        local sizeX = table.fillSizeX * progress;
        table.fill:SetWidth(sizeX);
    end

    slider.SetOnMouseDown = function(table, func)
        table.background:SetOnMouseDown(func);
        table.fill:SetOnMouseDown(func);
    end

    slider.SetOnMouseUp = function(table, func)
        table.background:SetOnMouseUp(func);
        table.fill:SetOnMouseUp(func);
    end

    slider.SetOnMouseHeld = function(table, func)
        table.background:SetOnMouseHeld(func);
        table.fill:SetOnMouseHeld(func);
    end

    slider:SetOnMouseHeld(function(eventID, widget, mouseX, mouseY)
        local fillPosX = slider.fill:GetWorldPosX();

        local fillDiff = mouseX - fillPosX;
        local fillSize = slider.fillSizeX;
        local progress = fillDiff / fillSize;

        progress = math.clamp(progress, 0.0, 1.0);
        slider:SetProgress(progress);
    end);

    return slider
end

function Module.NewSlider(parent, posX, posY, sizeX, sizeY, layer, sliderTemplateTable)
    local sliderTable = {}
    
    sliderTable.slider = parent:NewWidget(posX, posY, layer);

    local backgroundTemplate = sliderTemplateTable["backgroundTemplate"];
    local fillTemplate = sliderTemplateTable["fillTemplate"];

    sliderTable.background = sliderTable.slider:NewPanel(0, 0, sizeX, sizeY, 0, backgroundTemplate);
    
    -- Create a fill which grows logically from the left
    sliderTable.fillSizeX = sizeX-16;
    sliderTable.fillSizeY = sizeY-16;

    sliderTable.fill = sliderTable.background:NewPanel(8, 0, sliderTable.fillSizeX, sliderTable.fillSizeY, 1, fillTemplate);
    sliderTable.fill:SetAnchor(0.0, 0.5);
    sliderTable.fill:SetRelativePoint(0.0, 0.5);

    sliderTable = SetupSliderMethods(sliderTable);

    return sliderTable;
end

return Module