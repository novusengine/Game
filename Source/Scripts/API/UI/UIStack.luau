local Module = { }

local StackMeta = {
    Register = function(self, name, inWidget, slot, push, pop)
        local widgetInfo = {
            widget = inWidget,
            slot = slot,
            onPush = push,
            onPop = pop
        }

        table.insert(self.widgets, widgetInfo)
        local index = #self.widgets
        self.nameToIndex[name] = index
    end,
    Get = function(self, name)
        local index = self.nameToIndex[name]
        if (index == nil) then
            return nil
        end
        return self.widgets[index]
    end,

    Push = function(self, name)
        local widgetIndex = self.nameToIndex[name]
        if (widgetIndex == nil) then
            error(`UIStack: Tried to Push nonexistent widget ({name}) into UIStack ({self.name})`)
            return
        end

        local widgetInfo = self.widgets[widgetIndex]
        if (widgetInfo.slot == 0) then
            self:Clear()
        else
            local oldWidgetIndex = self.slots[widgetInfo.slot]
            if (oldWidgetIndex ~= nil) then
                local oldWidgetInfo = self.widgets[oldWidgetIndex]
                if (oldWidgetInfo.onPop ~= nil) then
                    oldWidgetInfo.onPop()
                else
                    oldWidgetInfo.widget:SetEnabled(false)
                end
            end
        end

        self.slots[widgetInfo.slot] = widgetIndex
        self:Refresh()

        if (widgetInfo.onPush ~= nil) then
            widgetInfo.onPush()
        else
            widgetInfo.widget:SetEnabled(true)
        end
    end,
    Pop = function(self)
        local count = self:Count()
        if (count == 0) then
            return
        end

        local widgetIndex = table.remove(self.indexStack, count)

        local widgetInfo = self.widgets[widgetIndex]
        self.slots[widgetInfo.slot] = nil
        self:Refresh()

        if (widgetInfo.onPop ~= nil) then
            widgetInfo.onPop()
        else
            widgetInfo.widget:SetEnabled(false)
        end
    end,
    Clear = function(self)
        for slot, widgetIndex in self.slots do
            local widgetInfo = self.widgets[widgetIndex]

            if (widgetInfo.onPop ~= nil) then
                widgetInfo.onPop()
            else
                widgetInfo.widget:SetEnabled(false)
            end
        end

        table.clear(self.slots)
        table.clear(self.indexStack)
    end,
    Count = function(self)
        return #self.indexStack
    end,

    Refresh = function(self)
        table.clear(self.indexStack)

        for slot, widgetIndex in self.slots do
            table.insert(self.indexStack, widgetIndex)
        end
        -- Fix screenspace position of widgets here
    end
}

__global_uiStacks = {
    nameToIndex = { },
    stacks = { },

    Register = function(self, stackName)
        local stack = {
            name = stackName,
            nameToIndex = {}, 
            widgets = {},
            indexStack = {},
            slots = {}
        }
        stack = setmetatable(stack, {__index = StackMeta})

        table.insert(self.stacks, stack)

        local stackIndex = #self.stacks
        self.nameToIndex[stackName] = stackIndex

        return stack;
    end,
    Get = function(self, name)
        local index = self.nameToIndex[name]
        if (index == nil) then
            return nil
        end
        return self.stacks[index]
    end,
}

function Module.Get(name)
    local stack = __global_uiStacks:Get(name)
    if (stack ~= nil) then
        return stack
    end

    return __global_uiStacks:Register(name)
end

return Module