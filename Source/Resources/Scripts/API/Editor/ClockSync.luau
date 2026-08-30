local timeSyncAPI = { };

export type TimeChangedCallback = (seconds : number) -> ()

export type TimeSyncAPI =
{
    Subscribe : (name : string, callback : TimeChangedCallback) -> (),
    Unsubscribe : (name : string) -> (),
}

local subscribers : { [string] : TimeChangedCallback } = { };
local nativeSubscriptionActive = false;

local function Dispatch(seconds : number)
    for _, callback in subscribers do callback(seconds); end
end

function timeSyncAPI.Subscribe(name : string, callback : TimeChangedCallback)
    subscribers[name] = callback;
    if not nativeSubscriptionActive then
        Time.SetOnSecondChanged(Dispatch);
        nativeSubscriptionActive = true;
    end
end

function timeSyncAPI.Unsubscribe(name : string)
    subscribers[name] = nil;
    if next(subscribers) == nil and nativeSubscriptionActive then
        Time.SetOnSecondChanged(nil);
        nativeSubscriptionActive = false;
    end
end

return timeSyncAPI :: TimeSyncAPI;
