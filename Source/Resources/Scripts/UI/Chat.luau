local UIButton = require("@src/API/UI/Button");
local UIInputBox = require("@src/API/UI/InputBox");
local InputAPI = require("@src/API/Input/Input");
--local UIScrollBox : ScrollBoxAPI = require("@src/API/UI/ScrollBox");
local UIVerticalLayout = require("@src/API/UI/VerticalLayout");
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
    chatScrollOffset : number, -- Pixel offset from the latest chat messages
}

local chatToOpenOnEnter : Chat? = nil;
local MAX_MESSAGE_HISTORY_SIZE : number = 50; -- Maximum number of messages to keep in history
local INPUT_LOCATION_BOTTOM : number = 1;
local INPUT_LOCATION_TOP : number = 2;
local INPUT_ALPHA_UNFOCUSED : number = 0.55;
local INPUT_ALPHA_FOCUSED : number = 1.0;
local CHAT_SCROLL_STEP : number = 20;

local function RegisterTemplates()
    UI.RegisterPanelTemplate("ChatRootPanel",
    {
        color = vector.create(0.02, 0.02, 0.02),
        cornerRadius = 0.0,
    });

    UI.RegisterPanelTemplate("ChatHistoryPanel",
    {
        color = vector.create(0.035, 0.035, 0.035),
        cornerRadius = 0.0,
    });

    UI.RegisterPanelTemplate("ChatInputPanel",
    {
        color = vector.create(0.055, 0.055, 0.055),
        cornerRadius = 0.0,
    });

    UI.RegisterPanelTemplate("ChatSeparator",
    {
        color = vector.create(0.45, 0.36, 0.10),
        cornerRadius = 0.0,
    });

    UI.RegisterTextTemplate("ChatText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 15.0,
        borderSize = 0.5,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("ChatInputText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 16.0,
        color = vector.create(0.9, 0.9, 0.9),
        borderSize = 0.5,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });
end

local function GetTextColor(message : ChatMessage) : vector
    -- Determine the text color based on the channel and whether the message is outgoing
    if (message.channel == "Whisper") then
        -- Magenta for whispers
        return vector.create(1.0, 0.45, 1.0); -- Magenta for whispers
    elseif (message.channel == "System") then
        -- Yellow for system messages
        return vector.create(1.0, 0.82, 0.15); -- Warm yellow for system messages
    end

    -- Default color for other messages
    return vector.create(0.92, 0.92, 0.92); -- Soft white for incoming messages
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
        formattedText = string.format("%s[%s] says: %s", timestampText, message.playerName, message.text);
    else
        formattedText = string.format("%s[%s] %s: %s", timestampText, message.channel, message.playerName, message.text);
    end

    return formattedText;
end

local function RefreshText(chat : Chat, preserveScrollPosition : boolean?, previousContentHeight : number?)
    local optionCategory = OptionsContext:GetCategory(OptionCategoryType.Gameplay);
    local optionGroup = optionCategory:Get("Chat");
    local optionSection = optionGroup:Get("General");

    local showTimestamp = (optionSection.options:Has("Show Timestamps") and optionSection:GetOption("Show Timestamps"):Get()) or false;
    local textSize = (optionSection.options:Has("Text Size") and optionSection:GetOption("Text Size"):Get()) or 15.0;

    -- Iterate newest-to-oldest. With the layout's reverse=true the first-added child sits
    -- at the bottom, so the newest message ends up at the bottom (matches original behavior).
    -- Keep all retained messages in the layout so the history can be scrolled in pixels rather
    -- than jumping by an entire message (which is especially coarse for wrapped messages).
    local widgetIndex = 1;
    for i = #chat.chatHistoryQueue, 1, -1 do
        local message = chat.chatHistoryQueue[i];

        if (chat.chatHistoryTexts[widgetIndex] == nil) then
            local text = chat.layout:NewText("", 0, 0, 0, "ChatText");
            text:SetWrapWidth(chat.historyContentWidth);
            text:SetWrapIndent(4);
            chat.chatHistoryTexts[widgetIndex] = text;
        end

        local text = chat.chatHistoryTexts[widgetIndex];
        text:SetEnabled(true);
        text:SetFontSize(textSize);
        text:SetText(FormatMessage(message, showTimestamp));
        text:SetColor(GetTextColor(message));

        widgetIndex += 1;
    end

    for j = widgetIndex, #chat.chatHistoryTexts do
        chat.chatHistoryTexts[j]:SetEnabled(false);
    end

    local contentHeight = chat.layout:GetMeasuredHeight();
    if (preserveScrollPosition and previousContentHeight ~= nil) then
        chat.chatScrollOffset += math.max(0, contentHeight - previousContentHeight);
    end

    local maxScrollOffset = math.max(0, contentHeight - chat.historyContentHeight);
    chat.chatScrollOffset = math.max(0, math.min(maxScrollOffset, chat.chatScrollOffset));
    chat.layout:SetPosY(chat.historyPaddingY - chat.chatScrollOffset);
end

local function AddMessageToChat(chat : Chat, message : string, channel : string?, playerName : string?, isOutgoing : boolean?)
    local playerName = playerName or ""; -- Default source if not provided
    local channel = channel or "System"; -- Default channel if not provided
    local isOutgoing = isOutgoing or false; -- Default to incoming if not specified
    
    local preserveScrollPosition = chat.chatScrollOffset > 0;
    local previousContentHeight = chat.layout:GetMeasuredHeight();

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

    RefreshText(chat, preserveScrollPosition, previousContentHeight); -- Refresh the chat display
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

local function SetInputLocation(chat : Chat, location : number)
    -- Use the same local origin for both regions so the separator and outer history
    -- margin remain consistent, regardless of which one is on top.
    chat.clipPanel:SetAnchor(0.0, 0.0);
    chat.clipPanel:SetRelativePoint(0.0, 0.0);
    chat.inputBox:SetAnchor(0.0, 0.0);
    chat.inputBox:SetRelativePoint(0.0, 0.0);

    if (location == INPUT_LOCATION_BOTTOM) then
        chat.inputBox:SetPosY(0);
        chat.separatorPanel:SetPosY(chat.inputBoxHeight);
        chat.clipPanel:SetPosY(chat.inputBoxHeight + chat.inputHistoryGap);
    else
        chat.clipPanel:SetPosY(chat.historyOuterMargin);
        chat.separatorPanel:SetPosY(chat.height - chat.inputBoxHeight - chat.separatorThickness);
        chat.inputBox:SetPosY(chat.height - chat.inputBoxHeight);
    end

    chat.clipPanel:ForceRefresh();
    chat.separatorPanel:ForceRefresh();
    chat.inputBox:ForceRefresh();
end

local function CreateChat(chatCanvas : Canvas)
    local chat = {};
    chat.chatHistoryQueue = {};
    chat.chatHistoryTexts = {};
    chat.chatScrollOffset = 0;

    -- Define constants for the chat area
    chat.width = 600;
    chat.height = 250;
    chat.inputBoxHeight = 30;
    chat.inputHistoryGap = 0;
    chat.historyOuterMargin = 5;
    chat.separatorThickness = 1;
    chat.historyPaddingX = 8;
    chat.historyPaddingY = 8;
    chat.scrollBoxWidth = chat.width;
    chat.scrollBoxHeight = chat.height - chat.inputBoxHeight - chat.inputHistoryGap - chat.historyOuterMargin;
    chat.historyContentWidth = chat.scrollBoxWidth - chat.historyPaddingX * 2;
    chat.historyContentHeight = chat.scrollBoxHeight - chat.historyPaddingY * 2;

    -- Create the base panel
    chat.basePanel = chatCanvas:NewPanel(20, 20, chat.width, chat.height, 0, "ChatRootPanel");
    chat.basePanel:SetAlpha(0.0);

    chat.clipPanel = chat.basePanel:NewPanel(0, 0, chat.scrollBoxWidth, chat.scrollBoxHeight, 0, "ChatHistoryPanel");
    chat.clipPanel:SetClipChildren(true); -- Enable clipping for the panel
    chat.clipPanel:SetClipRect(
        chat.historyPaddingX / chat.scrollBoxWidth,
        chat.historyPaddingY / chat.scrollBoxHeight,
        1.0 - chat.historyPaddingX / chat.scrollBoxWidth,
        1.0 - chat.historyPaddingY / chat.scrollBoxHeight);
    chat.clipPanel:SetAlpha(0.65);

    -- VerticalLayout(reverse=true): children stack bottom-up so newest message is at the bottom.
    -- heightMode="shrink" lets the layout grow upward as messages arrive; the clipPanel's clip
    -- mask hides anything that grows past its top edge.
    chat.layout = UIVerticalLayout.NewVerticalLayout(
        chat.clipPanel,
        chat.historyPaddingX,
        chat.historyPaddingY,
        chat.historyContentWidth,
        chat.historyContentHeight,
        0,
        {
            reverse = true,
            heightMode = "shrink",
            panelTemplate = "ChatRootPanel",
            alpha = 0.0,
        });
    chat.layout:SetAnchor(0.0, 0.0);
    chat.layout:SetRelativePoint(0.0, 0.0);

    -- Add scroll handlers
    chat.clipPanel:SetOnMouseScroll(function(eventID : number, widget : Widget, deltaX : number, deltaY : number)
        local contentHeight = chat.layout:GetMeasuredHeight();
        local maxScrollOffset = math.max(0, contentHeight - chat.historyContentHeight);
        chat.chatScrollOffset = math.max(0, math.min(
            maxScrollOffset,
            chat.chatScrollOffset + deltaY * CHAT_SCROLL_STEP));
        chat.layout:SetPosY(chat.historyPaddingY - chat.chatScrollOffset);
    end);

    -- Create the input box for sending messages
    chat.inputBox = UIInputBox.NewInputBox(chat.basePanel, 0, 0, chat.width, chat.inputBoxHeight, 0, {
        panelTemplate = "ChatInputPanel",
        textTemplate = "ChatInputText",
        placeholderText = "Type a message...",
    });
    chat.inputBox:SetTextVerticallyCentered();
    chat.inputBox.button.text:SetPosY(chat.inputBox.button.text:GetPosY() + 2);
    chat.inputBox.button.panel:SetAlpha(INPUT_ALPHA_UNFOCUSED);
    chat.inputBox:SetOnFocusBegin(function(inputBox : InputBox)
        inputBox.button.panel:SetAlpha(INPUT_ALPHA_FOCUSED);
    end);
    chat.inputBox:SetOnFocusEnd(function(inputBox : InputBox)
        inputBox.button.panel:SetAlpha(INPUT_ALPHA_UNFOCUSED);
    end);
    chat.inputBox:SetOnSubmit(function(inputBox : InputBox)
        return OnSubmit(chat, inputBox);
    end);

    chat.separatorPanel = chat.basePanel:NewPanel(0, 0, chat.width, chat.separatorThickness, 0, "ChatSeparator");

    -- Track the chat that receives keyboard focus when Enter is pressed.
    if (chatToOpenOnEnter == nil) then
        chatToOpenOnEnter = chat;
    end

    RegisterEvent(GameEvent.ChatMessageReceived, function(eventID : Event, data : any)
        AddMessageToChat(chat, data.message, data.channel, data.sender, false);
    end);
    return chat;
end

local function RegisterOptions(chat : Chat)
    local optionCategory = OptionsContext:GetCategory(OptionCategoryType.Gameplay);
    local optionGroup = optionCategory:Get("Chat");
    local optionSection = optionGroup:Get("General");

    local sizeOption = optionSection:RegisterOption("Text Size", 15.0, OptionType.Slider, { Min = 8, Max = 30, Step = 1});
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

    local inputLocationOption = optionSection:RegisterOption("Input Location", INPUT_LOCATION_TOP, OptionType.ComboBox, { options = { "Bottom", "Top" }});
    local inputLocationCancelCallback = inputLocationOption:RegisterOnValueChanged(function(self, value)    
        SetInputLocation(chat, value);
        return true;
    end);
    SetInputLocation(chat, inputLocationOption:Get());
end

local function OnGameLoaded(eventID : number, data : any)
    local chatCanvas : Canvas = UI.GetCanvas("Chat", 0, 0, 1920, 1080); -- Add layers here, for ordering input munching

    RegisterTemplates();

    if not IsInWorld() then
        return; -- Do not create chat if not in world
    end
    
    local mainChat = CreateChat(chatCanvas);
    RegisterOptions(mainChat);

    local binding = InputAPI.Keyboard(InputAPI.KeyValues.ENTER, InputModifier.None, InputModifierMatch.Any);
    InputAPI.Bind("OpenChat", "Open Chat", "Chat", binding, function(phase)
        if phase ~= InputPhase.Pressed or chatToOpenOnEnter == nil then
            return InputReply.Ignored;
        end

        if UI.WasJustFocusedWidget(chatToOpenOnEnter.inputBox.button.panel) then
            return InputReply.Ignored;
        end

        chatToOpenOnEnter.inputBox:Focus();
        return InputReply.Consumed;
    end, { defaultReply = InputReply.Ignored });

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

RegisterEvent(GameEvent.Loaded, OnGameLoaded)
