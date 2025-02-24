-- Define the class
export type MyClass = {
    value : number,

    new : () -> MyClass,
    increment : (self : MyClass) -> (),
    getValue : (self : MyClass) -> number,
}

local Module : MyClass = {}
Module.__index = Module

-- Constructor
function Module.new() : MyClass
    local self = setmetatable({}, Module)
    self.value = 0
    return self
end

-- Method
function Module:increment()
    self.value = self.value + 1
end

function Module:getValue()
    return self.value
end

return Module