local Button = require("@src/API/UI/Button");
local DragInput = require("@src/API/UI/DragInput");
local BrowserList = require("@src/API/Editor/UI/BrowserList");
local EditorRegistry = require("@src/API/Editor/Registry");
local BrowserWindow = require("@src/API/Editor/UI/BrowserWindow");
local InputField = require("@src/API/Editor/UI/InputField");
local GridLayout = require("@src/API/UI/GridLayout");
local HorizontalLayout = require("@src/API/UI/HorizontalLayout");
local TextUtil = require("@src/API/UI/TextUtil");
local Theme = require("@src/API/Editor/UI/Theme");

type MapRecord =
{
    id : integer,
    flags : integer,
    internalName : string,
    name : string,
    type : integer,
    maxPlayers : integer,
    isCreate : boolean?,
}

local WINDOW_WIDTH = 860;
local WINDOW_HEIGHT = 570;
local TITLE_HEIGHT = 22;
local TOOLBAR_HEIGHT = 24;
local LEFT_WIDTH = 310;
local ROW_HEIGHT = 44;
local LAYER = 1100i;
local TEXT = Theme.textTemplate;

local colors = {
    text = vec.New(0.90, 0.92, 0.96),
    muted = vec.New(0.58, 0.62, 0.69),
    success = vec.New(0.38, 0.82, 0.52),
    warning = vec.New(0.94, 0.72, 0.30),
    error = vec.New(0.94, 0.38, 0.38),
};

local function ToInteger(value : number) : integer
    local truncated = if value < 0 then math.ceil(value) else math.floor(value);
    return integer.create(truncated);
end

local function CopyMap(source : MapRecord) : MapRecord
    return {
        id = source.id,
        flags = source.flags,
        internalName = source.internalName,
        name = source.name,
        type = source.type,
        maxPlayers = source.maxPlayers,
        isCreate = source.isCreate,
    };
end

local function SameMap(left : MapRecord?, right : MapRecord?) : boolean
    return left ~= nil and right ~= nil and left.id == right.id and left.flags == right.flags and
        left.internalName == right.internalName and left.name == right.name and left.type == right.type and
        left.maxPlayers == right.maxPlayers and left.isCreate == right.isCreate;
end

local canvas = UI.GetCanvas("MapDatabaseEditor", vec.New(0, 0), vec.New(1920, 1080), false);
local shell = BrowserWindow.New(canvas, vec.New(510, -100), vec.New(WINDOW_WIDTH, WINDOW_HEIGHT), LAYER, {
    title = "Map Editor",
    titleHeight = TITLE_HEIGHT,
    toolbarHeight = TOOLBAR_HEIGHT,
    browserWidth = LEFT_WIDTH,
});
local window = shell.window;
local root = shell.root;
local toolbar = shell.toolbar;
local browser = shell.browser;
local inspector = shell.inspector;
local BODY_HEIGHT = shell.bodyHeight;
local RIGHT_WIDTH = shell.inspectorWidth;

local refreshButton = Button.New(toolbar, vec.New(0, 0), vec.New(74, 22), 1i, { text = "Refresh", textTemplate = TEXT });
local createButton = Button.New(toolbar, vec.New(0, 0), vec.New(94, 22), 1i, { text = "New Map", textTemplate = TEXT });
local searchInput = InputField.New(toolbar, vec.New(0, 0), vec.New(300, 22), 1i, {
    textTemplate = TEXT,
    placeholderText = "Search ID, internal name, or display name",
});
toolbar:SetChildOpts(refreshButton, { sizeX = 74, sizeY = 22 });
toolbar:SetChildOpts(createButton, { sizeX = 94, sizeY = 22 });
toolbar:SetChildOpts(searchInput, { modeX = "flex", sizeY = 22 });

local browserTitle = browser:NewText("Authoritative Maps", vec.New(0, 0), 1i, TEXT);
browserTitle:SetColor(Theme.selectedColor);
browser:SetChildOpts(browserTitle, { sizeY = 18 });
local browserState = browser:NewText("Map data has not been requested.", vec.New(0, 0), 1i, TEXT);
browserState:SetColor(colors.muted);
browser:SetChildOpts(browserState, { sizeY = 18 });

local mapList : any = nil;
local selectedMapID : integer? = nil;
local snapshot = { dataState = "Unavailable", maps = {} };
local visibleMaps : { MapRecord } = {};
local draft : MapRecord? = nil;
local baseline : MapRecord? = nil;
local pendingRequestID = 0i;
local pendingMutation = "";
local awaitingSnapshot = false;
local selectAfterSnapshot : integer? = nil;
local statusMessage = "Select a map to edit or create a new definition.";
local statusColor = colors.muted;
local refreshingFields = false;

local function IsDirty() : boolean
    return draft ~= nil and not SameMap(draft, baseline);
end

local RefreshBrowser : (boolean?) -> () = function(_) end;
local RefreshInspector : () -> () = function() end;
local ApplySnapshot : (boolean?) -> () = function(_) end;

mapList = BrowserList.New(browser, vec.New(0, 0), vec.New(LEFT_WIDTH - 16, BODY_HEIGHT - 68), 1i, {
    rowHeight = ROW_HEIGHT,
    textTemplate = TEXT,
    getPrimaryText = function(map : MapRecord) : string return map.name; end,
    getSecondaryText = function(map : MapRecord) : string return `#{map.id}  {map.internalName}`; end,
    isSelected = function(map : MapRecord) : boolean return selectedMapID == map.id; end,
    onActivate = function(map : MapRecord)
        if pendingRequestID ~= 0i or awaitingSnapshot then return; end
        if IsDirty() then
            statusMessage = "Save or revert the current draft before selecting another map.";
            statusColor = colors.warning;
            RefreshInspector();
            return;
        end
        selectedMapID = map.id;
        draft = CopyMap(map);
        baseline = CopyMap(map);
        statusMessage = "Editing the authoritative map snapshot.";
        statusColor = colors.muted;
        mapList:Refresh();
        RefreshInspector();
    end,
});
browser:SetChildOpts(mapList, { modeX = "flex", modeY = "flex" });

local inspectorTitle = inspector:NewText("Map Definition", vec.New(0, 0), 1i, TEXT);
inspectorTitle:SetColor(Theme.selectedColor);
inspector:SetChildOpts(inspectorTitle, { sizeY = 20 });

local form = GridLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 210), 1i, {
    columns = 2,
    cellSizeX = (RIGHT_WIDTH - 28) / 2,
    cellSizeY = 30,
    spacing = 4,
    padding = 2,
    cellAlignH = "stretch",
    cellAlignV = "center",
    panelTemplate = "DebugDarkGrey",
    alpha = 0.0,
});
inspector:SetChildOpts(form, { modeX = "flex", sizeY = 210 });

form:NewText("ID (assigned by server)", vec.New(0, 0), 1i, TEXT);
local idValue = form:NewText("No map selected", vec.New(0, 0), 1i, TEXT);
idValue:SetColor(colors.muted);
form:NewText("Internal name *", vec.New(0, 0), 1i, TEXT);
local internalNameInput = InputField.New(form, vec.New(0, 0), vec.New(238, 24), 1i, { textTemplate = TEXT });
form:NewText("Display name *", vec.New(0, 0), 1i, TEXT);
local nameInput = InputField.New(form, vec.New(0, 0), vec.New(238, 24), 1i, { textTemplate = TEXT });
form:NewText("Flags (u32)", vec.New(0, 0), 1i, TEXT);
local flagsInput = DragInput.New(form, vec.New(0, 0), vec.New(238, 24), 1i, { min = 0, max = 4294967295, speed = 1, format = "%.0f" });
form:NewText("Type (u8)", vec.New(0, 0), 1i, TEXT);
local typeInput = DragInput.New(form, vec.New(0, 0), vec.New(238, 24), 1i, { min = 0, max = 255, speed = 1, format = "%.0f" });
form:NewText("Maximum players (u16)", vec.New(0, 0), 1i, TEXT);
local maxPlayersInput = DragInput.New(form, vec.New(0, 0), vec.New(238, 24), 1i, { min = 0, max = 65535, speed = 1, format = "%.0f" });
InputField.Style(flagsInput.input);
InputField.Style(typeInput.input);
InputField.Style(maxPlayersInput.input);

local validationText = inspector:NewText("", vec.New(0, 0), 1i, TEXT);
validationText:SetColor(colors.error);
validationText:SetWrapWidth(RIGHT_WIDTH - 20);
inspector:SetChildOpts(validationText, { sizeY = 42 });

local actions = HorizontalLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 26), 1i, {
    panelTemplate = "DebugDarkGrey",
    alpha = 0.0,
    spacing = 6,
});
inspector:SetChildOpts(actions, { modeX = "flex", sizeY = 26 });
local saveButton = Button.New(actions, vec.New(0, 0), vec.New(110, 24), 1i, { text = "Save", textTemplate = TEXT });
local revertButton = Button.New(actions, vec.New(0, 0), vec.New(110, 24), 1i, { text = "Revert", textTemplate = TEXT });
actions:SetChildOpts(saveButton, { sizeX = 110, sizeY = 24 });
actions:SetChildOpts(revertButton, { sizeX = 110, sizeY = 24 });

local statusText = inspector:NewText(statusMessage, vec.New(0, 0), 1i, TEXT);
statusText:SetWrapWidth(RIGHT_WIDTH - 20);
statusText:SetColor(statusColor);
inspector:SetChildOpts(statusText, { modeX = "flex", sizeY = 58 });

local function ValidateDraft() : string?
    if draft == nil then return "Select a map or create a new one."; end
    if draft.internalName == "" then return "Internal name is required."; end
    if draft.name == "" then return "Display name is required."; end
    if #draft.internalName > 255 then return "Internal name must be at most 255 characters."; end
    if #draft.name > 255 then return "Display name must be at most 255 characters."; end
    if integer.lt(draft.flags, 0i) then return "Flags must be between 0 and 4294967295."; end
    if integer.lt(draft.type, 0i) or integer.gt(draft.type, 255i) then return "Type must be between 0 and 255."; end
    if integer.lt(draft.maxPlayers, 0i) or integer.gt(draft.maxPlayers, 65535i) then return "Maximum players must be between 0 and 65535."; end
    return nil;
end

local function RefreshActionState()
    local ready = snapshot.dataState == "Ready";
    local busy = pendingRequestID ~= 0i or awaitingSnapshot;
    createButton:SetInteractable(ready and not busy);
    refreshButton:SetInteractable(not busy and snapshot.dataState ~= "Loading");
    saveButton:SetInteractable(ready and draft ~= nil and IsDirty() and not busy and ValidateDraft() == nil);
    revertButton:SetInteractable(draft ~= nil and IsDirty() and not busy);
end

RefreshInspector = function()
    refreshingFields = true;
    if draft == nil then
        inspectorTitle:SetText("Map Definition");
        idValue:SetText("No map selected");
        internalNameInput:SetText("");
        nameInput:SetText("");
        flagsInput:SetValue(0, false);
        typeInput:SetValue(0, false);
        maxPlayersInput:SetValue(0, false);
    else
        inspectorTitle:SetText(if draft.isCreate then "Create Map Definition" else `Edit Map #{draft.id}`);
        idValue:SetText(if draft.isCreate then "Assigned after creation" else tostring(draft.id));
        internalNameInput:SetText(draft.internalName);
        nameInput:SetText(draft.name);
        flagsInput:SetValue(integer.tonumber(draft.flags), false);
        typeInput:SetValue(integer.tonumber(draft.type), false);
        maxPlayersInput:SetValue(integer.tonumber(draft.maxPlayers), false);
    end
    refreshingFields = false;

    local validation = ValidateDraft();
    validationText:SetText(validation or (if IsDirty() then "Unsaved local changes." else ""));
    validationText:SetColor(if validation then colors.error else colors.warning);
    statusText:SetText(statusMessage);
    statusText:SetColor(statusColor);
    RefreshActionState();
end

RefreshBrowser = function(preserveScroll : boolean?)
    visibleMaps = {};
    local filter = string.lower(searchInput.text or "");
    for _, map in snapshot.maps do
        if filter == "" or TextUtil.ContainsIgnoreCase(map.internalName, filter) or TextUtil.ContainsIgnoreCase(map.name, filter) or
            TextUtil.ContainsIgnoreCase(tostring(map.id), filter)
        then
            table.insert(visibleMaps, map);
        end
    end
    table.sort(visibleMaps, function(left : MapRecord, right : MapRecord) : boolean return integer.lt(left.id, right.id); end);
    mapList:SetItems(visibleMaps, preserveScroll);

    if snapshot.dataState == "Loading" then
        browserState:SetText("Loading authoritative map data...");
    elseif snapshot.dataState == "Failed" then
        browserState:SetText("Unavailable or map.view was denied.");
    elseif snapshot.dataState ~= "Ready" then
        browserState:SetText("Map data is unavailable while disconnected.");
    elseif #snapshot.maps == 0 then
        browserState:SetText("The authoritative map catalog is empty.");
    else
        browserState:SetText(`{#visibleMaps} shown / {#snapshot.maps} total`);
    end
end

local function SelectSnapshotMap(mapID : integer) : boolean
    for _, map in snapshot.maps do
        if map.id == mapID then
            selectedMapID = map.id;
            draft = CopyMap(map);
            baseline = CopyMap(map);
            return true;
        end
    end
    return false;
end

ApplySnapshot = function(preserveScroll : boolean?)
    snapshot = MapEditor.GetSnapshot();
    if awaitingSnapshot and snapshot.dataState == "Ready" then
        awaitingSnapshot = false;
        if selectAfterSnapshot ~= nil and SelectSnapshotMap(selectAfterSnapshot) then
            statusMessage = if pendingMutation == "Create" then `Map #{selectAfterSnapshot} was created and selected.` else "Map changes were saved.";
            statusColor = colors.success;
        else
            statusMessage = "The mutation succeeded, but the refreshed map could not be found.";
            statusColor = colors.warning;
        end
        selectAfterSnapshot = nil;
        pendingMutation = "";
    elseif awaitingSnapshot and snapshot.dataState == "Failed" then
        awaitingSnapshot = false;
        statusMessage = "The mutation succeeded, but refreshing the authoritative snapshot failed.";
        statusColor = colors.warning;
    end
    RefreshBrowser(preserveScroll);
    RefreshInspector();
end

internalNameInput:SetOnTextChanged(function(input)
    if refreshingFields or draft == nil then return; end
    draft.internalName = input.text;
    RefreshInspector();
end);
nameInput:SetOnTextChanged(function(input)
    if refreshingFields or draft == nil then return; end
    draft.name = input.text;
    RefreshInspector();
end);
flagsInput.onChanged = function(value)
    if draft == nil then return; end
    draft.flags = ToInteger(value);
    RefreshInspector();
end;
typeInput.onChanged = function(value)
    if draft == nil then return; end
    draft.type = ToInteger(value);
    RefreshInspector();
end;
maxPlayersInput.onChanged = function(value)
    if draft == nil then return; end
    draft.maxPlayers = ToInteger(value);
    RefreshInspector();
end;

searchInput:SetOnTextChanged(function() RefreshBrowser(); end);
refreshButton:SetOnMouseUp(function()
    if MapEditor.RequestSnapshot() then
        statusMessage = "Refreshing authoritative map data...";
        statusColor = colors.muted;
        ApplySnapshot(true);
    else
        statusMessage = "Map data cannot be requested while disconnected or outside the world.";
        statusColor = colors.error;
        RefreshInspector();
    end
end);
createButton:SetOnMouseUp(function()
    if IsDirty() then
        statusMessage = "Save or revert the current draft before creating a map.";
        statusColor = colors.warning;
        RefreshInspector();
        return;
    end
    selectedMapID = nil;
    draft = { id = 0i, flags = 0i, internalName = "", name = "", type = 0i, maxPlayers = 0i, isCreate = true };
    baseline = { id = 0i, flags = 0i, internalName = "", name = "", type = 0i, maxPlayers = 0i, isCreate = true };
    statusMessage = "The server will assign the new map ID.";
    statusColor = colors.muted;
    mapList:Refresh();
    RefreshInspector();
end);
revertButton:SetOnMouseUp(function()
    if baseline == nil then return; end
    draft = CopyMap(baseline);
    statusMessage = "Local changes were reverted.";
    statusColor = colors.muted;
    RefreshInspector();
end);
saveButton:SetOnMouseUp(function()
    if draft == nil or pendingRequestID ~= 0i or awaitingSnapshot then return; end
    local validation = ValidateDraft();
    if validation ~= nil then
        statusMessage = validation;
        statusColor = colors.error;
        RefreshInspector();
        return;
    end
    if not IsDirty() then
        statusMessage = "No changes to save.";
        statusColor = colors.muted;
        RefreshInspector();
        return;
    end

    pendingMutation = if draft.isCreate then "Create" else "Update";
    pendingRequestID = if draft.isCreate
        then MapEditor.Create(draft.flags, draft.internalName, draft.name, draft.type, draft.maxPlayers)
        else MapEditor.Update(draft.id, draft.flags, draft.internalName, draft.name, draft.type, draft.maxPlayers);
    if pendingRequestID == 0i then
        pendingMutation = "";
        statusMessage = "The mutation could not be submitted. Refresh the snapshot and try again.";
        statusColor = colors.error;
    else
        statusMessage = `Waiting for server response (request #{pendingRequestID})...`;
        statusColor = colors.muted;
    end
    RefreshInspector();
end);

local methods = {};

function methods:Show()
    root:SetVisible(true);
    snapshot = MapEditor.GetSnapshot();
    if snapshot.dataState == "Unavailable" or snapshot.dataState == "Failed" then
        MapEditor.RequestSnapshot();
        snapshot = MapEditor.GetSnapshot();
    end
    ApplySnapshot();
end

function methods:Hide()
    root:SetVisible(false);
end

function methods:IsVisible() : boolean
    return root:IsVisible();
end

window.closeButton:SetOnMouseUp(function() methods:Hide(); end);
EditorRegistry.Register("MapEditor", methods);

local lastDataState = "Unavailable";
RegisterEvent(GameEvent.Updated, function()
    if not root:IsVisible() then return; end

    if pendingRequestID ~= 0i then
        local result = MapEditor.TakeMutationResult(pendingRequestID);
        if result ~= nil then
            pendingRequestID = 0i;
            if result.succeeded then
                selectAfterSnapshot = result.artifactID;
                awaitingSnapshot = MapEditor.RequestSnapshot();
                statusMessage = if awaitingSnapshot then "Mutation accepted; refreshing authoritative map data..." else "Mutation accepted, but the snapshot refresh could not start.";
                statusColor = if awaitingSnapshot then colors.success else colors.warning;
            else
                statusMessage = if result.response ~= "" then result.response else "The server rejected the map mutation.";
                statusColor = colors.error;
                pendingMutation = "";
            end
            RefreshInspector();
        end
    end

    local currentState = MapEditor.GetState();
    if currentState ~= lastDataState or (awaitingSnapshot and currentState == "Ready") then
        lastDataState = currentState;
        ApplySnapshot(true);
    end
end);

RefreshBrowser(false);
RefreshInspector();
