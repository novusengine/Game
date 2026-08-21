local BrowserList = require("@src/API/Editor/UI/BrowserList");
local BrowserWindow = require("@src/API/Editor/UI/BrowserWindow");
local Button = require("@src/API/UI/Button");
local Dropdown = require("@src/API/UI/Dropdown");
local EditorRegistry = require("@src/API/Editor/Registry");
local HorizontalLayout = require("@src/API/UI/HorizontalLayout");
local InputField = require("@src/API/Editor/UI/InputField");
local Modal = require("@src/API/UI/Modal");
local TextUtil = require("@src/API/UI/TextUtil");
local Theme = require("@src/API/Editor/UI/Theme");
local VerticalLayout = require("@src/API/UI/VerticalLayout");

local WINDOW_WIDTH = 940;
local WINDOW_HEIGHT = 610;
local LEFT_WIDTH = 330;
local TEXT = Theme.textTemplate;
local LAYER = 1160i;
local colors = { muted = vec.New(0.58, 0.62, 0.69), success = vec.New(0.38, 0.82, 0.52), warning = vec.New(0.94, 0.72, 0.30), error = vec.New(0.94, 0.38, 0.38) };

local canvas = UI.GetCanvas("CreatureInteractionEditor", vec.New(0, 0), vec.New(1920, 1080), false);
local shell = BrowserWindow.New(canvas, vec.New(490, -90), vec.New(WINDOW_WIDTH, WINDOW_HEIGHT), LAYER, {
    title = "Creature Template Interactions", titleHeight = 22, toolbarHeight = 24, browserWidth = LEFT_WIDTH,
});
local root = shell.root;
local toolbar = shell.toolbar;
local browser = shell.browser;
local inspector = shell.inspector;
local RIGHT_WIDTH = shell.inspectorWidth;

local refreshButton = Button.New(toolbar, vec.New(0, 0), vec.New(74, 22), 1i, { text = "Refresh", textTemplate = TEXT });
local configuredButton = Button.New(toolbar, vec.New(0, 0), vec.New(112, 22), 1i, { text = "Show Configured", textTemplate = TEXT });
local searchInput = InputField.New(toolbar, vec.New(0, 0), vec.New(260, 22), 1i, { textTemplate = TEXT, placeholderText = "Search creature templates" });
local menusButton = Button.New(toolbar, vec.New(0, 0), vec.New(104, 22), 1i, { text = "Gossip Flow", textTemplate = TEXT });
toolbar:SetChildOpts(refreshButton, { sizeX = 74, sizeY = 22 });
toolbar:SetChildOpts(configuredButton, { sizeX = 112, sizeY = 22 });
toolbar:SetChildOpts(searchInput, { modeX = "flex", sizeY = 22 });
toolbar:SetChildOpts(menusButton, { sizeX = 104, sizeY = 22 });

local browserTitle = browser:NewText("Creature Templates", vec.New(0, 0), 1i, TEXT);
browserTitle:SetColor(Theme.selectedColor);
browser:SetChildOpts(browserTitle, { sizeY = 18 });
local browserState = browser:NewText("Interaction data has not been requested.", vec.New(0, 0), 1i, TEXT);
browserState:SetColor(colors.muted);
browser:SetChildOpts(browserState, { sizeY = 18 });

local snapshot = { dataState = "Unavailable", creatureTemplates = {}, creatureTemplateInteractions = {}, creatureTemplateGossip = {}, gossipMenus = {}, interactionRangePolicies = {} };
local visibleTemplates = {};
local configuredByTemplate = {};
local selectedTemplateID : integer? = nil;
local configuredOnly = false;
local pendingRequestID = 0i;
local awaitingSnapshot = false;
local selectAfterSnapshot : integer? = nil;
local statusMessage = "Select a creature template to inspect its interaction provider.";
local statusColor = colors.muted;
local refreshingFields = false;

local function ToDisplayASCII(value : string) : string
    return string.gsub(value, "[^ -~]", "?");
end

local function FindTemplate(templateID)
    for _, definition in snapshot.creatureTemplates do if definition.id == templateID then return definition; end end
    return nil;
end

local function FindProvider(templateID)
    for _, provider in snapshot.creatureTemplateInteractions do if provider.creatureTemplateID == templateID then return provider; end end
    return nil;
end

local function FindGossip(templateID)
    for _, binding in snapshot.creatureTemplateGossip do if binding.creatureTemplateID == templateID then return binding; end end
    return nil;
end

local function MenuOptions()
    local options = {};
    for _, menu in snapshot.gossipMenus do if menu.id ~= 0i then table.insert(options, { label = menu.internalName, value = menu.id }); end end
    table.sort(options, function(left, right) return string.lower(left.label) < string.lower(right.label); end);
    return options;
end

local function RangePolicyOptions()
    local options = {};
    for _, policy in snapshot.interactionRangePolicies do
        local label = if policy.name == "Default" then "Default (5 units)" else "Custom Distance";
        table.insert(options, { label = label, value = policy.value });
    end
    return options;
end

local RefreshTemplates : (boolean?) -> () = function(_) end;
local RefreshInspector : () -> () = function() end;
local ApplySnapshot : (boolean?) -> () = function(_) end;

local templateList : any = nil;
templateList = BrowserList.New(browser, vec.New(0, 0), vec.New(LEFT_WIDTH - 16, shell.bodyHeight - 68), 1i, {
    rowHeight = 42, textTemplate = TEXT,
    getPrimaryText = function(definition) return definition.name; end,
    getSecondaryText = function(definition)
        local state = if configuredByTemplate[definition.id] then "Configured" else "No provider";
        return `Template #{definition.id} - {state}`;
    end,
    isSelected = function(definition) return selectedTemplateID == definition.id; end,
    onActivate = function(definition)
        if pendingRequestID ~= 0i or awaitingSnapshot then return; end
        selectedTemplateID = definition.id;
        RefreshTemplates(true);
        RefreshInspector();
    end,
});
browser:SetChildOpts(templateList, { modeX = "flex", modeY = "flex" });

local templateTitle = inspector:NewText("No creature template selected", vec.New(0, 0), 1i, TEXT);
templateTitle:SetColor(Theme.selectedColor);
inspector:SetChildOpts(templateTitle, { sizeY = 20 });
local templateSubtitle = inspector:NewText("", vec.New(0, 0), 1i, TEXT);
templateSubtitle:SetColor(colors.muted);
inspector:SetChildOpts(templateSubtitle, { sizeY = 20 });

local providerState = inspector:NewText("Interaction Provider", vec.New(0, 0), 1i, TEXT);
providerState:SetColor(Theme.selectedColor);
inspector:SetChildOpts(providerState, { sizeY = 20 });
local providerExplanation = inspector:NewText("The provider controls whether native interactions exist and how close a player must be.", vec.New(0, 0), 1i, TEXT);
providerExplanation:SetWrapWidth(RIGHT_WIDTH - 20);
providerExplanation:SetColor(colors.muted);
inspector:SetChildOpts(providerExplanation, { modeX = "flex", sizeY = 36 });

local providerForm = HorizontalLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 48), 1i, { spacing = 6, panelTemplate = "DebugDarkGrey", alpha = 0.0 });
inspector:SetChildOpts(providerForm, { modeX = "flex", sizeY = 48 });
local function NewPropertyField(parent, labelText, width)
    local field = VerticalLayout.New(parent, vec.New(0, 0), vec.New(width, 46), 1i, { spacing = 2, panelTemplate = "DebugDarkGrey", alpha = 0.0 });
    local label = field:NewText(labelText, vec.New(0, 0), 1i, TEXT);
    label:SetColor(colors.muted);
    field:SetChildOpts(label, { modeX = "flex", sizeY = 16 });
    return field;
end
local policyField = NewPropertyField(providerForm, "Range Policy", 190);
local policyDropdown = Dropdown.New(policyField, canvas, vec.New(0, 0), vec.New(190, 24), 1i, { options = {}, placeholder = "Range policy", previewMaxCharacters = 28, button = { textTemplate = TEXT } });
policyField:SetChildOpts(policyDropdown, { modeX = "flex", sizeY = 24 });
local rangeField = NewPropertyField(providerForm, "Interaction Range", 150);
local rangeInput = InputField.New(rangeField, vec.New(0, 0), vec.New(150, 24), 1i, { textTemplate = TEXT, placeholderText = "5.0" });
rangeField:SetChildOpts(rangeInput, { modeX = "flex", sizeY = 24 });
local providerActions = NewPropertyField(providerForm, "Provider Actions", 210);
local providerActionRow = HorizontalLayout.New(providerActions, vec.New(0, 0), vec.New(210, 24), 1i, { spacing = 4, panelTemplate = "DebugDarkGrey", alpha = 0.0 });
providerActions:SetChildOpts(providerActionRow, { modeX = "flex", sizeY = 24 });
local saveProviderButton = Button.New(providerActionRow, vec.New(0, 0), vec.New(98, 24), 1i, { text = "Save Policy", textTemplate = TEXT });
local removeProviderButton = Button.New(providerActionRow, vec.New(0, 0), vec.New(108, 24), 1i, { text = "Remove Provider", textTemplate = TEXT });
providerActionRow:SetChildOpts(saveProviderButton, { sizeX = 98, sizeY = 24 });
providerActionRow:SetChildOpts(removeProviderButton, { sizeX = 108, sizeY = 24 });
providerForm:SetChildOpts(policyField, { modeX = "flex", weightX = 1, minX = 165, sizeY = 46 });
providerForm:SetChildOpts(rangeField, { modeX = "flex", weightX = 1, minX = 130, sizeY = 46 });
providerForm:SetChildOpts(providerActions, { sizeX = 210, sizeY = 46 });

local gossipTitle = inspector:NewText("Gossip Capability", vec.New(0, 0), 1i, TEXT);
gossipTitle:SetColor(Theme.selectedColor);
inspector:SetChildOpts(gossipTitle, { sizeY = 20 });
local gossipExplanation = inspector:NewText("A root menu gives every applicable spawn of this template the same native gossip entry point.", vec.New(0, 0), 1i, TEXT);
gossipExplanation:SetWrapWidth(RIGHT_WIDTH - 20);
gossipExplanation:SetColor(colors.muted);
inspector:SetChildOpts(gossipExplanation, { modeX = "flex", sizeY = 36 });
local gossipForm = HorizontalLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 48), 1i, { spacing = 6, panelTemplate = "DebugDarkGrey", alpha = 0.0 });
inspector:SetChildOpts(gossipForm, { modeX = "flex", sizeY = 48 });
local rootMenuField = NewPropertyField(gossipForm, "Root Gossip Menu", 340);
local rootMenuDropdown = Dropdown.New(rootMenuField, canvas, vec.New(0, 0), vec.New(340, 24), 1i, { options = {}, placeholder = "Select a root menu", previewMaxCharacters = 44, button = { textTemplate = TEXT } });
rootMenuField:SetChildOpts(rootMenuDropdown, { modeX = "flex", sizeY = 24 });
local gossipActions = NewPropertyField(gossipForm, "Capability Actions", 190);
local saveGossipButton = Button.New(gossipActions, vec.New(0, 0), vec.New(190, 24), 1i, { text = "Save Root Menu", textTemplate = TEXT });
gossipActions:SetChildOpts(saveGossipButton, { modeX = "flex", sizeY = 24 });
gossipForm:SetChildOpts(rootMenuField, { modeX = "flex", weightX = 1, minX = 280, sizeY = 46 });
gossipForm:SetChildOpts(gossipActions, { sizeX = 190, sizeY = 46 });

local enablePanel = VerticalLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 82), 1i, { spacing = 6, panelTemplate = "DebugDarkGrey", alpha = 0.0 });
inspector:SetChildOpts(enablePanel, { modeX = "flex", sizeY = 82 });
local enableExplanation = enablePanel:NewText("This template has no native interaction provider. Choose its range policy and root gossip menu, then enable it.", vec.New(0, 0), 1i, TEXT);
enableExplanation:SetWrapWidth(RIGHT_WIDTH - 20);
enableExplanation:SetColor(colors.muted);
enablePanel:SetChildOpts(enableExplanation, { modeX = "flex", sizeY = 34 });
local enableButton = Button.New(enablePanel, vec.New(0, 0), vec.New(210, 26), 1i, { text = "Enable Provider + Gossip", textTemplate = TEXT });
enablePanel:SetChildOpts(enableButton, { sizeX = 210, sizeY = 26 });

local statusText = inspector:NewText(statusMessage, vec.New(0, 0), 1i, TEXT);
statusText:SetWrapWidth(RIGHT_WIDTH - 20);
inspector:SetChildOpts(statusText, { modeX = "flex", sizeY = 48 });
local removeModal = Modal.New(canvas, vec.New(0, 0), vec.New(450, 175), integer.add(LAYER, 100i), {
    name = "CreatureInteractionEditorRemove", title = "Remove interaction provider", message = "", confirmText = "Remove", cancelText = "Cancel",
});

RefreshTemplates = function(preserveScroll)
    configuredByTemplate = {};
    for _, provider in snapshot.creatureTemplateInteractions do configuredByTemplate[provider.creatureTemplateID] = true; end
    visibleTemplates = {};
    local filter = searchInput.text or "";
    for _, definition in snapshot.creatureTemplates do
        if (not configuredOnly or configuredByTemplate[definition.id]) and
            (filter == "" or TextUtil.ContainsIgnoreCase(definition.name, filter) or TextUtil.ContainsIgnoreCase(definition.subname, filter) or TextUtil.ContainsIgnoreCase(tostring(definition.id), filter))
        then
            table.insert(visibleTemplates, definition);
        end
    end
    table.sort(visibleTemplates, function(left, right) return integer.lt(left.id, right.id); end);
    templateList:SetItems(visibleTemplates, preserveScroll);
    local configuredCount = #snapshot.creatureTemplateInteractions;
    if snapshot.dataState == "Loading" then browserState:SetText("Loading creature interaction data...");
    elseif snapshot.dataState == "Failed" then browserState:SetText("Unavailable or interaction.view was denied.");
    elseif snapshot.dataState ~= "Ready" then browserState:SetText("Creature interaction data is unavailable while disconnected.");
    else browserState:SetText(`{#visibleTemplates} shown / {#snapshot.creatureTemplates} templates - {configuredCount} configured`); end
    configuredButton:SetText(if configuredOnly then "Show All" else "Show Configured");
end

local function ReadRange(policy) : number?
    if policy == 0i then return 0.0; end
    local value = tonumber(rangeInput.text);
    if value == nil or value ~= value or value <= 0.0 or value > 3.4e38 then return nil; end
    return value;
end

RefreshInspector = function()
    refreshingFields = true;
    local definition = FindTemplate(selectedTemplateID);
    local provider = FindProvider(selectedTemplateID);
    local gossip = FindGossip(selectedTemplateID);
    local ready = snapshot.dataState == "Ready" and pendingRequestID == 0i and not awaitingSnapshot;
    local menuOptions = MenuOptions();
    policyDropdown:SetOptions(RangePolicyOptions());
    rootMenuDropdown:SetOptions(menuOptions);
    if definition then
        templateTitle:SetText(`{definition.name} - Template #{definition.id}`);
        templateSubtitle:SetText(if definition.subname ~= "" then definition.subname else "No subname");
    else
        templateTitle:SetText("No creature template selected");
        templateSubtitle:SetText("");
    end
    if provider then
        providerState:SetText("Interaction Provider: Enabled");
        providerState:SetColor(colors.success);
        policyDropdown:SetValue(provider.rangePolicy, false);
        rangeInput:SetText(tostring(provider.interactionRange));
    else
        providerState:SetText("Interaction Provider: Not Configured");
        providerState:SetColor(colors.muted);
        policyDropdown:SetValue(0i, false);
        rangeInput:SetText("0");
    end
    if gossip then rootMenuDropdown:SetValue(gossip.rootMenuID, false);
    elseif #menuOptions > 0 then rootMenuDropdown:SetValue(menuOptions[1].value, false); end

    local hasSelection = definition ~= nil;
    local providerEnabled = provider ~= nil;
    providerForm:SetVisible(hasSelection);
    gossipTitle:SetVisible(hasSelection);
    gossipExplanation:SetVisible(hasSelection);
    gossipForm:SetVisible(hasSelection);
    enablePanel:SetVisible(hasSelection and not providerEnabled);
    saveProviderButton:SetVisible(providerEnabled);
    removeProviderButton:SetVisible(providerEnabled);
    saveGossipButton:SetVisible(providerEnabled and gossip ~= nil);
    policyDropdown:SetInteractable(ready and hasSelection);
    rangeInput.button:SetInteractable(ready and hasSelection and policyDropdown:GetValue() == 1i);
    rootMenuDropdown:SetInteractable(ready and hasSelection and #menuOptions > 0);
    saveProviderButton:SetInteractable(ready and providerEnabled and ReadRange(policyDropdown:GetValue()) ~= nil);
    removeProviderButton:SetInteractable(ready and providerEnabled);
    saveGossipButton:SetInteractable(ready and providerEnabled and gossip ~= nil and #menuOptions > 0);
    enableButton:SetInteractable(ready and hasSelection and not providerEnabled and #menuOptions > 0 and ReadRange(policyDropdown:GetValue()) ~= nil);
    providerForm:Invalidate();
    gossipForm:Invalidate();
    inspector:Invalidate();
    statusText:SetText(statusMessage);
    statusText:SetColor(statusColor);
    refreshingFields = false;
end

ApplySnapshot = function(preserveScroll)
    snapshot = InteractionEditor.GetSnapshot();
    for _, definition in snapshot.creatureTemplates do
        definition.name = ToDisplayASCII(definition.name);
        definition.subname = ToDisplayASCII(definition.subname);
    end
    for _, menu in snapshot.gossipMenus do
        menu.internalName = ToDisplayASCII(menu.internalName);
    end
    if awaitingSnapshot and snapshot.dataState == "Ready" then
        awaitingSnapshot = false;
        statusMessage = "Authoritative creature interaction data refreshed.";
        statusColor = colors.success;
    end
    if selectAfterSnapshot ~= nil then selectedTemplateID = selectAfterSnapshot; selectAfterSnapshot = nil; end
    if FindTemplate(selectedTemplateID) == nil then selectedTemplateID = nil; end
    RefreshTemplates(preserveScroll);
    RefreshInspector();
end

local function Submit(requestID)
    pendingRequestID = requestID;
    if requestID == 0i then statusMessage = "The mutation could not be submitted."; statusColor = colors.error;
    else statusMessage = `Waiting for server response (request #{requestID})...`; statusColor = colors.muted; end
    RefreshInspector();
end

searchInput:SetOnTextChanged(function() RefreshTemplates(); end);
refreshButton:SetOnMouseUp(function() if InteractionEditor.RequestSnapshot() then ApplySnapshot(true); end end);
configuredButton:SetOnMouseUp(function() configuredOnly = not configuredOnly; RefreshTemplates(); end);
menusButton:SetOnMouseUp(function() EditorRegistry.Toggle("GossipFlowEditor"); end);
policyDropdown:SetOnValueChanged(function(value)
    if refreshingFields then return; end
    if value == 0i then rangeInput:SetText("0");
    elseif tonumber(rangeInput.text) == nil or tonumber(rangeInput.text) <= 0 then rangeInput:SetText("5"); end
    local ready = snapshot.dataState == "Ready" and pendingRequestID == 0i and not awaitingSnapshot and selectedTemplateID ~= nil;
    rangeInput.button:SetInteractable(ready and value == 1i);
end);
enableButton:SetOnMouseUp(function()
    local definition = FindTemplate(selectedTemplateID);
    local range = ReadRange(policyDropdown:GetValue());
    if definition == nil or range == nil or rootMenuDropdown:GetValue() == nil then return; end
    selectAfterSnapshot = definition.id;
    Submit(InteractionEditor.CreateCreatureTemplateInteraction(definition.id, policyDropdown:GetValue(), range, 0i, rootMenuDropdown:GetValue(), 0i));
end);
saveProviderButton:SetOnMouseUp(function()
    local provider = FindProvider(selectedTemplateID);
    local range = ReadRange(policyDropdown:GetValue());
    if provider == nil or range == nil then statusMessage = "Custom interaction distance must be greater than zero."; statusColor = colors.error; RefreshInspector(); return; end
    selectAfterSnapshot = provider.creatureTemplateID;
    Submit(InteractionEditor.UpdateCreatureTemplateInteraction(provider.creatureTemplateID, policyDropdown:GetValue(), range, provider.flags));
end);
saveGossipButton:SetOnMouseUp(function()
    local gossip = FindGossip(selectedTemplateID);
    if gossip == nil or rootMenuDropdown:GetValue() == nil then return; end
    selectAfterSnapshot = gossip.creatureTemplateID;
    Submit(InteractionEditor.UpdateCreatureTemplateGossip(gossip.creatureTemplateID, rootMenuDropdown:GetValue(), gossip.flags));
end);
removeProviderButton:SetOnMouseUp(function()
    local definition = FindTemplate(selectedTemplateID);
    if definition == nil or FindProvider(definition.id) == nil then return; end
    removeModal:Open({ message = `Remove the native interaction provider and gossip binding from every spawn of "{definition.name}" (template #{definition.id})?`, onConfirm = function()
        selectAfterSnapshot = definition.id;
        Submit(InteractionEditor.DeleteCreatureTemplateInteraction(definition.id));
    end });
end);

local methods = {};
function methods:Show()
    root:SetVisible(true);
    snapshot = InteractionEditor.GetSnapshot();
    if snapshot.dataState == "Unavailable" or snapshot.dataState == "Failed" then InteractionEditor.RequestSnapshot(); end
    ApplySnapshot();
end
function methods:Hide() root:SetVisible(false); removeModal:Close(); end
function methods:IsVisible() return root:IsVisible(); end
shell.window.closeButton:SetOnMouseUp(function() methods:Hide(); end);
EditorRegistry.Register("CreatureInteractionEditor", methods);

local lastState = "Unavailable";
RegisterEvent(GameEvent.Updated, function()
    if not root:IsVisible() then return; end
    if pendingRequestID ~= 0i then
        local result = InteractionEditor.TakeMutationResult(pendingRequestID);
        if result then
            pendingRequestID = 0i;
            if result.succeeded then
                if result.artifact == 10i or result.artifact == 11i then selectAfterSnapshot = result.artifactID; end
                awaitingSnapshot = InteractionEditor.RequestSnapshot();
                statusMessage = if awaitingSnapshot then "Mutation accepted; refreshing creature interaction data..." else "Mutation accepted, but refresh could not start.";
                statusColor = if awaitingSnapshot then colors.success else colors.warning;
            else
                statusMessage = if result.response ~= "" then result.response else "The server rejected the mutation.";
                statusColor = colors.error;
            end
            RefreshInspector();
        end
    end
    local state = InteractionEditor.GetState();
    if state ~= lastState or (awaitingSnapshot and state == "Ready") then lastState = state; ApplySnapshot(true); end
end);

RefreshTemplates(false);
RefreshInspector();
