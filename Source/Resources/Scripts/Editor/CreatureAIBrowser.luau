local Button = require("@src/API/UI/Button");
local CreatureAI = require("@src/API/Editor/CreatureAI");
local BrowserList = require("@src/API/Editor/UI/BrowserList");
local InputField = require("@src/API/Editor/UI/InputField");
local Dropdown = require("@src/API/UI/Dropdown");
local EditorRegistry = require("@src/API/Editor/Registry");
local BrowserWindow = require("@src/API/Editor/UI/BrowserWindow");
local GridLayout = require("@src/API/UI/GridLayout");
local HorizontalLayout = require("@src/API/UI/HorizontalLayout");
local Modal = require("@src/API/UI/Modal");
local TextUtil = require("@src/API/UI/TextUtil");
local VerticalLayout = require("@src/API/UI/VerticalLayout");

local WINDOW_WIDTH = 900;
local WINDOW_HEIGHT = 650;
local TITLE_HEIGHT = 18;
local TOOLBAR_HEIGHT = 26;
local LEFT_WIDTH = 320;
local LAYER = 1150i;
local TEXT = "DevToolsText";

local colors =
{
    muted = vec.New(0.58, 0.61, 0.67),
    selected = vec.New(0.30, 0.55, 0.85),
    success = vec.New(0.44, 0.78, 0.52),
    warning = vec.New(0.92, 0.70, 0.30),
};

type ScriptEntry =
{
    name : string,
    path : string,
    revision : integer,
    sourceAvailable : boolean,
    locked : boolean,
    lockedByRequester : boolean,
}

local scripts : { ScriptEntry } = { };
local selectedScript : ScriptEntry? = nil;
local filteredScripts : { ScriptEntry } = { };
local inspectedUnitID : integer? = nil;
local inspectedName = "No creature selected";
local inspectedGUID = "-";
local inspectedTemplateID = 0;
local templateScriptName = "";
local guidScriptName = "";
local effectiveScriptName = "";
local effectiveScope = "None";
local statusMessage = "Creature AI data has not been requested.";
local backendVersion = -1;

local canvas = UI.GetCanvas("CreatureAIBrowser", vec.New(0, 0), vec.New(1920, 1080), false);
local shell = BrowserWindow.New(canvas, vec.New(510, -100), vec.New(WINDOW_WIDTH, WINDOW_HEIGHT), LAYER, {
    title = "Creature AI Browser",
    titleHeight = TITLE_HEIGHT,
    toolbarHeight = TOOLBAR_HEIGHT,
    browserWidth = LEFT_WIDTH,
});
local window = shell.window;
local root = shell.root;
local toolbar = shell.toolbar;
local left = shell.browser;
local right = shell.inspector;
local RIGHT_WIDTH = shell.inspectorWidth;

local refreshButton = Button.New(toolbar, vec.New(0, 0), vec.New(72, 22), 1i, { text = "Refresh", textTemplate = TEXT });
local createButton = Button.New(toolbar, vec.New(0, 0), vec.New(88, 22), 1i, { text = "Create Script", textTemplate = TEXT });
local filterInput = InputField.New(toolbar, vec.New(0, 0), vec.New(260, 22), 1i, {
    textTemplate = TEXT,
    placeholderText = "Filter scripts by name or path",
});
toolbar:SetChildOpts(refreshButton, { sizeX = 72, sizeY = 22 });
toolbar:SetChildOpts(createButton, { sizeX = 88, sizeY = 22 });
toolbar:SetChildOpts(filterInput, { modeX = "flex", sizeY = 22 });

local catalogTitle = left:NewText("Scripts", vec.New(0, 0), 1i, TEXT);
catalogTitle:SetColor(colors.selected);
left:SetChildOpts(catalogTitle, { sizeY = 18 });

local scriptList : any = nil;
local RefreshCatalog : (boolean?) -> () = function(_) end;
local RefreshInspector : () -> () = function() end;

scriptList = BrowserList.New(left, vec.New(0, 0), vec.New(LEFT_WIDTH - 16, 380), 1i, {
    rowHeight = 40,
    textTemplate = TEXT,
    getPrimaryText = function(entry : ScriptEntry) : string
        return if entry.locked then `{entry.name}  [LOCKED]` else entry.name;
    end,
    getSecondaryText = function(entry : ScriptEntry) : string return entry.path; end,
    getSecondaryColor = function(entry : ScriptEntry) : vec return if entry.locked then colors.warning else colors.muted; end,
    isSelected = function(entry : ScriptEntry) : boolean return selectedScript == entry; end,
    onActivate = function(entry : ScriptEntry)
        selectedScript = entry;
        RefreshCatalog(true);
        RefreshInspector();
    end,
});
left:SetChildOpts(scriptList, { modeX = "flex", sizeY = 380 });

local selectedScriptSeparator = left:NewPanel(vec.New(0, 0), vec.New(LEFT_WIDTH - 16, 1), 1i, "DebugDarkGrey");
selectedScriptSeparator:SetColor(vec.New(0.30, 0.30, 0.35));
selectedScriptSeparator:SetAlpha(0.9);
selectedScriptSeparator:SetBorderSize(0.0);
left:SetChildOpts(selectedScriptSeparator, { modeX = "flex", sizeY = 1 });

local selectedScriptPanel = VerticalLayout.New(left, vec.New(0, 0), vec.New(LEFT_WIDTH - 16, 88), 1i, {
    panelTemplate = "DebugDarkGrey",
    padding = 7,
    spacing = 4,
    defaultChildOpts = { modeX = "flex" },
});
left:SetChildOpts(selectedScriptPanel, { modeX = "flex", modeY = "flex", minY = 78 });
local selectedScriptTitle = selectedScriptPanel:NewText("No script selected", vec.New(0, 0), 1i, TEXT);
local selectedScriptPath = selectedScriptPanel:NewText("", vec.New(0, 0), 1i, TEXT);
local selectedScriptLease = selectedScriptPanel:NewText("", vec.New(0, 0), 1i, TEXT);
local selectedScriptActivity = selectedScriptPanel:NewText("", vec.New(0, 0), 1i, TEXT);
local finishEditingButton = Button.New(selectedScriptPanel, vec.New(0, 0), vec.New(126, 22), 1i, {
    text = "Finish Editing",
    textTemplate = TEXT,
});
selectedScriptPath:SetColor(colors.muted);
selectedScriptLease:SetColor(colors.muted);
selectedScriptActivity:SetColor(colors.warning);
selectedScriptActivity:SetWrapWidth(LEFT_WIDTH - 44);
selectedScriptPanel:SetChildOpts(selectedScriptTitle, { sizeY = 17 });
selectedScriptPanel:SetChildOpts(selectedScriptPath, { sizeY = 17 });
selectedScriptPanel:SetChildOpts(selectedScriptLease, { sizeY = 17 });
selectedScriptPanel:SetChildOpts(selectedScriptActivity, { sizeY = 42 });
selectedScriptPanel:SetChildOpts(finishEditingButton, { sizeX = 126, sizeY = 22 });

local creatureTitle = right:NewText("Selected Creature", vec.New(0, 0), 1i, TEXT);
creatureTitle:SetColor(colors.selected);
right:SetChildOpts(creatureTitle, { sizeY = 18 });

local creatureGrid = GridLayout.New(right, vec.New(0, 0), vec.New(RIGHT_WIDTH - 16, 52), 1i, {
    columns = 2,
    cellSizeX = (RIGHT_WIDTH - 24) / 2,
    cellSizeY = 24,
    spacing = 4,
    padding = 2,
    cellAlignH = "stretch",
    cellAlignV = "center",
    panelTemplate = "DebugDarkGrey",
    alpha = 0.0,
});
right:SetChildOpts(creatureGrid, { modeX = "flex", sizeY = 60 });
local creatureNameText = creatureGrid:NewText("", vec.New(0, 0), 1i, TEXT);
local creatureGUIDText = creatureGrid:NewText("", vec.New(0, 0), 1i, TEXT);
local creatureTemplateText = creatureGrid:NewText("", vec.New(0, 0), 1i, TEXT);
local creatureUnitText = creatureGrid:NewText("", vec.New(0, 0), 1i, TEXT);

local effectivePanel = GridLayout.New(right, vec.New(0, 0), vec.New(RIGHT_WIDTH - 16, 52), 1i, {
    columns = 2,
    cellSizeX = (RIGHT_WIDTH - 24) / 2,
    cellSizeY = 24,
    spacing = 4,
    padding = 2,
    cellAlignH = "stretch",
    cellAlignV = "center",
    panelTemplate = "DebugDarkGrey",
});
right:SetChildOpts(effectivePanel, { modeX = "flex", sizeY = 60 });
local effectiveScriptText = effectivePanel:NewText("", vec.New(0, 0), 1i, TEXT);
local effectiveScopeText = effectivePanel:NewText("", vec.New(0, 0), 1i, TEXT);
local effectiveHintText = effectivePanel:NewText("", vec.New(0, 0), 1i, TEXT);
effectiveHintText:SetColor(colors.muted);
effectivePanel:NewText("GUID overrides the template binding", vec.New(0, 0), 1i, TEXT):SetColor(colors.muted);

local function AddSectionDivider()
    local divider = right:NewPanel(vec.New(0, 0), vec.New(RIGHT_WIDTH - 16, 1), 1i, "DebugDarkGrey");
    divider:SetColor(vec.New(0.30, 0.30, 0.35));
    divider:SetAlpha(0.9);
    divider:SetBorderSize(0.0);
    right:SetChildOpts(divider, { modeX = "flex", sizeY = 1 });
end

local function CreateBindingCard(title : string)
    local card = VerticalLayout.New(right, vec.New(0, 0), vec.New(RIGHT_WIDTH - 16, 108), 1i, {
        panelTemplate = "DebugDarkGrey",
        alpha = 0.0,
        spacing = 6,
        defaultChildOpts = { modeX = "flex" },
    });
    right:SetChildOpts(card, { modeX = "flex", sizeY = 108 });

    local titleText = card:NewText(title, vec.New(0, 0), 1i, TEXT);
    titleText:SetColor(colors.selected);
    card:SetChildOpts(titleText, { sizeY = 18 });

    local details = GridLayout.New(card, vec.New(0, 0), vec.New(RIGHT_WIDTH - 16, 54), 1i, {
        columns = 2,
        cellSizeX = (RIGHT_WIDTH - 26) / 2,
        cellSizeY = 24,
        spacing = 4,
        padding = 1,
        cellAlignH = "stretch",
        cellAlignV = "center",
        panelTemplate = "DebugDarkGrey",
        alpha = 0.0,
    });
    card:SetChildOpts(details, { modeX = "flex", sizeY = 54 });
    local scriptText = details:NewText("", vec.New(0, 0), 1i, TEXT);
    local stateText = details:NewText("", vec.New(0, 0), 1i, TEXT);
    local pathText = details:NewText("", vec.New(0, 0), 1i, TEXT);
    local ownerText = details:NewText("", vec.New(0, 0), 1i, TEXT);
    stateText:SetColor(colors.muted);
    pathText:SetColor(colors.muted);
    ownerText:SetColor(colors.muted);

    local actions = HorizontalLayout.New(card, vec.New(0, 0), vec.New(RIGHT_WIDTH - 16, 24), 1i, {
        panelTemplate = "DebugDarkGrey",
        alpha = 0.0,
        spacing = 5,
    });
    card:SetChildOpts(actions, { modeX = "flex", sizeY = 24 });
    local viewButton = Button.New(actions, vec.New(0, 0), vec.New(70, 22), 1i, { text = "View Source", textTemplate = TEXT });
    local editButton = Button.New(actions, vec.New(0, 0), vec.New(70, 22), 1i, { text = "Edit", textTemplate = TEXT });
    local duplicateButton = Button.New(actions, vec.New(0, 0), vec.New(70, 22), 1i, { text = "Duplicate", textTemplate = TEXT });
    local linkButton = Button.New(actions, vec.New(0, 0), vec.New(104, 22), 1i, { text = "Link Selected", textTemplate = TEXT });
    local unlinkButton = Button.New(actions, vec.New(0, 0), vec.New(70, 22), 1i, { text = "Unlink", textTemplate = TEXT });
    for _, button in { viewButton, editButton, duplicateButton, linkButton, unlinkButton } do
        actions:SetChildOpts(button, { modeX = "flex", sizeY = 22 });
    end

    return {
        scriptText = scriptText,
        stateText = stateText,
        pathText = pathText,
        ownerText = ownerText,
        viewButton = viewButton,
        editButton = editButton,
        duplicateButton = duplicateButton,
        linkButton = linkButton,
        unlinkButton = unlinkButton,
    };
end

AddSectionDivider();
local templateCard = CreateBindingCard("Creature Template Binding");
AddSectionDivider();
local guidCard = CreateBindingCard("GUID Override Binding");

local function FindScript(name : string) : ScriptEntry?
    for _, entry in scripts do
        if entry.name == name then return entry; end
    end
    return nil;
end

local function SelectScript(name : string)
    selectedScript = FindScript(name);
    RefreshCatalog(true);
    RefreshInspector();
end

RefreshCatalog = function(preserveScroll : boolean?)
    filteredScripts = { };
    local query = filterInput:GetText();
    for _, entry in scripts do
        if TextUtil.ContainsIgnoreCase(entry.name, query) or TextUtil.ContainsIgnoreCase(entry.path, query) then
            table.insert(filteredScripts, entry);
        end
    end
    scriptList:SetItems(filteredScripts, preserveScroll);
    catalogTitle:SetText(`Scripts ({#filteredScripts} of {#scripts})`);

    if selectedScript == nil then
        selectedScriptTitle:SetText("Selected Script: None");
        selectedScriptPath:SetText("Select a catalog entry to inspect or link it");
        selectedScriptLease:SetText("");
    else
        selectedScriptTitle:SetText(`Selected Script: {selectedScript.name}`);
        selectedScriptPath:SetText(selectedScript.path);
        selectedScriptLease:SetText(if selectedScript.locked and not selectedScript.lockedByRequester then "Locked by another developer - view/duplicate available" elseif selectedScript.lockedByRequester then "Checked out by this client" else "Available for editing");
        selectedScriptLease:SetColor(if selectedScript.locked and not selectedScript.lockedByRequester then colors.warning else colors.success);
    end
    finishEditingButton:SetInteractable(selectedScript ~= nil and selectedScript.lockedByRequester);
end;

local function RefreshBindingCard(card, scriptName : string)
    local entry = FindScript(scriptName);
    local hasBinding = scriptName ~= "";
    card.scriptText:SetText(`Assigned script: {if hasBinding then scriptName else "None"}`);
    card.stateText:SetText(`Status: {if not hasBinding then "Not linked" elseif entry == nil then "Missing from catalog" elseif entry.locked then "Locked for editing" else "Available"}`);
    card.pathText:SetText(`Source: {if entry ~= nil then entry.path else "-"}`);
    card.ownerText:SetText(if entry ~= nil and entry.lockedByRequester then "Edit lease: This client" elseif entry ~= nil and entry.locked then "Edit lease: Another developer" else "Edit lease: None");
    card.stateText:SetColor(if entry ~= nil and entry.locked then colors.warning elseif hasBinding then colors.success else colors.muted);
    card.viewButton:SetInteractable(hasBinding and entry ~= nil);
    card.editButton:SetInteractable(hasBinding and entry ~= nil and (not entry.locked or entry.lockedByRequester));
    card.duplicateButton:SetInteractable(hasBinding and entry ~= nil);
    card.linkButton:SetInteractable(inspectedUnitID ~= nil and selectedScript ~= nil and selectedScript.name ~= scriptName);
    card.unlinkButton:SetInteractable(hasBinding);
end

RefreshInspector = function()
    creatureNameText:SetText(`Name: {inspectedName}`);
    creatureGUIDText:SetText(`GUID: {inspectedGUID}`);
    creatureTemplateText:SetText(`Template ID: {inspectedTemplateID}`);
    creatureUnitText:SetText(`Client unit: {if inspectedUnitID ~= nil then tostring(inspectedUnitID) else "-"}`);

    local effectiveName = if effectiveScriptName == "" then "None" else effectiveScriptName;
    effectiveScriptText:SetText(`Effective script: {effectiveName}`);
    effectiveScriptText:SetColor(if effectiveName == "None" then colors.muted else colors.success);
    effectiveScopeText:SetText(`Effective scope: {effectiveScope}`);
    effectiveHintText:SetText(if guidScriptName ~= "" and templateScriptName ~= "" and guidScriptName ~= templateScriptName then "GUID differs from template" else "Bindings currently agree");

    RefreshBindingCard(templateCard, templateScriptName);
    RefreshBindingCard(guidCard, guidScriptName);
    selectedScriptActivity:SetText(`Status: {statusMessage}`);
end;

finishEditingButton:SetOnMouseUp(function()
    if selectedScript ~= nil and selectedScript.lockedByRequester then
        CreatureAI.FinishEditing(selectedScript.name);
    end
end);

local OpenDuplicate : (string) -> () = function(_) end;

templateCard.viewButton:SetOnMouseUp(function()
    SelectScript(templateScriptName);
    CreatureAI.View(templateScriptName);
end);
templateCard.editButton:SetOnMouseUp(function()
    SelectScript(templateScriptName);
    CreatureAI.Edit(templateScriptName);
end);
templateCard.duplicateButton:SetOnMouseUp(function() OpenDuplicate(templateScriptName); end);
templateCard.linkButton:SetOnMouseUp(function()
    if selectedScript == nil then return; end
    CreatureAI.Link("Template", selectedScript.name);
end);
templateCard.unlinkButton:SetOnMouseUp(function()
    CreatureAI.Unlink("Template");
end);

guidCard.viewButton:SetOnMouseUp(function()
    SelectScript(guidScriptName);
    CreatureAI.View(guidScriptName);
end);
guidCard.editButton:SetOnMouseUp(function()
    SelectScript(guidScriptName);
    CreatureAI.Edit(guidScriptName);
end);
guidCard.duplicateButton:SetOnMouseUp(function() OpenDuplicate(guidScriptName); end);
guidCard.linkButton:SetOnMouseUp(function()
    if selectedScript == nil then return; end
    CreatureAI.Link("GUID", selectedScript.name);
end);
guidCard.unlinkButton:SetOnMouseUp(function()
    CreatureAI.Unlink("GUID");
end);

local createModal = Modal.New(window, vec.New(0, 0), vec.New(500, 260), 100i, {
    name = "CreatureAICreate",
    title = "Create Creature AI Script",
    message = "Creation reserves the registered name and file path, but does not link the script.",
    confirmText = "Create",
    cancelText = "Cancel",
    titleTemplate = TEXT,
    messageTemplate = TEXT,
    messageWidth = 470,
    shieldSize = vec.New(WINDOW_WIDTH, WINDOW_HEIGHT),
    dismissible = true,
});

local createGrid = GridLayout.New(createModal.dialog, vec.New(16, -88), vec.New(468, 96), 2i, {
    columns = 2,
    cellSizeX = 230,
    cellSizeY = 28,
    spacing = 4,
    cellAlignH = "stretch",
    cellAlignV = "center",
    panelTemplate = "DebugDarkGrey",
    alpha = 0.0,
});
createGrid:SetAnchor(vec.New(0.0, 1.0));
createGrid:SetRelativePoint(vec.New(0.0, 1.0));
createGrid:NewText("Registered script name", vec.New(0, 0), 1i, TEXT);
local createNameInput = InputField.New(createGrid, vec.New(0, 0), vec.New(230, 24), 1i, { textTemplate = TEXT });
createGrid:NewText("Script file under AI/", vec.New(0, 0), 1i, TEXT);
local createPathInput = InputField.New(createGrid, vec.New(0, 0), vec.New(230, 24), 1i, { textTemplate = TEXT });
local createTemplateLabel = createGrid:NewText("Client template", vec.New(0, 0), 1i, TEXT);
local createTemplate = Dropdown.New(createGrid, canvas, vec.New(0, 0), vec.New(230, 24), 1i, {
    value = "periodic",
    options = {
        { label = "Periodic AI", value = "periodic" },
        { label = "Event-driven AI", value = "event" },
        { label = "Empty Script", value = "empty" },
    },
    button = { textTemplate = TEXT },
    menuLayer = integer.add(LAYER, 150i),
});

local duplicateSourceName = "";

local function ResolveAIScriptPath(value : string, scriptName : string) : (string?, string?)
    local relativePath = (value:gsub("\\", "/"));
    if string.lower(relativePath:sub(1, 3)) == "ai/" then
        relativePath = relativePath:sub(4);
    end
    if relativePath == "" then
        relativePath = `{scriptName}.luau`;
    end
    if relativePath:sub(1, 1) == "/" or relativePath:sub(-1) == "/" or
        relativePath:find(":", 1, true) ~= nil or relativePath:find("//", 1, true) ~= nil
    then
        return nil, "The script file must be a relative path beneath AI/.";
    end

    for component in relativePath:gmatch("[^/]+") do
        if component == "." or component == ".." then
            return nil, "The script file cannot contain '.' or '..' path components.";
        end
    end
    if relativePath:sub(-5) ~= ".luau" then
        relativePath ..= ".luau";
    end
    return `AI/{relativePath}`, nil;
end

local function BuildTemplateSource(scriptName : string, templateName : string) : string
    local lines = {
        "if (IsStateGlobal()) then return end",
        "",
        "local creatureAIHandler = require(\"@src/API/CreatureAIHandler\")",
        "",
        `local scriptNameHash = creatureAIHandler:RegisterScriptInfo("{scriptName}")`,
        "if not scriptNameHash then return end",
    };
    if templateName == "empty" then
        return table.concat(lines, "\n") .. "\n";
    end

    local updateBody = if templateName == "periodic"
        then "    return creatureAIHandler:Milliseconds(100)"
        else "    return creatureAIHandler:NextEvent()";
    table.insert(lines, "");
    table.insert(lines, "local function OnUpdate(unit : Unit, deltaTime : number, wakeReasons : integer) : integer");
    table.insert(lines, updateBody);
    table.insert(lines, "end");
    table.insert(lines, "");
    table.insert(lines, "creatureAIHandler:RegisterScriptUpdateCallback(scriptNameHash, OnUpdate)");
    return table.concat(lines, "\n") .. "\n";
end

createModal:SetOnConfirm(function()
    local name = createNameInput:GetText();
    if name == "" then
        statusMessage = "Enter a registered script name.";
        RefreshInspector();
        return;
    end
    local path, pathError = ResolveAIScriptPath(createPathInput:GetText(), name);
    if path == nil then
        statusMessage = pathError or "The requested script file path is invalid.";
        RefreshInspector();
        return;
    end
    if duplicateSourceName ~= "" then
        CreatureAI.Duplicate(duplicateSourceName, name, path);
    else
        CreatureAI.Create(name, path, BuildTemplateSource(name, tostring(createTemplate:GetValue())));
    end
    duplicateSourceName = "";
    createNameInput:SetText("");
    createPathInput:SetText("");
end);

OpenDuplicate = function(sourceName : string)
    local sourceEntry = FindScript(sourceName);
    if sourceEntry == nil then return; end

    duplicateSourceName = sourceName;
    createNameInput:SetText(`{sourceName}_copy`);
    local relativePath = if sourceEntry.path:sub(1, 3) == "AI/" then sourceEntry.path:sub(4) else sourceEntry.path;
    createPathInput:SetText((relativePath:gsub("%.luau$", "Copy.luau")));
    createTemplateLabel:SetVisible(false);
    createTemplate:SetVisible(false);
    createModal:SetTitle("Duplicate Creature AI Script");
    createModal:SetMessage(`Duplicate {sourceName} under a new registered name and file path. The duplicate is not linked automatically.`);
    if createModal.confirmButton ~= nil then createModal.confirmButton:SetText("Duplicate"); end
    createModal:Open();
end;

createButton:SetOnMouseUp(function()
    duplicateSourceName = "";
    createNameInput:SetText("");
    createPathInput:SetText("");
    createTemplateLabel:SetVisible(true);
    createTemplate:SetVisible(true);
    createModal:SetTitle("Create Creature AI Script");
    createModal:SetMessage("Creation reserves the registered name and file path, but does not link the script.");
    if createModal.confirmButton ~= nil then createModal.confirmButton:SetText("Create"); end
    createModal:Open();
end);
refreshButton:SetOnMouseUp(function() CreatureAI.RequestCatalog(); end);
filterInput:SetOnTextChanged(function()
    RefreshCatalog(false);
end);

local methods = { };

local function ClearInspectedCreature()
    inspectedUnitID = nil;
    inspectedName = "No active target";
    inspectedGUID = "-";
    inspectedTemplateID = 0;
    templateScriptName = "";
    guidScriptName = "";
    effectiveScriptName = "";
    effectiveScope = "None";
    CreatureAI.ClearInspection();
    RefreshInspector();
end

local function InspectTarget(unitID : integer)
    inspectedUnitID = unitID;
    inspectedName = Unit.GetName(unitID);
    inspectedGUID = "-";
    inspectedTemplateID = 0;
    templateScriptName = "";
    guidScriptName = "";
    effectiveScriptName = "";
    effectiveScope = "None";
    CreatureAI.InspectUnit(unitID);
    RefreshInspector();
end

local function SynchronizeActiveTarget()
    local targetID = Unit.GetTarget();
    if targetID == INVALID_UNIT_ID then
        ClearInspectedCreature();
    else
        InspectTarget(targetID);
    end
end

local function ApplyBackendState()
    local state = CreatureAI.GetState();
    if state.version == backendVersion then return; end
    backendVersion = state.version;

    local selectedName = if selectedScript ~= nil then selectedScript.name else "";
    scripts = state.scripts;
    table.sort(scripts, function(left : ScriptEntry, right : ScriptEntry) : boolean
        if left.lockedByRequester ~= right.lockedByRequester then
            return left.lockedByRequester;
        end
        if left.path ~= right.path then
            return left.path < right.path;
        end
        return left.name < right.name;
    end);
    selectedScript = if selectedName ~= "" then FindScript(selectedName) else nil;
    statusMessage = state.status;

    local inspection = state.inspection;
    if inspectedUnitID ~= nil and inspection ~= nil then
        inspectedGUID = inspection.guid;
        inspectedTemplateID = inspection.templateID;
        templateScriptName = inspection.templateScriptName;
        guidScriptName = inspection.guidScriptName;
        effectiveScriptName = inspection.effectiveScriptName;
        effectiveScope = inspection.effectiveScope;
    end

    RefreshCatalog(true);
    RefreshInspector();
end

function methods:Show()
    root:SetVisible(true);
    CreatureAI.RequestCatalog();
    SynchronizeActiveTarget();
    ApplyBackendState();
end

function methods:Hide()
    root:SetVisible(false);
end

function methods:IsVisible() : boolean
    return root:IsVisible();
end

function methods:InspectUnit(unitID : integer)
    root:SetVisible(true);
    CreatureAI.RequestCatalog();
    InspectTarget(unitID);
end

window.closeButton:SetOnMouseUp(function() methods:Hide(); end);
EditorRegistry.Register("CreatureAI", methods);

RegisterEvent(GameEvent.Updated, function()
    if root:IsVisible() then
        ApplyBackendState();
    end
end);

RegisterEvent(UnitEvent.TargetChanged, function(_, data)
    if data.unitID ~= Unit.GetLocal() then return; end
    if data.targetID == INVALID_UNIT_ID then
        ClearInspectedCreature();
    else
        InspectTarget(data.targetID);
    end
end);

RefreshCatalog(false);
RefreshInspector();
SynchronizeActiveTarget();
