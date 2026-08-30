local Button = require("@src/API/UI/Button");
local CreaturePicker = require("@src/API/Editor/UI/CreaturePicker");
local CurrencyPicker = require("@src/API/Editor/UI/CurrencyPicker");
local Declarative = require("@src/API/UI/Declarative");
local Draggable = require("@src/API/UI/Draggable");
local Dropdown = require("@src/API/UI/Dropdown");
local EditorRegistry = require("@src/API/Editor/Registry");
local GameTooltip = require("@src/API/Game/UI/GameTooltip");
local InputBox = require("@src/API/UI/InputBox");
local InputField = require("@src/API/Editor/UI/InputField");
local FactionPicker = require("@src/API/Editor/UI/FactionPicker");
local ItemPicker = require("@src/API/Editor/UI/ItemPicker");
local ItemUtil = require("@src/API/Game/ItemUtil");
local Modal = require("@src/API/UI/Modal");
local QuestController = require("@src/API/Editor/QuestController");
local QuestModel = require("@src/API/Editor/QuestModel");
local QuestPicker = require("@src/API/Editor/UI/QuestPicker");
local SearchPicker = require("@src/API/UI/SearchPicker");
local ScrollBox = require("@src/API/UI/ScrollBox");
local TextUtil = require("@src/API/UI/TextUtil");
local VirtualList = require("@src/API/UI/VirtualList");

local STATUS = QuestController.StatusLevel;
local QUEST_FLAG_SHOW_WHEN_UNAVAILABLE = 1i;
local OBJECTIVE_FLAG_OPTIONAL = 1i;
local OBJECTIVE_FLAG_DO_NOT_CONSUME = 2i;
local REWARD_FLAG_HIDDEN = 1i;
local ASSOCIATION_FLAG_CLOSE_ON_ACCEPT = 1i;
local ROLE_START = 1i;
local ROLE_TURN_IN = 2i;
local MAX_INTERNAL_NAME_LENGTH = 128;
local MAX_PREREQUISITES = 32;
local MAX_OBJECTIVES = 16;
local MAX_OBJECTIVE_TARGETS = 32;
local MAX_REWARD_GROUPS = 8;
local MAX_REWARDS_PER_GROUP = 16;
local MAX_SELECTED_REWARDS = 64;
local UNKNOWN_ITEM_ICON = "texture/ui/icons/question.png";
local SECTION_ACTION_WIDTH = 112;
local SECTION_ACTION_HEIGHT = 22;
local CENTER_CONTENT_SPACING = 4;
local CENTER_CONTENT_TOP_PADDING = 12;
local CENTER_CONTENT_BOTTOM_CLEARANCE = 72;
local INSPECTOR_FOOTER_HEIGHT = 42;
local QUEST_TEXT_KINDS =
{
    QuestTextTitle = { label = "Title", field = "titleTextID" },
    QuestTextLog = { label = "Quest Log", field = "logTextID" },
    QuestTextSummary = { label = "Summary", field = "summaryTextID" },
    QuestTextDetails = { label = "Details", field = "detailsTextID" },
    QuestTextProgress = { label = "Progress", field = "progressTextID" },
    QuestTextCompletion = { label = "Completion", field = "completionTextID" },
};

if QuestEditorView ~= nil then
    QuestEditorView:Destroy();
end
if QuestEditorLocalizedTextModalView ~= nil then
    QuestEditorLocalizedTextModalView:Destroy();
end

local editor : any =
{
    geometry = { windowPosition = vec.New(206, 86), windowSize = vec.New(1500, 830), toolbarHeight = 52, sidebarWidth = 235, inspectorWidth = 330 },
    colors =
    {
        muted = vec.New(0.55, 0.61, 0.68), accent = vec.New(0.38, 0.76, 0.98), warning = vec.New(0.96, 0.72, 0.24), success = vec.New(0.39, 0.80, 0.49),
        error = vec.New(0.94, 0.38, 0.38), selected = vec.New(0.12, 0.28, 0.39), hover = vec.New(0.10, 0.16, 0.21), card = vec.New(0.065, 0.078, 0.088), border = vec.New(0.22, 0.29, 0.34), danger = vec.New(0.62, 0.20, 0.23),
    },
    state =
    {
        controller = QuestController.New(), model = nil, snapshot = nil, visible = false, search = "", selectedQuestID = nil, selectedKind = "Quest", selectedRecordID = nil,
        inspectorTab = "Content", centerView = nil, inspectorView = nil, applyOperations = nil, applyIndex = 0, applyIDMap = { }, applyingQuestID = nil, pendingSnapshot = false,
        objectiveRows = { }, rewardRows = { }, orderedDrag = nil, collapsedSections = { },
        localizedText = {
            root = nil, dialog = nil, title = nil, internalNameInput = nil, englishInput = nil, translatorContext = "",
            translationRows = { }, translationInputs = { }, baselineTranslations = { }, baselineText = nil,
            statusText = nil, saveButton = nil, cancelButton = nil, closeButton = nil,
            editingID = nil, activeTextID = nil, targetKind = nil, targetID = nil, targetField = nil,
            pendingRequestID = 0i, awaitingCreate = false, operations = { }, operationIndex = 0, isOpen = false,
        },
    },
    refs = { },
};

local bodyHeight = editor.geometry.windowSize.y - editor.geometry.toolbarHeight;
local centerWidth = editor.geometry.windowSize.x - editor.geometry.sidebarWidth - editor.geometry.inspectorWidth;
local centerContentWidth = centerWidth - 38;
local inspectorContentWidth = editor.geometry.inspectorWidth - 28;
local halfInspectorWidth = (inspectorContentWidth - 8) / 2;
local inspectorViewportHeight = bodyHeight - INSPECTOR_FOOTER_HEIGHT;
editor.state.canvas = UI.GetCanvas("QuestEditor", vec.New(0, 0), vec.New(1920, 1080), false);
editor.state.snapshot = editor.state.controller:GetSnapshot();

local deleteModal = QuestEditorDeleteModal;
if deleteModal == nil then
    deleteModal = Modal.New(editor.state.canvas, vec.New(0, 0), vec.New(470, 180), 1900i, { name = "QuestEditorDelete", title = "Delete quest data", message = "", confirmText = "Delete", cancelText = "Cancel" });
    QuestEditorDeleteModal = deleteModal;
end

local closeModal = QuestEditorCloseModal;
if closeModal == nil then
    closeModal = Modal.New(editor.state.canvas, vec.New(0, 0), vec.New(480, 180), 1900i, { name = "QuestEditorClose", title = "Unsaved quest changes", message = "", confirmText = "Close", cancelText = "Keep Editing" });
    QuestEditorCloseModal = closeModal;
end

local itemPicker = QuestEditorItemPicker;
if itemPicker == nil then
    itemPicker = ItemPicker.New(editor.state.canvas, 1900i, { name = "QuestEditorItemPicker", title = "Select Item Target" });
    QuestEditorItemPicker = itemPicker;
end

local currencyPicker = QuestEditorCurrencyPicker;
if currencyPicker == nil then
    currencyPicker = CurrencyPicker.New(editor.state.canvas, 1900i, { name = "QuestEditorCurrencyPicker" });
    QuestEditorCurrencyPicker = currencyPicker;
end

local factionPicker = QuestEditorFactionPicker;
if factionPicker == nil then
    factionPicker = FactionPicker.New(editor.state.canvas, 1900i, { name = "QuestEditorFactionPicker" });
    QuestEditorFactionPicker = factionPicker;
end

local creaturePicker = QuestEditorCreaturePicker;
if creaturePicker == nil then
    creaturePicker = CreaturePicker.New(editor.state.canvas, 1900i, { name = "QuestEditorCreaturePicker", width = 620, height = 640 });
    QuestEditorCreaturePicker = creaturePicker;
end

local questPicker = QuestEditorQuestPicker;
if questPicker == nil then
    questPicker = QuestPicker.New(editor.state.canvas, 1900i, { name = "QuestEditorQuestPicker", width = 620, height = 640 });
    QuestEditorQuestPicker = questPicker;
end

local ApplySnapshot : () -> () = function() end;
local RefreshAll : (boolean?) -> () = function(_) end;
local RefreshCenter : () -> () = function() end;
local RefreshInspector : () -> () = function() end;
local RefreshSidebar : (boolean?) -> () = function(_) end;
local SelectQuest : (integer?, boolean?) -> () = function(_, _) end;
local UpdateToolbar : () -> () = function() end;
local OpenLocalizedTextModal : (integer?, string, integer, string) -> () = function(_, _, _, _) end;
local OpenObjectiveTargetPicker : (any, any?) -> () = function(_, _) end;
local view : any;
local editorTooltip : any = nil;

local function IsTemporaryID(id : integer) : boolean
    return integer.lt(id, 0i);
end

local function HasFlag(value : integer, flag : integer) : boolean
    return math.floor(integer.tonumber(value) / integer.tonumber(flag)) % 2 == 1;
end

local function WithFlag(value : integer, flag : integer, enabled : boolean) : integer
    local present = HasFlag(value, flag);
    if enabled == present then
        return value;
    end

    return if enabled then integer.add(value, flag) else integer.sub(value, flag);
end

local function ReadInteger(input : any, minimum : number, maximum : number) : integer?
    local value = tonumber(input:GetText());
    if value == nil or value < minimum or value > maximum or math.floor(value) ~= value then
        return nil;
    end

    return integer.create(value);
end

local function FindByID(collection : { any }, id : integer?) : any?
    if id == nil then
        return nil;
    end

    for _, value in collection do
        if value.id == id then
            return value;
        end
    end

    return nil;
end

local function FindText(textID : integer?) : any?
    return FindByID(editor.state.snapshot.texts, textID);
end

local function TextName(textID : integer?) : string
    if textID == nil or textID == 0i then
        return "None";
    end

    local text = FindText(textID);
    return if text ~= nil then text.internalName else `Unknown text #{textID}`;
end

local function QuestDisplayName(quest : any) : string
    local title = FindText(quest.titleTextID);
    return if title ~= nil and title.englishValue ~= "" then title.englishValue else quest.internalName;
end

local function LocalizedTextPreview(textID : integer?, fallback : string) : string
    local text = FindText(textID);
    return if text ~= nil and text.englishValue ~= "" then text.englishValue else fallback;
end

local function FindCreature(creatureTemplateID : integer?) : any?
    return FindByID(editor.state.snapshot.creatureTemplates, creatureTemplateID);
end

local function CreatureName(creatureTemplateID : integer?) : string
    local creature = FindCreature(creatureTemplateID);
    return if creature ~= nil then creature.name else `Unknown creature #{creatureTemplateID}`;
end

local function ItemName(itemTemplateID : integer?) : string
    local item = FindByID(editor.state.snapshot.itemTemplates, itemTemplateID);
    return if item ~= nil then item.name else `Unknown item #{itemTemplateID}`;
end

local function ItemIconTexture(itemTemplateID : integer?) : string
    local item = FindByID(editor.state.snapshot.itemTemplates, itemTemplateID);
    if item == nil or item.iconID == 0i then
        return UNKNOWN_ITEM_ICON;
    end

    local icon = Item.GetIconInfo(item.iconID);
    return if icon ~= nil then icon.Texture else UNKNOWN_ITEM_ICON;
end

local function AttachItemTooltip(widget : any, itemTemplateID : integer)
    local function PositionTooltip()
        editorTooltip.SetPositionAroundCursor(UI.GetMousePos());
    end

    widget:SetOnHoverBegin(function()
        editorTooltip.SetItem(itemTemplateID);
        PositionTooltip();
        editorTooltip.Show();
    end);
    widget:SetOnHoverHeld(function()
        if not editorTooltip.ContainsItem(itemTemplateID) then
            editorTooltip.SetItem(itemTemplateID);
        end

        PositionTooltip();
        if not editorTooltip.IsShown() then
            editorTooltip.Show();
        end
    end);
    widget:SetOnHoverEnd(function()
        editorTooltip.ClearLines();
        editorTooltip.Hide();
    end);
end

local function CurrencyName(currencyID : integer?) : string
    local currency = FindByID(editor.state.snapshot.currencies, currencyID);
    return if currency ~= nil then currency.name else `Unknown currency #{currencyID}`;
end

local function FactionName(factionID : integer?) : string
    local faction = FindByID(editor.state.snapshot.factions, factionID);
    return if faction ~= nil then faction.name else `Unknown faction #{factionID}`;
end

local function ObjectiveTypeName(objectiveType : integer) : string
    for _, option in editor.state.snapshot.questObjectiveTypes do
        if option.value == objectiveType then
            return option.name;
        end
    end

    return `Type {objectiveType}`;
end

local function UniqueQuestName(baseName : string) : string
    local used : { [string] : boolean } = { };
    if editor.state.model ~= nil then
        for _, questID in editor.state.model:GetQuestIDs() do
            used[string.lower(editor.state.model:GetQuest(questID).internalName)] = true;
        end
    end

    if not used[string.lower(baseName)] then
        return baseName;
    end

    local suffix = 2;
    while used[string.lower(`{baseName} {suffix}`)] do
        suffix += 1;
    end

    return `{baseName} {suffix}`;
end

local function GetTextPickerItems(includeNone : boolean?) : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { };
    if includeNone ~= false then
        table.insert(items, { value = 0i, primary = "None", secondary = "No localized text" });
    end

    for _, text in editor.state.snapshot.texts do
        if text.id ~= 0i then
            table.insert(items, { value = text.id, primary = text.internalName, secondary = text.englishValue, search = `{text.id} {text.internalName} {text.englishValue}` });
        end
    end

    return items;
end

local function GetConditionPickerItems() : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { { value = 0i, primary = "Always", secondary = "No condition set" } };
    for _, condition in editor.state.snapshot.conditionSets do
        if condition.id ~= 0i then
            table.insert(items, { value = condition.id, primary = condition.internalName, secondary = `Condition set #{condition.id}`, search = `{condition.id} {condition.internalName}` });
        end
    end

    return items;
end

local function GetQuestPickerItems(excludedQuestID : integer?, used : { [integer] : boolean }?) : { QuestPicker.Item }
    local items : { QuestPicker.Item } = { };
    if editor.state.model == nil then
        return items;
    end

    for _, questID in editor.state.model:GetQuestIDs() do
        local quest = editor.state.model:GetQuest(questID);
        if questID ~= excludedQuestID and not (used and used[questID]) then
            table.insert(items, { value = questID, primary = quest.internalName, secondary = if IsTemporaryID(questID) then "Unsaved quest" else `Quest #{questID}`, search = `{questID} {quest.internalName}` });
        end
    end

    return items;
end

local function GetCreaturePickerItems(used : { [integer] : boolean }?, requireInteractionProvider : boolean?) : { CreaturePicker.Item }
    local items : { CreaturePicker.Item } = { };
    local providerCreatureIDs : { [integer] : boolean } = { };
    if requireInteractionProvider then
        for _, provider in editor.state.snapshot.creatureTemplateInteractions do
            providerCreatureIDs[provider.creatureTemplateID] = true;
        end
    end

    for _, creature in editor.state.snapshot.creatureTemplates do
        local hasProvider = not requireInteractionProvider or providerCreatureIDs[creature.id] == true;
        if hasProvider and not (used and used[creature.id]) then
            table.insert(items, { value = creature.id, primary = creature.name, secondary = `Creature template #{creature.id}`, search = `{creature.id} {creature.name}` });
        end
    end

    return items;
end

local function GetItemPickerItems(used : { [integer] : boolean }?) : { any }
    local items : { any } = { };
    for _, item in editor.state.snapshot.itemTemplates do
        if not (used and used[item.id]) then
            table.insert(items, { value = item.id, primary = item.name, secondary = `Item template #{item.id}`, search = `{item.id} {item.name}`, icon = ItemIconTexture(item.id), rarity = item.rarity, trailing = tostring(item.id) });
        end
    end

    return items;
end

local function GetCurrencyPickerItems() : { CurrencyPicker.Item }
    local items : { CurrencyPicker.Item } = { };
    for _, currency in editor.state.snapshot.currencies do
        table.insert(items, { value = currency.id, primary = currency.name, secondary = `Currency #{currency.id}`, search = `{currency.id} {currency.name}` });
    end

    return items;
end

local function GetFactionPickerItems() : { FactionPicker.Item }
    local items : { FactionPicker.Item } = { };
    for _, faction in editor.state.snapshot.factions do
        table.insert(items, { value = faction.id, primary = faction.name, secondary = `Faction #{faction.id}`, search = `{faction.id} {faction.name}` });
    end

    return items;
end

local function GetObjectiveTypePickerItems() : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { };
    for _, option in editor.state.snapshot.questObjectiveTypes do
        if option.value ~= 0i then
            table.insert(items, { value = option.value, primary = option.name, secondary = "Quest objective type" });
        end
    end

    return items;
end

local function GetObjectiveTargetPickerItems(objective : any, currentTargetID : integer?) : { any }
    local used : { [integer] : boolean } = { };
    for _, target in editor.state.model:GetTargets(objective.id) do
        if target.id ~= currentTargetID then
            used[target.targetID] = true;
        end
    end

    return if objective.objectiveType == 2i then GetItemPickerItems(used) else GetCreaturePickerItems(used);
end

OpenObjectiveTargetPicker = function(objective : any, currentTarget : any?)
    editorTooltip.ClearLines();
    editorTooltip.Hide();
    local objectiveID = objective.id;
    local questID = objective.questID;
    local pickerItems = GetObjectiveTargetPickerItems(objective, if currentTarget ~= nil then currentTarget.id else nil);
    local function SelectTarget(targetID : integer)
        if editor.state.model ~= nil and FindByID(editor.state.model:GetObjectives(questID), objectiveID) ~= nil then
            if currentTarget ~= nil then
                editor.state.model:SetTargetFields(currentTarget.id, { targetID = targetID });
            else
                editor.state.model:AddTarget(objectiveID, targetID);
            end
            RefreshAll(true);
        end
    end

    if objective.objectiveType == 2i then
        itemPicker:Open({ title = if currentTarget ~= nil then "Replace Item Target" else "Add Item Target", items = pickerItems, onSelect = SelectTarget });
        return;
    end

    creaturePicker:Open({ title = if currentTarget ~= nil then "Replace Creature Target" else "Add Creature Target", items = pickerItems, onSelect = SelectTarget });
end

local function SelectRecord(kind : string, recordID : integer?)
    editor.state.selectedKind = kind;
    editor.state.selectedRecordID = recordID;
    RefreshCenter();
    RefreshInspector();
end

local function IsQuestTextKind(kind : string) : boolean
    return QUEST_TEXT_KINDS[kind] ~= nil;
end

local function GetSelectedQuest() : any?
    return if editor.state.model ~= nil and editor.state.selectedQuestID ~= nil then editor.state.model:GetQuest(editor.state.selectedQuestID) else nil;
end

local function GetSelectedRecord() : any?
    local quest = GetSelectedQuest();
    if quest == nil or editor.state.selectedKind == "Quest" then
        return quest;
    end
    if IsQuestTextKind(editor.state.selectedKind) then return nil; end

    local recordID = editor.state.selectedRecordID;
    if recordID == nil then
        return nil;
    end

    if editor.state.selectedKind == "Prerequisite" then
        for _, value in editor.state.model:GetPrerequisites(quest.id) do
            if value.id == recordID then
                return value;
            end
        end
    elseif editor.state.selectedKind == "Association" then
        for _, value in editor.state.model:GetAssociations(quest.id) do
            if value.id == recordID then
                return value;
            end
        end
    elseif editor.state.selectedKind == "Objective" then
        for _, value in editor.state.model:GetObjectives(quest.id) do
            if value.id == recordID then
                return value;
            end
        end
    elseif editor.state.selectedKind == "RewardGroup" then
        for _, value in editor.state.model:GetRewardGroups(quest.id) do
            if value.id == recordID then
                return value;
            end
        end
    end

    return nil;
end

local function GetApplyValidationError(questID : integer) : string?
    if editor.state.model == nil then
        return "Quest data is unavailable.";
    end

    local quest = editor.state.model:GetQuest(questID);
    if quest == nil then
        return "Select a quest to apply.";
    end
    if quest.internalName == "" or #quest.internalName > MAX_INTERNAL_NAME_LENGTH then
        return `The quest internal name must contain between 1 and {MAX_INTERNAL_NAME_LENGTH} characters.`;
    end
    if integer.lt(quest.level, 0i) or integer.gt(quest.level, 65535i) then
        return "Quest level must be between 0 and 65535.";
    end
    if quest.flags ~= 0i and quest.flags ~= QUEST_FLAG_SHOW_WHEN_UNAVAILABLE then
        return "The quest uses unsupported flags.";
    end
    for _, textID in { quest.titleTextID, quest.summaryTextID, quest.detailsTextID, quest.logTextID, quest.progressTextID, quest.completionTextID } do
        if textID == 0i or FindText(textID) == nil then
            return "Title, summary, details, log, progress, and completion text are all required.";
        end
    end
    if quest.unavailableReasonTextID ~= 0i and not HasFlag(quest.flags, QUEST_FLAG_SHOW_WHEN_UNAVAILABLE) then
        return "An unavailable reason requires Show When Unavailable.";
    end

    local seenPrerequisites : { [integer] : boolean } = { };
    local prerequisites = editor.state.model:GetPrerequisites(questID);
    if #prerequisites > MAX_PREREQUISITES then
        return `A quest cannot have more than {MAX_PREREQUISITES} prerequisites.`;
    end
    for _, prerequisite in prerequisites do
        if prerequisite.prerequisiteQuestID == questID then
            return "A quest cannot require itself.";
        end
        if editor.state.model:GetQuest(prerequisite.prerequisiteQuestID) == nil then
            return `Prerequisite #{prerequisite.id} references an unknown quest.`;
        end
        if seenPrerequisites[prerequisite.prerequisiteQuestID] then
            return "The same prerequisite quest is listed more than once.";
        end

        seenPrerequisites[prerequisite.prerequisiteQuestID] = true;
    end

    local function HasPrerequisitePath(fromQuestID : integer, targetQuestID : integer, visited : { [integer] : boolean }) : boolean
        if fromQuestID == targetQuestID then
            return true;
        end
        if visited[fromQuestID] then
            return false;
        end

        visited[fromQuestID] = true;
        for _, prerequisite in editor.state.model:GetPrerequisites(fromQuestID) do
            if HasPrerequisitePath(prerequisite.prerequisiteQuestID, targetQuestID, visited) then
                return true;
            end
        end

        return false;
    end
    for _, prerequisite in prerequisites do
        if HasPrerequisitePath(prerequisite.prerequisiteQuestID, questID, { }) then
            return "Quest prerequisites cannot contain a cycle.";
        end
    end

    local objectives = editor.state.model:GetObjectives(questID);
    if #objectives > MAX_OBJECTIVES then
        return `A quest cannot have more than {MAX_OBJECTIVES} objectives.`;
    end

    for _, objective in objectives do
        local authoritativeObjective = FindByID(editor.state.snapshot.questObjectives, objective.id);
        if authoritativeObjective ~= nil and authoritativeObjective.objectiveType ~= objective.objectiveType then
            return "Changing an existing objective type needs an atomic objective-and-target mutation, which the current server editor API does not expose yet.";
        end
        if objective.objectiveType ~= 1i and objective.objectiveType ~= 2i then
            return `Objective #{objective.id} has an unsupported type.`;
        end
        if objective.requiredCount == 0i then
            return `Objective #{objective.id} must require at least one contribution.`;
        end
        if integer.lt(objective.flags, 0i) or integer.gt(objective.flags, 3i) then
            return `Objective #{objective.id} uses unsupported flags.`;
        end
        if objective.objectiveType ~= 2i and HasFlag(objective.flags, OBJECTIVE_FLAG_DO_NOT_CONSUME) then
            return `Objective #{objective.id} can only use Do Not Consume for collected items.`;
        end

        local targets = editor.state.model:GetTargets(objective.id);
        if #targets == 0 then
            return `Objective #{objective.id} must have at least one target.`;
        end
        if #targets > MAX_OBJECTIVE_TARGETS then
            return `Objective #{objective.id} cannot have more than {MAX_OBJECTIVE_TARGETS} targets.`;
        end
        if objective.textID ~= 0i and FindText(objective.textID) == nil then
            return `Objective #{objective.id} references missing localized objective text.`;
        end
        if objective.textID == 0i and #targets ~= 1 then
            return `Objective #{objective.id} requires localized text when it has multiple targets.`;
        end

        local seenTargets : { [integer] : boolean } = { };
        for _, target in targets do
            if target.targetID == 0i or target.contribution == 0i then
                return `Every target in objective #{objective.id} needs a resource and positive contribution.`;
            end
            local targetExists = if objective.objectiveType == 2i then FindByID(editor.state.snapshot.itemTemplates, target.targetID) ~= nil else FindCreature(target.targetID) ~= nil;
            if not targetExists then
                return `Objective target #{target.id} references an unknown resource.`;
            end
            if seenTargets[target.targetID] then
                return `Objective #{objective.id} contains the same target more than once.`;
            end

            seenTargets[target.targetID] = true;
        end
    end

    local totalRewards = 0;
    local totalSelections = 0;
    local rewardGroups = editor.state.model:GetRewardGroups(questID);
    if #rewardGroups > MAX_REWARD_GROUPS then
        return `A quest cannot have more than {MAX_REWARD_GROUPS} reward groups.`;
    end
    for _, group in rewardGroups do
        local rewards = editor.state.model:GetRewards(group.id);
        if #rewards == 0 then
            return `Reward group #{group.id} must contain at least one reward.`;
        end
        if integer.lt(group.selectionCount, 0i) or integer.gt(group.selectionCount, 16i) or integer.tonumber(group.selectionCount) > #rewards then
            return `Reward group #{group.id} has an invalid selection count.`;
        end
        if #rewards > MAX_REWARDS_PER_GROUP then
            return `Reward group #{group.id} cannot contain more than {MAX_REWARDS_PER_GROUP} rewards.`;
        end
        if group.flags ~= 0i then
            return `Reward group #{group.id} uses unsupported flags.`;
        end

        totalRewards += #rewards;
        totalSelections += integer.tonumber(group.selectionCount);
        for _, reward in rewards do
            local value = reward.value;
            if value.flags ~= 0i and value.flags ~= REWARD_FLAG_HIDDEN then
                return `Reward #{value.id} uses unsupported flags.`;
            end
            if group.selectionCount ~= 0i and HasFlag(value.flags, REWARD_FLAG_HIDDEN) then
                return `Selectable reward group #{group.id} cannot contain hidden rewards.`;
            end
            if reward.kind == "Item" and (value.itemTemplateID == 0i or value.quantity == 0i) then
                return `Item reward #{value.id} needs an item and positive quantity.`;
            elseif reward.kind == "Currency" and (value.currencyID == 0i or value.amount == 0i) then
                return `Currency reward #{value.id} needs a currency and positive amount.`;
            elseif reward.kind == "Reputation" and (value.factionID == 0i or value.amount == 0i) then
                return `Reputation reward #{value.id} needs a faction and non-zero amount.`;
            elseif reward.kind == "Experience" and value.amount == 0i then
                return `Experience reward #{value.id} needs a positive amount.`;
            end
        end
    end
    if totalSelections > MAX_SELECTED_REWARDS then
        return `Quest completion cannot select more than {MAX_SELECTED_REWARDS} rewards.`;
    end
    if totalRewards > MAX_SELECTED_REWARDS then
        return `A quest cannot contain more than {MAX_SELECTED_REWARDS} rewards.`;
    end

    local seenCreatures : { [integer] : boolean } = { };
    for _, association in editor.state.model:GetAssociations(questID) do
        if association.creatureTemplateID == 0i or integer.lt(association.roleFlags, 1i) or integer.gt(association.roleFlags, 3i) then
            return `Quest-giver association #{association.id} is invalid.`;
        end
        if association.flags ~= 0i and association.flags ~= ASSOCIATION_FLAG_CLOSE_ON_ACCEPT then
            return `Quest-giver association #{association.id} uses unsupported flags.`;
        end
        if seenCreatures[association.creatureTemplateID] then
            return "The same creature is associated with this quest more than once.";
        end

        seenCreatures[association.creatureTemplateID] = true;
    end

    return nil;
end

local function ResolveAppliedID(id : integer) : integer
    return if IsTemporaryID(id) then (editor.state.applyIDMap[id] or 0i) else id;
end

local function GenerateDisplayedPriorities(questID : integer)
    local objectives = editor.state.model:GetObjectives(questID);
    for objectiveIndex, objective in objectives do
        editor.state.model:SetObjectiveFields(objective.id, { priority = integer.create(#objectives - objectiveIndex) });

        local targets = editor.state.model:GetTargets(objective.id);
        for targetIndex, target in targets do
            editor.state.model:SetTargetFields(target.id, { priority = integer.create(#targets - targetIndex) });
        end
    end
end

local function SubmitOperation(operation : any)
    local value = operation.value;
    local requestID = 0i;
    if operation.kind == "CreateQuest" then
        requestID = InteractionEditor.CreateQuestTemplate(value.internalName, value.titleTextID, value.summaryTextID, value.detailsTextID, value.logTextID, value.progressTextID, value.completionTextID, value.visibilityConditionSetID, value.availabilityConditionSetID, value.unavailableReasonTextID, value.level, value.flags);
    elseif operation.kind == "UpdateQuest" then
        requestID = InteractionEditor.UpdateQuestTemplate(value.id, value.internalName, value.titleTextID, value.summaryTextID, value.detailsTextID, value.logTextID, value.progressTextID, value.completionTextID, value.visibilityConditionSetID, value.availabilityConditionSetID, value.unavailableReasonTextID, value.level, value.flags);
    elseif operation.kind == "CreatePrerequisite" then
        requestID = InteractionEditor.CreateQuestPrerequisite(ResolveAppliedID(value.questID), ResolveAppliedID(value.prerequisiteQuestID));
    elseif operation.kind == "UpdatePrerequisite" then
        requestID = InteractionEditor.UpdateQuestPrerequisite(value.id, ResolveAppliedID(value.questID), ResolveAppliedID(value.prerequisiteQuestID));
    elseif operation.kind == "DeletePrerequisite" then
        requestID = InteractionEditor.DeleteQuestPrerequisite(value.id);
    elseif operation.kind == "CreateQuestGiver" then
        requestID = InteractionEditor.CreateCreatureTemplateQuestGiver(value.creatureTemplateID, value.flags);
    elseif operation.kind == "DeleteQuestGiver" then
        requestID = InteractionEditor.DeleteCreatureTemplateQuestGiver(value.creatureTemplateID);
    elseif operation.kind == "DeleteProvider" then
        requestID = InteractionEditor.DeleteCreatureTemplateInteraction(value.creatureTemplateID);
    elseif operation.kind == "CreateAssociation" then
        requestID = InteractionEditor.CreateCreatureTemplateQuest(value.creatureTemplateID, ResolveAppliedID(value.questID), value.roleFlags, value.priority, value.visibilityConditionSetID, value.enabledConditionSetID, value.flags);
    elseif operation.kind == "UpdateAssociation" then
        requestID = InteractionEditor.UpdateCreatureTemplateQuest(value.id, value.creatureTemplateID, ResolveAppliedID(value.questID), value.roleFlags, value.priority, value.visibilityConditionSetID, value.enabledConditionSetID, value.flags);
    elseif operation.kind == "DeleteAssociation" then
        requestID = InteractionEditor.DeleteCreatureTemplateQuest(value.id);
    elseif operation.kind == "CreateObjective" then
        local targets = { };
        for _, target in operation.targets do
            table.insert(targets, { priority = target.priority, targetID = target.targetID, contribution = target.contribution });
        end
        requestID = InteractionEditor.CreateQuestObjective(ResolveAppliedID(value.questID), value.priority, value.objectiveType, value.requiredCount, value.textID, value.flags, targets);
    elseif operation.kind == "UpdateObjective" then
        requestID = InteractionEditor.UpdateQuestObjective(value.id, ResolveAppliedID(value.questID), value.priority, value.objectiveType, value.requiredCount, value.textID, value.flags);
    elseif operation.kind == "DeleteObjective" then
        requestID = InteractionEditor.DeleteQuestObjective(value.id);
    elseif operation.kind == "CreateTarget" then
        requestID = InteractionEditor.CreateQuestObjectiveTarget(ResolveAppliedID(value.objectiveID), value.priority, value.targetID, value.contribution);
    elseif operation.kind == "UpdateTarget" then
        requestID = InteractionEditor.UpdateQuestObjectiveTarget(value.id, ResolveAppliedID(value.objectiveID), value.priority, value.targetID, value.contribution);
    elseif operation.kind == "DeleteTarget" then
        requestID = InteractionEditor.DeleteQuestObjectiveTarget(value.id);
    elseif operation.kind == "CreateRewardGroup" then
        local items = { };
        local currencies = { };
        local reputations = { };
        local experience = { };
        for _, reward in operation.rewards do
            local rewardValue = reward.value;
            if reward.kind == "Item" then
                table.insert(items, { priority = rewardValue.priority, itemTemplateID = rewardValue.itemTemplateID, quantity = rewardValue.quantity, flags = rewardValue.flags });
            elseif reward.kind == "Currency" then
                table.insert(currencies, { priority = rewardValue.priority, currencyID = rewardValue.currencyID, amount = rewardValue.amount, flags = rewardValue.flags });
            elseif reward.kind == "Reputation" then
                table.insert(reputations, { priority = rewardValue.priority, factionID = rewardValue.factionID, amount = rewardValue.amount, flags = rewardValue.flags });
            else
                table.insert(experience, { priority = rewardValue.priority, amount = rewardValue.amount, flags = rewardValue.flags });
            end
        end
        requestID = InteractionEditor.CreateQuestRewardGroup(ResolveAppliedID(value.questID), value.priority, value.selectionCount, value.flags, items, currencies, reputations, experience);
    elseif operation.kind == "UpdateRewardGroup" then
        requestID = InteractionEditor.UpdateQuestRewardGroup(value.id, ResolveAppliedID(value.questID), value.priority, value.selectionCount, value.flags);
    elseif operation.kind == "DeleteRewardGroup" then
        requestID = InteractionEditor.DeleteQuestRewardGroup(value.id);
    elseif operation.kind == "CreateRewardItem" then
        requestID = InteractionEditor.CreateQuestRewardItem(ResolveAppliedID(value.rewardGroupID), value.priority, value.itemTemplateID, value.quantity, value.flags);
    elseif operation.kind == "UpdateRewardItem" then
        requestID = InteractionEditor.UpdateQuestRewardItem(value.id, ResolveAppliedID(value.rewardGroupID), value.priority, value.itemTemplateID, value.quantity, value.flags);
    elseif operation.kind == "DeleteRewardItem" then
        requestID = InteractionEditor.DeleteQuestRewardItem(value.id);
    elseif operation.kind == "CreateRewardCurrency" then
        requestID = InteractionEditor.CreateQuestRewardCurrency(ResolveAppliedID(value.rewardGroupID), value.priority, value.currencyID, value.amount, value.flags);
    elseif operation.kind == "UpdateRewardCurrency" then
        requestID = InteractionEditor.UpdateQuestRewardCurrency(value.id, ResolveAppliedID(value.rewardGroupID), value.priority, value.currencyID, value.amount, value.flags);
    elseif operation.kind == "DeleteRewardCurrency" then
        requestID = InteractionEditor.DeleteQuestRewardCurrency(value.id);
    elseif operation.kind == "CreateRewardReputation" then
        requestID = InteractionEditor.CreateQuestRewardReputation(ResolveAppliedID(value.rewardGroupID), value.priority, value.factionID, value.amount, value.flags);
    elseif operation.kind == "UpdateRewardReputation" then
        requestID = InteractionEditor.UpdateQuestRewardReputation(value.id, ResolveAppliedID(value.rewardGroupID), value.priority, value.factionID, value.amount, value.flags);
    elseif operation.kind == "DeleteRewardReputation" then
        requestID = InteractionEditor.DeleteQuestRewardReputation(value.id);
    elseif operation.kind == "CreateRewardExperience" then
        requestID = InteractionEditor.CreateQuestRewardExperience(ResolveAppliedID(value.rewardGroupID), value.priority, value.amount, value.flags);
    elseif operation.kind == "UpdateRewardExperience" then
        requestID = InteractionEditor.UpdateQuestRewardExperience(value.id, ResolveAppliedID(value.rewardGroupID), value.priority, value.amount, value.flags);
    elseif operation.kind == "DeleteRewardExperience" then
        requestID = InteractionEditor.DeleteQuestRewardExperience(value.id);
    end

    editor.state.controller:Submit(requestID);
    if requestID == 0i then
        editor.state.applyOperations = nil;
        editor.state.applyIndex = 0;
        editor.state.applyIDMap = { };
        editor.state.applyingQuestID = nil;
        UpdateToolbar();
    end
end

local function BeginApply()
    local questID = editor.state.selectedQuestID;
    if editor.state.model == nil or questID == nil or not editor.state.controller:IsReady() then
        return;
    end

    local validationError = GetApplyValidationError(questID);
    if validationError ~= nil then
        editor.state.controller:SetStatus(validationError, STATUS.Warning);
        UpdateToolbar();
        return;
    end

    GenerateDisplayedPriorities(questID);
    local operations = editor.state.model:GetOperations(questID);
    if #operations == 0 then
        return;
    end

    editor.state.applyOperations = operations;
    editor.state.applyIndex = 1;
    editor.state.applyIDMap = { };
    editor.state.applyingQuestID = questID;
    editor.state.pendingSnapshot = false;
    UpdateToolbar();
    SubmitOperation(operations[1]);
end

local function ContinueApply(result : any)
    local operations = editor.state.applyOperations;
    if operations == nil then
        return;
    end
    if not result.succeeded then
        editor.state.applyOperations = nil;
        editor.state.applyIndex = 0;
        editor.state.applyIDMap = { };
        editor.state.applyingQuestID = nil;
        UpdateToolbar();
        return;
    end

    local completed = operations[editor.state.applyIndex];
    if completed.temporaryID ~= nil then
        editor.state.applyIDMap[completed.temporaryID] = result.artifactID;
        local resolveKind = string.sub(completed.kind, 7);
        editor.state.model:ResolveTemporaryID(resolveKind, completed.temporaryID, result.artifactID);
    end

    editor.state.applyIndex += 1;
    local nextOperation = operations[editor.state.applyIndex];
    if nextOperation ~= nil then
        SubmitOperation(nextOperation);
        UpdateToolbar();
        return;
    end

    local selectedQuestID = editor.state.applyingQuestID;
    if selectedQuestID ~= nil and IsTemporaryID(selectedQuestID) then
        selectedQuestID = editor.state.applyIDMap[selectedQuestID];
    end

    editor.state.applyOperations = nil;
    editor.state.applyIndex = 0;
    editor.state.applyIDMap = { };
    editor.state.applyingQuestID = nil;
    editor.state.selectedQuestID = selectedQuestID;
    editor.state.controller:SelectQuest(selectedQuestID);
    editor.state.selectedKind = "Quest";
    editor.state.selectedRecordID = nil;
    editor.state.pendingSnapshot = false;
    editor.state.model = nil;
    editor.state.controller:RequestSnapshot();
    UpdateToolbar();
end

local function MakeSectionHeader(section : string, title : string, action : any?)
    local collapsed = editor.state.collapsedSections[section] == true;
    local children =
    {
        Declarative.Button(`{if collapsed then "[+]" else "[-]"}  {title}`, { grow = 1, height = SECTION_ACTION_HEIGHT, fontSize = 10, textAlign = "left", textPadX = 0, panelTemplate = "DebugDarkGrey", alpha = 0, hoverAlpha = 0.45, borderSize = 0, setup = function(button)
            button.text:SetColor(editor.colors.accent);
        end, onClick = function()
            editor.state.collapsedSections[section] = not collapsed;
            RefreshCenter();
        end }),
    };
    if action ~= nil then
        table.insert(children, action);
    end

    return Declarative.Horizontal({ width = centerContentWidth, height = SECTION_ACTION_HEIGHT, spacing = 6, alignV = "center", template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, table.unpack(children));
end

local function MakeEmptySectionText(text : string)
    return Declarative.Text(text, { width = centerContentWidth, height = 18, fontSize = 8, color = editor.colors.muted, template = "DefaultButtonText" });
end

local function MakeSectionDivider()
    return Declarative.Panel({ width = centerContentWidth, height = 9, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Panel({ position = vec.New(0, 4), size = vec.New(centerContentWidth, 1), template = "DebugDarkGrey", color = editor.colors.border, alpha = 1, borderSize = 0 })
    );
end

local function MeasureVerticalContent(nodes : { any }, spacing : number, paddingTop : number, paddingBottom : number) : number
    local height = paddingTop + paddingBottom + math.max(0, #nodes - 1) * spacing;
    for _, node in nodes do
        local options = node.options or node;
        height += options.height or 0;
    end

    return height;
end

local function MakeAddPicker(id : string, width : number, placeholder : string, items : { SearchPicker.Item }, callback : (integer) -> (), compact : boolean?)
    local height = if compact then SECTION_ACTION_HEIGHT else 28;
    return Declarative.Component({ id = id, width = width, height = height, factory = function(parent, position, size, layer)
        local picker = SearchPicker.New(parent, editor.state.canvas, position, size, layer, { items = items, placeholder = placeholder, popupWidth = 380, popupHeight = 390, rowHeight = 40, onValueChanged = function(value)
            callback(value);
        end });
        if compact then picker.button:SetFontSize(8); end
        return picker;
    end });
end

local function MoveOrderedKey(order : { any }, activeKey : any, targetIndex : number) : { any }
    local result = { };
    for _, key in order do
        if key ~= activeKey then table.insert(result, key); end
    end
    table.insert(result, math.clamp(targetIndex, 1, #result + 1), activeKey);
    return result;
end

local function ResolveWidgetRoot(widget : any) : any
    if type(widget) == "table" then
        local root = rawget(widget, "root");
        if root ~= nil then return root; end
    end
    return widget;
end

local function BeginOrderedDrag(rows : { any }, activeKey : any, surface : any, commit : ({ any }) -> (), createDragSurface : (any, vec) -> any)
    if not editor.state.controller:IsReady() then return; end

    local order = { };
    local slots = { };
    local rowByKey = { };
    for index, row in rows do
        table.insert(order, row.key);
        table.insert(slots, row.surface:GetPos());
        row.worldY = row.surface:GetWorldPosY();
        rowByKey[row.key] = row;
    end

    local size = vec.New(surface:GetWidth(), surface:GetHeight());
    local sourceRoot = ResolveWidgetRoot(surface);
    local placeholderOverlay = sourceRoot:NewPanel(vec.New(0, 0), size, 100i, "DebugDarkGrey");
    placeholderOverlay:SetColor(vec.New(0.015, 0.018, 0.022));
    placeholderOverlay:SetAlpha(0.68);
    placeholderOverlay:SetBorderColor(editor.colors.border);
    placeholderOverlay:SetBorderSize(1);
    placeholderOverlay:SetInteractable(false);

    local startWorld = surface:GetWorldPos();
    local hostWorld = editor.refs.dragHost:GetWorldPos();
    local dragSurface = createDragSurface(editor.refs.dragHost, size);
    dragSurface:SetAnchor(vec.New(0, 0));
    dragSurface:SetRelativePoint(vec.New(0, 0));
    ResolveWidgetRoot(dragSurface):SetInteractable(false);
    dragSurface:SetPos(startWorld - hostWorld);

    editor.state.orderedDrag = {
        activeKey = activeKey, originalOrder = order, previewOrder = order, targetIndex = table.find(order, activeKey) or 1,
        rows = rows, rowByKey = rowByKey, slots = slots, startWorld = startWorld, hostWorld = hostWorld, surface = surface,
        dragSurface = dragSurface, placeholderOverlay = placeholderOverlay, commit = commit,
    };
end

local function PreviewOrderedDrag(mousePosition : vec, delta : vec)
    local drag = editor.state.orderedDrag;
    if drag == nil then return; end

    local targetIndex = 1;
    for _, key in drag.originalOrder do
        if key ~= drag.activeKey then
            local row = drag.rowByKey[key];
            if mousePosition.y < row.worldY + row.surface:GetHeight() / 2 then targetIndex += 1; end
        end
    end
    targetIndex = math.clamp(targetIndex, 1, #drag.originalOrder);

    local desiredWorld = drag.startWorld + delta;
    if targetIndex ~= drag.targetIndex then
        drag.targetIndex = targetIndex;
        drag.previewOrder = MoveOrderedKey(drag.originalOrder, drag.activeKey, targetIndex);
        for _, row in drag.rows do
            local previewIndex = table.find(drag.previewOrder, row.key);
            row.surface:SetPos(drag.slots[previewIndex]);
        end
    end
    drag.dragSurface:SetPos(desiredWorld - drag.hostWorld);
end

local function CommitOrderedDrag()
    local drag = editor.state.orderedDrag;
    if drag == nil then return; end
    editor.state.orderedDrag = nil;
    UI.DestroyWidget(ResolveWidgetRoot(drag.dragSurface));
    UI.DestroyWidget(drag.placeholderOverlay);

    local originalIndex = table.find(drag.originalOrder, drag.activeKey) or drag.targetIndex;
    if drag.targetIndex ~= originalIndex then
        drag.commit(drag.previewOrder);
    else
        for index, key in drag.originalOrder do drag.rowByKey[key].surface:SetPos(drag.slots[index]); end
    end
end

local function CreateDocumentRowSurface(parent : any, position : vec, size : vec, layer : integer, leading : string, title : string, detail : string, trailing : string, accent : vec?, selected : boolean)
    local button = Button.New(parent, position, size, layer, { text = "", panelTemplate = "DebugDarkGrey", color = editor.colors.card, hoverColor = editor.colors.hover, highlightColor = editor.colors.selected, borderColor = if selected then editor.colors.accent else editor.colors.border, borderSize = 1, clipChildren = true });
    button:SetHighlighted(selected);

    local leadingText = button:NewText(leading, vec.New(7, 0), 1i, "DefaultButtonText");
    leadingText:SetAnchor(vec.New(0, 0.5));
    leadingText:SetRelativePoint(vec.New(0, 0.5));
    leadingText:SetFontSize(8);
    leadingText:SetColor(accent or editor.colors.muted);

    local primaryText = button:NewText(title, vec.New(96, 7), 1i, "DefaultButtonText");
    primaryText:SetAnchor(vec.New(0, 0.5));
    primaryText:SetRelativePoint(vec.New(0, 0.5));
    primaryText:SetFontSize(9);

    local secondaryText = button:NewText(detail, vec.New(96, -7), 1i, "DefaultButtonText");
    secondaryText:SetAnchor(vec.New(0, 0.5));
    secondaryText:SetRelativePoint(vec.New(0, 0.5));
    secondaryText:SetFontSize(7);
    secondaryText:SetColor(editor.colors.muted);

    local trailingText = button:NewText(trailing, vec.New(-7, 0), 1i, "DefaultButtonText");
    trailingText:SetAnchor(vec.New(1, 0.5));
    trailingText:SetRelativePoint(vec.New(1, 0.5));
    trailingText:SetFontSize(8);
    trailingText:SetColor(editor.colors.muted);
    return button;
end

local function MakeDocumentRow(kind : string, recordID : integer, leading : string, title : string, detail : string, trailing : string, accent : vec?)
    local selected = editor.state.selectedKind == kind and editor.state.selectedRecordID == recordID;
    return Declarative.Component({ width = centerContentWidth, height = 46, factory = function(parent, position, size, layer)
        local button = CreateDocumentRowSurface(parent, position, size, layer, leading, title, detail, trailing, accent, selected);
        button:SetOnMouseUp(function(_, _, mouseButton)
            if mouseButton ~= 0i then return; end
            if button.suppressClick then button.suppressClick = false; return; end
            SelectRecord(kind, recordID);
        end);
        if kind == "Objective" then
            table.insert(editor.state.objectiveRows, { key = recordID, surface = button });
            Draggable.Attach(button, button, {
                button = 0i, moveTarget = false, threshold = 4,
                onBegin = function(_, mousePosition)
                    button.suppressClick = true;
                    local quest = GetSelectedQuest();
                    if quest == nil then return; end
                    BeginOrderedDrag(editor.state.objectiveRows, recordID, button, function(order)
                        editor.state.model:ReorderObjectives(quest.id, order);
                        RefreshAll(true);
                    end, function(dragParent, dragSize)
                        return CreateDocumentRowSurface(dragParent, vec.New(0, 0), dragSize, 1i, leading, title, detail, trailing, accent, selected);
                    end);
                end,
                onMove = function(_, mousePosition, delta) PreviewOrderedDrag(mousePosition, delta); end,
                onEnd = function() CommitOrderedDrag(); end,
            });
        end
        return button;
    end });
end

local function MakeQuestTextCard(kind : string, quest : any)
    local config = QUEST_TEXT_KINDS[kind];
    local textID = quest[config.field];
    local text = FindText(textID);
    local preview = if text ~= nil and text.englishValue ~= "" then text.englishValue else "No English source text.";
    local selected = editor.state.selectedKind == kind;
    local cardWidth = (centerContentWidth - 5) / 2;
    return Declarative.Component({ width = cardWidth, height = 52, factory = function(parent, position, size, layer)
        local button = Button.New(parent, position, size, layer, { text = "", panelTemplate = "DebugDarkGrey", color = editor.colors.card, hoverColor = editor.colors.hover, highlightColor = editor.colors.selected, borderColor = if selected then editor.colors.accent else editor.colors.border, borderSize = 1, clipChildren = true });
        button:SetHighlighted(selected);

        local labelText = button:NewText(string.upper(config.label), vec.New(7, 9), 1i, "DefaultButtonText");
        labelText:SetAnchor(vec.New(0, 0.5));
        labelText:SetRelativePoint(vec.New(0, 0.5));
        labelText:SetFontSize(7);
        labelText:SetColor(editor.colors.muted);

        local previewText = button:NewText(preview, vec.New(7, -8), 1i, "DefaultButtonText");
        previewText:SetAnchor(vec.New(0, 0.5));
        previewText:SetRelativePoint(vec.New(0, 0.5));
        previewText:SetFontSize(9);
        button:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i then SelectRecord(kind, textID); end end);
        return button;
    end });
end

local function AssociationRole(association : any) : (string, integer, vec)
    if association.roleFlags == ROLE_START then
        return "STARTS", 1i, editor.colors.accent;
    elseif association.roleFlags == 3i then
        return "BOTH", 3i, editor.colors.warning;
    end

    return "TURNS IN", 2i, editor.colors.success;
end

local function RewardDescription(reward : any) : string
    local value = reward.value;
    if reward.kind == "Item" then
        return `{ItemName(value.itemTemplateID)} x {value.quantity}`;
    elseif reward.kind == "Currency" then
        return `{CurrencyName(value.currencyID)} x {value.amount}`;
    elseif reward.kind == "Reputation" then
        return `{FactionName(value.factionID)}: {value.amount}`;
    end

    return `{value.amount} experience`;
end

RefreshCenter = function()
    editor.state.objectiveRows = { };
    if editor.state.centerView ~= nil then
        editor.state.centerView:Destroy();
        editor.state.centerView = nil;
    end

    local quest = GetSelectedQuest();
    local body = { };
    if quest == nil then
        table.insert(body, Declarative.Text("Select or create a quest to begin editing.", { width = centerContentWidth, height = 90, fontSize = 13, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
    else
        table.insert(body, Declarative.Button(QuestDisplayName(quest), { width = centerContentWidth, height = 36, fontSize = 16, textAlign = "left", textPadX = 4, panelTemplate = "DebugDarkGrey", color = vec.New(0.04, 0.05, 0.06), borderSize = 0, onClick = function()
            SelectRecord("Quest", nil);
        end }));
        table.insert(body, Declarative.Text(`Quest {quest.id}   Level {quest.level}   {if HasFlag(quest.flags, QUEST_FLAG_SHOW_WHEN_UNAVAILABLE) then "Shown when unavailable" else "Hidden when unavailable"}`, { width = centerContentWidth, height = 20, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }));

        local usedCreatures : { [integer] : boolean } = { };
        local associations = editor.state.model:GetAssociations(quest.id);
        for _, association in associations do usedCreatures[association.creatureTemplateID] = true; end
        table.sort(associations, function(left, right)
            local _, leftOrder = AssociationRole(left);
            local _, rightOrder = AssociationRole(right);
            if leftOrder ~= rightOrder then return integer.lt(leftOrder, rightOrder); end
            return CreatureName(left.creatureTemplateID) < CreatureName(right.creatureTemplateID);
        end);
        table.insert(body, MakeSectionHeader("associations", "QUEST NPCS", Declarative.Button("+ NPC", { width = SECTION_ACTION_WIDTH, height = SECTION_ACTION_HEIGHT, fontSize = 8, onClick = function()
            creaturePicker:Open({ title = "Add Quest NPC", items = GetCreaturePickerItems(usedCreatures, true), onSelect = function(creatureTemplateID)
                SelectRecord("Association", editor.state.model:AddAssociation(quest.id, creatureTemplateID));
                RefreshAll(true);
            end });
        end })));
        if not editor.state.collapsedSections.associations then
            if #associations == 0 then
                table.insert(body, MakeEmptySectionText("No creatures offer or accept this quest."));
            else
                for _, association in associations do
                    local role, _, roleColor = AssociationRole(association);
                    table.insert(body, MakeDocumentRow("Association", association.id, role, CreatureName(association.creatureTemplateID), `Creature #{association.creatureTemplateID}`, "", roleColor));
                end
            end
        end
        table.insert(body, MakeSectionDivider());

        local usedPrerequisites : { [integer] : boolean } = { };
        local prerequisites = editor.state.model:GetPrerequisites(quest.id);
        for _, prerequisite in prerequisites do usedPrerequisites[prerequisite.prerequisiteQuestID] = true; end
        table.insert(body, MakeSectionHeader("prerequisites", "PREREQUISITES", Declarative.Button("+ Prerequisite", { width = SECTION_ACTION_WIDTH, height = SECTION_ACTION_HEIGHT, fontSize = 8, onClick = function()
            questPicker:Open({ title = "Add Prerequisite Quest", items = GetQuestPickerItems(quest.id, usedPrerequisites), onSelect = function(prerequisiteQuestID)
                SelectRecord("Prerequisite", editor.state.model:AddPrerequisite(quest.id, prerequisiteQuestID));
                RefreshAll(true);
            end });
        end })));
        if not editor.state.collapsedSections.prerequisites then
            if #prerequisites == 0 then
                table.insert(body, MakeEmptySectionText("No prerequisite quests."));
            else
                for _, prerequisite in prerequisites do
                    local requiredQuest = editor.state.model:GetQuest(prerequisite.prerequisiteQuestID);
                    local requiredName = if requiredQuest ~= nil then QuestDisplayName(requiredQuest) else `Unknown quest #{prerequisite.prerequisiteQuestID}`;
                    table.insert(body, MakeDocumentRow("Prerequisite", prerequisite.id, "REQUIRES", requiredName, `Quest #{prerequisite.prerequisiteQuestID}`, "", editor.colors.muted));
                end
            end
        end
        table.insert(body, MakeSectionDivider());

        table.insert(body, MakeSectionHeader("texts", "QUEST TEXT", nil));
        if not editor.state.collapsedSections.texts then
            table.insert(body, Declarative.Horizontal({ width = centerContentWidth, height = 52, spacing = 5, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                MakeQuestTextCard("QuestTextTitle", quest),
                MakeQuestTextCard("QuestTextLog", quest)
            ));
            table.insert(body, Declarative.Horizontal({ width = centerContentWidth, height = 52, spacing = 5, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                MakeQuestTextCard("QuestTextSummary", quest),
                MakeQuestTextCard("QuestTextDetails", quest)
            ));
            table.insert(body, Declarative.Horizontal({ width = centerContentWidth, height = 52, spacing = 5, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                MakeQuestTextCard("QuestTextProgress", quest),
                MakeQuestTextCard("QuestTextCompletion", quest)
            ));
        end
        table.insert(body, MakeSectionDivider());

        local objectives = editor.state.model:GetObjectives(quest.id);
        table.insert(body, MakeSectionHeader("objectives", "OBJECTIVES", MakeAddPicker("addObjective", SECTION_ACTION_WIDTH, "+ Objective", GetObjectiveTypePickerItems(), function(objectiveType)
            local objectiveID = editor.state.model:AddObjective(quest.id, objectiveType);
            SelectRecord("Objective", objectiveID);
            RefreshAll(true);
        end, true)));
        if not editor.state.collapsedSections.objectives then
            if #objectives == 0 then
                table.insert(body, MakeEmptySectionText("No objectives. Quests without objectives can complete immediately."));
            else
                for index, objective in objectives do
                    local targets = editor.state.model:GetTargets(objective.id);
                    local automaticName = if objective.textID == 0i and #targets == 1 then (if objective.objectiveType == 2i then ItemName(targets[1].targetID) else CreatureName(targets[1].targetID)) else nil;
                    local optional = HasFlag(objective.flags, OBJECTIVE_FLAG_OPTIONAL);
                    local targetNames = { };
                    for _, target in targets do table.insert(targetNames, if objective.objectiveType == 2i then ItemName(target.targetID) else CreatureName(target.targetID)); end
                    local detail = if #targetNames == 0 then "No targets" else table.concat(targetNames, ", ");
                    table.insert(body, MakeDocumentRow("Objective", objective.id, tostring(index), automaticName or LocalizedTextPreview(objective.textID, TextName(objective.textID)), detail, `{ObjectiveTypeName(objective.objectiveType)}\n{objective.requiredCount} required{if optional then " - Optional" else ""}`, editor.colors.accent));
                end
            end
        end
        table.insert(body, MakeSectionDivider());

        local groups = editor.state.model:GetRewardGroups(quest.id);
        table.insert(body, MakeSectionHeader("rewards", "REWARDS", Declarative.Button("+ Group", { width = SECTION_ACTION_WIDTH, height = SECTION_ACTION_HEIGHT, fontSize = 8, onClick = function()
            SelectRecord("RewardGroup", editor.state.model:AddRewardGroup(quest.id));
            RefreshAll(true);
        end })));
        if not editor.state.collapsedSections.rewards then
            if #groups == 0 then
                table.insert(body, MakeEmptySectionText("No quest rewards."));
            else
                for index, group in groups do
                    local rewards = editor.state.model:GetRewards(group.id);
                    local choiceText = if group.selectionCount == 0i then "All granted" else `Choose {group.selectionCount}`;
                    local descriptions = { };
                    for _, reward in rewards do
                        table.insert(descriptions, RewardDescription(reward));
                    end
                    table.insert(body, MakeDocumentRow("RewardGroup", group.id, if group.selectionCount == 0i then "GUARANTEED" else "CHOICE", if group.selectionCount == 0i then "Guaranteed rewards" else "Choice rewards", if #descriptions == 0 then "No rewards" else table.concat(descriptions, " - "), choiceText, editor.colors.warning));
                end
            end
        end
        table.insert(body, MakeSectionDivider());
    end

    local contentHeight = math.max(bodyHeight, MeasureVerticalContent(body, CENTER_CONTENT_SPACING, CENTER_CONTENT_TOP_PADDING, CENTER_CONTENT_BOTTOM_CLEARANCE));
    editor.refs.centerHost:SetContentHeight(contentHeight);
    editor.state.centerView = Declarative.Build(editor.refs.centerHost.content, Declarative.Vertical({ position = vec.New(18, -CENTER_CONTENT_TOP_PADDING), width = centerContentWidth, height = contentHeight - CENTER_CONTENT_TOP_PADDING - CENTER_CONTENT_BOTTOM_CLEARANCE, anchor = Declarative.Anchor.TopLeft, relativePoint = Declarative.Anchor.TopLeft, spacing = CENTER_CONTENT_SPACING, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, table.unpack(body)));
end

local function MakeInspectorPicker(id : string, label : string, items : { SearchPicker.Item }, value : any, onChanged : (any) -> ())
    return Declarative.Vertical({ width = inspectorContentWidth, height = 55, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = inspectorContentWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
        Declarative.Component({ id = id, width = inspectorContentWidth, height = 32, factory = function(parent, position, size, layer)
            return SearchPicker.New(parent, editor.state.canvas, position, size, layer, { items = items, value = value, popupWidth = 390, popupHeight = 400, rowHeight = 40, onValueChanged = function(nextValue)
                onChanged(nextValue);
            end });
        end })
    );
end

local function MakeInspectorModalPicker(label : string, value : string, onClick : () -> ())
    return Declarative.Vertical({ width = inspectorContentWidth, height = 55, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = inspectorContentWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
        Declarative.Button(value, { width = inspectorContentWidth, height = 32, fontSize = 9, textAlign = "left", textPadX = 8, onClick = onClick })
    );
end

local function MakeLocalizedTextPicker(id : string, label : string, textID : integer, includeNone : boolean, targetKind : string, targetID : integer, targetField : string, onChanged : (integer) -> ())
    return Declarative.Vertical({ width = inspectorContentWidth, height = 55, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = inspectorContentWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
        Declarative.Horizontal({ width = inspectorContentWidth, height = 32, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Component({ id = id, width = inspectorContentWidth - 88, height = 32, factory = function(parent, position, size, layer)
                return SearchPicker.New(parent, editor.state.canvas, position, size, layer, { items = GetTextPickerItems(includeNone), value = textID, placeholder = "Select localized text", popupWidth = 390, popupHeight = 400, rowHeight = 40, onValueChanged = function(value)
                    if value ~= nil then onChanged(value); end
                end });
            end }),
            Declarative.Button("Edit", { width = 40, height = 32, fontSize = 9, interactable = editor.state.controller:IsReady() and FindText(textID) ~= nil, onClick = function()
                OpenLocalizedTextModal(textID, targetKind, targetID, targetField);
            end }),
            Declarative.Button("New", { width = 40, height = 32, fontSize = 9, interactable = editor.state.controller:IsReady(), onClick = function()
                OpenLocalizedTextModal(nil, targetKind, targetID, targetField);
            end })
        )
    );
end

local function MakeReadOnlyTextPreview(label : string, value : string)
    return Declarative.Vertical({ width = inspectorContentWidth, height = 92, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = inspectorContentWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
        Declarative.Component({ width = inspectorContentWidth, height = 68, factory = function(parent, position, size, layer)
            local input = InputField.New(parent, position, size, layer, { defaultText = value, defaultTextFontSize = 10, multiline = true, textInsetX = 7 });
            input.button.root:SetInteractable(false);
            return input;
        end })
    );
end

local function MakeCompactNumberInput(id : string, label : string, value : integer, minimum : number, maximum : number, width : number, onChanged : (integer) -> ())
    return Declarative.Vertical({ width = width, height = 38, spacing = 2, template = "DebugDarkGrey", alpha = 0, borderSize = 0, interactable = false },
        Declarative.Text(string.upper(label), { width = width, height = 9, fontSize = 7, color = editor.colors.muted, template = "DefaultButtonText", interactable = false }),
        Declarative.Component({ id = id, width = width, height = 27, factory = function(parent, position, size, layer)
            local input = InputField.New(parent, position, size, layer, { defaultText = tostring(value), defaultTextFontSize = 9 });
            input:SetTextVerticallyCentered();
            input:SetOnTextChanged(function()
                local nextValue = ReadInteger(input, minimum, maximum);
                if nextValue ~= nil then onChanged(nextValue); end
            end);
            return input;
        end })
    );
end

local function CreateRewardDragSurface(parent : any, size : vec, kind : string, value : any)
    local button = Button.New(parent, vec.New(0, 0), size, 1i, { text = "", panelTemplate = "DebugDarkGrey", color = editor.colors.card, hoverColor = editor.colors.card, borderColor = editor.colors.accent, borderSize = 1 });
    button.root:SetClipChildren(true);

    local kindText = button.root:NewText(string.upper(kind), vec.New(8, -10), 1i, "DefaultButtonText");
    kindText:SetAnchor(vec.New(0, 1));
    kindText:SetRelativePoint(vec.New(0, 1));
    kindText:SetFontSize(7);
    kindText:SetColor(editor.colors.muted);
    kindText:SetInteractable(false);

    local target = if kind == "Item" then ItemName(value.itemTemplateID) elseif kind == "Currency" then CurrencyName(value.currencyID) elseif kind == "Reputation" then FactionName(value.factionID) else "Experience";
    local targetText = button.root:NewText(target, vec.New(74, -9), 1i, "DefaultButtonText");
    targetText:SetAnchor(vec.New(0, 1));
    targetText:SetRelativePoint(vec.New(0, 1));
    targetText:SetFontSize(9);
    targetText:SetInteractable(false);
    if kind == "Item" then
        local itemTemplate = FindByID(editor.state.snapshot.itemTemplates, value.itemTemplateID);
        if itemTemplate ~= nil then targetText:SetColor(ItemUtil.GetRarityColor(itemTemplate.rarity)); end
    end

    local amount = if kind == "Item" then value.quantity else value.amount;
    local amountText = button.root:NewText(`{if kind == "Item" then "Quantity" else "Amount"}: {amount}`, vec.New(8, 9), 1i, "DefaultButtonText");
    amountText:SetAnchor(vec.New(0, 0));
    amountText:SetRelativePoint(vec.New(0, 0));
    amountText:SetFontSize(8);
    amountText:SetColor(vec.New(0.90, 0.93, 0.96));
    amountText:SetInteractable(false);
    return button;
end

local function MakeRewardCard(reward : any)
    local kind = reward.kind;
    local value = reward.value;
    local rewardKey = `{kind}:{value.id}`;
    local amountField = if kind == "Item" then "quantity" else "amount";
    local amountMinimum = if kind == "Reputation" then -2147483648 else 1;
    local amountMaximum = if kind == "Reputation" then 2147483647 elseif kind == "Item" then 4294967295 else 9007199254740991;
    local cardContentWidth = inspectorContentWidth - 10;
    local selectorWidth = if kind == "Item" then 190 elseif kind == "Currency" then 82 elseif kind == "Reputation" then 116 else 86;
    local selectorHeight = 22;
    local selector = nil;
    if kind == "Item" then
        selector = Declarative.Component({ width = selectorWidth, height = selectorHeight, factory = function(parent, position, size, layer)
            local button = Button.New(parent, position, size, layer, { text = TextUtil.TruncateToWidth(ItemName(value.itemTemplateID), "DefaultButtonText", size.x - 14), textTemplate = "DefaultButtonText", textAlign = "left", textPadX = 7, panelTemplate = "DebugDarkGrey", color = editor.colors.card, hoverColor = editor.colors.hover, borderColor = editor.colors.border, borderSize = 1 });
            button:SetFontSize(7);
            local itemTemplate = FindByID(editor.state.snapshot.itemTemplates, value.itemTemplateID);
            if itemTemplate ~= nil then button.text:SetColor(ItemUtil.GetRarityColor(itemTemplate.rarity)); end
            AttachItemTooltip(button, value.itemTemplateID);
            button:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton ~= 0i then return; end
                editorTooltip.ClearLines();
                editorTooltip.Hide();
                itemPicker:Open({ title = "Replace Reward Item", items = GetItemPickerItems(), onSelect = function(itemTemplateID)
                    editor.state.model:SetRewardFields(kind, value.id, { itemTemplateID = itemTemplateID });
                    RefreshAll(true);
                end });
            end);
            return button;
        end });
    elseif kind == "Currency" then
        selector = Declarative.Component({ width = selectorWidth, height = selectorHeight, factory = function(parent, position, size, layer)
            local button = Button.New(parent, position, size, layer, { text = TextUtil.TruncateToWidth(CurrencyName(value.currencyID), "DefaultButtonText", size.x - 10, 7), textTemplate = "DefaultButtonText", panelTemplate = "DebugDarkGrey", color = editor.colors.card, hoverColor = editor.colors.hover, borderColor = editor.colors.border, borderSize = 1 });
            button:SetFontSize(7);
            button:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton ~= 0i then return; end
                currencyPicker:Open({ title = "Replace Reward Currency", items = GetCurrencyPickerItems(), onSelect = function(currencyID)
                    editor.state.model:SetRewardFields(kind, value.id, { currencyID = currencyID });
                    RefreshAll(true);
                end });
            end);
            return button;
        end });
    elseif kind == "Reputation" then
        selector = Declarative.Component({ width = selectorWidth, height = selectorHeight, factory = function(parent, position, size, layer)
            local button = Button.New(parent, position, size, layer, { text = TextUtil.TruncateToWidth(FactionName(value.factionID), "DefaultButtonText", size.x - 10, 7), textTemplate = "DefaultButtonText", panelTemplate = "DebugDarkGrey", color = editor.colors.card, hoverColor = editor.colors.hover, borderColor = editor.colors.border, borderSize = 1 });
            button:SetFontSize(7);
            button:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton ~= 0i then return; end
                factionPicker:Open({ title = "Replace Reward Faction", items = GetFactionPickerItems(), onSelect = function(factionID)
                    editor.state.model:SetRewardFields(kind, value.id, { factionID = factionID });
                    RefreshAll(true);
                end });
            end);
            return button;
        end });
    else
        selector = Declarative.Text("Direct grant", { width = selectorWidth, height = selectorHeight, fontSize = 7, color = editor.colors.muted, template = "DefaultButtonText", interactable = false });
    end

    return Declarative.Panel({ width = inspectorContentWidth, height = 90, template = "DebugDarkGrey", color = editor.colors.card, borderColor = editor.colors.border, borderSize = 1, setup = function(card)
        table.insert(editor.state.rewardRows, { key = rewardKey, kind = kind, id = value.id, surface = card });
        Draggable.Attach(card, card, {
            button = 0i, moveTarget = false, threshold = 4,
            onBegin = function()
                editorTooltip.ClearLines();
                editorTooltip.Hide();
                BeginOrderedDrag(editor.state.rewardRows, rewardKey, card, function(order)
                    local orderedRewards = { };
                    for _, key in order do
                        for _, row in editor.state.rewardRows do
                            if row.key == key then
                                table.insert(orderedRewards, { kind = row.kind, id = row.id });
                                break;
                            end
                        end
                    end
                    editor.state.model:ReorderRewards(value.rewardGroupID, orderedRewards);
                    RefreshAll(true);
                end, function(dragParent, dragSize)
                    return CreateRewardDragSurface(dragParent, dragSize, kind, value);
                end);
            end,
            onMove = function(_, mousePosition, delta) PreviewOrderedDrag(mousePosition, delta); end,
            onEnd = function() CommitOrderedDrag(); end,
        });
    end },
        Declarative.Vertical({ position = vec.New(0, 0), width = inspectorContentWidth, height = 90, spacing = 4, padding = 5, template = "DebugDarkGrey", alpha = 0, borderSize = 0, interactable = false },
            Declarative.Horizontal({ width = cardContentWidth, height = 30, spacing = 4, alignV = "center", template = "DebugDarkGrey", alpha = 0, borderSize = 0, interactable = false },
                Declarative.Text(string.upper(kind), { width = 58, height = 30, fontSize = 7, color = editor.colors.muted, template = "DefaultButtonText", interactable = false }),
                Declarative.Panel({ grow = 1, height = 30, template = "DebugDarkGrey", alpha = 0, borderSize = 0, interactable = false }),
                selector,
                Declarative.Button("x", { width = 28, height = 28, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
                    editor.state.model:RemoveReward(kind, value.id);
                    RefreshAll(true);
                end })
            ),
            Declarative.Horizontal({ width = cardContentWidth, height = 38, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0, interactable = false },
                MakeCompactNumberInput(`rewardAmount{kind}{value.id}`, if kind == "Item" then "Quantity" else "Amount", value[amountField], amountMinimum, amountMaximum, 144, function(nextValue)
                    editor.state.model:SetRewardFields(kind, value.id, { [amountField] = nextValue });
                    RefreshCenter();
                    UpdateToolbar();
                end),
                Declarative.Vertical({ width = 144, height = 38, spacing = 2, template = "DebugDarkGrey", alpha = 0, borderSize = 0, interactable = false },
                    Declarative.Text("VISIBILITY", { width = 144, height = 9, fontSize = 7, color = editor.colors.muted, template = "DefaultButtonText", interactable = false }),
                    Declarative.Button(if HasFlag(value.flags, REWARD_FLAG_HIDDEN) then "[x] Hidden" else "[ ] Hidden", { width = 144, height = 27, fontSize = 8, textAlign = "left", textPadX = 6, color = if HasFlag(value.flags, REWARD_FLAG_HIDDEN) then editor.colors.selected else editor.colors.card, onClick = function()
                        editor.state.model:SetRewardFields(kind, value.id, { flags = WithFlag(value.flags, REWARD_FLAG_HIDDEN, not HasFlag(value.flags, REWARD_FLAG_HIDDEN)) });
                        RefreshInspector();
                        UpdateToolbar();
                    end })
                )
            )
        )
    );
end

local function MakeAddRewardPicker(rewardGroupID : integer)
    local rewardKinds =
    {
        { value = "Item", primary = "Item", secondary = "Grant an item and quantity" },
        { value = "Currency", primary = "Currency", secondary = "Grant a currency amount" },
        { value = "Reputation", primary = "Reputation", secondary = "Modify faction reputation" },
        { value = "Experience", primary = "Experience", secondary = "Grant experience" },
    };
    local function AddReward(kind : string, targetID : integer?)
        editor.state.model:AddReward(rewardGroupID, kind, targetID);
        RefreshAll(true);
    end

    return Declarative.Component({ id = `addReward{rewardGroupID}`, width = inspectorContentWidth, height = 30, factory = function(parent, position, size, layer)
        local picker = SearchPicker.New(parent, editor.state.canvas, position, size, layer, { items = rewardKinds, placeholder = "+ Reward", popupWidth = 330, popupHeight = 220, rowHeight = 42, onValueChanged = function(kind)
            if kind == "Item" then
                itemPicker:Open({ title = "Add Item Reward", items = GetItemPickerItems(), onSelect = function(itemTemplateID) AddReward("Item", itemTemplateID); end });
            elseif kind == "Currency" then
                currencyPicker:Open({ title = "Add Currency Reward", items = GetCurrencyPickerItems(), onSelect = function(currencyID) AddReward("Currency", currencyID); end });
            elseif kind == "Reputation" then
                factionPicker:Open({ title = "Add Reputation Reward", items = GetFactionPickerItems(), onSelect = function(factionID) AddReward("Reputation", factionID); end });
            elseif kind == "Experience" then
                AddReward("Experience", nil);
            end
        end });
        picker.button:SetFontSize(9);
        return picker;
    end });
end

local function MakeObjectiveTargetRow(objective : any, target : any)
    local targetName = if objective.objectiveType == 2i then ItemName(target.targetID) else CreatureName(target.targetID);
    local targetTextWidth = inspectorContentWidth - 72 - 28 - 16;
    return Declarative.Horizontal({ width = inspectorContentWidth, height = 50, spacing = 4, padding = 4, alignV = "center", template = "DebugDarkGrey", color = editor.colors.card, borderColor = editor.colors.border, borderSize = 1 },
        Declarative.Component({ width = targetTextWidth, height = 38, factory = function(parent, position, size, layer)
            local targetArea = Button.New(parent, position, size, layer, { text = "", panelTemplate = "DebugDarkGrey", color = editor.colors.card, hoverColor = editor.colors.hover, borderSize = 0 });
            targetArea.root:SetClipChildren(true);

            local name = targetArea.root:NewText(TextUtil.TruncateToWidth(targetName, "DefaultButtonText", targetTextWidth), vec.New(0, -4), 1i, "DefaultButtonText");
            name:SetAnchor(vec.New(0, 1));
            name:SetRelativePoint(vec.New(0, 1));
            name:SetFontSize(8);
            name:SetInteractable(false);
            if objective.objectiveType == 2i then
                local itemTemplate = FindByID(editor.state.snapshot.itemTemplates, target.targetID);
                if itemTemplate ~= nil then
                    name:SetColor(ItemUtil.GetRarityColor(itemTemplate.rarity));
                end
                AttachItemTooltip(targetArea, target.targetID);
            else
                name:SetColor(vec.New(0.90, 0.93, 0.96));
            end

            local metadata = targetArea.root:NewText(`#{target.targetID}`, vec.New(0, -23), 1i, "DefaultButtonText");
            metadata:SetAnchor(vec.New(0, 1));
            metadata:SetRelativePoint(vec.New(0, 1));
            metadata:SetFontSize(7);
            metadata:SetColor(editor.colors.muted);
            metadata:SetInteractable(false);
            targetArea:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton == 0i then OpenObjectiveTargetPicker(objective, target); end
            end);
            return targetArea;
        end }),
        Declarative.Vertical({ width = 72, height = 38, spacing = 2, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Text("CONTRIBUTES", { width = 72, height = 9, fontSize = 7, color = editor.colors.muted, template = "DefaultButtonText" }),
            Declarative.Component({ id = `targetContribution{target.id}`, width = 72, height = 27, factory = function(parent, position, size, layer)
                local input = InputField.New(parent, position, size, layer, { defaultText = tostring(target.contribution), defaultTextFontSize = 9 });
                input:SetTextVerticallyCentered();
                input:SetOnTextChanged(function()
                    local contribution = ReadInteger(input, 1, 4294967295);
                    if contribution ~= nil then
                        editor.state.model:SetTargetFields(target.id, { contribution = contribution });
                        UpdateToolbar();
                    end
                end);
                return input;
            end })
        ),
        Declarative.Button("x", { width = 28, height = 28, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
            editor.state.model:RemoveTarget(target.id);
            RefreshAll(true);
        end })
    );
end

local function MakeInspectorDropdown(id : string, label : string, options : { any }, value : any, onChanged : (any) -> ())
    return Declarative.Vertical({ width = inspectorContentWidth, height = 55, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = inspectorContentWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
        Declarative.Component({ id = id, width = inspectorContentWidth, height = 32, factory = function(parent, position, size, layer)
            local dropdown = Dropdown.New(parent, editor.state.canvas, position, size, layer, { options = options, value = value, itemHeight = 24, itemFontSize = 9, onValueChanged = function(nextValue)
                onChanged(nextValue);
            end });
            dropdown.button:SetFontSize(9);
            return dropdown;
        end })
    );
end

local function MakeNumberInput(id : string, label : string, value : integer, minimum : number, maximum : number, onChanged : (integer) -> (), width : number?)
    local fieldWidth = width or halfInspectorWidth;
    return Declarative.Vertical({ width = fieldWidth, height = 55, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = fieldWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
        Declarative.Component({ id = id, width = fieldWidth, height = 32, factory = function(parent, position, size, layer)
            local input = InputField.New(parent, position, size, layer, { defaultText = tostring(value), defaultTextFontSize = 10 });
            input:SetTextVerticallyCentered();
            input:SetOnTextChanged(function()
                local nextValue = ReadInteger(input, minimum, maximum);
                if nextValue ~= nil then
                    onChanged(nextValue);
                end
            end);
            return input;
        end })
    );
end

local function MakeToggle(label : string, enabled : boolean, onChanged : (boolean) -> ())
    return Declarative.Button(`{if enabled then "[x]" else "[ ]"}  {label}`, { width = inspectorContentWidth, height = 28, fontSize = 9, textAlign = "left", textPadX = 8, color = if enabled then editor.colors.selected else editor.colors.card, onClick = function()
        onChanged(not enabled);
    end });
end

local function AddQuestContentInspector(body : { any }, quest : any)
    table.insert(body, Declarative.Text("Internal name", { width = inspectorContentWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }));
    table.insert(body, Declarative.Component({ width = inspectorContentWidth, height = 32, factory = function(parent, position, size, layer)
        local input = InputField.New(parent, position, size, layer, { defaultText = quest.internalName, defaultTextFontSize = 10 });
        input:SetTextVerticallyCentered();
        input:SetOnTextChanged(function()
            editor.state.model:SetQuestFields(quest.id, { internalName = input:GetText() });
            RefreshSidebar(true);
            UpdateToolbar();
        end);
        return input;
    end }));
end

local function AddQuestLogicInspector(body : { any }, quest : any)
    table.insert(body, MakeNumberInput("questLevel", "Quest level", quest.level, 0, 65535, function(value)
        editor.state.model:SetQuestFields(quest.id, { level = value });
        RefreshCenter();
        UpdateToolbar();
    end, inspectorContentWidth));
    table.insert(body, Declarative.Text("Flags", { width = inspectorContentWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }));
    table.insert(body, MakeToggle("Show when unavailable", HasFlag(quest.flags, QUEST_FLAG_SHOW_WHEN_UNAVAILABLE), function(enabled)
        editor.state.model:SetQuestFields(quest.id, { flags = WithFlag(quest.flags, QUEST_FLAG_SHOW_WHEN_UNAVAILABLE, enabled) });
        RefreshCenter();
        RefreshInspector();
        UpdateToolbar();
    end));
    table.insert(body, MakeInspectorPicker("visibilityCondition", "Visibility condition", GetConditionPickerItems(), quest.visibilityConditionSetID, function(value)
        editor.state.model:SetQuestFields(quest.id, { visibilityConditionSetID = value });
        UpdateToolbar();
    end));
    table.insert(body, MakeInspectorPicker("availabilityCondition", "Availability condition", GetConditionPickerItems(), quest.availabilityConditionSetID, function(value)
        editor.state.model:SetQuestFields(quest.id, { availabilityConditionSetID = value });
        UpdateToolbar();
    end));
    table.insert(body, MakeLocalizedTextPicker("unavailableReason", "Unavailable reason", quest.unavailableReasonTextID, true, "Quest", quest.id, "unavailableReasonTextID", function(value)
        editor.state.model:SetQuestFields(quest.id, { unavailableReasonTextID = value });
        UpdateToolbar();
    end));
end

local function AddQuestMetadataInspector(body : { any }, quest : any)
    table.insert(body, Declarative.Text(`Quest ID\n{quest.id}`, { width = inspectorContentWidth, height = 48, fontSize = 11, color = editor.colors.warning, template = "DefaultButtonText" }));
    table.insert(body, Declarative.Text("AUTHORITATIVE SOURCE\nWorld database / server runtime", { width = inspectorContentWidth, height = 52, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
    table.insert(body, Declarative.Text("CLIENT EXPORT\nStatic snapshot. Client data may lag behind the editor until the next export.", { width = inspectorContentWidth, height = 66, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
end

local function AddQuestTextInspector(body : { any }, quest : any, kind : string)
    local config = QUEST_TEXT_KINDS[kind];
    local textID = quest[config.field];
    local text = FindText(textID);
    table.insert(body, MakeLocalizedTextPicker(`questText{config.field}`, config.label, textID, false, "Quest", quest.id, config.field, function(value)
        editor.state.model:SetQuestFields(quest.id, { [config.field] = value });
        editor.state.selectedRecordID = value;
        RefreshAll(true);
    end));
    table.insert(body, MakeReadOnlyTextPreview("English source preview (read-only)", if text ~= nil then text.englishValue else "Localized text is unavailable."));
    table.insert(body, Declarative.Text("Use Edit to change the source or any translation in the localized-text editor.", { width = inspectorContentWidth, height = 34, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
end

local function AddRecordInspector(body : { any }, quest : any, record : any)
    local kind = editor.state.selectedKind;
    if kind == "Prerequisite" then
        local prerequisiteQuest = editor.state.model:GetQuest(record.prerequisiteQuestID);
        local prerequisiteName = if prerequisiteQuest ~= nil then QuestDisplayName(prerequisiteQuest) else `Unknown quest #{record.prerequisiteQuestID}`;
        table.insert(body, MakeInspectorModalPicker("Prerequisite quest", prerequisiteName, function()
            local used : { [integer] : boolean } = { };
            for _, prerequisite in editor.state.model:GetPrerequisites(quest.id) do
                if prerequisite.id ~= record.id then used[prerequisite.prerequisiteQuestID] = true; end
            end
            questPicker:Open({ title = "Replace Prerequisite Quest", items = GetQuestPickerItems(quest.id, used), onSelect = function(value)
                editor.state.model:SetPrerequisiteFields(record.id, { prerequisiteQuestID = value });
                RefreshAll(true);
            end });
        end));
        table.insert(body, Declarative.Button("Remove prerequisite", { width = inspectorContentWidth, height = 30, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
            editor.state.model:RemovePrerequisite(record.id);
            SelectRecord("Quest", nil);
            RefreshAll(true);
        end }));
    elseif kind == "Association" then
        table.insert(body, MakeInspectorModalPicker("Creature template", CreatureName(record.creatureTemplateID), function()
            local used : { [integer] : boolean } = { };
            for _, association in editor.state.model:GetAssociations(quest.id) do
                if association.id ~= record.id then used[association.creatureTemplateID] = true; end
            end
            creaturePicker:Open({ title = "Replace Quest NPC", items = GetCreaturePickerItems(used, true), onSelect = function(value)
                editor.state.model:SetAssociationFields(record.id, { creatureTemplateID = value });
                RefreshAll(true);
            end });
        end));
        table.insert(body, MakeInspectorDropdown("associationRole", "Quest role", { { label = "Starts quest", value = ROLE_START }, { label = "Turns in quest", value = ROLE_TURN_IN }, { label = "Starts and turns in", value = 3i } }, record.roleFlags, function(value)
            editor.state.model:SetAssociationFields(record.id, { roleFlags = value });
            RefreshCenter();
            UpdateToolbar();
        end));
        table.insert(body, MakeInspectorPicker("associationVisibility", "Visibility condition", GetConditionPickerItems(), record.visibilityConditionSetID, function(value) editor.state.model:SetAssociationFields(record.id, { visibilityConditionSetID = value }); UpdateToolbar(); end));
        table.insert(body, MakeInspectorPicker("associationEnabled", "Enabled condition", GetConditionPickerItems(), record.enabledConditionSetID, function(value) editor.state.model:SetAssociationFields(record.id, { enabledConditionSetID = value }); UpdateToolbar(); end));
        table.insert(body, MakeToggle("Close interaction after accepting", HasFlag(record.flags, ASSOCIATION_FLAG_CLOSE_ON_ACCEPT), function(enabled) editor.state.model:SetAssociationFields(record.id, { flags = WithFlag(record.flags, ASSOCIATION_FLAG_CLOSE_ON_ACCEPT, enabled) }); RefreshInspector(); UpdateToolbar(); end));
        table.insert(body, Declarative.Button("Remove NPC association", { width = inspectorContentWidth, height = 30, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
            editor.state.model:RemoveAssociation(record.id);
            SelectRecord("Quest", nil);
            RefreshAll(true);
        end }));
    elseif kind == "Objective" then
        table.insert(body, MakeInspectorDropdown("objectiveType", "Objective type", { { label = "Kill Creature", value = 1i }, { label = "Collect Item", value = 2i } }, record.objectiveType, function(value)
            if not IsTemporaryID(record.id) then
                editor.state.controller:SetStatus("Existing objective types cannot change until the server supports atomic objective-and-target mutations.", STATUS.Warning);
                UpdateToolbar();
                return;
            end

            for _, target in editor.state.model:GetTargets(record.id) do
                editor.state.model:RemoveTarget(target.id);
            end
            editor.state.model:SetObjectiveFields(record.id, { objectiveType = value });
            RefreshAll(true);
        end));
        local targets = editor.state.model:GetTargets(record.id);
        table.insert(body, MakeLocalizedTextPicker("objectiveText", "Objective text", record.textID, true, "Objective", record.id, "textID", function(value) editor.state.model:SetObjectiveFields(record.id, { textID = value }); RefreshAll(true); end));
        if record.textID ~= 0i and #targets == 1 then
            table.insert(body, Declarative.Button("Detach objective text", { width = inspectorContentWidth, height = 28, fontSize = 9, onClick = function()
                editor.state.model:SetObjectiveFields(record.id, { textID = 0i });
                RefreshAll(true);
            end }));
        end
        local objectiveText = FindText(record.textID);
        table.insert(body, MakeReadOnlyTextPreview("English source preview (read-only)", if objectiveText ~= nil then objectiveText.englishValue else "Automatic text from the selected target."));
        table.insert(body, Declarative.Text("Localized text is optional with one target. Without it, tracking uses 'current/required name' and quest offers use 'required name'.", { width = inspectorContentWidth, height = 42, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
        table.insert(body, MakeNumberInput("objectiveRequired", "Required count", record.requiredCount, 1, 4294967295, function(value) editor.state.model:SetObjectiveFields(record.id, { requiredCount = value }); RefreshCenter(); UpdateToolbar(); end, inspectorContentWidth));
        table.insert(body, MakeToggle("Optional", HasFlag(record.flags, OBJECTIVE_FLAG_OPTIONAL), function(enabled) editor.state.model:SetObjectiveFields(record.id, { flags = WithFlag(record.flags, OBJECTIVE_FLAG_OPTIONAL, enabled) }); RefreshInspector(); UpdateToolbar(); end));
        table.insert(body, MakeToggle("Do not consume collected items", HasFlag(record.flags, OBJECTIVE_FLAG_DO_NOT_CONSUME), function(enabled) editor.state.model:SetObjectiveFields(record.id, { flags = WithFlag(record.flags, OBJECTIVE_FLAG_DO_NOT_CONSUME, enabled) }); RefreshInspector(); UpdateToolbar(); end));
        table.insert(body, Declarative.Text("TARGETS", { width = inspectorContentWidth, height = 20, fontSize = 9, color = editor.colors.accent, template = "DefaultButtonText" }));
        for _, target in targets do
            table.insert(body, MakeObjectiveTargetRow(record, target));
        end
        table.insert(body, Declarative.Button(if record.objectiveType == 2i then "+ Item Target" else "+ Creature Target", { width = inspectorContentWidth, height = 30, fontSize = 9, onClick = function()
            OpenObjectiveTargetPicker(record);
        end }));
        table.insert(body, Declarative.Button("Remove objective", { width = inspectorContentWidth, height = 30, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
            editor.state.model:RemoveObjective(record.id);
            SelectRecord("Quest", nil);
            RefreshAll(true);
        end }));
    elseif kind == "RewardGroup" then
        editor.state.rewardRows = { };
        local rewardCount = #editor.state.model:GetRewards(record.id);
        table.insert(body, MakeInspectorDropdown("rewardGrantMode", "Grant mode", { { label = "Grant all", value = 0i }, { label = "Player chooses", value = 1i } }, if record.selectionCount == 0i then 0i else 1i, function(value)
            editor.state.model:SetRewardGroupFields(record.id, { selectionCount = if value == 0i then 0i elseif record.selectionCount == 0i then 1i else record.selectionCount });
            RefreshAll(true);
        end));
        if record.selectionCount ~= 0i then
            table.insert(body, MakeNumberInput("selectionCount", "Choose count", record.selectionCount, 1, math.max(1, math.min(16, rewardCount)), function(value) editor.state.model:SetRewardGroupFields(record.id, { selectionCount = value }); RefreshCenter(); UpdateToolbar(); end, inspectorContentWidth));
        end
        table.insert(body, MakeNumberInput("groupPriority", "Reward group order", record.priority, -2147483648, 2147483647, function(value) editor.state.model:SetRewardGroupFields(record.id, { priority = value }); RefreshCenter(); UpdateToolbar(); end, inspectorContentWidth));
        table.insert(body, Declarative.Text("REWARDS", { width = inspectorContentWidth, height = 20, fontSize = 9, color = editor.colors.accent, template = "DefaultButtonText" }));
        for _, reward in editor.state.model:GetRewards(record.id) do
            table.insert(body, MakeRewardCard(reward));
        end
        table.insert(body, MakeAddRewardPicker(record.id));
        table.insert(body, Declarative.Button("Remove reward group", { width = inspectorContentWidth, height = 30, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
            editor.state.model:RemoveRewardGroup(record.id);
            SelectRecord("Quest", nil);
            RefreshAll(true);
        end }));
    end
end

RefreshInspector = function()
    if editorTooltip ~= nil then
        editorTooltip.ClearLines();
        editorTooltip.Hide();
    end
    if editor.state.inspectorView ~= nil then
        editor.state.inspectorView:Destroy();
        editor.state.inspectorView = nil;
    end

    local quest = GetSelectedQuest();
    local record = GetSelectedRecord();
    local body = { };
    if editor.refs.deleteQuest ~= nil then
        local questSelected = quest ~= nil and editor.state.selectedKind == "Quest";
        editor.refs.deleteQuest:SetVisible(questSelected);
        editor.refs.duplicateQuest:SetVisible(questSelected);
    end
    if quest == nil then
        table.insert(body, Declarative.Text("QUEST", { width = inspectorContentWidth, height = 18, fontSize = 9, color = editor.colors.warning, template = "DefaultButtonText" }));
        table.insert(body, Declarative.Text("No quest selected", { width = inspectorContentWidth, height = 34, fontSize = 15, template = "DefaultButtonText" }));
    else
        local kind = editor.state.selectedKind;
        local headerKind = if IsQuestTextKind(kind) then "QUEST TEXT" elseif kind == "Association" then "QUEST NPC" elseif kind == "RewardGroup" then "REWARD GROUP" else string.upper(kind);
        local headerTitle = QuestDisplayName(quest);
        local headerSubtitle = if kind == "Quest" then `Quest {quest.id}` else `Record {editor.state.selectedRecordID}`;
        if IsQuestTextKind(kind) then
            headerTitle = QUEST_TEXT_KINDS[kind].label;
            headerSubtitle = "Localized quest text";
        elseif kind == "Association" and record ~= nil then
            headerTitle = CreatureName(record.creatureTemplateID);
            headerSubtitle = `Creature #{record.creatureTemplateID}`;
        elseif kind == "Prerequisite" and record ~= nil then
            local requiredQuest = editor.state.model:GetQuest(record.prerequisiteQuestID);
            headerTitle = if requiredQuest ~= nil then QuestDisplayName(requiredQuest) else `Unknown quest #{record.prerequisiteQuestID}`;
            headerSubtitle = `Quest #{record.prerequisiteQuestID}`;
        elseif kind == "Objective" and record ~= nil then
            headerTitle = LocalizedTextPreview(record.textID, TextName(record.textID));
            headerSubtitle = ObjectiveTypeName(record.objectiveType);
        elseif kind == "RewardGroup" and record ~= nil then
            headerTitle = if record.selectionCount == 0i then "Guaranteed Rewards" else "Choice Rewards";
            headerSubtitle = `Reward Group #{record.id}`;
        end
        if kind ~= "Quest" then
            table.insert(body, Declarative.Button(`< {QuestDisplayName(quest)}`, { width = inspectorContentWidth, height = 22, fontSize = 8, textAlign = "left", textPadX = 0, panelTemplate = "DebugDarkGrey", alpha = 0, borderSize = 0, color = editor.colors.card, onClick = function() SelectRecord("Quest", nil); end }));
        end
        table.insert(body, Declarative.Text(headerKind, { width = inspectorContentWidth, height = 18, fontSize = 9, color = editor.colors.accent, template = "DefaultButtonText" }));
        table.insert(body, Declarative.Text(headerTitle, { width = inspectorContentWidth, height = 30, fontSize = 14, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }));
        table.insert(body, Declarative.Text(headerSubtitle, { width = inspectorContentWidth, height = 18, fontSize = 8, color = editor.colors.muted, template = "DefaultButtonText" }));

        if kind == "Quest" then
            table.insert(body, Declarative.Horizontal({ width = inspectorContentWidth, height = 30, spacing = 5, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                Declarative.Button("Content", { width = (inspectorContentWidth - 10) / 3, height = 30, fontSize = 9, color = if editor.state.inspectorTab == "Content" then editor.colors.selected else editor.colors.card, onClick = function() editor.state.inspectorTab = "Content"; RefreshInspector(); end }),
                Declarative.Button("Logic", { width = (inspectorContentWidth - 10) / 3, height = 30, fontSize = 9, color = if editor.state.inspectorTab == "Logic" then editor.colors.selected else editor.colors.card, onClick = function() editor.state.inspectorTab = "Logic"; RefreshInspector(); end }),
                Declarative.Button("Metadata", { width = (inspectorContentWidth - 10) / 3, height = 30, fontSize = 9, color = if editor.state.inspectorTab == "Metadata" then editor.colors.selected else editor.colors.card, onClick = function() editor.state.inspectorTab = "Metadata"; RefreshInspector(); end })
            ));
            if editor.state.inspectorTab == "Content" then
                AddQuestContentInspector(body, quest);
            elseif editor.state.inspectorTab == "Logic" then
                AddQuestLogicInspector(body, quest);
            else
                AddQuestMetadataInspector(body, quest);
            end
        elseif IsQuestTextKind(kind) then
            AddQuestTextInspector(body, quest, kind);
        elseif record ~= nil then
            AddRecordInspector(body, quest, record);
        else
            table.insert(body, Declarative.Text("The selected record no longer exists.", { width = inspectorContentWidth, height = 44, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
        end
    end

    local contentHeight = math.max(inspectorViewportHeight, MeasureVerticalContent(body, 7, 8, 12));
    editor.refs.inspectorHost:SetContentHeight(contentHeight);
    editor.state.inspectorView = Declarative.Build(editor.refs.inspectorHost.content, Declarative.Vertical({ position = vec.New(14, -8), width = inspectorContentWidth, height = contentHeight - 20, spacing = 7, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, table.unpack(body)));
end

RefreshSidebar = function(preserveScroll : boolean?)
    local rows = { };
    if editor.state.model ~= nil then
        local query = string.lower(editor.state.search);
        for _, questID in editor.state.model:GetQuestIDs() do
            local quest = editor.state.model:GetQuest(questID);
            local titlePreview = QuestDisplayName(quest);
            if query == "" or string.find(string.lower(quest.internalName), query, 1, true) ~= nil or string.find(string.lower(titlePreview), query, 1, true) ~= nil or string.find(tostring(questID), query, 1, true) ~= nil then
                table.insert(rows, { id = questID, quest = quest, title = titlePreview });
            end
        end
    end

    editor.refs.questCount:SetText(`{#rows} quest{if #rows == 1 then "" else "s"}`);
    editor.refs.questList:SetItems(rows, preserveScroll == true);
end

UpdateToolbar = function()
    local quest = GetSelectedQuest();
    local dirty = quest ~= nil and editor.state.model:IsQuestDirty(quest.id);
    local anyDirty = editor.state.model ~= nil and editor.state.model:IsDirty();
    local statusMessage, statusLevel = editor.state.controller:GetStatus();
    local validationError = if dirty then GetApplyValidationError(quest.id) else nil;
    if validationError ~= nil then
        statusMessage = validationError;
        statusLevel = STATUS.Warning;
    elseif editor.state.pendingSnapshot and anyDirty then
        statusMessage = "Authoritative data changed. Apply or revert this draft before refreshing.";
        statusLevel = STATUS.Warning;
    end

    local statusColor = if statusLevel == STATUS.Success then editor.colors.success elseif statusLevel == STATUS.Error then editor.colors.error elseif statusLevel == STATUS.Warning then editor.colors.warning else editor.colors.muted;
    editor.refs.status:SetText(statusMessage);
    editor.refs.status:SetColor(statusColor);
    editor.refs.dirty:SetText(if quest == nil then "NO QUEST" elseif dirty then "UNSAVED" else "SAVED");
    editor.refs.dirty:SetColor(if dirty then editor.colors.warning else editor.colors.muted);
    editor.refs.apply:SetInteractable(dirty and validationError == nil and editor.state.controller:IsReady() and editor.state.applyOperations == nil);
    editor.refs.revert:SetInteractable(anyDirty and editor.state.applyOperations == nil);
end

RefreshAll = function(preserveScroll : boolean?)
    RefreshSidebar(preserveScroll);
    RefreshCenter();
    RefreshInspector();
    UpdateToolbar();
end

SelectQuest = function(questID : integer?, allowDirty : boolean?)
    if not allowDirty and questID ~= editor.state.selectedQuestID and editor.state.model ~= nil and editor.state.model:IsDirty() then
        editor.state.controller:SetStatus("Apply or revert the current quest before selecting another quest.", STATUS.Warning);
        UpdateToolbar();
        return;
    end

    editor.state.selectedQuestID = questID;
    editor.state.controller:SelectQuest(questID);
    editor.state.selectedKind = "Quest";
    editor.state.selectedRecordID = nil;
    RefreshAll(true);
end

ApplySnapshot = function()
    editor.state.snapshot = editor.state.controller:Refresh();
    if editor.state.snapshot.dataState ~= "Ready" or editor.state.controller:IsAwaitingSnapshot() then
        UpdateToolbar();
        return;
    end
    if editor.state.model ~= nil and editor.state.model:IsDirty() then
        editor.state.pendingSnapshot = true;
        UpdateToolbar();
        return;
    end

    if editor.state.model == nil then
        editor.state.model = QuestModel.New(editor.state.snapshot);
    else
        editor.state.model:Reload(editor.state.snapshot);
    end

    local selectedQuestID = editor.state.controller:GetSelectedQuestID() or editor.state.selectedQuestID;
    if selectedQuestID == nil or editor.state.model:GetQuest(selectedQuestID) == nil then
        selectedQuestID = editor.state.model:GetQuestIDs()[1];
    end

    editor.state.selectedQuestID = selectedQuestID;
    editor.state.controller:SelectQuest(selectedQuestID);
    editor.state.selectedKind = "Quest";
    editor.state.selectedRecordID = nil;
    editor.state.pendingSnapshot = false;
    RefreshAll(true);
end

local function CreateQuestList(parent, position, size, layer)
    return VirtualList.New(parent, position, size, layer, {
        rowHeight = 48,
        scrollBox = { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = size.x - 10, contentSizeY = size.y, verticalScrollBar = true, scrollBarWidth = 10, alpha = 0 },
        createRow = function(rowParent)
            local row = Button.New(rowParent, vec.New(0, 0), vec.New(size.x - 10, 46), 1i, { text = "", panelTemplate = "DebugDarkGrey", textTemplate = "DefaultButtonText", textAlign = "left", textPadX = 8, color = editor.colors.card, highlightColor = editor.colors.selected });
            row:SetFontSize(9);
            row:SetAnchor(vec.New(0, 1));
            row:SetRelativePoint(vec.New(0, 1));
            row.bound = nil;
            row:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton == 0i and row.bound ~= nil then
                    SelectQuest(row.bound.id);
                end
            end);
            return row;
        end,
        bindRow = function(row, _, value)
            row.bound = value;
            row:SetText(`{value.title}\n#{value.id}  Lv {value.quest.level}  {value.quest.internalName}`);
            row:SetHighlighted(value.id == editor.state.selectedQuestID);
        end,
    });
end

view = Declarative.Build(editor.state.canvas,
    Declarative.Panel({ id = "root", position = editor.geometry.windowPosition, size = editor.geometry.windowSize, template = "DialogBox", clipChildren = true },
        Declarative.Horizontal({ id = "topbar", position = vec.New(14, editor.geometry.windowSize.y - 44), size = vec.New(editor.geometry.windowSize.x - 28, 34), spacing = 6, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Panel({ id = "dragHandle", grow = 1, height = 34, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, Declarative.Text("Quest Editor", { position = vec.New(0, 0), width = 180, height = 20, anchor = Declarative.Anchor.Left, relativePoint = Declarative.Anchor.Left, fontSize = 15, template = "DefaultButtonText" })),
            Declarative.Text("", { id = "status", width = 335, height = 34, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            Declarative.Text("NO QUEST", { id = "dirty", width = 76, height = 34, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
            Declarative.Button("Revert Quest", { id = "revert", width = 94, height = 34, fontSize = 9, interactable = false, onClick = function()
                if editor.state.model == nil or editor.state.applyOperations ~= nil then
                    return;
                end

                local hadPendingSnapshot = editor.state.pendingSnapshot;
                editor.state.model:Revert();
                editor.state.pendingSnapshot = false;
                if hadPendingSnapshot then
                    ApplySnapshot();
                else
                    RefreshAll(false);
                end
            end }),
            Declarative.Button("Apply Quest", { id = "apply", width = 94, height = 34, fontSize = 9, color = editor.colors.selected, interactable = false, onClick = BeginApply }),
            Declarative.Panel({ width = 14, height = 34, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }),
            Declarative.Button("Refresh", { width = 76, height = 34, fontSize = 10, onClick = function()
                if editor.state.model ~= nil and editor.state.model:IsDirty() then
                    editor.state.controller:SetStatus("Apply or revert the current quest before refreshing.", STATUS.Warning);
                    UpdateToolbar();
                else
                    editor.state.controller:RequestSnapshot();
                end
            end }),
            Declarative.Button("Close", { width = 62, height = 34, fontSize = 10, onClick = function()
                local function hide()
                    editor.state.visible = false;
                    itemPicker:Close();
                    currencyPicker:Close();
                    factionPicker:Close();
                    creaturePicker:Close();
                    questPicker:Close();
                    editorTooltip.ClearLines();
                    editorTooltip.Hide();
                    view.root:SetVisible(false);
                end

                if editor.state.model ~= nil and editor.state.model:IsDirty() then
                    closeModal:Open({ message = "Close with unsaved quest changes? The local draft remains available until scripts reload.", onConfirm = hide });
                else
                    hide();
                end
            end })
        ),
        Declarative.Horizontal({ position = vec.New(1, 1), size = vec.New(editor.geometry.windowSize.x - 2, bodyHeight), spacing = 0, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Vertical({ width = editor.geometry.sidebarWidth, height = bodyHeight, spacing = 7, padding = 10, template = "DebugDarkGrey", alpha = 0.72 },
                Declarative.Horizontal({ width = editor.geometry.sidebarWidth - 20, height = 20, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                    Declarative.Text("QUESTS", { grow = 1, height = 20, fontSize = 10, color = editor.colors.accent, template = "DefaultButtonText" }),
                    Declarative.Text("0 quests", { id = "questCount", width = 70, height = 20, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" })
                ),
                Declarative.Component({ id = "search", width = editor.geometry.sidebarWidth - 20, height = 30, factory = function(parent, position, size, layer)
                    local input = InputBox.New(parent, position, size, layer, { placeholderText = "Find quest...", defaultTextFontSize = 10 });
                    input:SetTextVerticallyCentered();
                    input:SetOnTextChanged(function()
                        editor.state.search = input:GetText();
                        RefreshSidebar(false);
                    end);
                    return input;
                end }),
                Declarative.Component({ id = "questList", width = editor.geometry.sidebarWidth - 20, height = bodyHeight - 127, factory = CreateQuestList }),
                Declarative.Button("+ New Quest", { width = editor.geometry.sidebarWidth - 20, height = 30, fontSize = 10, onClick = function()
                    if editor.state.model == nil or not editor.state.controller:IsReady() or editor.state.model:IsDirty() then
                        editor.state.controller:SetStatus("Apply or revert the current quest before creating another quest.", STATUS.Warning);
                        UpdateToolbar();
                        return;
                    end

                    SelectQuest(editor.state.model:AddQuest(UniqueQuestName("New Quest")), true);
                end })
            ),
            Declarative.Panel({ width = centerWidth, height = bodyHeight, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                Declarative.Component({ id = "centerHost", position = vec.New(0, 0), size = vec.New(centerWidth, bodyHeight), factory = function(parent, position, size, layer)
                    return ScrollBox.New(parent, position, size, layer, { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = centerWidth - 10, contentSizeY = bodyHeight, verticalScrollBar = true, scrollBarWidth = 10, alpha = 0.34 });
                end })
            ),
            Declarative.Panel({ width = editor.geometry.inspectorWidth, height = bodyHeight, template = "DebugDarkGrey", alpha = 0.72 },
                Declarative.Component({ id = "inspectorHost", position = vec.New(0, INSPECTOR_FOOTER_HEIGHT), size = vec.New(editor.geometry.inspectorWidth, inspectorViewportHeight), factory = function(parent, position, size, layer)
                    return ScrollBox.New(parent, position, size, layer, { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = editor.geometry.inspectorWidth - 10, contentSizeY = size.y, verticalScrollBar = true, scrollBarWidth = 10, alpha = 0 });
                end }),
                Declarative.Horizontal({ position = vec.New(10, 7), size = vec.New(editor.geometry.inspectorWidth - 20, 30), spacing = 6, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                    Declarative.Button("Delete", { id = "deleteQuest", width = 70, height = 30, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
                        local quest = GetSelectedQuest();
                        if quest == nil or editor.state.applyOperations ~= nil then
                            return;
                        end

                        deleteModal:Open({ message = `Delete '{quest.internalName}' and all of its prerequisites, objectives, rewards, and associations?`, onConfirm = function()
                            if IsTemporaryID(quest.id) then
                                editor.state.model:RemoveQuest(quest.id);
                                editor.state.selectedQuestID = nil;
                                editor.state.controller:SelectQuest(nil);
                                SelectQuest(editor.state.model:GetQuestIDs()[1]);
                            elseif editor.state.model:IsDirty() then
                                editor.state.controller:SetStatus("Revert the quest draft before deleting the authoritative quest.", STATUS.Warning);
                                UpdateToolbar();
                            elseif editor.state.controller:IsReady() then
                                editor.state.controller:Submit(InteractionEditor.DeleteQuestTemplate(quest.id));
                            end
                        end });
                    end }),
                    Declarative.Panel({ grow = 1, height = 30, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }),
                    Declarative.Button("Duplicate Quest", { id = "duplicateQuest", width = 112, height = 30, fontSize = 9, onClick = function()
                        local quest = GetSelectedQuest();
                        if quest == nil or editor.state.model:IsDirty() or not editor.state.controller:IsReady() then
                            editor.state.controller:SetStatus("Apply or revert the current quest before duplicating it.", STATUS.Warning);
                            UpdateToolbar();
                            return;
                        end

                        SelectQuest(editor.state.model:DuplicateQuest(quest.id, UniqueQuestName(`{quest.internalName} Copy`)), true);
                    end })
                )
            )
        ),
        Declarative.Panel({ id = "dragHost", position = vec.New(0, 0), size = editor.geometry.windowSize, layer = 100i, template = "DebugDarkGrey", alpha = 0, borderSize = 0, interactable = false })
    )
);

local localizedText = editor.state.localizedText;
local localizedTextView = Declarative.Build(editor.state.canvas, Declarative.Panel({ position = vec.New(0, 0), size = vec.New(1920, 1080), layer = 1800i, anchor = Declarative.Anchor.TopLeft, relativePoint = Declarative.Anchor.TopLeft, template = "DebugDarkGrey", color = vec.New(0.02, 0.025, 0.03), alpha = 0.55, borderSize = 0, interactable = true, enabled = false, events = { mouseDown = function() end, mouseScroll = function() end } }));
localizedText.root = localizedTextView.root;
QuestEditorLocalizedTextModalView = localizedTextView;

local localizedDialog = localizedText.root:NewPanel(vec.New(0, 0), vec.New(760, 388), 1i, "DialogBox");
localizedDialog:SetAnchor(vec.New(0.5, 0.5));
localizedDialog:SetRelativePoint(vec.New(0.5, 0.5));
localizedText.dialog = localizedDialog;
local localizedEyebrow = localizedDialog:NewText("LOCALIZED TEXT", vec.New(12, -9), 2i, "DefaultButtonText");
localizedEyebrow:SetAnchor(vec.New(0, 1));
localizedEyebrow:SetRelativePoint(vec.New(0, 1));
localizedEyebrow:SetFontSize(9);
localizedEyebrow:SetColor(editor.colors.accent);
localizedText.title = localizedDialog:NewText("New text", vec.New(12, -25), 2i, "DefaultButtonText");
localizedText.title:SetAnchor(vec.New(0, 1));
localizedText.title:SetRelativePoint(vec.New(0, 1));
localizedText.title:SetFontSize(12);
local localizedHeaderSeparator = localizedDialog:NewPanel(vec.New(0, -54), vec.New(760, 1), 2i, "DebugDarkGrey");
localizedHeaderSeparator:SetAnchor(vec.New(0, 1));
localizedHeaderSeparator:SetRelativePoint(vec.New(0, 1));
localizedHeaderSeparator:SetColor(editor.colors.border);
localizedHeaderSeparator:SetBorderSize(0);
local localizedFooterSeparator = localizedDialog:NewPanel(vec.New(0, 46), vec.New(760, 1), 2i, "DebugDarkGrey");
localizedFooterSeparator:SetAnchor(vec.New(0, 0));
localizedFooterSeparator:SetRelativePoint(vec.New(0, 0));
localizedFooterSeparator:SetColor(editor.colors.border);
localizedFooterSeparator:SetBorderSize(0);
localizedText.closeButton = Button.New(localizedDialog, vec.New(-10, -10), vec.New(28, 28), 2i, { text = "x", textTemplate = "DefaultButtonText" });
localizedText.closeButton:SetAnchor(vec.New(1, 1));
localizedText.closeButton:SetRelativePoint(vec.New(1, 1));
localizedText.closeButton:SetFontSize(9);

local function NewLocalizedLabel(text : string, position : vec)
    local label = localizedDialog:NewText(text, position, 2i, "DefaultButtonText");
    label:SetAnchor(vec.New(0, 1));
    label:SetRelativePoint(vec.New(0, 1));
    label:SetFontSize(8);
    label:SetColor(editor.colors.muted);
    return label;
end

NewLocalizedLabel("Internal text key", vec.New(12, -69));
localizedText.internalNameInput = InputField.New(localizedDialog, vec.New(12, -83), vec.New(736, 28), 2i, { defaultTextFontSize = 10, placeholderText = "Internal key", textInsetX = 7 });
localizedText.internalNameInput:SetAnchor(vec.New(0, 1));
localizedText.internalNameInput:SetRelativePoint(vec.New(0, 1));
localizedText.internalNameInput:SetTextVerticallyCentered();
local localizedSourceLabel = NewLocalizedLabel("enUS *", vec.New(12, -128));
localizedSourceLabel:SetColor(editor.colors.accent);
localizedText.englishInput = InputField.New(localizedDialog, vec.New(130, -120), vec.New(618, 64), 2i, { defaultTextFontSize = 10, placeholderText = "English source text", multiline = true, textInsetX = 7 });
localizedText.englishInput:SetAnchor(vec.New(0, 1));
localizedText.englishInput:SetRelativePoint(vec.New(0, 1));
localizedText.statusText = localizedDialog:NewText("", vec.New(12, 18), 2i, "DefaultButtonText");
localizedText.statusText:SetAnchor(vec.New(0, 0));
localizedText.statusText:SetRelativePoint(vec.New(0, 0));
localizedText.statusText:SetFontSize(8);
localizedText.statusText:SetColor(editor.colors.muted);
localizedText.cancelButton = Button.New(localizedDialog, vec.New(-102, 10), vec.New(76, 28), 2i, { text = "Cancel", textTemplate = "DefaultButtonText" });
localizedText.cancelButton:SetAnchor(vec.New(1, 0));
localizedText.cancelButton:SetRelativePoint(vec.New(1, 0));
localizedText.cancelButton:SetFontSize(10);
localizedText.saveButton = Button.New(localizedDialog, vec.New(-12, 10), vec.New(84, 28), 2i, { text = "Save Text", textTemplate = "DefaultButtonText", color = editor.colors.selected, hoverColor = vec.New(0.18, 0.34, 0.47) });
localizedText.saveButton:SetAnchor(vec.New(1, 0));
localizedText.saveButton:SetRelativePoint(vec.New(1, 0));
localizedText.saveButton:SetFontSize(10);

local function SetLocalizedTextStatus(message : string, color : vec)
    localizedText.statusText:SetText(message);
    localizedText.statusText:SetColor(color);
end

local function GetLocalizedTranslation(textID : integer, locale : integer)
    for _, translation in editor.state.snapshot.translations do
        if translation.textID == textID and translation.locale == locale then return translation; end
    end
    return nil;
end

local function UpsertLocalizedText(textID : integer, internalName : string, englishValue : string, translatorContext : string)
    local text = FindText(textID);
    if text == nil then
        table.insert(editor.state.snapshot.texts, { id = textID, internalName = internalName, englishValue = englishValue, translatorContext = translatorContext });
        return;
    end

    text.internalName = internalName;
    text.englishValue = englishValue;
    text.translatorContext = translatorContext;
end

local function UpsertLocalizedTranslation(textID : integer, locale : integer, value : string)
    local translation = GetLocalizedTranslation(textID, locale);
    if translation == nil then
        table.insert(editor.state.snapshot.translations, { textID = textID, locale = locale, value = value });
    else
        translation.value = value;
    end
end

local function RemoveLocalizedTranslation(textID : integer, locale : integer)
    for index, translation in editor.state.snapshot.translations do
        if translation.textID == textID and translation.locale == locale then
            table.remove(editor.state.snapshot.translations, index);
            return;
        end
    end
end

local function IsLocalizedTextDirty() : boolean
    if localizedText.editingID == nil then return true; end
    local baseline = localizedText.baselineText;
    if baseline == nil or localizedText.internalNameInput:GetText() ~= baseline.internalName or localizedText.englishInput:GetText() ~= baseline.englishValue then return true; end
    for locale, input in localizedText.translationInputs do
        if input:GetText() ~= (localizedText.baselineTranslations[locale] or "") then return true; end
    end
    return false;
end

local function RefreshLocalizedTextActions()
    local busy = localizedText.pendingRequestID ~= 0i;
    local valid = string.match(localizedText.internalNameInput:GetText(), "%S") ~= nil and string.match(localizedText.englishInput:GetText(), "%S") ~= nil;
    localizedText.internalNameInput.button:SetInteractable(not busy);
    localizedText.englishInput.button:SetInteractable(not busy);
    for _, input in localizedText.translationInputs do input.button:SetInteractable(not busy); end
    localizedText.saveButton:SetInteractable(not busy and valid and IsLocalizedTextDirty());
    localizedText.cancelButton:SetInteractable(not busy);
    localizedText.closeButton:SetInteractable(not busy);
    if not busy then
        if not valid then SetLocalizedTextStatus("Internal key and enUS source are required.", editor.colors.error);
        elseif IsLocalizedTextDirty() then SetLocalizedTextStatus("Unsaved localized-text changes", editor.colors.warning);
        else SetLocalizedTextStatus("Localized text matches authoritative data", editor.colors.muted); end
    end
end

local function BuildLocalizedTranslationRows(textID : integer?)
    for _, row in localizedText.translationRows do
        row.label:SetVisible(false);
        row.input.root:SetVisible(false);
    end
    localizedText.translationInputs = { };
    localizedText.baselineTranslations = { };
    localizedText.dialog:SetSize(vec.New(760, 244 + math.max(1, #editor.state.snapshot.locales) * 72));
    local inputY = -194;
    for index, locale in editor.state.snapshot.locales do
        local localeName = string.lower(string.sub(locale.name, 1, 1)) .. string.sub(locale.name, 2);
        local translation = if textID == nil then nil else GetLocalizedTranslation(textID, locale.value);
        local value = if translation == nil then "" else translation.value;
        localizedText.baselineTranslations[locale.value] = value;
        local row = localizedText.translationRows[locale.value];
        if row == nil then
            local label = NewLocalizedLabel(localeName, vec.New(12, inputY - 12));
            local input = InputField.New(localizedDialog, vec.New(130, inputY), vec.New(618, 64), 2i, { defaultTextFontSize = 10, placeholderText = "No translation yet", placeholderTextColor = editor.colors.muted, multiline = true, textInsetX = 7 });
            input:SetAnchor(vec.New(0, 1));
            input:SetRelativePoint(vec.New(0, 1));
            input:SetOnTextChanged(function() RefreshLocalizedTextActions(); end);
            row = { label = label, input = input };
            localizedText.translationRows[locale.value] = row;
        end
        row.label:SetText(localeName);
        row.label:SetPos(vec.New(12, inputY - 12));
        row.label:SetVisible(true);
        row.input:SetPos(vec.New(130, inputY));
        row.input:SetText(value);
        row.input.root:SetVisible(true);
        localizedText.translationInputs[locale.value] = row.input;
        inputY = -194 - index * 72;
    end
end

local function SetLocalizedTextOpen(open : boolean)
    localizedText.isOpen = open;
    localizedText.root:SetEnabled(open);
end

local function CompleteLocalizedTextSave()
    local textID = localizedText.activeTextID;
    if textID == nil or textID == 0i then
        SetLocalizedTextStatus("The saved localized-text ID is unavailable.", editor.colors.error);
        return;
    end

    local assigned = false;
    if editor.state.model ~= nil and localizedText.targetKind == "Quest" and editor.state.model:GetQuest(localizedText.targetID) ~= nil then
        editor.state.model:SetQuestFields(localizedText.targetID, { [localizedText.targetField] = textID });
        assigned = true;
    elseif editor.state.model ~= nil and localizedText.targetKind == "Objective" then
        local quest = GetSelectedQuest();
        if quest ~= nil and FindByID(editor.state.model:GetObjectives(quest.id), localizedText.targetID) ~= nil then
            editor.state.model:SetObjectiveFields(localizedText.targetID, { [localizedText.targetField] = textID });
            assigned = true;
        end
    end

    editor.state.controller:SetStatus(if assigned then "Localized text saved and assigned." else "Localized text saved, but its target is no longer available.", if assigned then STATUS.Success else STATUS.Warning);
    SetLocalizedTextOpen(false);
    RefreshAll(true);
end

local function SubmitNextLocalizedTextOperation()
    localizedText.operationIndex += 1;
    local operation = localizedText.operations[localizedText.operationIndex];
    if operation == nil then CompleteLocalizedTextSave(); return; end
    local textID = localizedText.activeTextID;
    if textID == nil or textID == 0i then RefreshLocalizedTextActions(); SetLocalizedTextStatus("The localized-text ID is unavailable.", editor.colors.error); return; end
    local requestID = 0i;
    if operation.kind == "updateText" then requestID = InteractionEditor.UpdateText(textID, operation.internalName, operation.englishValue, operation.translatorContext);
    elseif operation.kind == "createTranslation" then requestID = InteractionEditor.CreateTranslation(textID, operation.locale, operation.value);
    elseif operation.kind == "updateTranslation" then requestID = InteractionEditor.UpdateTranslation(textID, operation.locale, operation.value);
    elseif operation.kind == "deleteTranslation" then requestID = InteractionEditor.DeleteTranslation(textID, operation.locale); end
    localizedText.pendingRequestID = requestID;
    RefreshLocalizedTextActions();
    if requestID == 0i then SetLocalizedTextStatus("The localized-text mutation could not be submitted.", editor.colors.error); end
end

local function BeginLocalizedTextSave()
    if localizedText.pendingRequestID ~= 0i then return; end
    local internalName = localizedText.internalNameInput:GetText();
    local englishValue = localizedText.englishInput:GetText();
    local translatorContext = localizedText.translatorContext;
    if string.match(internalName, "%S") == nil or string.match(englishValue, "%S") == nil then RefreshLocalizedTextActions(); return; end

    localizedText.operations = { };
    localizedText.operationIndex = 0;
    local baseline = localizedText.baselineText;
    if localizedText.editingID ~= nil and (baseline == nil or internalName ~= baseline.internalName or englishValue ~= baseline.englishValue or translatorContext ~= baseline.translatorContext) then
        table.insert(localizedText.operations, { kind = "updateText", internalName = internalName, englishValue = englishValue, translatorContext = translatorContext });
    end
    for locale, input in localizedText.translationInputs do
        local value = input:GetText();
        local previous = localizedText.baselineTranslations[locale] or "";
        if value ~= previous then
            if value == "" then table.insert(localizedText.operations, { kind = "deleteTranslation", locale = locale });
            elseif previous == "" then table.insert(localizedText.operations, { kind = "createTranslation", locale = locale, value = value });
            else table.insert(localizedText.operations, { kind = "updateTranslation", locale = locale, value = value }); end
        end
    end

    SetLocalizedTextStatus("Saving localized text...", editor.colors.muted);
    if localizedText.editingID == nil then
        localizedText.awaitingCreate = true;
        localizedText.pendingRequestID = InteractionEditor.CreateText(internalName, englishValue, translatorContext);
        if localizedText.pendingRequestID == 0i then localizedText.awaitingCreate = false; end
        RefreshLocalizedTextActions();
        if localizedText.pendingRequestID == 0i then SetLocalizedTextStatus("The localized text could not be created.", editor.colors.error); end
    else
        localizedText.activeTextID = localizedText.editingID;
        SubmitNextLocalizedTextOperation();
    end
end

local function ApplySuccessfulLocalizedOperation(operation : any)
    if operation == nil then return; end
    local textID = localizedText.activeTextID;
    if textID == nil then return; end
    if operation.kind == "updateText" then
        localizedText.baselineText = { internalName = operation.internalName, englishValue = operation.englishValue, translatorContext = operation.translatorContext };
        UpsertLocalizedText(textID, operation.internalName, operation.englishValue, operation.translatorContext);
    elseif operation.kind == "deleteTranslation" then
        localizedText.baselineTranslations[operation.locale] = "";
        RemoveLocalizedTranslation(textID, operation.locale);
    else
        localizedText.baselineTranslations[operation.locale] = operation.value;
        UpsertLocalizedTranslation(textID, operation.locale, operation.value);
    end
end

local function UpdateLocalizedTextSave() : boolean
    if localizedText.pendingRequestID == 0i then return false; end
    local result = InteractionEditor.TakeMutationResult(localizedText.pendingRequestID);
    if result == nil then return false; end
    localizedText.pendingRequestID = 0i;
    if not result.succeeded then
        localizedText.awaitingCreate = false;
        localizedText.operations = { };
        RefreshLocalizedTextActions();
        SetLocalizedTextStatus(if result.response ~= "" then result.response else "The localized-text mutation failed.", editor.colors.error);
        return true;
    end

    if localizedText.awaitingCreate then
        localizedText.awaitingCreate = false;
        localizedText.activeTextID = result.artifactID;
        localizedText.editingID = result.artifactID;
        localizedText.baselineText = { internalName = localizedText.internalNameInput:GetText(), englishValue = localizedText.englishInput:GetText(), translatorContext = localizedText.translatorContext };
        UpsertLocalizedText(result.artifactID, localizedText.baselineText.internalName, localizedText.baselineText.englishValue, localizedText.baselineText.translatorContext);
    else
        ApplySuccessfulLocalizedOperation(localizedText.operations[localizedText.operationIndex]);
    end
    SubmitNextLocalizedTextOperation();
    return true;
end

OpenLocalizedTextModal = function(textID : integer?, targetKind : string, targetID : integer, targetField : string)
    if localizedText.pendingRequestID ~= 0i then return; end
    local text = if textID == nil then nil else FindText(textID);
    localizedText.editingID = if text == nil then nil else text.id;
    localizedText.activeTextID = localizedText.editingID;
    localizedText.targetKind = targetKind;
    localizedText.targetID = targetID;
    localizedText.targetField = targetField;
    localizedText.awaitingCreate = false;
    localizedText.operations = { };
    localizedText.operationIndex = 0;
    localizedText.baselineText = if text == nil then nil else { internalName = text.internalName, englishValue = text.englishValue, translatorContext = text.translatorContext or "" };
    localizedText.title:SetText(if text == nil then "New localized text" else text.internalName);
    localizedText.internalNameInput:SetText(if text == nil then "" else text.internalName);
    localizedText.englishInput:SetText(if text == nil then "" else text.englishValue);
    localizedText.translatorContext = if text == nil then "" else text.translatorContext or "";
    BuildLocalizedTranslationRows(localizedText.editingID);
    RefreshLocalizedTextActions();
    SetLocalizedTextOpen(true);
end

localizedText.internalNameInput:SetOnTextChanged(function() RefreshLocalizedTextActions(); end);
localizedText.englishInput:SetOnTextChanged(function() RefreshLocalizedTextActions(); end);
localizedText.saveButton:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i then BeginLocalizedTextSave(); end end);
localizedText.cancelButton:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i and localizedText.pendingRequestID == 0i then SetLocalizedTextOpen(false); end end);
localizedText.closeButton:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i and localizedText.pendingRequestID == 0i then SetLocalizedTextOpen(false); end end);

QuestEditorView = view;
editor.refs = view.refs;
Draggable.Attach(view.root, view:Get("dragHandle"));
Draggable.Attach(view.root, view:Get("topbar"));
editorTooltip = GameTooltip.New(view.root);
view.root:SetVisible(false);

local methods = { };
function methods:Show()
    editor.state.visible = true;
    view.root:SetVisible(true);
    editor.state.snapshot = editor.state.controller:GetSnapshot();
    if editor.state.snapshot.dataState == "Ready" then
        ApplySnapshot();
    else
        editor.state.controller:RequestSnapshot();
    end
end

function methods:Hide()
    local function hide()
        editor.state.visible = false;
        itemPicker:Close();
        currencyPicker:Close();
        factionPicker:Close();
        creaturePicker:Close();
        questPicker:Close();
        SetLocalizedTextOpen(false);
        editorTooltip.ClearLines();
        editorTooltip.Hide();
        view.root:SetVisible(false);
    end

    if localizedText.pendingRequestID ~= 0i then
        editor.state.controller:SetStatus("Wait for the localized-text save to finish before closing.", STATUS.Warning);
        UpdateToolbar();
        return;
    end

    if editor.state.model ~= nil and editor.state.model:IsDirty() then
        closeModal:Open({ message = "Close with unsaved quest changes?", onConfirm = hide });
    else
        hide();
    end
end

function methods:IsVisible()
    return view.root:IsVisible();
end

EditorRegistry.Register("QuestEditor", methods);

RegisterEvent(GameEvent.Updated, function()
    if not editor.state.visible then
        return;
    end

    local localizedTextMutationCompleted = UpdateLocalizedTextSave();
    if editor.state.controller:Update() then
        if localizedTextMutationCompleted then
            UpdateToolbar();
        elseif editor.state.applyOperations ~= nil or (editor.state.model ~= nil and editor.state.model:IsDirty()) then
            editor.state.pendingSnapshot = true;
            UpdateToolbar();
        else
            ApplySnapshot();
        end
    end

    local result = editor.state.controller:TakeCompletedResult();
    if result == nil then
        return;
    end

    if editor.state.applyOperations ~= nil then
        ContinueApply(result);
    elseif result.succeeded then
        editor.state.selectedQuestID = nil;
        editor.state.controller:SelectQuest(nil);
        editor.state.controller:RequestSnapshot();
    else
        UpdateToolbar();
    end
end);

if editor.state.snapshot.dataState == "Ready" then
    ApplySnapshot();
else
    RefreshCenter();
    RefreshInspector();
    UpdateToolbar();
end
