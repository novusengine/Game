local UIButton = require("@src/API/UI/Button");
local UIInputBox = require("@src/API/UI/InputBox");
--local UIScrollBox : ScrollBoxAPI = require("@src/API/UI/ScrollBox");
local ChatListener = require("@src/API/Game/ChatListener");
local OptionsContext = require("@src/API/OptionsContext");

export type ChatMessage =
{
    text : string,
    channel : string, -- The channel of the message (e.g., "General", "Trade")
    playerName : string, -- The source of the message (e.g., player name) if incoming, the destination if outgoing
    isOutgoing : boolean, -- True if the message is incoming (from another player), false if outgoing (sent by the player)
    timestamp : number, -- Unix timestamp
}

export type Chat =
{
    basePanel : Panel,

    --scrollBox : ScrollBox,
    clipPanel : Panel, -- The fake "scroll box" that clips the content
    inputBox : InputBox,

    chatHistoryQueue : {ChatMessage}, -- Queue of chat messages
    chatHistoryTexts : {Text}, -- Text objects for displaying chat messages
    chatScrollIndex : number, -- Index for scrolling through chat history
}

local chatToOpenOnEnter : Chat? = nil;
local MAX_MESSAGE_HISTORY_SIZE : number = 50; -- Maximum number of messages to keep in history

local function RegisterTemplates()
    UI.RegisterTextTemplate("ChatText",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 18.0,
        borderSize = 1.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });
end

local function GetTextColor(message : ChatMessage) : vector
    -- Determine the text color based on the channel and whether the message is outgoing
    if (message.channel == "Whisper") then
        -- Magenta for whispers
        return vector.create(1.0, 0.0, 1.0); -- Magenta for whispers
    elseif (message.channel == "System") then
        -- Yellow for system messages
        return vector.create(1.0, 1.0, 0.0); -- Yellow for system messages
    end

    -- Default color for other messages
    return vector.create(1.0, 1.0, 1.0); -- White for incoming messages
end

local function FormatMessage(message : ChatMessage, showTimestamp : boolean) : string
    -- Format the message text with timestamp and source
    local formattedText = "";

    local timestampText = "";
    if (showTimestamp) then
        timestampText = string.format("%s ", os.date("%H:%M:%S", message.timestamp));
    end

    -- If the channel is Whisper, format it differently
    if (message.channel == "Whisper") then
        if (message.isOutgoing) then
            formattedText = string.format("%sTo %s: %s", timestampText, message.playerName, message.text);
        else
            formattedText = string.format("%s%s whispers: %s", timestampText, message.playerName, message.text);
        end
    elseif (message.channel == "Say") then
        formattedText = string.format("%s%s: %s", timestampText, message.playerName, message.text);
    else
        formattedText = string.format("%s[%s] %s: %s", timestampText, message.channel, message.playerName, message.text);
    end

    return formattedText;
end

local function RefreshText(chat : Chat)
    local yPadding = 5;
    local currentY = yPadding; -- Add a bit of padding to the bottom of the "scroll box"

    local startIndex = #chat.chatHistoryQueue - chat.chatScrollIndex + 1; -- Calculate the starting index based on the scroll index

    local optionCategory = OptionsContext:GetCategory(OptionCategoryType.Gameplay);
    local optionGroup = optionCategory:Get("Chat");
    local optionSection = optionGroup:Get("General");

    local showTimestamp = (optionSection.options:Has("Show Timestamps") and optionSection:GetOption("Show Timestamps"):Get()) or false;

    -- Loop through the chat history queue backwards beginning at the startIndex
    local widgetIndex = 1;
    for i = startIndex, 1, -1 do
        if (currentY <= chat.scrollBoxHeight) then
            local message = chat.chatHistoryQueue[i];

            -- Create a new text widget if we need to display more messages than we have text widgets
            if (chat.chatHistoryTexts[widgetIndex] == nil) then
                chat.chatHistoryTexts[widgetIndex] = chat.clipPanel:NewText("", 0, currentY, 0, "ChatText");
                chat.chatHistoryTexts[widgetIndex]:SetAnchor(0.0, 0.0);
                chat.chatHistoryTexts[widgetIndex]:SetRelativePoint(0.0, 0.0);
                chat.chatHistoryTexts[widgetIndex]:SetWrapWidth(chat.scrollBoxWidth); -- Set the wrap width to fit the scroll box
                chat.chatHistoryTexts[widgetIndex]:SetWrapIndent(4);
            end
            chat.chatHistoryTexts[widgetIndex]:SetPosY(currentY); -- Set the Y position of the text widget

            -- Update the text widget with the message content
            chat.chatHistoryTexts[widgetIndex]:SetText(FormatMessage(message, showTimestamp));

            -- Update the text color based on the message
            local textColor = GetTextColor(message);
            chat.chatHistoryTexts[widgetIndex]:SetColor(textColor);
            chat.chatHistoryTexts[widgetIndex]:SetEnabled(true); -- Enable the text widget

            currentY += chat.chatHistoryTexts[widgetIndex]:GetHeight(); -- Move down for the next message
            widgetIndex += 1;
        else
            break;
        end
    end

    for j = widgetIndex, #chat.chatHistoryTexts do
        -- Disable any remaining text widget that are not used
        chat.chatHistoryTexts[j]:SetEnabled(false); -- Disable the text widget
    end
end

local function AddMessageToChat(chat : Chat, message : string, channel : string?, playerName : string?, isOutgoing : boolean?)
    local playerName = playerName or ""; -- Default source if not provided
    local channel = channel or "System"; -- Default channel if not provided
    local isOutgoing = isOutgoing or false; -- Default to incoming if not specified
    
    -- Push a new message to the chat history queue
    local newMessage : ChatMessage = {
        text = message,
        channel = channel,
        playerName = playerName,
        isOutgoing = isOutgoing,
        timestamp = os.time(), -- Get the current Unix timestamp
    };
    table.insert(chat.chatHistoryQueue, newMessage);

    -- If the chat history exceeds the maximum size, remove the oldest message
    if (#chat.chatHistoryQueue > MAX_MESSAGE_HISTORY_SIZE) then
        table.remove(chat.chatHistoryQueue, 1); -- Remove the oldest message
    end

    -- Increment the scroll index only if it's NOT the first message
    -- This should ensure that if the chat is scrolled up, it doesn't jump down one message when a new message is added
    if (chat.chatScrollIndex ~= 1) then
        chat.chatScrollIndex += 1; -- Increment the scroll index for new messages
    end

    RefreshText(chat); -- Refresh the chat display
end

local function OnSubmit(chat : Chat, inputBox : InputBox)
    local text = inputBox:GetText();
    if (text == nil or text == "") then
        return true; -- Do not submit empty messages
    end

    local shouldBlockMessage = ChatListener:OnChat(text);

    if (not shouldBlockMessage) then
        AddMessageToChat(chat, text, "Say", "Pursche", true); -- Add the message to the chat history
    end
    inputBox:SetText(""); -- Clear the input box after submitting

    chatToOpenOnEnter = chat; -- Set the chat to open on enter
    return true; -- Unfocus
end

local function CreateChat(chatCanvas : Canvas)
    local chat = {};
    chat.chatHistoryQueue = {};
    chat.chatHistoryTexts = {};
    chat.chatScrollIndex = 1; -- Initialize the scroll index

    -- Define constants for the chat area
    chat.width = 600;
    chat.height = 250;
    local leftButtonPaddingX = 5;
    chat.inputBoxHeight = 30;
    chat.scrollBoxWidth = chat.width - leftButtonPaddingX;
    chat.scrollBoxHeight = chat.height - chat.inputBoxHeight;

    -- Create the base panel
    chat.basePanel = chatCanvas:NewPanel(20, 20, chat.width, chat.height, 0, "DialogBox");

    chat.clipPanel = chat.basePanel:NewPanel(leftButtonPaddingX, 0, chat.scrollBoxWidth, chat.scrollBoxHeight, 0, "DialogBox");
    chat.clipPanel:SetAnchor(0.0, 1.0);
    chat.clipPanel:SetRelativePoint(0.0, 1.0);
    chat.clipPanel:SetClipChildren(true); -- Enable clipping for the panel
    chat.clipPanel:SetAlpha(0.3);

    -- Add scroll handlers
    chat.clipPanel:SetOnMouseScroll(function(eventID : number, widget : Widget, deltaX : number, deltaY : number)
        -- Handle scrolling with mouse wheel
        if (deltaY < 0) then
            chat.chatScrollIndex -= 1; -- Scroll up
        else
            chat.chatScrollIndex += 1; -- Scroll down
        end

        -- Ensure the scroll index is within bounds
        if (chat.chatScrollIndex < 1) then
            chat.chatScrollIndex = 1;
        elseif (chat.chatScrollIndex > #chat.chatHistoryQueue) then
            chat.chatScrollIndex = #chat.chatHistoryQueue;
        end

        RefreshText(chat); -- Refresh the text display after scrolling
    end);

    -- Create the input box for sending messages
    chat.inputBox = UIInputBox.NewInputBox(chat.basePanel, leftButtonPaddingX, 0, chat.width - leftButtonPaddingX, chat.inputBoxHeight, 0, {
        --defaultText = "Type your message here...",
    });
    chat.inputBox:SetAnchor(0.0, 1.0);
    chat.inputBox:SetRelativePoint(0.0, 1.0);
    chat.inputBox:SetOnSubmit(function(inputBox : InputBox)
        return OnSubmit(chat, inputBox);
    end);
    --chat.inputBox:SetAlpha(0.3); -- WTF why does this make all the text go invisible, but not new text from the input box... Is it affecting the text template?

    -- Left side buttons
    if (chatToOpenOnEnter == nil) then
        chatToOpenOnEnter = chat;
    end

    return chat;
end

local function RegisterOptions(chat : Chat)
    local optionCategory = OptionsContext:GetCategory(OptionCategoryType.Gameplay);
    local optionGroup = optionCategory:Get("Chat");
    local optionSection = optionGroup:Get("General");

    local sizeOption = optionSection:RegisterOption("Text Size", 18.0, OptionType.Slider, { Min = 8, Max = 30, Step = 1});
    local sizeCancelCallback = sizeOption:RegisterOnValueChanged(function(self, value)
        for _, textWidget in chat.chatHistoryTexts do
            textWidget:SetFontSize(value);
        end

        RefreshText(chat); -- Refresh the chat display after changing text size
        return true;
    end);

    local showTimestampOption = optionSection:RegisterOption("Show Timestamps", true, OptionType.Checkbox);
    local showTimestampCancelCallback = showTimestampOption:RegisterOnValueChanged(function(self, value)
        RefreshText(chat); -- Refresh the chat display after changing timestamp visibility
        return true;
    end);

    local inputLocationOption = optionSection:RegisterOption("Input Location", 1, OptionType.ComboBox, { options = { "Bottom", "Top" }});
    local inputLocationCancelCallback = inputLocationOption:RegisterOnValueChanged(function(self, value)    
        if (value == 1) then
            chat.clipPanel:SetAnchor(0.0, 1.0);
            chat.clipPanel:SetRelativePoint(0.0, 1.0);

            chat.inputBox:SetPosY(0);
        elseif (value == 2) then
            chat.clipPanel:SetAnchor(0.0, 0.0);
            chat.clipPanel:SetRelativePoint(0.0, 0.0);

            chat.inputBox:SetPosY(chat.height - chat.inputBoxHeight);
        end
        chat.clipPanel:ForceRefresh();

        return true;
    end);
end

local function OnGameLoaded(eventID : number, motd : string)
    local chatCanvas : Canvas = UI.GetCanvas("Chat", 0, 0, 1920, 1080); -- Add layers here, for ordering input munching

    RegisterTemplates();
    local mainChat = CreateChat(chatCanvas);
    RegisterOptions(mainChat);

    -- Set up global enter key handler
    UI.AddOnKeyboard(function(eventID, ...)
        if (eventID == UIKeyboardEvent.Key) then
            local keyID, action, modifierMask = ...;

            local keyIDEnter = 257;
            -- actionMask is up, down etc
            -- modifierMask is ctrl, shift etc

            if (action ~= InputAction.Press or keyID ~= keyIDEnter) then
                return false;
            end

            if (chatToOpenOnEnter == nil) then
                return false; -- No chat to open
            end

            if (UI.WasJustFocusedWidget(chatToOpenOnEnter.inputBox.button.panel)) then
                -- If the input box was just focused, submit the message
                return false; -- Do not submit, this is the enter press to submit the message and we don't want to open the chat again
            end

            chatToOpenOnEnter.inputBox:Focus(); -- Focus the input box of the chat to open
            return true;
        end
        return false;
    end);

    -- Set up callback for pushing messages from C++
    UI.RegisterSendMessageToChatCallback(function(channel : string, playerName : string, text : string, isOutgoing : boolean)
        if (chatToOpenOnEnter == nil) then
            return false; -- No chat to open
        end

        AddMessageToChat(mainChat, text, channel, playerName, isOutgoing);
        return true;
    end);

    -- Debug: Add some initial messages
    AddMessageToChat(mainChat, "Welcome to NovusEngine!", "System", "", true);
    -- Examples of other channels
    --AddMessageToChat(mainChat, "Hello Pursche",         "Whisper", "Nix", false);
    --AddMessageToChat(mainChat, "WTS [Linen Cloth]",     "Trade", "Foe", true);
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded)