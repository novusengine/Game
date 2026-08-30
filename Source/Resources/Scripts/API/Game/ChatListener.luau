
export type Listener = (message : string) -> (boolean, boolean)
export type ChatListener =
{
    RegisterListener : (self : ChatListener, callback : Listener) -> (),
    OnChat : (self : ChatListener, message : string) -> boolean,
    SetDebug : (self : ChatListener, debug : boolean) -> (),
}

local ChatListenerMetatable = {
    RegisterListener = function(self, callback)
        if (typeof(callback) ~= "function") then
            error("ChatListener:RegisterListener expects a function, got " .. typeof(callback), 2);
        end

        table.insert(self.listeners, callback);
        if (self.debug) then
            local src  = debug.info(2, "s"); -- where RegisterListener was called
            local line = debug.info(2, "l");
            local name = debug.info(callback, "n");
            print(string.format(
                "Chat listener '%s' registered from %s:%d",
                name, src, line
            ));
        end
    end,
    OnChat = function(self, message)
        if (self.listeners == nil) then
            return false; -- No listeners registered, don't block message
        end

        for _, listener in self.listeners do
            local resultConsume, resultBlockMessage = listener(message);
            resultConsume = resultConsume or false;
            resultBlockMessage = resultBlockMessage or false;

            if (resultConsume) then
                if (self.debug) then
                    local src  = debug.info(listener, "s");
                    local line = debug.info(listener, "l");
                    local name = debug.info(listener, "n");
                    print(string.format(
                        "Chat listener '%s' consumed message from %s:%d",
                        name, src, line
                    ));
                end
                return resultBlockMessage; -- Message consumed
            end
        end

        return false; -- No listener consumed the message, don't block message
    end,
    SetDebug = function(self, debug)
        -- Set debug mode for chat listener
        self.debug = debug;
    end,
};
ChatListenerMetatable.__index = ChatListenerMetatable;

local chatListener : ChatListener & { debug : boolean, listeners : { Listener } } = {
    debug = false, -- Default debug mode is off
    listeners = {}, -- List of registered chat listeners
} :: any

setmetatable(chatListener, ChatListenerMetatable);

return chatListener;
