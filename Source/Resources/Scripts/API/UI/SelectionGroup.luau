export type ChangedCallback<T> = (value : T?, previousValue : T?) -> boolean?;

export type SelectionGroup<T> =
{
    Get : (self : SelectionGroup<T>) -> T?,
    Set : (self : SelectionGroup<T>, value : T?, notify : boolean?) -> boolean,
    Clear : (self : SelectionGroup<T>, notify : boolean?) -> boolean,
    SetOnChanged : (self : SelectionGroup<T>, callback : ChangedCallback<T>?) -> (),
}

export type SelectionGroupAPI =
{
    New : <T>(initialValue : T?, callback : ChangedCallback<T>?) -> SelectionGroup<T>,
}

local selectionGroupAPI : SelectionGroupAPI = { };

function selectionGroupAPI.New<T>(initialValue : T?, callback : ChangedCallback<T>?) : SelectionGroup<T>
    local self = {
        _value = initialValue,
        _onChanged = callback,
    } :: any;

    function self:Get() : T?
        return self._value;
    end

    function self:Set(value : T?, notify : boolean?) : boolean
        if value == self._value then return true; end
        local previousValue = self._value;
        if notify ~= false and self._onChanged ~= nil and self._onChanged(value, previousValue) == false then
            return false;
        end
        self._value = value;
        return true;
    end

    function self:Clear(notify : boolean?) : boolean
        return self:Set(nil, notify);
    end

    function self:SetOnChanged(newCallback : ChangedCallback<T>?)
        self._onChanged = newCallback;
    end

    return self :: SelectionGroup<T>;
end

return selectionGroupAPI;
