local sliderAPI = { }

--[[
usage:
.NewSlider(panel, 0, 0, 200, 25, 0, {
    backgroundTemplate = "DefaultSliderBackground",
    fillTemplate = "DefaultSliderFill",
    Min = 0,
    Max = 100,
    Step = 5,
})
]]--

local function SetupSliderMethods(sliderTable)
    sliderTable.GetValue = function(self)
        return self.Value
    end

    sliderTable.GetProgress = function(self)
        local sizeX = self.fill:GetWidth()
        return math.clamp(sizeX / self.fillSizeX, 0.0, 1.0)
    end

    sliderTable.SetProgress = function(self, progress)
        progress = math.clamp(progress, 0.0, 1.0)
        local sizeX = self.fillSizeX * progress
        self.fill:SetWidth(sizeX)
        if self.Min and self.Max then
            local newValue = self.Min + progress * (self.Max - self.Min)
            if self.Step then
                newValue = self.Min + math.floor((newValue - self.Min) / self.Step + 0.5) * self.Step
            end
            newValue = math.clamp(newValue, self.Min, self.Max)
            if newValue ~= self.Value then
                self.Value = newValue
                if self.onValueChanged then
                    -- Pass the actual value, not the progress, to the callback.
                    self:onValueChanged(newValue)
                end
            end
        end
    end

    sliderTable.SetOnMouseDown = function(self, func)
        self.background:SetOnMouseDown(func)
        self.fill:SetOnMouseDown(func)
    end

    sliderTable.SetOnMouseUp = function(self, func)
        self.background:SetOnMouseUp(func)
        self.fill:SetOnMouseUp(func)
    end

    sliderTable.SetOnMouseHeld = function(self, func)
        self.background:SetOnMouseHeld(func)
        self.fill:SetOnMouseHeld(func)
    end

    sliderTable:SetOnMouseHeld(function(eventID, widget, mouseX, mouseY)
        local fillPosX = sliderTable.fill:GetWorldPosX()
        local fillDiff = mouseX - fillPosX
        local fillSize = sliderTable.fillSizeX
        local progress = fillDiff / fillSize
        progress = math.clamp(progress, 0.0, 1.0)
        sliderTable:SetProgress(progress)
    end)

    sliderTable.SetValue = function(self, value)
        value = math.clamp(value, self.Min, self.Max)
        if self.Step then
            value = self.Min + math.floor((value - self.Min) / self.Step + 0.5) * self.Step
            value = math.clamp(value, self.Min, self.Max)
        end
        local progress = (value - self.Min) / (self.Max - self.Min)
        self:SetProgress(progress)
    end

    sliderTable.SetOnValueChanged = function(self, callback)
        self.onValueChanged = callback
    end

    return sliderTable
end

function sliderAPI.NewSlider(parent, posX, posY, sizeX, sizeY, layer, sliderTemplateTable)
    local sliderTable = {}
    
    sliderTable.slider = parent:NewWidget(posX, posY, layer)
    sliderTemplateTable = sliderTemplateTable or {}

    local backgroundTemplate = sliderTemplateTable["backgroundTemplate"] or "DefaultSliderBackground"
    local fillTemplate = sliderTemplateTable["fillTemplate"] or "DefaultSliderFill"

    sliderTable.background = sliderTable.slider:NewPanel(0, 0, sizeX, sizeY, 0, backgroundTemplate)
    
    sliderTable.fillSizeX = sliderTemplateTable["fillSizeX"] or sizeX - 16
    sliderTable.fillSizeY = sliderTemplateTable["fillSizeY"] or sizeY - 16
    local fillPosX = sliderTemplateTable["fillPosX"] or 8
    local fillPosY = sliderTemplateTable["fillPosY"] or 0

    sliderTable.fill = sliderTable.background:NewPanel(fillPosX, fillPosY, sliderTable.fillSizeX, sliderTable.fillSizeY, 1, fillTemplate)
    sliderTable.fill:SetAnchor(0.0, 0.5)
    sliderTable.fill:SetRelativePoint(0.0, 0.5)

    sliderTable.Min = sliderTemplateTable["Min"] or 0
    sliderTable.Max = sliderTemplateTable["Max"] or 1
    sliderTable.Step = sliderTemplateTable["Step"]

    -- Initialize the slider value to Min by default
    sliderTable.Value = sliderTable.Min

    sliderTable = SetupSliderMethods(sliderTable)

    return sliderTable
end

return sliderAPI