local UIButton = require("@src/API/UI/Button");
local UIInputBox = require("@src/API/UI/InputBox");
local InputAPI = require("@src/API/Input/Input");
local UIVerticalLayout = require("@src/API/UI/VerticalLayout");
local ChatListener = require("@src/API/Game/ChatListener");
local OptionsContext = require("@src/API/OptionsContext");
local EditMode = require("@src/API/Game/EditMode");
local EditModeDefaults = require("@src/API/Game/EditModeDefaults");

local COMBAT_CHANNEL = "Combat";

export type ChatMessage =
{
    text : string,
    channel : string, -- The channel of the message (e.g., "General", "Trade")
    playerName : string, -- The source of the message (e.g., player name) if incoming, the destination if outgoing
    isOutgoing : boolean, -- True if the message is incoming (from another player), false if outgoing (sent by the player)
    timestamp : number, -- Unix timestamp
}

export type ChannelScrollState =
{
    offset : number,
    wasAtBottom : boolean,
}

export type Chat =
{
    basePanel : Panel,

    --scrollBox : ScrollBox,
    clipPanel : Panel, -- The fake "scroll box" that clips the content
    inputBox : UIInputBox.InputBox,
    channelStrip : Panel,
    channelNames : { string },
    channelButtons : { UIButton.Button },
    activeChannel : string,

    normalHistory : {ChatMessage}, -- Non-combat messages (Say, System, Whisper, etc.)
    combatHistory : {ChatMessage}, -- Combat log messages
    chatHistoryTexts : {TextWidget}, -- Text objects for displaying chat messages
    chatScrollOffset : number, -- Pixel offset from the latest chat messages
    chatVisibleContentHeight : number, -- Visible content height after channel strip adjustment
    chatActiveContentHeight : number, -- Content height of the active channel's messages
    channelScrollStates : { [string]: ChannelScrollState }, -- Per-channel scroll state
    pendingScrollRestore : ChannelScrollState?, -- Scroll state to restore after channel switch
}

local chatToOpenOnEnter : Chat? = nil;
local MAX_MESSAGE_HISTORY_SIZE : number = 256;
local INPUT_LOCATION_BOTTOM : number = 1;
local INPUT_LOCATION_TOP : number = 2;
local INPUT_ALPHA_UNFOCUSED : number = 0.55;
local INPUT_ALPHA_FOCUSED : number = 1.0;
local CHAT_SCROLL_STEP : number = 20;
local CHANNEL_BUTTON_WIDTH : number = 70;
local CHANNEL_BUTTON_HEIGHT : number = 24;
local CHANNEL_BUTTON_PADDING : number = 4;
local COMBAT_EVENT_DAMAGE : integer = 0i;
local COMBAT_EVENT_HEAL : integer = 1i;
local COMBAT_EVENT_RESURRECT : integer = 2i;

local function RegisterTemplates()
    UI.RegisterPanelTemplate("ChatRootPanel",
    {
        color = vec.New(0.02, 0.02, 0.02),
        cornerRadius = 0.0,
    });

    UI.RegisterPanelTemplate("ChatHistoryPanel",
    {
        color = vec.New(0.035, 0.035, 0.035),
        cornerRadius = 0.0,
    });

    UI.RegisterPanelTemplate("ChatInputPanel",
    {
        color = vec.New(0.055, 0.055, 0.055),
        cornerRadius = 0.0,
    });

    UI.RegisterPanelTemplate("ChatSeparator",
    {
        color = vec.New(0.45, 0.36, 0.10),
        cornerRadius = 0.0,
    });

    UI.RegisterTextTemplate("ChatText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 15.0,
        borderSize = 0.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("ChatInputText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 16.0,
        color = vec.New(0.9, 0.9, 0.9),
        borderSize = 0.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterPanelTemplate("ChatChannelButton",
    {
        color = vec.New(0.055, 0.055, 0.055),
        cornerRadius = 0.0,
    });

    UI.RegisterPanelTemplate("ChatChannelStrip",
    {
        color = vec.New(0.02, 0.02, 0.02),
        cornerRadius = 0.0,
    });

    UI.RegisterPanelTemplate("ChatChannelButtonActive",
    {
        color = vec.New(0.15, 0.15, 0.15),
        cornerRadius = 0.0,
    });
end

local function GetTextColor(message : ChatMessage) : vec
    -- Determine the text color based on the channel and whether the message is outgoing
    if (message.channel == "Whisper") then
        return vec.New(1.0, 0.45, 1.0);
    elseif (message.channel == "System") then
        return vec.New(1.0, 0.82, 0.15);
    elseif (message.channel == COMBAT_CHANNEL) then
        return vec.New(1.0, 0.55, 0.55);
    end

    return vec.New(0.92, 0.92, 0.92);
end

local function FormatMessage(message : ChatMessage, showTimestamp : boolean) : string
    local formattedText = "";
    local timestampText = "";
    if (showTimestamp) then
        timestampText = string.format("%s ", os.date("%H:%M:%S", message.timestamp));
    end

    if (message.channel == "Whisper") then
        if (message.isOutgoing) then
            formattedText = string.format("%sTo %s: %s", timestampText, message.playerName, message.text);
        else
            formattedText = string.format("%s%s whispers: %s", timestampText, message.playerName, message.text);
        end
    elseif (message.channel == "Say") then
        formattedText = string.format("%s[%s] says: %s", timestampText, message.playerName, message.text);
    elseif (message.channel == COMBAT_CHANNEL) then
        formattedText = string.format("%s%s", timestampText, message.text);
    else
        formattedText = string.format("%s[%s] %s: %s", timestampText, message.channel, message.playerName, message.text);
    end

    return formattedText;
end

local function RefreshText(chat : Chat, preserveScrollPosition : boolean?, previousContentHeight : number?)
    local optionCategory = OptionsContext:GetCategory(OptionsContext.OptionCategoryType.Gameplay);
    local optionGroup = optionCategory:Get("Chat");
    local optionSection = optionGroup:Get("General");

    local showTimestamp = (optionSection.options:Has("Show Timestamps") and optionSection:GetOption("Show Timestamps"):Get()) or false;
    local textSize = (optionSection.options:Has("Text Size") and optionSection:GetOption("Text Size"):Get()) or 15.0;

    local sourceQueue;
    if (chat.activeChannel == COMBAT_CHANNEL) then
        sourceQueue = chat.combatHistory;
    else
        sourceQueue = chat.normalHistory;
    end

    local widgetIndex = 1;
    for i = #sourceQueue, 1, -1 do
        local message = sourceQueue[i];

        if (chat.chatHistoryTexts[widgetIndex] == nil) then
            local text = chat.layout:NewText("", vec.New(0, 0), 0i, "ChatText");
            text:SetWrapWidth(chat.historyContentWidth);
            text:SetWrapIndent(4i);
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

    local contentHeight = 0;
    for i = 1, widgetIndex - 1 do
        contentHeight += chat.chatHistoryTexts[i]:GetHeight();
    end
    chat.chatActiveContentHeight = contentHeight;

    local maxScrollOffset = math.max(0, contentHeight - chat.chatVisibleContentHeight);

    if (chat.pendingScrollRestore ~= nil) then
        local restore = chat.pendingScrollRestore;
        chat.pendingScrollRestore = nil;

        if (restore.wasAtBottom) then
            chat.chatScrollOffset = maxScrollOffset;
        else
            chat.chatScrollOffset = math.max(0, math.min(maxScrollOffset, restore.offset));
        end
    elseif (preserveScrollPosition and previousContentHeight ~= nil) then
        chat.chatScrollOffset += math.max(0, contentHeight - previousContentHeight);
    end

    chat.chatScrollOffset = math.max(0, math.min(maxScrollOffset, chat.chatScrollOffset));
    chat.layout:SetPosY(chat.historyPaddingY - chat.chatScrollOffset);
end

local function SetActiveChannel(chat : Chat, channelName : string)
    -- Save current channel's scroll state before switching
    if (chat.activeChannel ~= nil) then
        local maxScrollOffset = math.max(0, chat.chatActiveContentHeight - chat.chatVisibleContentHeight);
        chat.channelScrollStates[chat.activeChannel] = {
            offset = chat.chatScrollOffset,
            wasAtBottom = chat.chatScrollOffset >= maxScrollOffset - 1,
        };
    end

    for j, otherBtn in chat.channelButtons do
        otherBtn:SetVisualEnabled(false);
        otherBtn.root:SetAnchor(vec.New(0.0, 0.0));
        otherBtn.root:SetRelativePoint(vec.New(0.0, 0.0));
    end

    chat.activeChannel = channelName;
    local activeIndex = table.find(chat.channelNames, channelName);
    if (activeIndex ~= nil) then
        local button = chat.channelButtons[activeIndex];
        button:SetVisualEnabled(true);
        button.root:SetAnchor(vec.New(0.0, 0.0));
        button.root:SetRelativePoint(vec.New(0.0, 0.0));
    end

    -- Queue restore for RefreshText
    chat.pendingScrollRestore = chat.channelScrollStates[channelName];
    RefreshText(chat);
end

local function AddMessageToChat(chat : Chat, message : string, channel : string?, playerName : string?, isOutgoing : boolean?)
    local resolvedChannel = channel or "System";
    local resolvedPlayerName = playerName or "";
    local resolvedOutgoing = isOutgoing or false;

    local preserveScrollPosition = chat.chatScrollOffset > 0;
    local previousContentHeight = chat.chatActiveContentHeight;

    local newMessage : ChatMessage = {
        text = message,
        channel = resolvedChannel,
        playerName = resolvedPlayerName,
        isOutgoing = resolvedOutgoing,
        timestamp = os.time(),
    };

    local queue;
    if (resolvedChannel == COMBAT_CHANNEL) then
        queue = chat.combatHistory;
    else
        queue = chat.normalHistory;
    end

    table.insert(queue, newMessage);

    if (#queue > MAX_MESSAGE_HISTORY_SIZE) then
        table.remove(queue, 1);
    end

    RefreshText(chat, preserveScrollPosition, previousContentHeight);
end

local function OnSubmit(chat : Chat, inputBox : UIInputBox.InputBox)
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
    chat.clipPanel:SetAnchor(vec.New(0.0, 0.0));
    chat.clipPanel:SetRelativePoint(vec.New(0.0, 0.0));
    chat.inputBox:SetAnchor(vec.New(0.0, 0.0));
    chat.inputBox:SetRelativePoint(vec.New(0.0, 0.0));

    if (location == INPUT_LOCATION_BOTTOM) then
        chat.inputBox:SetPosY(0);
        chat.separatorPanel:SetPosY(chat.inputBoxHeight);
        chat.clipPanel:SetPosY(chat.inputBoxHeight + chat.separatorThickness + chat.inputHistoryGap);
    else
        chat.inputBox:SetPosY(chat.height - chat.inputBoxHeight);
        chat.separatorPanel:SetPosY(chat.height - chat.inputBoxHeight - chat.separatorThickness);
        chat.clipPanel:SetPosY(chat.inputHistoryGap);
    end

    chat.clipPanel:ForceRefresh();
    chat.separatorPanel:ForceRefresh();
    chat.inputBox:ForceRefresh();
end

local function CreateChat(chatCanvas : Canvas)
    local chat = {};
    chat.normalHistory = {};
    chat.combatHistory = {};
    chat.chatHistoryTexts = {};
    chat.chatScrollOffset = 0;
    chat.channelScrollStates = {};
    chat.pendingScrollRestore = nil;
    chat.chatActiveContentHeight = 0;

    -- Define constants for the chat area
    chat.width = 600;
    chat.height = 250;
    chat.inputBoxHeight = 30;
    chat.inputHistoryGap = 0;
    chat.historyOuterMargin = 5;
    chat.separatorThickness = 2;
    chat.historyPaddingX = 4;
    chat.historyPaddingY = 4;
    chat.scrollBoxWidth = chat.width;
    chat.scrollBoxHeight = chat.height - chat.inputHistoryGap - chat.separatorThickness;
    chat.historyContentWidth = chat.scrollBoxWidth - chat.historyPaddingX * 2;
    chat.historyContentHeight = chat.scrollBoxHeight - chat.historyPaddingY * 2;

    -- Create the base panel
    local editModeDefault = EditModeDefaults.GetElement(EditModeDefaults.ElementIDs.ChatWindow);
    if editModeDefault == nil then
        error("ChatWindow is not defined in EditModeDefaults");
    end
    local defaultPosition = vec.New(editModeDefault.defaultPosX, editModeDefault.defaultPosY);
    chat.basePanel = chatCanvas:NewPanel(defaultPosition, vec.New(chat.width, chat.height), 0i, "ChatRootPanel");
    chat.basePanel:SetAlpha(0.0);

    chat.clipPanel = chat.basePanel:NewPanel(vec.New(0, 0), vec.New(chat.scrollBoxWidth, chat.scrollBoxHeight), 0i, "ChatHistoryPanel");
    chat.clipPanel:SetClipChildren(true); -- Enable clipping for the panel
    chat.clipPanel:SetClipRect(
        vec.New(chat.historyPaddingX / chat.scrollBoxWidth, chat.historyPaddingY / chat.scrollBoxHeight),
        vec.New(1.0 - chat.historyPaddingX / chat.scrollBoxWidth, 1.0 - chat.historyPaddingY / chat.scrollBoxHeight));
    chat.clipPanel:SetAlpha(0.65);

    -- VerticalLayout(reverse=true): children stack bottom-up so newest message is at the bottom.
    -- heightMode="shrink" lets the layout grow upward as messages arrive; the clipPanel's clip
    -- mask hides anything that grows past its top edge.
    chat.layout = UIVerticalLayout.New(
        chat.clipPanel,
        vec.New(chat.historyPaddingX, chat.historyPaddingY),
        vec.New(chat.historyContentWidth, chat.historyContentHeight),
        0i,
        {
            reverse = true,
            heightMode = "shrink",
            panelTemplate = "ChatRootPanel",
            alpha = 0.0,
        });
    chat.layout:SetAnchor(vec.New(0.0, 0.0));
    chat.layout:SetRelativePoint(vec.New(0.0, 0.0));
    chat.chatVisibleContentHeight = chat.historyContentHeight;

    -- Add scroll handlers
    chat.clipPanel:SetOnMouseScroll(function(eventID : integer, widget : Widget, deltaX : number, deltaY : number)
        local maxScrollOffset = math.max(0, chat.chatActiveContentHeight - chat.chatVisibleContentHeight);
        chat.chatScrollOffset = math.max(0, math.min(
            maxScrollOffset,
            chat.chatScrollOffset + deltaY * CHAT_SCROLL_STEP));
        chat.layout:SetPosY(chat.historyPaddingY - chat.chatScrollOffset);
    end);

    -- Create the input box for sending messages
    chat.inputBox = UIInputBox.New(chat.basePanel, vec.New(0, 0), vec.New(chat.width, chat.inputBoxHeight), 0i, {
        panelTemplate = "ChatInputPanel",
        textTemplate = "ChatInputText",
        placeholderText = "Type a message...",
    });
    chat.inputBox:SetTextVerticallyCentered();
    chat.inputBox.button.text:SetPosY(chat.inputBox.button.text:GetPosY() + 2);
    chat.inputBox.button.root:SetAlpha(INPUT_ALPHA_UNFOCUSED);
    chat.inputBox:SetOnFocusBegin(function(inputBox : UIInputBox.InputBox)
        inputBox.button.root:SetAlpha(INPUT_ALPHA_FOCUSED);
    end);
    chat.inputBox:SetOnFocusEnd(function(inputBox : UIInputBox.InputBox)
        inputBox.button.root:SetAlpha(INPUT_ALPHA_UNFOCUSED);
    end);
    chat.inputBox:SetOnSubmit(function(inputBox : UIInputBox.InputBox)
        return OnSubmit(chat, inputBox);
    end);

    chat.separatorPanel = chat.basePanel:NewPanel(vec.New(0, 0), vec.New(chat.width, chat.separatorThickness), 0i, "ChatSeparator");

    local channelNames = { "Say", "Combat" };
    local totalChannelWidth = #channelNames * CHANNEL_BUTTON_WIDTH + (#channelNames - 1) * CHANNEL_BUTTON_PADDING;
    local channelStripHeight = CHANNEL_BUTTON_HEIGHT + CHANNEL_BUTTON_PADDING;

    chat.channelStrip = chat.basePanel:NewPanel(vec.New(0, -24), vec.New(totalChannelWidth, CHANNEL_BUTTON_HEIGHT), 0i, "ChatChannelStrip");
    chat.channelStrip:SetAlpha(0.0);

    chat.channelNames = channelNames;
    chat.channelButtons = {};
    chat.channelStripHeight = channelStripHeight;

    for i, channelName in channelNames do
        local btnX = (i - 1) * (CHANNEL_BUTTON_WIDTH + CHANNEL_BUTTON_PADDING);
        local btnY = 0;
        local button = UIButton.New(chat.channelStrip, vec.New(btnX, btnY), vec.New(CHANNEL_BUTTON_WIDTH, CHANNEL_BUTTON_HEIGHT), 1i, {
            panelTemplate = "ChatChannelButton",
            textTemplate = "ChatText",
            text = channelName,
            textAlign = "center",
        });
        button:SetFontSize(13);
        chat.channelButtons[i] = button;
        button.root:SetOnMouseDown(function()
            SetActiveChannel(chat, channelName);
        end);
    end

    SetActiveChannel(chat, channelNames[1]);

    chat.clipPanel:SetPosY(chat.channelStripHeight);
    chat.clipPanel:SetSize(vec.New(chat.scrollBoxWidth, chat.scrollBoxHeight - chat.channelStripHeight));
    chat.layout:SetSize(vec.New(chat.historyContentWidth, chat.historyContentHeight - chat.channelStripHeight));
    chat.chatVisibleContentHeight = chat.historyContentHeight - chat.channelStripHeight;

    -- Track the chat that receives keyboard focus when Enter is pressed.
    if (chatToOpenOnEnter == nil) then
        chatToOpenOnEnter = chat;
    end

    EditMode.Register({
        id = EditModeDefaults.ElementIDs.ChatWindow,
        size = vec.New(chat.width, chat.height),
        getPos = function() return chat.basePanel:GetPos() end,
        setPos = function(pos) chat.basePanel:SetPos(pos) end,
        isEnabled = function() return chat.basePanel:IsVisible() end,
        setEnabled = function(enabled) chat.basePanel:SetVisible(enabled) end,
    });

    RegisterEvent(GameEvent.ChatMessageReceived, function(eventID : Event, data : any)
        AddMessageToChat(chat, data.message, data.channel, data.sender, false);
    end);

    RegisterEvent(GameEvent.CombatLog, function(eventID : Event, data : any)
        local text = "";
        if data.eventID == COMBAT_EVENT_DAMAGE then
            if data.value2 > 0.0 then
                text = string.format("%s dealt %.2f damage to %s (Overkill: %.2f)", data.sourceName, data.value1, data.targetName, data.value2);
            else
                text = string.format("%s dealt %.2f damage to %s", data.sourceName, data.value1, data.targetName);
            end
        elseif data.eventID == COMBAT_EVENT_HEAL then
            if data.value2 > 0.0 then
                text = string.format("%s healed %s for %.2f (Overheal: %.2f)", data.sourceName, data.targetName, data.value1, data.value2);
            else
                text = string.format("%s healed %s for %.2f", data.sourceName, data.targetName, data.value1);
            end
        elseif data.eventID == COMBAT_EVENT_RESURRECT then
            text = string.format("%s resurrected %s", data.sourceName, data.targetName);
        end
        AddMessageToChat(chat, text, COMBAT_CHANNEL, data.sourceName, false);
    end);

    return chat;
end

local function RegisterOptions(chat : Chat)
    local optionCategory = OptionsContext:GetCategory(OptionsContext.OptionCategoryType.Gameplay);
    local optionGroup = optionCategory:Get("Chat");
    local optionSection = optionGroup:Get("General");

    local sizeOption = optionSection:RegisterOption("Text Size", 15.0, OptionsContext.OptionType.Slider, { Min = 8, Max = 30, Step = 1});
    local sizeCancelCallback = sizeOption:RegisterOnValueChanged(function(self, value)
        RefreshText(chat);
        return true;
    end);

    local showTimestampOption = optionSection:RegisterOption("Show Timestamps", true, OptionsContext.OptionType.Checkbox);
    local showTimestampCancelCallback = showTimestampOption:RegisterOnValueChanged(function(self, value)
        RefreshText(chat); -- Refresh the chat display after changing timestamp visibility
        return true;
    end);

    local inputLocationOption = optionSection:RegisterOption("Input Location", INPUT_LOCATION_TOP, OptionsContext.OptionType.ComboBox, { options = { "Bottom", "Top" }});
    local inputLocationCancelCallback = inputLocationOption:RegisterOnValueChanged(function(self, value)    
        SetInputLocation(chat, value);
        return true;
    end);
    SetInputLocation(chat, inputLocationOption:Get());
end

local function OnGameLoaded(eventID : integer, data : any)
    local chatCanvas : Canvas = UI.GetCanvas("Chat", vec.New(0, 0), vec.New(1920, 1080)); -- Add layers here, for ordering input munching

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

        if UI.WasJustFocusedWidget(chatToOpenOnEnter.inputBox.button.root) then
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

RegisterEvent(GameEvent.Loaded, OnGameLoaded);
