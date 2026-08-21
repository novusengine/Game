local InteractionAPI = require("@src/API/Game/Interaction");
local Button = require("@src/API/UI/Button");
local Draggable = require("@src/API/UI/Draggable");
local HorizontalLayout = require("@src/API/UI/HorizontalLayout");
local ScrollBox = require("@src/API/UI/ScrollBox");
local TextUtil = require("@src/API/UI/TextUtil");
local VerticalLayout = require("@src/API/UI/VerticalLayout");

local WINDOW_WIDTH = 350;
local WINDOW_HEIGHT = 550;
local CONTENT_WIDTH = WINDOW_WIDTH - 32;
local SCROLL_CONTENT_WIDTH = CONTENT_WIDTH - 10;
local SCROLL_VIEW_HEIGHT = WINDOW_HEIGHT - 32 - 28 - 1 - 28 - 30;
local OPTION_HEIGHT = 48;
local OPTION_SPACING = 6;
local LAYER = 1200i;
local BODY_TEXT = "InteractionDialogueBody";
local OPTION_TEXT = "InteractionDialogueOption";
local SMALL_TEXT = "InteractionDialogueSmall";
local TITLE_TEXT = "InteractionDialogueTitle";

local colors =
{
    muted = vec.New(0.58, 0.61, 0.67),
    title = vec.New(0.90, 0.78, 0.48),
    success = vec.New(0.44, 0.78, 0.52),
    warning = vec.New(0.92, 0.70, 0.30),
};

local resultNames =
{
    [0] = "That response is unavailable.",
    [1] = "That response is no longer valid.",
    [2] = "The requirements for that response changed.",
    [3] = "That action could not be completed.",
    [4] = "Action completed.",
};

UI.RegisterTextTemplate(BODY_TEXT, {
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = 10.0,
    borderSize = 1.0,
    borderColor = vec.New(0.0, 0.0, 0.0),
});
UI.RegisterTextTemplate(OPTION_TEXT, {
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = 9.0,
    borderSize = 1.0,
    borderColor = vec.New(0.0, 0.0, 0.0),
});
UI.RegisterTextTemplate(SMALL_TEXT, {
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = 7.0,
    borderSize = 1.0,
    borderColor = vec.New(0.0, 0.0, 0.0),
});
UI.RegisterTextTemplate(TITLE_TEXT, {
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = 14.0,
    color = colors.title,
    borderSize = 1.0,
    borderColor = vec.New(0.0, 0.0, 0.0),
});

local canvas = UI.GetCanvas("InteractionMenu", vec.New(0, 0), vec.New(1920, 1080), false);
local root = VerticalLayout.New(canvas, vec.New(-515, 15), vec.New(WINDOW_WIDTH, WINDOW_HEIGHT), LAYER, {
    panelTemplate = "DialogBox",
    padding = 16,
    spacing = 10,
    defaultChildOpts = { modeX = "flex" },
});
root:SetAnchor(vec.New(0.5, 0.5));
root:SetRelativePoint(vec.New(0.5, 0.5));
root:SetVisible(false);

local header = HorizontalLayout.New(root, vec.New(0, 0), vec.New(CONTENT_WIDTH, 28), 1i, {
    panelTemplate = "DebugDarkGrey",
    alpha = 0.0,
    spacing = 8,
    alignV = "center",
});
root:SetChildOpts(header, { modeX = "flex", sizeY = 28 });

local sourceText = header:NewText("Conversation", vec.New(0, 0), 1i, TITLE_TEXT);
local closeIconButton = Button.New(header, vec.New(0, 0), vec.New(26, 24), 1i, { text = "X", textTemplate = OPTION_TEXT });
header:SetChildOpts(sourceText, { modeX = "flex", sizeY = 22 });
header:SetChildOpts(closeIconButton, { sizeX = 26, sizeY = 24 });
Draggable.Attach(root, header);

local divider = root:NewPanel(vec.New(0, 0), vec.New(CONTENT_WIDTH, 1), 1i, "DebugDarkGrey");
divider:SetColor(vec.New(0.30, 0.30, 0.35));
divider:SetBorderSize(0.0);
root:SetChildOpts(divider, { modeX = "flex", sizeY = 1 });

local contentScroll = ScrollBox.New(root, vec.New(0, 0), vec.New(CONTENT_WIDTH, SCROLL_VIEW_HEIGHT), 1i, {
    panelTemplate = "DebugDarkGrey",
    contentPanelTemplate = "DebugDarkGrey",
    contentSizeX = SCROLL_CONTENT_WIDTH,
    contentSizeY = 1,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    autoScrollToStart = true,
    alpha = 0.0,
});
root:SetChildOpts(contentScroll, { modeX = "flex", sizeY = SCROLL_VIEW_HEIGHT });

local contentLayout = VerticalLayout.New(contentScroll.content, vec.New(0, 0), vec.New(SCROLL_CONTENT_WIDTH, 1), 1i, {
    panelTemplate = "DebugDarkGrey",
    alpha = 0.0,
    spacing = OPTION_SPACING,
    defaultChildOpts = { modeX = "flex", sizeY = OPTION_HEIGHT },
});

local greetingText = contentLayout:NewText("", vec.New(0, 0), 1i, BODY_TEXT);
greetingText:SetWrapWidth(SCROLL_CONTENT_WIDTH);
contentLayout:SetChildOpts(greetingText, { modeX = "flex", sizeY = 0 });

local footer = HorizontalLayout.New(root, vec.New(0, 0), vec.New(CONTENT_WIDTH, 28), 1i, {
    panelTemplate = "DebugDarkGrey",
    alpha = 0.0,
    spacing = 8,
    alignV = "center",
});
root:SetChildOpts(footer, { modeX = "flex", sizeY = 28 });

local statusText = footer:NewText("", vec.New(0, 0), 1i, SMALL_TEXT);
statusText:SetColor(colors.muted);
local goodbyeButton = Button.New(footer, vec.New(0, 0), vec.New(112, 26), 1i, { text = "Goodbye", textTemplate = OPTION_TEXT });
footer:SetChildOpts(statusText, { modeX = "flex", sizeY = 20 });
footer:SetChildOpts(goodbyeButton, { sizeX = 112, sizeY = 26 });

local currentSession : InteractionAPI.Session? = nil;
local optionButtons : { any } = { };
local stateVersion = -1;
local locallyDismissedSessionID : integer? = nil;
local selectionPending = false;

local function ClearOptions()
    for _, button in optionButtons do
        contentLayout:Remove(button.root);
        UI.DestroyWidget(button.root._raw);
    end
    optionButtons = { };
end

local function SelectOption(option : InteractionAPI.Option)
    if not option.enabled or selectionPending or currentSession == nil then return; end

    if InteractionAPI.Select(currentSession.sessionID, currentSession.revision, option.token) then
        selectionPending = true;
        statusText:SetText("Waiting for a response...");
        statusText:SetColor(colors.muted);
        for _, button in optionButtons do button:SetInteractable(false); end
    else
        statusText:SetText("Unable to send that response.");
        statusText:SetColor(colors.warning);
    end
end

local function RefreshContentHeight(greetingHeight : number, optionCount : number)
    local childCount = (greetingHeight > 0 and 1 or 0) + optionCount;
    local contentHeight = greetingHeight + optionCount * OPTION_HEIGHT + math.max(0, childCount - 1) * OPTION_SPACING;
    contentLayout:SetHeight(math.max(1, contentHeight));
    contentScroll:SetContentHeight(math.max(1, contentHeight));
end

local function PopulateOptions(options : { InteractionAPI.Option }, greetingHeight : number)
    ClearOptions();

    for _, option in options do
        local button = Button.New(contentLayout, vec.New(0, 0), vec.New(SCROLL_CONTENT_WIDTH, OPTION_HEIGHT), 1i, {
            text = TextUtil.TruncateToWidth(option.text, OPTION_TEXT, SCROLL_CONTENT_WIDTH - 24),
            textTemplate = OPTION_TEXT,
            textAlign = "left",
            textPadX = 12,
        });
        button.text:SetAnchor(vec.New(0.0, 1.0));
        button.text:SetRelativePoint(vec.New(0.0, 1.0));
        button.text:SetPos(vec.New(12, -7));

        local reason = if option.disabledReason ~= "" then option.disabledReason else "This response is unavailable.";
        local reasonText = button:NewText(if option.enabled then "" else TextUtil.TruncateToWidth(reason, SMALL_TEXT, SCROLL_CONTENT_WIDTH - 24), vec.New(12, -29), 1i, SMALL_TEXT);
        reasonText:SetAnchor(vec.New(0.0, 1.0));
        reasonText:SetRelativePoint(vec.New(0.0, 1.0));
        reasonText:SetColor(colors.warning);
        reasonText:SetInteractable(false);

        button:SetInteractable(option.enabled);
        button:SetOnMouseUp(function(_, _, mouseButton)
            if mouseButton == 0i then SelectOption(option); end
        end);
        table.insert(optionButtons, button);
    end

    RefreshContentHeight(greetingHeight, #options);
    contentScroll:ScrollToStart();
end

local function Hide(sendClose : boolean)
    if sendClose and currentSession ~= nil then
        if not InteractionAPI.Close(currentSession.sessionID) then
            statusText:SetText("Unable to close the conversation.");
            statusText:SetColor(colors.warning);
            return;
        end
        locallyDismissedSessionID = currentSession.sessionID;
    end
    root:SetVisible(false);
end

closeIconButton:SetOnMouseUp(function() Hide(true); end);
goodbyeButton:SetOnMouseUp(function() Hide(true); end);

local function ApplyState()
    local state = InteractionAPI.GetState();
    if state.version == stateVersion then return; end
    stateVersion = state.version;
    selectionPending = false;

    local session = state.activeSession;
    if session == nil then
        currentSession = nil;
        ClearOptions();
        root:SetVisible(false);
        return;
    end
    if locallyDismissedSessionID == session.sessionID then return; end

    local isNewRevision = currentSession == nil or currentSession.sessionID ~= session.sessionID or currentSession.revision ~= session.revision;
    currentSession = session;

    local sourceName = if session.sourceUnitID == INVALID_UNIT_ID then "" else Unit.GetName(session.sourceUnitID);
    sourceText:SetText(TextUtil.TruncateToWidth(if sourceName ~= "" then sourceName else "Conversation", TITLE_TEXT, CONTENT_WIDTH - 34));
    local wrappedGreeting, greetingSize = UI.WrapText(session.greeting, BODY_TEXT, SCROLL_CONTENT_WIDTH);
    local greetingHeight = if session.greeting == "" then 0 else math.max(16, greetingSize.y);
    greetingText:SetText(wrappedGreeting);
    greetingText:SetVisible(session.greeting ~= "");
    contentLayout:SetChildOpts(greetingText, { modeX = "flex", sizeY = greetingHeight });
    if isNewRevision then
        PopulateOptions(session.options, greetingHeight);
        statusText:SetText("");
        locallyDismissedSessionID = nil;
    else
        RefreshContentHeight(greetingHeight, #session.options);
    end

    if state.lastResult ~= nil and state.lastResult.sessionID == session.sessionID and state.lastResult.revision == session.revision then
        statusText:SetText(resultNames[integer.tonumber(state.lastResult.result)] or "The server rejected that response.");
        statusText:SetColor(if state.lastResult.result == 4i then colors.success else colors.warning);
        for index, button in optionButtons do
            button:SetInteractable(session.options[index].enabled);
        end
    end

    root:SetVisible(true);
end

RegisterEvent(GameEvent.Updated, function()
    ApplyState();
end);

ApplyState();
