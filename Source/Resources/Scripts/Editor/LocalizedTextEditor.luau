local BrowserList = require("@src/API/Editor/UI/BrowserList");
local BrowserWindow = require("@src/API/Editor/UI/BrowserWindow");
local Button = require("@src/API/UI/Button");
local Dropdown = require("@src/API/UI/Dropdown");
local EditorRegistry = require("@src/API/Editor/Registry");
local GridLayout = require("@src/API/UI/GridLayout");
local HorizontalLayout = require("@src/API/UI/HorizontalLayout");
local InputField = require("@src/API/Editor/UI/InputField");
local TextUtil = require("@src/API/UI/TextUtil");
local Theme = require("@src/API/Editor/UI/Theme");

type TextEntry =
{
    id : integer,
    internalName : string,
    englishValue : string,
    translatorContext : string,
}

type Translation =
{
    textID : integer,
    locale : integer,
    value : string,
}

local WINDOW_WIDTH = 900;
local WINDOW_HEIGHT = 650;
local LEFT_WIDTH = 330;
local LAYER = 1150i;
local TEXT = Theme.textTemplate;

local colors = {
    muted = vec.New(0.58, 0.62, 0.69),
    success = vec.New(0.38, 0.82, 0.52),
    warning = vec.New(0.94, 0.72, 0.30),
    error = vec.New(0.94, 0.38, 0.38),
};

local function CopyText(value : TextEntry) : TextEntry
    return { id = value.id, internalName = value.internalName, englishValue = value.englishValue, translatorContext = value.translatorContext };
end

local function SameText(left : TextEntry?, right : TextEntry?) : boolean
    return left ~= nil and right ~= nil and left.id == right.id and left.internalName == right.internalName and
        left.englishValue == right.englishValue and left.translatorContext == right.translatorContext;
end

local canvas = UI.GetCanvas("LocalizedTextEditor", vec.New(0, 0), vec.New(1920, 1080), false);
local shell = BrowserWindow.New(canvas, vec.New(510, -100), vec.New(WINDOW_WIDTH, WINDOW_HEIGHT), LAYER, {
    title = "Localized Text Editor",
    titleHeight = 22,
    toolbarHeight = 24,
    browserWidth = LEFT_WIDTH,
});
local root = shell.root;
local toolbar = shell.toolbar;
local browser = shell.browser;
local inspector = shell.inspector;
local RIGHT_WIDTH = shell.inspectorWidth;

local refreshButton = Button.New(toolbar, vec.New(0, 0), vec.New(74, 22), 1i, { text = "Refresh", textTemplate = TEXT });
local createButton = Button.New(toolbar, vec.New(0, 0), vec.New(92, 22), 1i, { text = "New Text", textTemplate = TEXT });
local searchInput = InputField.New(toolbar, vec.New(0, 0), vec.New(300, 22), 1i, { textTemplate = TEXT, placeholderText = "Search ID, name, or English text" });
toolbar:SetChildOpts(refreshButton, { sizeX = 74, sizeY = 22 });
toolbar:SetChildOpts(createButton, { sizeX = 92, sizeY = 22 });
toolbar:SetChildOpts(searchInput, { modeX = "flex", sizeY = 22 });

local browserTitle = browser:NewText("Localized Text", vec.New(0, 0), 1i, TEXT);
browserTitle:SetColor(Theme.selectedColor);
browser:SetChildOpts(browserTitle, { sizeY = 18 });
local browserState = browser:NewText("Text data has not been requested.", vec.New(0, 0), 1i, TEXT);
browserState:SetColor(colors.muted);
browser:SetChildOpts(browserState, { sizeY = 18 });

local snapshot = { dataState = "Unavailable", texts = {}, translations = {}, locales = {} };
local visibleTexts : { TextEntry } = {};
local selectedTextID : integer? = nil;
local draft : TextEntry? = nil;
local baseline : TextEntry? = nil;
local selectedLocale = 1i;
local translationValue = "";
local translationExists = false;
local pendingRequestID = 0i;
local deletingText = false;
local awaitingSnapshot = false;
local recoveryRequested = false;
local selectAfterSnapshot : integer? = nil;
local statusMessage = "Select an entry or create a new localized text definition.";
local statusColor = colors.muted;
local refreshingFields = false;

local textList : any = nil;
local RefreshBrowser : (boolean?) -> () = function(_) end;
local RefreshInspector : () -> () = function() end;
local ApplySnapshot : (boolean?) -> () = function(_) end;

local function IsDirty() : boolean return draft ~= nil and not SameText(draft, baseline); end

textList = BrowserList.New(browser, vec.New(0, 0), vec.New(LEFT_WIDTH - 16, shell.bodyHeight - 68), 1i, {
    rowHeight = 44,
    textTemplate = TEXT,
    getPrimaryText = function(entry : TextEntry) : string return if entry.internalName ~= "" then entry.internalName else `(unnamed #{entry.id})`; end,
    getSecondaryText = function(entry : TextEntry) : string return `#{entry.id}  {entry.englishValue}`; end,
    isSelected = function(entry : TextEntry) : boolean return selectedTextID == entry.id; end,
    onActivate = function(entry : TextEntry)
        if pendingRequestID ~= 0i or awaitingSnapshot then return; end
        if IsDirty() then
            statusMessage = "Save or revert the current draft before selecting another entry.";
            statusColor = colors.warning;
            RefreshInspector();
            return;
        end
        selectedTextID = entry.id;
        draft = CopyText(entry);
        baseline = CopyText(entry);
        RefreshInspector();
        textList:Refresh();
    end,
});
browser:SetChildOpts(textList, { modeX = "flex", modeY = "flex" });

local inspectorTitle = inspector:NewText("Text Definition", vec.New(0, 0), 1i, TEXT);
inspectorTitle:SetColor(Theme.selectedColor);
inspector:SetChildOpts(inspectorTitle, { sizeY = 20 });
local form = GridLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 220), 1i, {
    columns = 2, cellSizeX = (RIGHT_WIDTH - 28) / 2, cellSizeY = 52, spacing = 4, padding = 2,
    cellAlignH = "stretch", cellAlignV = "center", panelTemplate = "DebugDarkGrey", alpha = 0.0,
});
inspector:SetChildOpts(form, { modeX = "flex", sizeY = 220 });
form:NewText("ID", vec.New(0, 0), 1i, TEXT);
local idValue = form:NewText("No text selected", vec.New(0, 0), 1i, TEXT);
idValue:SetColor(colors.muted);
form:NewText("Internal name *", vec.New(0, 0), 1i, TEXT);
local internalNameInput = InputField.New(form, vec.New(0, 0), vec.New(240, 24), 1i, { textTemplate = TEXT });
form:NewText("English value *", vec.New(0, 0), 1i, TEXT);
local englishInput = InputField.New(form, vec.New(0, 0), vec.New(240, 48), 1i, { textTemplate = TEXT, multiline = true });
form:NewText("Translator context", vec.New(0, 0), 1i, TEXT);
local contextInput = InputField.New(form, vec.New(0, 0), vec.New(240, 48), 1i, { textTemplate = TEXT, multiline = true });

local textActions = HorizontalLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 26), 1i, { panelTemplate = "DebugDarkGrey", alpha = 0.0, spacing = 6 });
inspector:SetChildOpts(textActions, { modeX = "flex", sizeY = 26 });
local saveButton = Button.New(textActions, vec.New(0, 0), vec.New(92, 24), 1i, { text = "Save", textTemplate = TEXT });
local revertButton = Button.New(textActions, vec.New(0, 0), vec.New(92, 24), 1i, { text = "Revert", textTemplate = TEXT });
local deleteButton = Button.New(textActions, vec.New(0, 0), vec.New(92, 24), 1i, { text = "Delete", textTemplate = TEXT });
textActions:SetChildOpts(saveButton, { sizeX = 92, sizeY = 24 });
textActions:SetChildOpts(revertButton, { sizeX = 92, sizeY = 24 });
textActions:SetChildOpts(deleteButton, { sizeX = 92, sizeY = 24 });

local translationTitle = inspector:NewText("Translations", vec.New(0, 0), 1i, TEXT);
translationTitle:SetColor(Theme.selectedColor);
inspector:SetChildOpts(translationTitle, { sizeY = 20 });
local translationRow = HorizontalLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 64), 1i, { panelTemplate = "DebugDarkGrey", alpha = 0.0, spacing = 6, alignV = "center" });
inspector:SetChildOpts(translationRow, { modeX = "flex", sizeY = 64 });
local localeDropdown = Dropdown.New(translationRow, canvas, vec.New(0, 0), vec.New(150, 24), 1i, { options = {}, value = selectedLocale, button = { textTemplate = TEXT } });
local translationInput = InputField.New(translationRow, vec.New(0, 0), vec.New(260, 60), 1i, { textTemplate = TEXT, placeholderText = "Localized value", multiline = true });
translationRow:SetChildOpts(localeDropdown, { sizeX = 150, sizeY = 24 });
translationRow:SetChildOpts(translationInput, { modeX = "flex", sizeY = 60 });
local translationActions = HorizontalLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 26), 1i, { panelTemplate = "DebugDarkGrey", alpha = 0.0, spacing = 6 });
inspector:SetChildOpts(translationActions, { modeX = "flex", sizeY = 26 });
local saveTranslationButton = Button.New(translationActions, vec.New(0, 0), vec.New(132, 24), 1i, { text = "Save Translation", textTemplate = TEXT });
local deleteTranslationButton = Button.New(translationActions, vec.New(0, 0), vec.New(132, 24), 1i, { text = "Delete Translation", textTemplate = TEXT });
translationActions:SetChildOpts(saveTranslationButton, { sizeX = 132, sizeY = 24 });
translationActions:SetChildOpts(deleteTranslationButton, { sizeX = 132, sizeY = 24 });

local statusText = inspector:NewText(statusMessage, vec.New(0, 0), 1i, TEXT);
statusText:SetWrapWidth(RIGHT_WIDTH - 20);
statusText:SetColor(statusColor);
inspector:SetChildOpts(statusText, { modeX = "flex", sizeY = 58 });

local function FindTranslation(textID : integer, locale : integer) : Translation?
    for _, translation in snapshot.translations do
        if translation.textID == textID and translation.locale == locale then return translation; end
    end
    return nil;
end

local function ValidateDraft() : string?
    if draft == nil then return "Select a text definition or create a new one."; end
    if draft.internalName == "" then return "Internal name is required."; end
    if draft.englishValue == "" then return "English value is required."; end
    return nil;
end

local function RefreshActions()
    local ready = snapshot.dataState == "Ready";
    local busy = pendingRequestID ~= 0i or awaitingSnapshot;
    refreshButton:SetInteractable(not busy and snapshot.dataState ~= "Loading");
    createButton:SetInteractable(ready and not busy);
    internalNameInput.button:SetInteractable(ready and not busy and draft ~= nil);
    englishInput.button:SetInteractable(ready and not busy and draft ~= nil);
    contextInput.button:SetInteractable(ready and not busy and draft ~= nil);
    saveButton:SetInteractable(ready and not busy and IsDirty() and ValidateDraft() == nil);
    revertButton:SetInteractable(not busy and IsDirty());
    deleteButton:SetInteractable(ready and not busy and draft ~= nil and draft.id ~= 0i);
    localeDropdown:SetInteractable(ready and not busy and draft ~= nil and draft.id ~= 0i);
    translationInput.button:SetInteractable(ready and not busy and draft ~= nil and draft.id ~= 0i);
    saveTranslationButton:SetInteractable(ready and not busy and draft ~= nil and draft.id ~= 0i and translationValue ~= "");
    deleteTranslationButton:SetInteractable(ready and not busy and translationExists);
end

local function RefreshTranslation()
    local translation = if draft ~= nil and draft.id ~= 0i then FindTranslation(draft.id, selectedLocale) else nil;
    translationExists = translation ~= nil;
    translationValue = if translation then translation.value else "";
    translationInput:SetText(translationValue);
end

RefreshInspector = function()
    refreshingFields = true;
    if draft == nil then
        inspectorTitle:SetText("Text Definition");
        idValue:SetText("No text selected");
        internalNameInput:SetText("");
        englishInput:SetText("");
        contextInput:SetText("");
    else
        inspectorTitle:SetText(if draft.id == 0i then "Create Text Definition" else `Edit Text #{draft.id}`);
        idValue:SetText(if draft.id == 0i then "Assigned after creation" else tostring(draft.id));
        internalNameInput:SetText(draft.internalName);
        englishInput:SetText(draft.englishValue);
        contextInput:SetText(draft.translatorContext);
    end
    RefreshTranslation();
    refreshingFields = false;
    local validation = ValidateDraft();
    statusText:SetText(validation or statusMessage);
    statusText:SetColor(if validation and draft ~= nil then colors.error else statusColor);
    RefreshActions();
end

RefreshBrowser = function(preserveScroll : boolean?)
    visibleTexts = {};
    local editableTextCount = 0;
    local filter = string.lower(searchInput.text or "");
    for _, entry in snapshot.texts do
        if entry.id ~= 0i then editableTextCount += 1; end
        if entry.id ~= 0i and (filter == "" or TextUtil.ContainsIgnoreCase(entry.internalName, filter) or TextUtil.ContainsIgnoreCase(entry.englishValue, filter) or TextUtil.ContainsIgnoreCase(tostring(entry.id), filter)) then
            table.insert(visibleTexts, entry);
        end
    end
    table.sort(visibleTexts, function(left : TextEntry, right : TextEntry) : boolean return integer.lt(left.id, right.id); end);
    textList:SetItems(visibleTexts, preserveScroll);
    if snapshot.dataState == "Loading" then browserState:SetText("Loading authoritative text data...");
    elseif snapshot.dataState == "Failed" then browserState:SetText("Unavailable or interaction.view was denied.");
    elseif snapshot.dataState ~= "Ready" then browserState:SetText("Text data is unavailable while disconnected.");
    else browserState:SetText(`{#visibleTexts} shown / {editableTextCount} total`); end
end

ApplySnapshot = function(preserveScroll : boolean?)
    snapshot = InteractionEditor.GetSnapshot();
    local localeOptions = {};
    for _, locale in snapshot.locales do table.insert(localeOptions, { label = locale.name, value = locale.value }); end
    localeDropdown:SetOptions(localeOptions);
    if #localeOptions > 0 and localeDropdown:GetSelectedOption() == nil then
        selectedLocale = localeOptions[1].value;
        localeDropdown:SetValue(selectedLocale, false);
    end
    if snapshot.dataState == "Ready" and selectAfterSnapshot ~= nil then
        for _, entry in snapshot.texts do
            if entry.id == selectAfterSnapshot then
                selectedTextID = entry.id;
                draft = CopyText(entry);
                baseline = CopyText(entry);
                break;
            end
        end
        selectAfterSnapshot = nil;
    end
    if awaitingSnapshot and snapshot.dataState == "Ready" then
        awaitingSnapshot = false;
        recoveryRequested = false;
        statusMessage = "Authoritative localized text data refreshed.";
        statusColor = colors.success;
    elseif awaitingSnapshot and snapshot.dataState == "Failed" then
        awaitingSnapshot = false;
        statusMessage = "The mutation succeeded, but refreshing the snapshot failed.";
        statusColor = colors.warning;
    end
    RefreshBrowser(preserveScroll);
    RefreshInspector();
end

internalNameInput:SetOnTextChanged(function(input) if not refreshingFields and draft then draft.internalName = input.text; RefreshInspector(); end end);
englishInput:SetOnTextChanged(function(input) if not refreshingFields and draft then draft.englishValue = input.text; RefreshInspector(); end end);
contextInput:SetOnTextChanged(function(input) if not refreshingFields and draft then draft.translatorContext = input.text; RefreshInspector(); end end);
translationInput:SetOnTextChanged(function(input) if not refreshingFields then translationValue = input.text; RefreshActions(); end end);
localeDropdown:SetOnValueChanged(function(value) selectedLocale = value :: integer; RefreshInspector(); end);
searchInput:SetOnTextChanged(function() RefreshBrowser(); end);

refreshButton:SetOnMouseUp(function() awaitingSnapshot = InteractionEditor.RequestSnapshot(); if awaitingSnapshot then ApplySnapshot(true); end end);
createButton:SetOnMouseUp(function()
    if IsDirty() then statusMessage = "Save or revert the current draft first."; statusColor = colors.warning; RefreshInspector(); return; end
    selectedTextID = nil;
    draft = { id = 0i, internalName = "", englishValue = "", translatorContext = "" };
    baseline = CopyText(draft);
    textList:Refresh();
    RefreshInspector();
end);
revertButton:SetOnMouseUp(function() if baseline then draft = CopyText(baseline); RefreshInspector(); end end);
saveButton:SetOnMouseUp(function()
    if draft == nil or ValidateDraft() ~= nil then return; end
    pendingRequestID = if draft.id == 0i then InteractionEditor.CreateText(draft.internalName, draft.englishValue, draft.translatorContext)
        else InteractionEditor.UpdateText(draft.id, draft.internalName, draft.englishValue, draft.translatorContext);
    deletingText = false;
    if pendingRequestID == 0i then statusMessage = "The text mutation could not be submitted."; statusColor = colors.error; end
    RefreshInspector();
end);
deleteButton:SetOnMouseUp(function()
    if draft and draft.id ~= 0i then pendingRequestID = InteractionEditor.DeleteText(draft.id); selectAfterSnapshot = nil; deletingText = pendingRequestID ~= 0i; end
    RefreshInspector();
end);
saveTranslationButton:SetOnMouseUp(function()
    if draft == nil or draft.id == 0i or translationValue == "" then return; end
    pendingRequestID = if translationExists then InteractionEditor.UpdateTranslation(draft.id, selectedLocale, translationValue)
        else InteractionEditor.CreateTranslation(draft.id, selectedLocale, translationValue);
    deletingText = false;
    selectAfterSnapshot = draft.id;
    RefreshInspector();
end);
deleteTranslationButton:SetOnMouseUp(function()
    if draft and draft.id ~= 0i and translationExists then pendingRequestID = InteractionEditor.DeleteTranslation(draft.id, selectedLocale); selectAfterSnapshot = draft.id; deletingText = false; end
    RefreshInspector();
end);

local methods = {};
function methods:Show()
    root:SetVisible(true);
    snapshot = InteractionEditor.GetSnapshot();
    if snapshot.dataState == "Unavailable" or snapshot.dataState == "Failed" then InteractionEditor.RequestSnapshot(); end
    ApplySnapshot();
end
function methods:Hide() root:SetVisible(false); end
function methods:IsVisible() : boolean return root:IsVisible(); end
shell.window.closeButton:SetOnMouseUp(function() methods:Hide(); end);
EditorRegistry.Register("LocalizedTextEditor", methods);

local lastDataState = "Unavailable";
local lastRevision = InteractionEditor.GetRevision();
RegisterEvent(GameEvent.Updated, function()
    if not root:IsVisible() then return; end
    if pendingRequestID ~= 0i then
        local result = InteractionEditor.TakeMutationResult(pendingRequestID);
        if result ~= nil then
            pendingRequestID = 0i;
            if result.succeeded then
                if deletingText then selectedTextID = nil; draft = nil; baseline = nil;
                elseif result.artifact == 0i and result.artifactID ~= 0i then selectAfterSnapshot = result.artifactID;
                elseif draft ~= nil then selectAfterSnapshot = draft.id; end
                local confirmation = if result.response ~= "" then result.response else "The server confirmed the mutation.";
                statusMessage = `{confirmation} Revision {result.revision}.`;
                statusColor = colors.success;
            else
                statusMessage = if result.response ~= "" then result.response else "The server rejected the mutation.";
                statusColor = colors.error;
            end
            deletingText = false;
            RefreshInspector();
        end
    end
    local currentState = InteractionEditor.GetState();
    local currentRevision = InteractionEditor.GetRevision();
    if currentState == "Failed" and not awaitingSnapshot and not recoveryRequested then
        recoveryRequested = true;
        awaitingSnapshot = InteractionEditor.RequestSnapshot();
        statusMessage = if awaitingSnapshot then "Recovering authoritative localized text data..." else "Change-set recovery could not start.";
        statusColor = if awaitingSnapshot then colors.warning else colors.error;
    end
    if currentState ~= lastDataState or currentRevision ~= lastRevision or (awaitingSnapshot and currentState == "Ready") then
        lastDataState = currentState;
        lastRevision = currentRevision;
        ApplySnapshot(true);
    end
end);

RefreshBrowser(false);
RefreshInspector();
