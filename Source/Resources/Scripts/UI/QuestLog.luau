local FrameManager = require("@src/API/Game/FrameManager");
local InteractionAPI = require("@src/API/Game/Interaction");
local Button = require("@src/API/UI/Button");
local Draggable = require("@src/API/UI/Draggable");
local HorizontalLayout = require("@src/API/UI/HorizontalLayout");
local ScrollBox = require("@src/API/UI/ScrollBox");
local TextUtil = require("@src/API/UI/TextUtil");
local VerticalLayout = require("@src/API/UI/VerticalLayout");

local WIDTH = 520;
local HEIGHT = 600;
local CONTENT_WIDTH = WIDTH - 32;
local ROW_HEIGHT = 48;
local SPACING = 6;
local LAYER = 1180i;
local TITLE_TEXT = "InteractionDialogueTitle";
local BODY_TEXT = "InteractionDialogueBody";
local OPTION_TEXT = "InteractionDialogueOption";
local SMALL_TEXT = "InteractionDialogueSmall";

local canvas = UI.GetCanvas("QuestLog", vec.New(0, 0), vec.New(1920, 1080), false);
local root = VerticalLayout.New(canvas, vec.New(-260, 0), vec.New(WIDTH, HEIGHT), LAYER, {
    panelTemplate = "DialogBox", padding = 16, spacing = 10, defaultChildOpts = { modeX = "flex" },
});
root:SetAnchor(vec.New(0.5, 0.5));
root:SetRelativePoint(vec.New(0.5, 0.5));

local header = HorizontalLayout.New(root, vec.New(0, 0), vec.New(CONTENT_WIDTH, 28), 1i, { panelTemplate = "DebugDarkGrey", alpha = 0.0, spacing = 8, alignV = "center" });
root:SetChildOpts(header, { modeX = "flex", sizeY = 28 });
local title = header:NewText("Quest Log", vec.New(0, 0), 1i, TITLE_TEXT);
local closeButton = Button.New(header, vec.New(0, 0), vec.New(26, 24), 1i, { text = "X", textTemplate = OPTION_TEXT });
header:SetChildOpts(title, { modeX = "flex", sizeY = 22 });
header:SetChildOpts(closeButton, { sizeX = 26, sizeY = 24 });
Draggable.Attach(root, header);

local scroll = ScrollBox.New(root, vec.New(0, 0), vec.New(CONTENT_WIDTH, HEIGHT - 110), 1i, {
    panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = CONTENT_WIDTH - 10, contentSizeY = 1,
    verticalScrollBar = true, scrollBarWidth = 10, autoScrollToStart = true, alpha = 0.0,
});
root:SetChildOpts(scroll, { modeX = "flex", sizeY = HEIGHT - 110 });
local content = VerticalLayout.New(scroll.content, vec.New(0, 0), vec.New(CONTENT_WIDTH - 10, 1), 1i, {
    panelTemplate = "DebugDarkGrey", alpha = 0.0, spacing = SPACING, defaultChildOpts = { modeX = "flex", sizeY = ROW_HEIGHT },
});

local footer = HorizontalLayout.New(root, vec.New(0, 0), vec.New(CONTENT_WIDTH, 28), 1i, { panelTemplate = "DebugDarkGrey", alpha = 0.0, spacing = 8, alignV = "center" });
root:SetChildOpts(footer, { modeX = "flex", sizeY = 28 });
local status = footer:NewText("", vec.New(0, 0), 1i, SMALL_TEXT);
local abandonButton = Button.New(footer, vec.New(0, 0), vec.New(112, 26), 1i, { text = "Abandon", textTemplate = OPTION_TEXT });
footer:SetChildOpts(status, { modeX = "flex", sizeY = 20 });
footer:SetChildOpts(abandonButton, { sizeX = 112, sizeY = 26 });

local widgets : { any } = { };
local selectedQuestID : integer? = nil;
local lastVersion = -1;

local function Clear()
    for _, widget in widgets do
        content:Remove(widget.root);
        UI.DestroyWidget(widget.root._raw);
    end
    widgets = { };
end

local function AddRow(text : string, enabled : boolean, onClick : (() -> ())?)
    local row = Button.New(content, vec.New(0, 0), vec.New(CONTENT_WIDTH - 10, ROW_HEIGHT), 1i, {
        text = TextUtil.TruncateToWidth(text, OPTION_TEXT, CONTENT_WIDTH - 34), textTemplate = OPTION_TEXT, textAlign = "left", textPadX = 12,
    });
    row:SetInteractable(enabled);
    if onClick ~= nil then row:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i then onClick(); end end); end
    table.insert(widgets, row);
end

local function Refresh()
    local version = InteractionAPI.GetVersion();
    if version == lastVersion then return; end

    local state = InteractionAPI.GetState();
    lastVersion = state.version;
    Clear();

    local selected = nil;
    for _, quest in state.questLog.quests do
        if quest.questID == selectedQuestID then selected = quest; break; end
    end
    if selected == nil and #state.questLog.quests > 0 then selected = state.questLog.quests[1]; selectedQuestID = selected.questID; end

    for _, quest in state.questLog.quests do
        local marker = if quest.questID == selectedQuestID then "> " else "";
        local ready = if quest.state == 1i then " (Complete)" else "";
        AddRow(marker .. quest.title .. ready, true, function() selectedQuestID = quest.questID; lastVersion = -1; Refresh(); end);
    end
    if selected ~= nil then
        AddRow(selected.logText, false, nil);
        for _, objective in selected.objectives do
            AddRow((if objective.optional then "Optional: " else "") .. objective.displayText, false, nil);
        end
    elseif #state.questLog.quests == 0 then
        AddRow("You have no active quests.", false, nil);
    end

    local height = math.max(1, #widgets * ROW_HEIGHT + math.max(0, #widgets - 1) * SPACING);
    content:SetHeight(height);
    scroll:SetContentHeight(height);
    abandonButton:SetInteractable(selected ~= nil);
end

closeButton:SetOnMouseUp(function() FrameManager.Close("QuestLog"); end);
abandonButton:SetOnMouseUp(function()
    if selectedQuestID ~= nil and InteractionAPI.AbandonQuest(selectedQuestID) then status:SetText("Abandoning quest..."); end
end);

FrameManager.Register("QuestLog", { root = root, role = "ManagedPanel", dismissible = true, onOpen = function() root:SetEnabled(true); lastVersion = -1; Refresh(); end, onClose = function() root:SetEnabled(false); end });
RegisterEvent(GameEvent.Updated, function() if FrameManager.IsOpen("QuestLog") then Refresh(); end end);
