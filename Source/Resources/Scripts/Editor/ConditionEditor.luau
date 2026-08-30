local BrowserList = require("@src/API/Editor/UI/BrowserList");
local BrowserWindow = require("@src/API/Editor/UI/BrowserWindow");
local Button = require("@src/API/UI/Button");
local Dropdown = require("@src/API/UI/Dropdown");
local EditorRegistry = require("@src/API/Editor/Registry");
local GridLayout = require("@src/API/UI/GridLayout");
local HorizontalLayout = require("@src/API/UI/HorizontalLayout");
local InputField = require("@src/API/Editor/UI/InputField");
local Modal = require("@src/API/UI/Modal");
local TextUtil = require("@src/API/UI/TextUtil");
local Theme = require("@src/API/Editor/UI/Theme");
local VerticalLayout = require("@src/API/UI/VerticalLayout");

local WINDOW_WIDTH = 980;
local WINDOW_HEIGHT = 700;
local LEFT_WIDTH = 300;
local TEXT = Theme.textTemplate;
local LAYER = 1160i;
local colors = { muted = vec.New(0.58, 0.62, 0.69), success = vec.New(0.38, 0.82, 0.52), warning = vec.New(0.94, 0.72, 0.30), error = vec.New(0.94, 0.38, 0.38) };

local canvas = UI.GetCanvas("ConditionEditor", vec.New(0, 0), vec.New(1920, 1080), false);
local shell = BrowserWindow.New(canvas, vec.New(470, -80), vec.New(WINDOW_WIDTH, WINDOW_HEIGHT), LAYER, {
    title = "Condition Editor", titleHeight = 22, toolbarHeight = 24, browserWidth = LEFT_WIDTH,
});
local root = shell.root;
local toolbar = shell.toolbar;
local browser = shell.browser;
local inspector = shell.inspector;
local RIGHT_WIDTH = shell.inspectorWidth;

local refreshButton = Button.New(toolbar, vec.New(0, 0), vec.New(74, 22), 1i, { text = "Refresh", textTemplate = TEXT });
local newSetButton = Button.New(toolbar, vec.New(0, 0), vec.New(104, 22), 1i, { text = "New Set", textTemplate = TEXT });
local searchInput = InputField.New(toolbar, vec.New(0, 0), vec.New(300, 22), 1i, { textTemplate = TEXT, placeholderText = "Search condition sets" });
toolbar:SetChildOpts(refreshButton, { sizeX = 74, sizeY = 22 });
toolbar:SetChildOpts(newSetButton, { sizeX = 104, sizeY = 22 });
toolbar:SetChildOpts(searchInput, { modeX = "flex", sizeY = 22 });

local browserTitle = browser:NewText("Reusable Condition Sets", vec.New(0, 0), 1i, TEXT);
browserTitle:SetColor(Theme.selectedColor);
browser:SetChildOpts(browserTitle, { sizeY = 18 });
local browserState = browser:NewText("Condition data has not been requested.", vec.New(0, 0), 1i, TEXT);
browserState:SetColor(colors.muted);
browser:SetChildOpts(browserState, { sizeY = 18 });

local snapshot = { dataState = "Unavailable", conditionSets = {}, conditionGroups = {}, conditions = {}, conditionDescriptors = {}, conditionComparisons = {}, conditionGroupOperators = {} };
local visibleSets = {};
local selectedSet : any = nil;
local selectedNode : any = nil;
local treeItems = {};
local pendingRequestID = 0i;
local awaitingSnapshot = false;
local selectAfterSnapshot : integer? = nil;
local statusMessage = "Select a condition set to edit its expression tree.";
local statusColor = colors.muted;
local refreshingFields = false;

local comparisonLabels = {
    None = "None",
    Equal = "Equals",
    NotEqual = "Does not equal",
    Less = "Less than",
    LessOrEqual = "Less than or equal",
    Greater = "Greater than",
    GreaterOrEqual = "Greater than or equal",
    HasAllBits = "Has all bits",
    HasAnyBits = "Has any bits",
};

local function FormatIdentifier(value)
    local spaced = string.gsub(value, "(%l)(%u)", "%1 %2");
    return string.upper(string.sub(spaced, 1, 1)) .. string.sub(spaced, 2);
end

local function FormatComparison(value)
    return comparisonLabels[value] or FormatIdentifier(value);
end

local RefreshSets : (boolean?) -> () = function(_) end;
local RefreshTree : (boolean?) -> () = function(_) end;
local RefreshInspector : () -> () = function() end;
local ApplySnapshot : (boolean?) -> () = function(_) end;

local setList : any = nil;
setList = BrowserList.New(browser, vec.New(0, 0), vec.New(LEFT_WIDTH - 16, shell.bodyHeight - 68), 1i, {
    rowHeight = 40, textTemplate = TEXT,
    getPrimaryText = function(set) return set.internalName; end,
    getSecondaryText = function(set) return `Condition set #{set.id}`; end,
    isSelected = function(set) return selectedSet ~= nil and selectedSet.id == set.id; end,
    onActivate = function(set)
        if pendingRequestID ~= 0i or awaitingSnapshot then return; end
        selectedSet = set;
        selectedNode = nil;
        RefreshSets(true);
        RefreshTree();
        RefreshInspector();
    end,
});
browser:SetChildOpts(setList, { modeX = "flex", modeY = "flex" });

local setTitle = inspector:NewText("Condition Set", vec.New(0, 0), 1i, TEXT);
setTitle:SetColor(Theme.selectedColor);
inspector:SetChildOpts(setTitle, { sizeY = 18 });
local setRow = HorizontalLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 26), 1i, { panelTemplate = "DebugDarkGrey", alpha = 0.0, spacing = 6 });
inspector:SetChildOpts(setRow, { modeX = "flex", sizeY = 26 });
local setNameInput = InputField.New(setRow, vec.New(0, 0), vec.New(300, 24), 1i, { textTemplate = TEXT, placeholderText = "Internal name" });
local saveSetButton = Button.New(setRow, vec.New(0, 0), vec.New(86, 24), 1i, { text = "Save Set", textTemplate = TEXT });
local deleteSetButton = Button.New(setRow, vec.New(0, 0), vec.New(86, 24), 1i, { text = "Delete Set", textTemplate = TEXT });
setRow:SetChildOpts(setNameInput, { modeX = "flex", sizeY = 24 });
setRow:SetChildOpts(saveSetButton, { sizeX = 86, sizeY = 24 });
setRow:SetChildOpts(deleteSetButton, { sizeX = 86, sizeY = 24 });

local treeToolbar = HorizontalLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 26), 1i, { panelTemplate = "DebugDarkGrey", alpha = 0.0, spacing = 6 });
inspector:SetChildOpts(treeToolbar, { modeX = "flex", sizeY = 26 });
local addConditionButton = Button.New(treeToolbar, vec.New(0, 0), vec.New(106, 24), 1i, { text = "Add Condition", textTemplate = TEXT });
local addGroupButton = Button.New(treeToolbar, vec.New(0, 0), vec.New(92, 24), 1i, { text = "Add Group", textTemplate = TEXT });
local deleteNodeButton = Button.New(treeToolbar, vec.New(0, 0), vec.New(92, 24), 1i, { text = "Delete Node", textTemplate = TEXT });
treeToolbar:SetChildOpts(addConditionButton, { sizeX = 106, sizeY = 24 });
treeToolbar:SetChildOpts(addGroupButton, { sizeX = 92, sizeY = 24 });
treeToolbar:SetChildOpts(deleteNodeButton, { sizeX = 92, sizeY = 24 });

local treeList : any = nil;
treeList = BrowserList.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 250), 1i, {
    rowHeight = 38, textTemplate = TEXT,
    getPrimaryText = function(node)
        local indent = string.rep("    ", node.depth);
        if node.kind == "group" then return `{indent}{if node.negated then "NOT " else ""}{if node.groupOperator == 0i then "All of" else "Any of"}`; end
        return `{indent}{node.typeName} {node.comparisonName} {node.parameters[1]}`;
    end,
    getSecondaryText = function(node) return if node.kind == "group" then `Group #{node.id}` else `Condition #{node.id} - order {node.orderIndex}`; end,
    isSelected = function(node) return selectedNode ~= nil and selectedNode.kind == node.kind and selectedNode.id == node.id; end,
    onActivate = function(node) selectedNode = node; RefreshTree(true); RefreshInspector(); end,
});
inspector:SetChildOpts(treeList, { modeX = "flex", sizeY = 250 });

local nodeTitle = inspector:NewText("Node Properties", vec.New(0, 0), 1i, TEXT);
nodeTitle:SetColor(Theme.selectedColor);
inspector:SetChildOpts(nodeTitle, { sizeY = 18 });
local nodeForm = GridLayout.New(inspector, vec.New(0, 0), vec.New(RIGHT_WIDTH - 20, 104), 1i, {
    columns = 4, cellSizeX = (RIGHT_WIDTH - 34) / 4, cellSizeY = 48, spacing = 4, padding = 2,
    cellAlignH = "stretch", cellAlignV = "center", panelTemplate = "DebugDarkGrey", alpha = 0.0,
});
inspector:SetChildOpts(nodeForm, { modeX = "flex", sizeY = 104 });

local function NewPropertyField(labelText)
    local field = VerticalLayout.New(nodeForm, vec.New(0, 0), vec.New(140, 46), 1i, { spacing = 2, panelTemplate = "DebugDarkGrey", alpha = 0.0 });
    local label = field:NewText(labelText, vec.New(0, 0), 1i, TEXT);
    label:SetColor(colors.muted);
    field:SetChildOpts(label, { modeX = "flex", sizeY = 16 });
    return field, label;
end

local typeField = NewPropertyField("Condition Type");
local typeDropdown = Dropdown.New(typeField, canvas, vec.New(0, 0), vec.New(140, 24), 1i, { options = {}, placeholder = "Condition type", button = { textTemplate = TEXT } });
typeField:SetChildOpts(typeDropdown, { modeX = "flex", sizeY = 24 });
local comparisonField = NewPropertyField("Comparison");
local comparisonDropdown = Dropdown.New(comparisonField, canvas, vec.New(0, 0), vec.New(140, 24), 1i, { options = {}, placeholder = "Comparison", button = { textTemplate = TEXT } });
comparisonField:SetChildOpts(comparisonDropdown, { modeX = "flex", sizeY = 24 });
local operatorField = NewPropertyField("Group Logic");
local operatorDropdown = Dropdown.New(operatorField, canvas, vec.New(0, 0), vec.New(140, 24), 1i, { options = {}, placeholder = "Group operator", button = { textTemplate = TEXT } });
operatorField:SetChildOpts(operatorDropdown, { modeX = "flex", sizeY = 24 });
local negatedField = NewPropertyField("Result");
local negatedDropdown = Dropdown.New(negatedField, canvas, vec.New(0, 0), vec.New(140, 24), 1i, { options = { { label = "Normal", value = false }, { label = "Negated", value = true } }, value = false, button = { textTemplate = TEXT } });
negatedField:SetChildOpts(negatedDropdown, { modeX = "flex", sizeY = 24 });
local parameterFields = {};
local parameterLabels = {};
local parameterInputs = {};
for index = 1, 4 do
    parameterFields[index], parameterLabels[index] = NewPropertyField(`Parameter {index}`);
    parameterInputs[index] = InputField.New(parameterFields[index], vec.New(0, 0), vec.New(140, 24), 1i, { textTemplate = TEXT, placeholderText = `Parameter {index}` });
    parameterFields[index]:SetChildOpts(parameterInputs[index], { modeX = "flex", sizeY = 24 });
end
local orderField = NewPropertyField("Evaluation Order");
local orderInput = InputField.New(orderField, vec.New(0, 0), vec.New(140, 24), 1i, { textTemplate = TEXT, placeholderText = "Order" });
orderField:SetChildOpts(orderInput, { modeX = "flex", sizeY = 24 });
local saveField = NewPropertyField("Actions");
local saveNodeButton = Button.New(saveField, vec.New(0, 0), vec.New(140, 24), 1i, { text = "Save Node", textTemplate = TEXT });
saveField:SetChildOpts(saveNodeButton, { modeX = "flex", sizeY = 24 });

local statusText = inspector:NewText(statusMessage, vec.New(0, 0), 1i, TEXT);
statusText:SetWrapWidth(RIGHT_WIDTH - 20);
inspector:SetChildOpts(statusText, { modeX = "flex", sizeY = 44 });
local deleteModal = Modal.New(canvas, vec.New(0, 0), vec.New(440, 170), integer.add(LAYER, 100i), {
    name = "ConditionEditorDelete", title = "Delete authored condition data", message = "", confirmText = "Delete", cancelText = "Cancel",
});

local function FindDescriptor(conditionType)
    for _, descriptor in snapshot.conditionDescriptors do if descriptor.type == conditionType then return descriptor; end end
    return nil;
end

local function ComparisonOptions(descriptor)
    local options = {};
    if descriptor == nil then return options; end
    local comparisonMask = integer.tonumber(descriptor.comparisonMask);
    for _, comparison in snapshot.conditionComparisons do
        local power = 2 ^ integer.tonumber(comparison.value);
        if math.floor(comparisonMask / power) % 2 == 1 then table.insert(options, { label = FormatComparison(comparison.name), value = comparison.value }); end
    end
    if #options == 0 then
        for comparisonValue = 0, 15 do
            if math.floor(comparisonMask / (2 ^ comparisonValue)) % 2 == 1 then
                local value = integer.create(comparisonValue);
                table.insert(options, { label = `Comparison {value}`, value = value });
            end
        end
    end
    return options;
end

local function DefaultCondition()
    local descriptor = snapshot.conditionDescriptors[1];
    if descriptor == nil then return nil; end
    local comparisons = ComparisonOptions(descriptor);
    if #comparisons == 0 then return nil; end
    local parameters = { 0i, 0i, 0i, 0i };
    for index = 1, 4 do parameters[index] = if descriptor.parameters[index] then descriptor.parameters[index].minimum else 0i; end
    return { conditionType = descriptor.type, comparison = comparisons[1].value, parameters = parameters };
end

local function Submit(requestID)
    pendingRequestID = requestID;
    if requestID == 0i then statusMessage = "The mutation could not be submitted."; statusColor = colors.error;
    else statusMessage = `Waiting for server response (request #{requestID})...`; statusColor = colors.muted; end
    RefreshInspector();
end

local function ParentGroupID() : integer?
    if selectedNode == nil then
        for _, group in snapshot.conditionGroups do if selectedSet and group.conditionSetID == selectedSet.id and group.parentGroupID == 0i then return group.id; end end
        return nil;
    end
    return if selectedNode.kind == "group" then selectedNode.id else selectedNode.conditionGroupID;
end

RefreshSets = function(preserveScroll)
    visibleSets = {};
    local editableSetCount = 0;
    local filter = string.lower(searchInput.text or "");
    for _, set in snapshot.conditionSets do
        if set.id ~= 0i then
            editableSetCount += 1;
            if filter == "" or TextUtil.ContainsIgnoreCase(set.internalName, filter) or TextUtil.ContainsIgnoreCase(tostring(set.id), filter) then table.insert(visibleSets, set); end
        end
    end
    table.sort(visibleSets, function(left, right) return integer.lt(left.id, right.id); end);
    setList:SetItems(visibleSets, preserveScroll);
    if snapshot.dataState == "Loading" then browserState:SetText("Loading condition data...");
    elseif snapshot.dataState == "Failed" then browserState:SetText("Unavailable or interaction.view was denied.");
    elseif snapshot.dataState ~= "Ready" then browserState:SetText("Condition data is unavailable while disconnected.");
    else browserState:SetText(`{#visibleSets} shown / {editableSetCount} total`); end
end

RefreshTree = function(preserveScroll)
    treeItems = {};
    if selectedSet == nil then treeList:SetItems(treeItems); return; end
    local descriptors = {};
    for _, descriptor in snapshot.conditionDescriptors do descriptors[descriptor.type] = descriptor; end
    local comparisons = {};
    for _, comparison in snapshot.conditionComparisons do comparisons[comparison.value] = FormatComparison(comparison.name); end
    local groupsByParent = {};
    local conditionsByGroup = {};
    for _, group in snapshot.conditionGroups do
        if group.conditionSetID == selectedSet.id then
            local key = group.parentGroupID;
            groupsByParent[key] = groupsByParent[key] or {};
            table.insert(groupsByParent[key], group);
        end
    end
    for _, condition in snapshot.conditions do
        conditionsByGroup[condition.conditionGroupID] = conditionsByGroup[condition.conditionGroupID] or {};
        table.insert(conditionsByGroup[condition.conditionGroupID], condition);
    end
    local function SortNodes(values) table.sort(values, function(left, right) return left.orderIndex == right.orderIndex and integer.lt(left.id, right.id) or integer.lt(left.orderIndex, right.orderIndex); end); end
    local function AppendGroup(group, depth)
        local groupNode = { kind = "group", depth = depth, id = group.id, conditionSetID = group.conditionSetID, parentGroupID = group.parentGroupID, groupOperator = group.groupOperator, negated = group.negated, orderIndex = group.orderIndex };
        table.insert(treeItems, groupNode);
        local childGroups = groupsByParent[group.id] or {};
        local conditions = conditionsByGroup[group.id] or {};
        local children = {};
        for _, childGroup in childGroups do table.insert(children, { kind = "group", value = childGroup, orderIndex = childGroup.orderIndex, id = childGroup.id }); end
        for _, condition in conditions do table.insert(children, { kind = "condition", value = condition, orderIndex = condition.orderIndex, id = condition.id }); end
        table.sort(children, function(left, right)
            if left.orderIndex ~= right.orderIndex then return integer.lt(left.orderIndex, right.orderIndex); end
            if left.kind ~= right.kind then return left.kind == "group"; end
            return integer.lt(left.id, right.id);
        end);
        for _, child in children do
            if child.kind == "group" then AppendGroup(child.value, depth + 1);
            else
                local condition = child.value;
                local descriptor = descriptors[condition.conditionType];
                table.insert(treeItems, { kind = "condition", depth = depth + 1, id = condition.id, conditionGroupID = condition.conditionGroupID,
                    orderIndex = condition.orderIndex, conditionType = condition.conditionType, comparison = condition.comparison, parameters = condition.parameters,
                    typeName = if descriptor then FormatIdentifier(descriptor.name) else `Unknown ({condition.conditionType})`, comparisonName = comparisons[condition.comparison] or tostring(condition.comparison) });
            end
        end
    end
    local roots = groupsByParent[0i] or {};
    SortNodes(roots);
    for _, rootGroup in roots do AppendGroup(rootGroup, 0); end
    treeList:SetItems(treeItems, preserveScroll);
end

RefreshInspector = function()
    refreshingFields = true;
    setNameInput:SetText(if selectedSet then selectedSet.internalName else "");
    local descriptorOptions = {};
    for _, descriptor in snapshot.conditionDescriptors do table.insert(descriptorOptions, { label = FormatIdentifier(descriptor.name), value = descriptor.type }); end
    typeDropdown:SetOptions(descriptorOptions);
    local groupOperatorOptions = {};
    for _, groupOperator in snapshot.conditionGroupOperators do
        table.insert(groupOperatorOptions, { label = if groupOperator.name == "All" then "All conditions" else "Any condition", value = groupOperator.value });
    end
    operatorDropdown:SetOptions(groupOperatorOptions);
    if selectedNode and selectedNode.kind == "condition" then
        local descriptor = FindDescriptor(selectedNode.conditionType);
        typeDropdown:SetValue(selectedNode.conditionType, false);
        comparisonDropdown:SetOptions(ComparisonOptions(descriptor));
        comparisonDropdown:SetValue(selectedNode.comparison, false);
        orderInput:SetText(tostring(selectedNode.orderIndex));
        for index = 1, 4 do
            parameterInputs[index]:SetText(tostring(selectedNode.parameters[index]));
            parameterLabels[index]:SetText(if descriptor and descriptor.parameters[index].kind ~= 0i then FormatIdentifier(descriptor.parameters[index].name) else `Parameter {index}`);
        end
        nodeTitle:SetText(`Edit Condition #{selectedNode.id}`);
    elseif selectedNode and selectedNode.kind == "group" then
        nodeTitle:SetText(`Edit Group #{selectedNode.id}`);
        operatorDropdown:SetValue(selectedNode.groupOperator, false);
        negatedDropdown:SetValue(selectedNode.negated, false);
        orderInput:SetText(tostring(selectedNode.orderIndex));
        for index = 1, 4 do parameterInputs[index]:SetText(""); end
    else
        nodeTitle:SetText("Node Properties");
        comparisonDropdown:SetOptions({}); orderInput:SetText("");
        for index = 1, 4 do parameterInputs[index]:SetText(""); end
    end
    refreshingFields = false;
    local ready = snapshot.dataState == "Ready" and pendingRequestID == 0i and not awaitingSnapshot;
    refreshButton:SetInteractable(not awaitingSnapshot and pendingRequestID == 0i);
    newSetButton:SetInteractable(ready and DefaultCondition() ~= nil);
    setNameInput.button:SetInteractable(ready and selectedSet ~= nil);
    saveSetButton:SetInteractable(ready and selectedSet ~= nil and setNameInput.text ~= "");
    deleteSetButton:SetInteractable(ready and selectedSet ~= nil);
    addConditionButton:SetInteractable(ready and selectedSet ~= nil and DefaultCondition() ~= nil);
    addGroupButton:SetInteractable(ready and selectedSet ~= nil and DefaultCondition() ~= nil);
    deleteNodeButton:SetInteractable(ready and selectedNode ~= nil and not (selectedNode.kind == "group" and selectedNode.parentGroupID == 0i));
    local conditionSelected = selectedNode ~= nil and selectedNode.kind == "condition";
    local groupSelected = selectedNode ~= nil and selectedNode.kind == "group";
    typeField:SetVisible(conditionSelected);
    comparisonField:SetVisible(conditionSelected);
    operatorField:SetVisible(groupSelected);
    negatedField:SetVisible(groupSelected);
    orderField:SetVisible(selectedNode ~= nil);
    saveField:SetVisible(selectedNode ~= nil);
    typeDropdown:SetInteractable(ready and selectedNode ~= nil and selectedNode.kind == "condition");
    comparisonDropdown:SetInteractable(ready and selectedNode ~= nil and selectedNode.kind == "condition");
    operatorDropdown:SetInteractable(ready and selectedNode ~= nil and selectedNode.kind == "group");
    negatedDropdown:SetInteractable(ready and selectedNode ~= nil and selectedNode.kind == "group");
    orderInput.button:SetInteractable(ready and selectedNode ~= nil);
    saveNodeButton:SetInteractable(ready and selectedNode ~= nil);
    local selectedDescriptor = if selectedNode and selectedNode.kind == "condition" then FindDescriptor(selectedNode.conditionType) else nil;
    for index = 1, 4 do
        local parameterVisible = selectedDescriptor ~= nil and selectedDescriptor.parameters[index].kind ~= 0i;
        parameterFields[index]:SetVisible(parameterVisible);
        parameterInputs[index].button:SetInteractable(ready and parameterVisible);
    end
    nodeForm:Invalidate();
    statusText:SetText(statusMessage); statusText:SetColor(statusColor);
end

ApplySnapshot = function(preserveScroll)
    snapshot = InteractionEditor.GetSnapshot();
    if awaitingSnapshot and snapshot.dataState == "Ready" then
        awaitingSnapshot = false;
        if selectAfterSnapshot ~= nil then for _, set in snapshot.conditionSets do if set.id == selectAfterSnapshot then selectedSet = set; break; end end end
        selectAfterSnapshot = nil;
        statusMessage = "Authoritative condition data refreshed."; statusColor = colors.success;
    end
    if selectedSet then for _, set in snapshot.conditionSets do if set.id == selectedSet.id then selectedSet = set; break; end end end
    selectedNode = nil;
    RefreshSets(preserveScroll); RefreshTree(); RefreshInspector();
end

searchInput:SetOnTextChanged(function() RefreshSets(); end);
refreshButton:SetOnMouseUp(function() if InteractionEditor.RequestSnapshot() then ApplySnapshot(true); end end);
newSetButton:SetOnMouseUp(function()
    local default = DefaultCondition(); if default == nil then return; end
    local suffix = 1i;
    for _, set in snapshot.conditionSets do if integer.ge(set.id, suffix) then suffix = integer.add(set.id, 1i); end end
    local name = `condition_set_{suffix}`;
    Submit(InteractionEditor.CreateConditionSet(name, 0i, default.conditionType, default.comparison, default.parameters[1], default.parameters[2], default.parameters[3], default.parameters[4]));
end);
saveSetButton:SetOnMouseUp(function() if selectedSet and setNameInput.text ~= "" then selectAfterSnapshot = selectedSet.id; Submit(InteractionEditor.UpdateConditionSet(selectedSet.id, setNameInput.text)); end end);
deleteSetButton:SetOnMouseUp(function()
    if selectedSet == nil then return; end
    local set = selectedSet;
    deleteModal:Open({ message = `Delete condition set "{set.internalName}" and its complete expression tree?`, onConfirm = function()
        Submit(InteractionEditor.DeleteConditionSet(set.id)); selectedSet = nil; selectedNode = nil;
    end });
end);
addConditionButton:SetOnMouseUp(function()
    local groupID = ParentGroupID(); local default = DefaultCondition(); if groupID == nil or default == nil then return; end
    selectAfterSnapshot = selectedSet.id;
    Submit(InteractionEditor.CreateCondition(groupID, 100i, default.conditionType, default.comparison, default.parameters[1], default.parameters[2], default.parameters[3], default.parameters[4]));
end);
addGroupButton:SetOnMouseUp(function()
    local parentID = ParentGroupID(); local default = DefaultCondition(); if parentID == nil or default == nil then return; end
    selectAfterSnapshot = selectedSet.id;
    Submit(InteractionEditor.CreateConditionGroup(selectedSet.id, parentID, 0i, false, 100i, default.conditionType, default.comparison,
        default.parameters[1], default.parameters[2], default.parameters[3], default.parameters[4]));
end);
deleteNodeButton:SetOnMouseUp(function()
    if selectedNode == nil then return; end
    local node = selectedNode;
    deleteModal:Open({ message = if node.kind == "group" then "Delete this group, all nested groups, and their conditions?" else "Delete this condition?",
        onConfirm = function()
            selectAfterSnapshot = selectedSet.id;
            Submit(if node.kind == "group" then InteractionEditor.DeleteConditionGroup(node.id) else InteractionEditor.DeleteCondition(node.id));
        end });
end);
saveNodeButton:SetOnMouseUp(function()
    if selectedNode == nil then return; end
    selectAfterSnapshot = selectedSet.id;
    local order = integer.create(tonumber(orderInput.text) or 0);
    if selectedNode.kind == "group" then Submit(InteractionEditor.UpdateConditionGroup(selectedNode.id, operatorDropdown:GetValue(), negatedDropdown:GetValue(), order)); return; end
    local parameters = {};
    for index = 1, 4 do parameters[index] = integer.create(tonumber(parameterInputs[index].text) or 0); end
    Submit(InteractionEditor.UpdateCondition(selectedNode.id, selectedNode.conditionGroupID, order, typeDropdown:GetValue(), comparisonDropdown:GetValue(),
        parameters[1], parameters[2], parameters[3], parameters[4]));
end);
typeDropdown:SetOnValueChanged(function(value)
    if refreshingFields or selectedNode == nil or selectedNode.kind ~= "condition" then return; end
    selectedNode.conditionType = value;
    local descriptor = FindDescriptor(value); local options = ComparisonOptions(descriptor);
    if #options > 0 then selectedNode.comparison = options[1].value; end
    if descriptor then for index = 1, 4 do selectedNode.parameters[index] = descriptor.parameters[index].minimum; end end
    RefreshInspector();
end);

local methods = {};
function methods:Show()
    root:SetVisible(true); snapshot = InteractionEditor.GetSnapshot();
    if snapshot.dataState == "Unavailable" or snapshot.dataState == "Failed" then InteractionEditor.RequestSnapshot(); end
    ApplySnapshot();
end
function methods:Hide() root:SetVisible(false); deleteModal:Close(); end
function methods:IsVisible() return root:IsVisible(); end
shell.window.closeButton:SetOnMouseUp(function() methods:Hide(); end);
EditorRegistry.Register("ConditionEditor", methods);

local lastState = "Unavailable";
RegisterEvent(GameEvent.Updated, function()
    if not root:IsVisible() then return; end
    if pendingRequestID ~= 0i then
        local result = InteractionEditor.TakeMutationResult(pendingRequestID);
        if result then
            pendingRequestID = 0i;
            if result.succeeded then
                if result.artifact == 3i then selectAfterSnapshot = result.artifactID; end
                awaitingSnapshot = InteractionEditor.RequestSnapshot();
                statusMessage = if awaitingSnapshot then "Mutation accepted; refreshing condition data..." else "Mutation accepted, but refresh could not start.";
                statusColor = if awaitingSnapshot then colors.success else colors.warning;
            else statusMessage = if result.response ~= "" then result.response else "The server rejected the mutation."; statusColor = colors.error; end
            RefreshInspector();
        end
    end
    local state = InteractionEditor.GetState();
    if state ~= lastState or (awaitingSnapshot and state == "Ready") then lastState = state; ApplySnapshot(true); end
end);

RefreshSets(false); RefreshTree(false); RefreshInspector();
