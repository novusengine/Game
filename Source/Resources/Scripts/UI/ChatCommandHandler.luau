local ChatListener = require("@src/API/Game/ChatListener");

local function CommandHandler(message)
    if (message:sub(1, 1) ~= ".") then
        return false, false; -- Do not consume the message, allow it to be displayed
    end

    local command = message:sub(2);
    ExecCmd(command);

    return true, true; -- Consume the message and block it from being displayed
end

--ChatListener:SetDebug(true); -- Enable debug mode for chat listener
ChatListener:RegisterListener(CommandHandler);