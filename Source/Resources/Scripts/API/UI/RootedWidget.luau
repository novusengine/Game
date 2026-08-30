export type RootedWidget<T> = T & Widget & { root : Widget }
export type RootedPanel<T> = T & Panel & { root : Panel }

export type RootedWidgetAPI =
{
    Attach : <T>(object : T) -> T,
}

local rootedWidgetAPI : RootedWidgetAPI = { }

local function ReadPreviousIndex(previousIndex : any, object : any, key : any) : any
    if type(previousIndex) == "function" then
        return previousIndex(object, key)
    end
    if type(previousIndex) == "table" then
        return previousIndex[key]
    end
    return nil
end

function rootedWidgetAPI.Attach<T>(object : T) : T
    local previousMetatable = getmetatable(object)
    local previousIndex = if previousMetatable ~= nil then previousMetatable.__index else nil
    local metatable = { }

    if previousMetatable ~= nil then
        for key, value in previousMetatable do
            metatable[key] = value
        end
    end

    metatable.__index = function(self, key)
        local previousValue = ReadPreviousIndex(previousIndex, self, key)
        if previousValue ~= nil then
            return previousValue
        end

        local root = rawget(self, "root")
        if root == nil then
            return nil
        end

        local value = root[key]
        if type(value) ~= "function" then
            return value
        end

        return function(_, ...)
            return value(root, ...)
        end
    end

    setmetatable(object, metatable)
    return object
end

return rootedWidgetAPI
