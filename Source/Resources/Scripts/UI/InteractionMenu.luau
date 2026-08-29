local InteractionAPI = require("@src/API/Game/Interaction");
local ItemUtil = require("@src/API/Game/ItemUtil");
local GameTooltip = require("@src/API/Game/UI/GameTooltip");
local Button = require("@src/API/UI/Button");
local Draggable = require("@src/API/UI/Draggable");
local HorizontalLayout = require("@src/API/UI/HorizontalLayout");
local ScrollBox = require("@src/API/UI/ScrollBox");
local TextUtil = require("@src/API/UI/TextUtil");
local VerticalLayout = require("@src/API/UI/VerticalLayout");

local WINDOW_WIDTH = 350;
local WINDOW_HEIGHT = 550;
local VENDOR_WINDOW_WIDTH = 400;
local VENDOR_WINDOW_HEIGHT = 580;
local CONTENT_WIDTH = WINDOW_WIDTH - 32;
local SCROLL_CONTENT_WIDTH = CONTENT_WIDTH - 10;
local SCROLL_VIEW_HEIGHT = WINDOW_HEIGHT - 32 - 28 - 1 - 28 - 30;
local OPTION_HEIGHT = 48;
local OPTION_SPACING = 6;
local QUEST_SECTION_HEIGHT = 20;
local QUEST_OBJECTIVE_HEIGHT = 22;
local QUEST_REWARD_CARD_HEIGHT = 48;
local QUEST_REWARD_CARD_GAP = 6;
local QUEST_ACCEPT_OPTION_KIND = 1i;
local QUEST_COMPLETE_OPTION_KIND = 2i;
local LAYER = 1200i;
local BODY_TEXT = "InteractionDialogueBody";
local OPTION_TEXT = "InteractionDialogueOption";
local SMALL_TEXT = "InteractionDialogueSmall";
local SECTION_TEXT = "InteractionDialogueSection";
local TITLE_TEXT = "InteractionDialogueTitle";
local QUEST_ACTION_PANEL = "InteractionQuestActionPanel";
local QUEST_ACTION_HOVER_PANEL = "InteractionQuestActionHoverPanel";
local QUEST_ACTION_DOWN_PANEL = "InteractionQuestActionDownPanel";
local QUEST_ACTION_DISABLED_PANEL = "InteractionQuestActionDisabledPanel";
local VENDOR_COLUMNS = 2;
local VENDOR_ROWS = 6;
local VENDOR_SLOTS_PER_PAGE = VENDOR_COLUMNS * VENDOR_ROWS;
local VENDOR_CARD_WIDTH = 180;
local VENDOR_CARD_HEIGHT = 70;
local VENDOR_CARD_GAP = 5;
local VENDOR_ICON_SIZE = 34;
local EMPTY_SLOT_TEXTURE = "texture/ui/icons/itemslots/slot_empty.png";
local UNKNOWN_ITEM_TEXTURE = "texture/ui/icons/question.png";

local colors =
{
    panel = vec.New(0.055, 0.065, 0.075),
    panelHover = vec.New(0.085, 0.105, 0.12),
    border = vec.New(0.22, 0.28, 0.32),
    section = vec.New(0.48, 0.78, 0.96),
    questAction = vec.New(0.72, 0.54, 0.18),
    questActionHover = vec.New(0.86, 0.68, 0.28),
    questActionPressed = vec.New(0.56, 0.40, 0.11),
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

type QuestReward =
{
    groupID : integer,
    selectionID : integer,
    type : integer,
    resourceID : integer,
    amount : integer,
    amountNegative : boolean,
    selectionCount : integer,
    flags : integer,
    selected : boolean,
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
UI.RegisterTextTemplate(SECTION_TEXT, {
    font = "Data/Fonts/OpenSans-Regular.ttf",
    size = 8.5,
    color = colors.section,
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
UI.RegisterPanelTemplate(QUEST_ACTION_PANEL, {
    color = colors.questAction,
    alpha = 1.0,
    cornerRadius = 2.0,
    borderColor = colors.title,
    borderSize = 1.0,
    onClickTemplate = QUEST_ACTION_DOWN_PANEL,
    onHoverTemplate = QUEST_ACTION_HOVER_PANEL,
    onUninteractableTemplate = QUEST_ACTION_DISABLED_PANEL,
});
UI.RegisterPanelTemplate(QUEST_ACTION_HOVER_PANEL, {
    color = colors.questActionHover,
    alpha = 1.0,
});
UI.RegisterPanelTemplate(QUEST_ACTION_DOWN_PANEL, {
    color = colors.questActionPressed,
    alpha = 1.0,
});
UI.RegisterPanelTemplate(QUEST_ACTION_DISABLED_PANEL, {
    color = colors.questAction,
    alpha = 0.4,
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

local detailsLabel = contentLayout:NewText("DETAILS", vec.New(0, 0), 1i, SECTION_TEXT);
detailsLabel:SetInteractable(false);
detailsLabel:SetVisible(false);
contentLayout:SetChildOpts(detailsLabel, { modeX = "flex", sizeY = QUEST_SECTION_HEIGHT });

local greetingText = contentLayout:NewText("", vec.New(0, 0), 1i, BODY_TEXT);
greetingText:SetWrapWidth(SCROLL_CONTENT_WIDTH);
contentLayout:SetChildOpts(greetingText, { modeX = "flex", sizeY = 0 });

local vendorStatusText = contentScroll.root:NewText("", vec.New(0, 2), 2i, SMALL_TEXT);
vendorStatusText:SetAnchor(vec.New(0, 0));
vendorStatusText:SetRelativePoint(vec.New(0, 0));
vendorStatusText:SetColor(colors.muted);
vendorStatusText:SetInteractable(false);
vendorStatusText:SetVisible(false);

local footer = HorizontalLayout.New(root, vec.New(0, 0), vec.New(CONTENT_WIDTH, 28), 1i, {
    panelTemplate = "DebugDarkGrey",
    alpha = 0.0,
    spacing = 8,
    alignV = "center",
});
root:SetChildOpts(footer, { modeX = "flex", sizeY = 28 });

local statusText = footer:NewText("", vec.New(0, 0), 1i, SMALL_TEXT);
statusText:SetColor(colors.muted);
local previousPageButton = Button.New(footer, vec.New(0, 0), vec.New(32, 26), 1i, { text = "<", textTemplate = OPTION_TEXT });
local pagePanel = footer:NewPanel(vec.New(0, 0), vec.New(64, 26), 1i, "DebugDarkGrey");
pagePanel:SetAlpha(0);
pagePanel:SetBorderSize(0);
local pageText = pagePanel:NewText("", vec.New(0, 0), 1i, SMALL_TEXT);
pageText:SetAnchor(vec.New(0.5, 0.5));
pageText:SetRelativePoint(vec.New(0.5, 0.5));
pageText:SetInteractable(false);
local nextPageButton = Button.New(footer, vec.New(0, 0), vec.New(32, 26), 1i, { text = ">", textTemplate = OPTION_TEXT });
local questActionButton = Button.New(footer, vec.New(0, 0), vec.New(112, 26), 1i, {
    text = "Accept Quest", textTemplate = OPTION_TEXT, panelTemplate = QUEST_ACTION_PANEL,
});
questActionButton.text:SetColor(vec.New(1.0, 1.0, 1.0));
local questBackButton = Button.New(footer, vec.New(0, 0), vec.New(112, 26), 1i, { text = "Back", textTemplate = OPTION_TEXT });
local goodbyeButton = Button.New(footer, vec.New(0, 0), vec.New(112, 26), 1i, { text = "Goodbye", textTemplate = OPTION_TEXT });
footer:SetChildOpts(statusText, { modeX = "flex", sizeY = 20 });
footer:SetChildOpts(previousPageButton, { sizeX = 32, sizeY = 26 });
footer:SetChildOpts(pagePanel, { sizeX = 64, sizeY = 26 });
footer:SetChildOpts(nextPageButton, { sizeX = 32, sizeY = 26 });
footer:SetChildOpts(questActionButton, { sizeX = 112, sizeY = 26 });
footer:SetChildOpts(questBackButton, { sizeX = 112, sizeY = 26 });
footer:SetChildOpts(goodbyeButton, { sizeX = 112, sizeY = 26 });
footer:Move(questActionButton, 1);
footer:Move(questBackButton, 1);
previousPageButton:SetVisible(false);
pagePanel:SetVisible(false);
nextPageButton:SetVisible(false);
questActionButton:SetVisible(false);
questBackButton:SetVisible(false);

local surfaceLayoutIsVendor : boolean? = nil;
local currentStatusMessage = "";
local currentStatusColor = colors.muted;
local function SetStatus(message : string, color : vec)
    currentStatusMessage = message;
    currentStatusColor = color;
    statusText:SetText(if surfaceLayoutIsVendor == true then "" else message);
    statusText:SetColor(color);
    vendorStatusText:SetText(TextUtil.TruncateToWidth(message, SMALL_TEXT, VENDOR_CARD_WIDTH));
    vendorStatusText:SetColor(color);
end

local currentSession : InteractionAPI.Session? = nil;
local optionButtons : { any } = { };
local contentWidgets : { any } = { };
local stateVersion = -1;
local locallyDismissedSessionID : integer? = nil;
local selectionPending = false;
local vendorPage = 1;
local currentGreetingHeight = 0;
local vendorWindowPosition = vec.New(0, 15);
local conversationWindowPosition = vec.New(-515, 15);
local vendorTooltip = GameTooltip.New(canvas);

Draggable.Attach(root, header, {
    onMove = function(target)
        local position = vec.New(math.floor(target:GetPosX() + 0.5), math.floor(target:GetPosY() + 0.5));
        target:SetPos(position);
        if surfaceLayoutIsVendor == true then
            vendorWindowPosition = position;
        else
            conversationWindowPosition = position;
        end
    end,
});

local function SetSurfaceLayout(isVendor : boolean)
    if surfaceLayoutIsVendor == isVendor then
        root:SetPos(if isVendor then vendorWindowPosition else conversationWindowPosition);
        return;
    end

    local windowWidth = if isVendor then VENDOR_WINDOW_WIDTH else WINDOW_WIDTH;
    local windowHeight = if isVendor then VENDOR_WINDOW_HEIGHT else WINDOW_HEIGHT;
    local contentWidth = windowWidth - 32;
    local hasScrollBar = not isVendor;
    local scrollContentWidth = contentWidth - if hasScrollBar then contentScroll.scrollBarWidth else 0;
    local scrollViewHeight = windowHeight - 32 - 28 - 1 - 28 - 30;

    root:SetSize(vec.New(windowWidth, windowHeight));
    root:SetPos(if isVendor then vendorWindowPosition else conversationWindowPosition);
    surfaceLayoutIsVendor = isVendor;
    header:SetWidth(contentWidth);
    divider:SetWidth(contentWidth);
    footer:SetWidth(contentWidth);
    contentScroll.width = contentWidth;
    contentScroll.canHaveVerticalScrollBar = hasScrollBar;
    contentScroll.widthWithScrollBar = scrollContentWidth;
    contentScroll.height = scrollViewHeight;
    contentScroll.root:SetSize(vec.New(contentScroll.widthWithScrollBar, scrollViewHeight));
    contentScroll._viewport:SetSize(vec.New(contentScroll.widthWithScrollBar, scrollViewHeight));
    contentScroll.content:SetWidth(scrollContentWidth);
    contentLayout:SetWidth(scrollContentWidth);
    contentScroll.scrollBarBackground:SetHeight(scrollViewHeight);
    contentScroll.scrollBarBackground:SetVisible(hasScrollBar);
    root:SetChildOpts(contentScroll, { modeX = "flex", sizeY = scrollViewHeight });
    previousPageButton:SetVisible(isVendor);
    pagePanel:SetVisible(isVendor);
    nextPageButton:SetVisible(isVendor);
    statusText:SetText(if isVendor then "" else currentStatusMessage);
    statusText:SetColor(currentStatusColor);
    vendorStatusText:SetVisible(isVendor);
    root:Refresh();
end

local function GetScrollContentWidth() : number
    return contentScroll.content:GetWidth();
end

local function GetItemInfo(itemTemplateID : integer)
    return Item.GetItemInfo(itemTemplateID);
end

local function GetItemName(itemTemplateID : integer) : string
    local item = GetItemInfo(itemTemplateID);
    return if item ~= nil then item.Name else `Unknown item #{itemTemplateID}`;
end

local function GetItemTexture(itemTemplateID : integer) : string
    local item = GetItemInfo(itemTemplateID);
    if item == nil or item.IconID == 0i then
        return UNKNOWN_ITEM_TEXTURE;
    end

    local icon = Item.GetIconInfo(item.IconID);
    return if icon ~= nil then icon.Texture else UNKNOWN_ITEM_TEXTURE;
end

local SelectOption : (InteractionAPI.Option) -> () = function() end;

local function CreateVendorCost(parent : Panel, option : InteractionAPI.Option, position : vec, maximumWidth : number)
    local components = { };
    for _, component in option.costCurrencyComponents do
        table.insert(components, { kind = 0i, value = component });
    end

    for _, component in option.costItemComponents do
        table.insert(components, { kind = 1i, value = component });
    end

    if option.costID == 0i or not option.costDataAvailable or #components == 0 then
        local text = if option.costID == 0i then "Free" elseif option.costDataAvailable then "Incomplete price" else "Price unavailable";
        local costText = parent:NewText(text, position, 2i, SMALL_TEXT);
        costText:SetAnchor(vec.New(0, 1));
        costText:SetRelativePoint(vec.New(0, 1));
        costText:SetColor(colors.warning);
        costText:SetInteractable(false);
        return;
    end

    local x = position.x;
    local function AddText(text : string)
        local remainingWidth = math.max(0, position.x + maximumWidth - x);
        local displayedText = TextUtil.TruncateToWidth(text, SMALL_TEXT, remainingWidth);
        local widget = parent:NewText(displayedText, vec.New(x, position.y), 2i, SMALL_TEXT);
        widget:SetAnchor(vec.New(0, 1));
        widget:SetRelativePoint(vec.New(0, 1));
        widget:SetColor(colors.warning);
        widget:SetInteractable(false);
        x += UI.CalculateTextSize(displayedText, SMALL_TEXT).x;
    end

    for index, entry in components do
        if index > 1 then
            AddText(" + ");
        end

        if entry.kind == 0i then
            AddText(`{entry.value.amount} {entry.value.name}`);
        else
            AddText(`{entry.value.amount}x `);
            if x + 14 > position.x + maximumWidth then
                break;
            end

            local component = entry.value;
            local icon = parent:NewPanel(vec.New(x, position.y + 1), vec.New(14, 14), 3i, "DebugDarkGrey");
            icon:SetAnchor(vec.New(0, 1));
            icon:SetRelativePoint(vec.New(0, 1));
            icon:SetBackground(GetItemTexture(component.itemTemplateID));
            icon:SetColor(vec.New(1, 1, 1));
            icon:SetBorderSize(0);
            icon:SetOnHoverBegin(function()
                vendorTooltip.SetItem(component.itemTemplateID);
                vendorTooltip.SetPositionAroundCursor(UI.GetMousePos());
                vendorTooltip.Show();
            end);
            icon:SetOnHoverHeld(function()
                if not vendorTooltip.ContainsItem(component.itemTemplateID) then
                    vendorTooltip.SetItem(component.itemTemplateID);
                end

                vendorTooltip.SetPositionAroundCursor(UI.GetMousePos());
                if not vendorTooltip.IsShown() then
                    vendorTooltip.Show();
                end
            end);
            icon:SetOnHoverEnd(function()
                vendorTooltip.ClearLines();
                vendorTooltip.Hide();
            end);
            icon:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton == 0i then
                    SelectOption(option);
                end
            end);
            x += 14;
        end

        if x >= position.x + maximumWidth then
            break;
        end
    end
end

local function ClearOptions()
    vendorTooltip.ClearLines();
    vendorTooltip.Hide();
    for _, button in contentWidgets do
        contentLayout:Remove(button.root);
        UI.DestroyWidget(button.root._raw);
    end
    optionButtons = { };
    contentWidgets = { };
    questActionButton:SetVisible(false);
    questBackButton:SetVisible(false);
end

SelectOption = function(option : InteractionAPI.Option)
    if not option.enabled or selectionPending or currentSession == nil then
        return;
    end

    local sent = if currentSession.surfaceType == 1i then InteractionAPI.Purchase(currentSession.sessionID, currentSession.revision, option.token, 1i)
        elseif option.kind == QUEST_COMPLETE_OPTION_KIND then InteractionAPI.CompleteQuest(currentSession.sessionID, currentSession.revision, option.token)
        else InteractionAPI.Select(currentSession.sessionID, currentSession.revision, option.token);
    if sent then
        vendorTooltip.ClearLines();
        vendorTooltip.Hide();
        selectionPending = true;
        SetStatus("Waiting for a response...", colors.muted);
        for _, button in optionButtons do
            button:SetInteractable(false);
        end
    else
        SetStatus("Unable to send that response.", colors.warning);
    end
end

local function CreateVendorCard(parent : Panel, position : vec, option : InteractionAPI.Option?)
    local button = Button.New(parent, position, vec.New(VENDOR_CARD_WIDTH, VENDOR_CARD_HEIGHT), 1i, {
        text = "", panelTemplate = "DebugDarkGrey", color = vec.New(0.075, 0.09, 0.105), hoverColor = vec.New(0.10, 0.13, 0.15),
    });
    button:SetAnchor(vec.New(0, 1));
    button:SetRelativePoint(vec.New(0, 1));
    button.root:SetBorderColor(vec.New(0.22, 0.28, 0.32));
    button.root:SetBorderSize(1);
    button.root:SetClipChildren(true);

    local iconPanel = button.root:NewPanel(vec.New(6, -6), vec.New(VENDOR_ICON_SIZE, VENDOR_ICON_SIZE), 2i, "DebugDarkGrey");
    iconPanel:SetAnchor(vec.New(0, 1));
    iconPanel:SetRelativePoint(vec.New(0, 1));
    iconPanel:SetBackground(if option ~= nil then GetItemTexture(option.itemTemplateID) else EMPTY_SLOT_TEXTURE);
    iconPanel:SetColor(vec.New(1, 1, 1));
    iconPanel:SetBorderColor(colors.muted);
    iconPanel:SetBorderSize(1);
    iconPanel:SetInteractable(false);

    if option == nil then
        button:SetInteractable(false);
        return button;
    end

    local textX = 47;
    local textWidth = VENDOR_CARD_WIDTH - textX - 6;
    local itemInfo = GetItemInfo(option.itemTemplateID);
    local nameText = button.root:NewText(TextUtil.TruncateToWidth(GetItemName(option.itemTemplateID), OPTION_TEXT, textWidth), vec.New(textX, -4), 2i, OPTION_TEXT);
    nameText:SetAnchor(vec.New(0, 1));
    nameText:SetRelativePoint(vec.New(0, 1));
    if itemInfo ~= nil then
        nameText:SetColor(ItemUtil.GetRarityColor(itemInfo.Rarity));
    end

    nameText:SetInteractable(false);
    CreateVendorCost(button.root, option, vec.New(textX, -18), textWidth);
    local secondary = if not option.enabled then (if option.disabledReason ~= "" then option.disabledReason else "Unavailable") elseif integer.gt(option.quantity, 1i) then `Quantity {option.quantity}` else "";
    local secondaryText = button.root:NewText(TextUtil.TruncateToWidth(secondary, SMALL_TEXT, textWidth), vec.New(textX, -41), 2i, SMALL_TEXT);
    secondaryText:SetAnchor(vec.New(0, 1));
    secondaryText:SetRelativePoint(vec.New(0, 1));
    secondaryText:SetColor(if option.enabled then colors.muted else colors.warning);
    secondaryText:SetInteractable(false);

    local function PositionTooltip()
        vendorTooltip.SetPositionAroundCursor(UI.GetMousePos());
    end

    button:SetInteractable(true);
    button:SetOnHoverBegin(function()
        vendorTooltip.SetItem(option.itemTemplateID);
        PositionTooltip();
        vendorTooltip.Show();
    end);
    button:SetOnHoverHeld(function()
        if not vendorTooltip.ContainsItem(option.itemTemplateID) then
            vendorTooltip.SetItem(option.itemTemplateID);
        end

        PositionTooltip();
        if not vendorTooltip.IsShown() then
            vendorTooltip.Show();
        end
    end);
    button:SetOnHoverEnd(function()
        vendorTooltip.ClearLines();
        vendorTooltip.Hide();
    end);
    button:SetOnMouseUp(function(_, _, mouseButton)
        if mouseButton == 0i then
            SelectOption(option);
        end
    end);
    button.vendorOption = option;
    return button;
end

local function RefreshContentHeight(greetingHeight : number, optionCount : number)
    local childCount = (greetingHeight > 0 and 1 or 0) + optionCount;
    local contentHeight = greetingHeight + optionCount * OPTION_HEIGHT + math.max(0, childCount - 1) * OPTION_SPACING;
    contentLayout:SetHeight(math.max(1, contentHeight));
    contentScroll:SetContentHeight(math.max(1, contentHeight));
end

local function AddQuestContentWidget(widget, height : number)
    contentLayout:SetChildOpts(widget.root, { modeX = "flex", sizeY = height });
    table.insert(contentWidgets, widget);
end

local function CreateQuestSectionLabel(text : string)
    local label = contentLayout:NewText(text, vec.New(0, 0), 1i, SECTION_TEXT);
    label:SetInteractable(false);
    AddQuestContentWidget({ root = label }, QUEST_SECTION_HEIGHT);
end

local function CreateQuestSectionDivider()
    local sectionDivider = contentLayout:NewPanel(vec.New(0, 0), vec.New(GetScrollContentWidth(), 1), 1i, "DebugDarkGrey");
    sectionDivider:SetColor(colors.border);
    sectionDivider:SetBorderSize(0);
    AddQuestContentWidget({ root = sectionDivider }, 1);
end

local function GetRewardPresentation(reward : QuestReward) : (string, string, string, vec?)
    local signedAmount = (if reward.amountNegative then "-" else "") .. tostring(reward.amount);
    if reward.type == 0i then
        local item = GetItemInfo(reward.resourceID);
        local name = if item ~= nil then item.Name else `Unknown item #{reward.resourceID}`;
        local nameColor = if item ~= nil then ItemUtil.GetRarityColor(item.Rarity) else nil;
        return name, `x{signedAmount}`, "IT", nameColor;
    elseif reward.type == 1i then
        return `Currency #{reward.resourceID}`, signedAmount, "CU", nil;
    elseif reward.type == 2i then
        return `Reputation #{reward.resourceID}`, signedAmount, "REP", nil;
    end

    return "Experience", signedAmount, "XP", nil;
end

local function CreateQuestRewardCard(parent : Panel, position : vec, width : number, reward : QuestReward, allowSelection : boolean)
    local selectable = allowSelection and reward.selectionCount ~= 0i;
    local selected = selectable and reward.selected;
    local card = Button.New(parent, position, vec.New(width, QUEST_REWARD_CARD_HEIGHT), 1i, {
        text = "", panelTemplate = "DebugDarkGrey", color = colors.panel, hoverColor = if selectable then colors.panelHover else colors.panel,
        highlightColor = vec.New(0.15, 0.135, 0.055), borderColor = if selected then colors.title else colors.border, borderSize = 1,
    });
    card:SetAnchor(vec.New(0, 1));
    card:SetRelativePoint(vec.New(0, 1));
    card:SetInteractable(selectable or reward.type == 0i);
    if not selectable and reward.type == 0i then card:SetVisualEnabled(false); end
    card:SetHighlighted(selected);

    local name, amount, badge, nameColor = GetRewardPresentation(reward);
    local iconSize = 30;
    local icon = card.root:NewPanel(vec.New(6, -9), vec.New(iconSize, iconSize), 2i, "DebugDarkGrey");
    icon:SetAnchor(vec.New(0, 1));
    icon:SetRelativePoint(vec.New(0, 1));
    icon:SetColor(vec.New(0.075, 0.09, 0.105));
    icon:SetBorderColor(colors.border);
    icon:SetBorderSize(1);
    icon:SetInteractable(false);
    if reward.type == 0i then
        icon:SetBackground(GetItemTexture(reward.resourceID));
        icon:SetColor(vec.New(1, 1, 1));
    else
        local badgeText = icon:NewText(badge, vec.New(0, 0), 1i, SMALL_TEXT);
        badgeText:SetAnchor(vec.New(0.5, 0.5));
        badgeText:SetRelativePoint(vec.New(0.5, 0.5));
        badgeText:SetInteractable(false);
    end

    local textX = 42;
    local choiceWidth = if selectable then 14 else 0;
    local textWidth = width - textX - 6 - choiceWidth;
    local nameText = card.root:NewText(TextUtil.TruncateToWidth(name, OPTION_TEXT, textWidth), vec.New(textX, -7), 2i, OPTION_TEXT);
    nameText:SetAnchor(vec.New(0, 1));
    nameText:SetRelativePoint(vec.New(0, 1));
    nameText:SetColor(nameColor or vec.New(0.92, 0.92, 0.92));
    nameText:SetInteractable(false);
    local amountText = card.root:NewText(amount, vec.New(textX, -25), 2i, SMALL_TEXT);
    amountText:SetAnchor(vec.New(0, 1));
    amountText:SetRelativePoint(vec.New(0, 1));
    amountText:SetColor(colors.muted);
    amountText:SetInteractable(false);

    if selectable then
        local choiceText = card.root:NewText(if selected then "[x]" else "[ ]", vec.New(-6, -6), 2i, SMALL_TEXT);
        choiceText:SetAnchor(vec.New(1, 1));
        choiceText:SetRelativePoint(vec.New(1, 1));
        choiceText:SetColor(if selected then colors.title else colors.muted);
        choiceText:SetInteractable(false);
    end

    if reward.type == 0i then
        local function PositionTooltip()
            vendorTooltip.SetPositionAroundCursor(UI.GetMousePos());
        end
        card:SetOnHoverBegin(function()
            vendorTooltip.SetItem(reward.resourceID);
            PositionTooltip();
            vendorTooltip.Show();
        end);
        card:SetOnHoverHeld(function()
            if not vendorTooltip.ContainsItem(reward.resourceID) then vendorTooltip.SetItem(reward.resourceID); end
            PositionTooltip();
            if not vendorTooltip.IsShown() then vendorTooltip.Show(); end
        end);
        card:SetOnHoverEnd(function()
            vendorTooltip.ClearLines();
            vendorTooltip.Hide();
        end);
    end

    card:SetOnMouseUp(function(_, _, mouseButton)
        if mouseButton == 0i and selectable then InteractionAPI.ToggleReward(reward.selectionID); end
    end);
end

local function PopulateQuestContent(session : InteractionAPI.Session, options : { InteractionAPI.Option }, greetingHeight : number)
    local option = options[1];
    local isOffer = option ~= nil and option.kind == QUEST_ACCEPT_OPTION_KIND;
    local isComplete = option ~= nil and option.kind == QUEST_COMPLETE_OPTION_KIND;
    local isInProgress = not isOffer and not isComplete;
    local showObjectives = not isComplete;
    local showRewards = not isInProgress;
    local hasDetails = greetingHeight > 0;
    local contentHeight = greetingHeight + (if hasDetails then QUEST_SECTION_HEIGHT + OPTION_SPACING else 0);
    local contentCount = if hasDetails then 2 else 0;
    detailsLabel:SetText(if isOffer then "DETAILS" elseif isComplete then "COMPLETION" else "PROGRESS");
    local function Account(height : number)
        if contentCount > 0 then contentHeight += OPTION_SPACING; end
        contentHeight += height;
        contentCount += 1;
    end

    if showObjectives and #session.objectives > 0 then
        if hasDetails then
            CreateQuestSectionDivider();
            Account(1);
        end
        CreateQuestSectionLabel("OBJECTIVES");
        Account(QUEST_SECTION_HEIGHT);
        local objectivesHeight = #session.objectives * QUEST_OBJECTIVE_HEIGHT;
        local objectivesPanel = contentLayout:NewPanel(vec.New(0, 0), vec.New(GetScrollContentWidth(), objectivesHeight), 1i, "DebugDarkGrey");
        objectivesPanel:SetAlpha(0);
        objectivesPanel:SetBorderSize(0);
        for index, objective in session.objectives do
            local label = (if objective.optional then "Optional: " else "") .. objective.displayText;
            local objectiveText = objectivesPanel:NewText(TextUtil.TruncateToWidth("-  " .. label, OPTION_TEXT, GetScrollContentWidth()), vec.New(0, -(index - 1) * QUEST_OBJECTIVE_HEIGHT), 1i, OPTION_TEXT);
            objectiveText:SetAnchor(vec.New(0, 1));
            objectiveText:SetRelativePoint(vec.New(0, 1));
            objectiveText:SetColor(if objective.optional then colors.muted else colors.title);
            objectiveText:SetInteractable(false);
        end
        AddQuestContentWidget({ root = objectivesPanel }, objectivesHeight);
        Account(objectivesHeight);
    end

    local rewardGroups = { };
    local rewardGroupOrder = { };
    if showRewards then
        for _, reward in session.rewards do
            if integer.band(reward.flags, 1i) == 0i then
                local group = rewardGroups[reward.groupID];
                if group == nil then
                    group = { selectionCount = reward.selectionCount, rewards = { } };
                    rewardGroups[reward.groupID] = group;
                    table.insert(rewardGroupOrder, group);
                end
                table.insert(group.rewards, reward);
            end
        end
    end

    if #rewardGroupOrder > 0 then
        if hasDetails or (showObjectives and #session.objectives > 0) then
            CreateQuestSectionDivider();
            Account(1);
        end
        CreateQuestSectionLabel("REWARDS");
        Account(QUEST_SECTION_HEIGHT);
        local cardWidth = math.floor((GetScrollContentWidth() - QUEST_REWARD_CARD_GAP) / 2);
        local allowsRewardSelection = isComplete;
        for groupIndex, group in rewardGroupOrder do
            local rowCount = math.ceil(#group.rewards / 2);
            local groupHeight = 22 + rowCount * QUEST_REWARD_CARD_HEIGHT + math.max(0, rowCount - 1) * QUEST_REWARD_CARD_GAP;
            local groupPanel = contentLayout:NewPanel(vec.New(0, 0), vec.New(GetScrollContentWidth(), groupHeight), 1i, "DebugDarkGrey");
            groupPanel:SetAlpha(0);
            groupPanel:SetBorderSize(0);
            local groupTitle = if group.selectionCount == 0i then "Guaranteed Rewards" elseif allowsRewardSelection then "Choose Rewards" else "Choice Rewards";
            local rule = if group.selectionCount == 0i then "You receive all" elseif allowsRewardSelection then `Choose {group.selectionCount} of {#group.rewards}` else `Choose {group.selectionCount} when completing`;
            local titleText = groupPanel:NewText(groupTitle, vec.New(0, 0), 1i, OPTION_TEXT);
            titleText:SetAnchor(vec.New(0, 1));
            titleText:SetRelativePoint(vec.New(0, 1));
            titleText:SetInteractable(false);
            local ruleText = groupPanel:NewText(rule, vec.New(0, 0), 1i, SMALL_TEXT);
            ruleText:SetAnchor(vec.New(1, 1));
            ruleText:SetRelativePoint(vec.New(1, 1));
            ruleText:SetColor(colors.muted);
            ruleText:SetInteractable(false);
            for rewardIndex, reward in group.rewards do
                local row = math.floor((rewardIndex - 1) / 2);
                local column = (rewardIndex - 1) % 2;
                CreateQuestRewardCard(groupPanel, vec.New(column * (cardWidth + QUEST_REWARD_CARD_GAP), -22 - row * (QUEST_REWARD_CARD_HEIGHT + QUEST_REWARD_CARD_GAP)), cardWidth, reward, allowsRewardSelection);
            end
            AddQuestContentWidget({ root = groupPanel }, groupHeight);
            Account(groupHeight);
            if groupIndex < #rewardGroupOrder then
                local groupGap = 4;
                local spacer = contentLayout:NewPanel(vec.New(0, 0), vec.New(GetScrollContentWidth(), groupGap), 1i, "DebugDarkGrey");
                spacer:SetAlpha(0);
                spacer:SetBorderSize(0);
                AddQuestContentWidget({ root = spacer }, groupGap);
                Account(groupGap);
            end
        end
    end

    if option ~= nil then
        local actionButton = if isInProgress then questBackButton else questActionButton;
        actionButton:SetText(if isOffer then "Accept Quest" elseif isComplete then "Complete Quest" else "Back");
        actionButton:SetInteractable(option.enabled);
        actionButton:SetVisible(true);
        actionButton:SetOnMouseUp(function(_, _, mouseButton)
            if mouseButton == 0i then SelectOption(option); end
        end);
        table.insert(optionButtons, actionButton);
    end

    contentLayout:SetHeight(math.max(1, contentHeight));
    contentScroll:SetContentHeight(math.max(1, contentHeight));
    contentScroll:ScrollToStart();
end

local function PopulateOptions(options : { InteractionAPI.Option }, greetingHeight : number)
    ClearOptions();

    if currentSession ~= nil and currentSession.surfaceType == 1i then
        local pageCount = math.max(1, math.ceil(#options / VENDOR_SLOTS_PER_PAGE));
        vendorPage = math.clamp(vendorPage, 1, pageCount);
        pageText:SetText(`Page {vendorPage} / {pageCount}`);
        previousPageButton:SetInteractable(vendorPage > 1);
        nextPageButton:SetInteractable(vendorPage < pageCount);

        local gridWidth = VENDOR_COLUMNS * VENDOR_CARD_WIDTH + (VENDOR_COLUMNS - 1) * VENDOR_CARD_GAP;
        local gridHeight = VENDOR_ROWS * VENDOR_CARD_HEIGHT + (VENDOR_ROWS - 1) * VENDOR_CARD_GAP;
        local grid = contentLayout:NewPanel(vec.New(0, 0), vec.New(gridWidth, gridHeight), 1i, "DebugDarkGrey");
        grid:SetAlpha(0);
        contentLayout:SetChildOpts(grid, { modeX = "fixed", sizeX = gridWidth, sizeY = gridHeight, alignH = "center" });
        table.insert(contentWidgets, { root = grid });

        local firstOption = (vendorPage - 1) * VENDOR_SLOTS_PER_PAGE + 1;
        for slotIndex = 1, VENDOR_SLOTS_PER_PAGE do
            local row = math.floor((slotIndex - 1) / VENDOR_COLUMNS);
            local column = (slotIndex - 1) % VENDOR_COLUMNS;
            local option = options[firstOption + slotIndex - 1];
            local button = CreateVendorCard(grid, vec.New(column * (VENDOR_CARD_WIDTH + VENDOR_CARD_GAP), -row * (VENDOR_CARD_HEIGHT + VENDOR_CARD_GAP)), option);
            if option ~= nil then
                table.insert(optionButtons, button);
            end
        end

        local contentHeight = greetingHeight + gridHeight + (if greetingHeight > 0 then OPTION_SPACING else 0);
        contentLayout:SetHeight(math.max(1, contentHeight));
        contentScroll:SetContentHeight(math.max(1, contentHeight));
        contentScroll:ScrollToStart();
        return;
    end

    if currentSession ~= nil and currentSession.surfaceType == 2i then
        PopulateQuestContent(currentSession, options, greetingHeight);
        return;
    end

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
            if mouseButton == 0i then
                SelectOption(option);
            end
        end);
        table.insert(optionButtons, button);
        table.insert(contentWidgets, button);
    end

    RefreshContentHeight(greetingHeight, #contentWidgets);
    contentScroll:ScrollToStart();
end

previousPageButton:SetOnMouseUp(function()
    if currentSession == nil or currentSession.surfaceType ~= 1i or vendorPage <= 1 then
        return;
    end

    vendorPage -= 1;
    PopulateOptions(currentSession.options, currentGreetingHeight);
end);

nextPageButton:SetOnMouseUp(function()
    if currentSession == nil or currentSession.surfaceType ~= 1i then
        return;
    end

    local pageCount = math.max(1, math.ceil(#currentSession.options / VENDOR_SLOTS_PER_PAGE));
    if vendorPage >= pageCount then
        return;
    end

    vendorPage += 1;
    PopulateOptions(currentSession.options, currentGreetingHeight);
end);

local function Hide(sendClose : boolean)
    if sendClose and currentSession ~= nil then
        if not InteractionAPI.Close(currentSession.sessionID) then
            SetStatus("Unable to close the conversation.", colors.warning);
            return;
        end
        locallyDismissedSessionID = currentSession.sessionID;
    end
    vendorTooltip.ClearLines();
    vendorTooltip.Hide();
    root:SetVisible(false);
end

closeIconButton:SetOnMouseUp(function()
    Hide(true);
end);
goodbyeButton:SetOnMouseUp(function()
    Hide(true);
end);

local function ApplyState()
    if InteractionAPI.GetVersion() == stateVersion then
        return;
    end

    local state = InteractionAPI.GetState();
    stateVersion = state.version;
    selectionPending = false;

    local session = state.activeSession;
    if session == nil then
        currentSession = nil;
        ClearOptions();
        root:SetVisible(false);
        return;
    end
    if locallyDismissedSessionID == session.sessionID then
        return;
    end

    local isNewRevision = currentSession == nil or currentSession.sessionID ~= session.sessionID or currentSession.revision ~= session.revision;
    currentSession = session;
    local isVendor = session.surfaceType == 1i;
    local isQuest = session.surfaceType == 2i;
    local questOption = if isQuest then session.options[1] else nil;
    local isQuestOffer = questOption ~= nil and questOption.kind == QUEST_ACCEPT_OPTION_KIND;
    SetSurfaceLayout(isVendor);
    goodbyeButton:SetText(if isQuestOffer then "Decline" else "Goodbye");
    if isNewRevision then
        vendorPage = 1;
    end

    local sourceName = if session.sourceUnitID == INVALID_UNIT_ID then "" else Unit.GetName(session.sourceUnitID);
    local surfaceTitle = if isVendor and sourceName ~= "" then sourceName elseif session.heading ~= "" then session.heading elseif sourceName ~= "" then sourceName elseif isVendor then "Vendor" else "Conversation";
    sourceText:SetText(TextUtil.TruncateToWidth(surfaceTitle, TITLE_TEXT, root:GetWidth() - 66));
    local scrollContentWidth = GetScrollContentWidth();
    local wrappedGreeting, greetingSize = UI.WrapText(session.greeting, BODY_TEXT, scrollContentWidth);
    local greetingHeight = if session.greeting == "" then 0 else math.max(16, greetingSize.y);
    currentGreetingHeight = greetingHeight;
    detailsLabel:SetVisible(isQuest and session.greeting ~= "");
    greetingText:SetWrapWidth(scrollContentWidth);
    greetingText:SetText(wrappedGreeting);
    greetingText:SetVisible(session.greeting ~= "");
    contentLayout:SetChildOpts(greetingText, { modeX = "flex", sizeY = greetingHeight });
    PopulateOptions(session.options, greetingHeight);
    if isNewRevision then
        SetStatus("", colors.muted);
        locallyDismissedSessionID = nil;
    end

    if state.lastResult ~= nil and state.lastResult.sessionID == session.sessionID and state.lastResult.revision == session.revision then
        SetStatus(resultNames[integer.tonumber(state.lastResult.result)] or "The server rejected that response.", if state.lastResult.result == 4i then colors.success else colors.warning);
        for index, button in optionButtons do
            local option = button.vendorOption or session.options[index];
            button:SetInteractable(isVendor or option.enabled);
        end
    end

    root:SetVisible(true);
    root:Refresh();
    root.root:ForceRefresh();
end

RegisterEvent(GameEvent.Updated, function()
    ApplyState();
end);

ApplyState();
