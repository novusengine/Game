local Declarative = require("@src/API/UI/Declarative");
local Draggable = require("@src/API/UI/Draggable");
local Button = require("@src/API/UI/Button");
local Dropdown = require("@src/API/UI/Dropdown");
local InputBox = require("@src/API/UI/InputBox");
local InputField = require("@src/API/Editor/UI/InputField");
local Modal = require("@src/API/UI/Modal");
local TextUtil = require("@src/API/UI/TextUtil");
local VirtualList = require("@src/API/UI/VirtualList");
local FrameManager = require("@src/API/Game/FrameManager");
local GossipMenuController = require("@src/API/Editor/GossipMenuController");
local GossipMenuModel = require("@src/API/Editor/GossipMenuModel");
local EditorRegistry = require("@src/API/Editor/Registry");
local SearchPicker = require("@src/API/UI/SearchPicker");

local STATUS = GossipMenuController.StatusLevel;

if GossipFlowEditorView ~= nil then GossipFlowEditorView:Destroy(); end
if GossipFlowLocalizedTextModalView ~= nil then GossipFlowLocalizedTextModalView:Destroy(); end
if GossipFlowLocalizedTextModalRoot ~= nil then GossipFlowLocalizedTextModalRoot:SetEnabled(false); GossipFlowLocalizedTextModalRoot = nil; end

local editor : any = {
    geometry = {
        windowPosition = vec.New(289, 76),
        windowSize = vec.New(1342, 770),
        toolbarSideInset = 16,
        toolbarRegionHeight = 54,
        toolbarHeight = 34,
        bodyEdgeInset = 1,
        bodyTopGap = 6,
        sidebarWidth = 215,
        sidebarPadding = 8,
        sidebarSpacing = 6,
        sidebarHeaderHeight = 20,
        sidebarPickerHeight = 30,
        sidebarSearchHeight = 28,
        sidebarFooterHeight = 24,
        sidebarScrollbarWidth = 8,
        menuRowHeight = 44,
        inspectorWidth = 286,
        inspectorPadding = 8,
        inspectorSpacing = 5,
        inspectorResponseRowHeight = 44,
        nodeWidth = 248,
        nodeHeaderHeight = 30,
        nodeHeaderIDWidth = 50,
        nodeHeaderGap = 6,
        nodeContentInset = 8,
        nodeGreetingInset = 6,
        nodeResponseInset = 12,
        nodeResponseTextInset = 6,
        nodeGreetingHeight = 34,
        nodeResponsePrimaryHeight = 18,
        nodeResponseSecondaryHeight = 14,
        nodeResponsePaddingY = 4,
        nodeSeparatorHeight = 1,
        nodeBottomPadding = 8,
        nodeRowGap = 36,
        nodeColumnGap = 156,
        graphControlsSize = vec.New(256, 34),
    },
    colors = {
        muted = vec.New(0.58, 0.62, 0.69),
        warning = vec.New(0.94, 0.72, 0.30),
        success = vec.New(0.42, 0.78, 0.50),
        nodeFill = vec.New(0.095, 0.12, 0.15),
        nodeBorder = vec.New(0.30, 0.52, 0.72),
        rootBorder = vec.New(0.74, 0.56, 0.28),
        rootSelectedBorder = vec.New(0.96, 0.76, 0.34),
        selectedBorder = vec.New(0.48, 0.78, 0.98),
        nodeSeparator = vec.New(0.17, 0.20, 0.23),
        nodeResponseFill = vec.New(0.095, 0.12, 0.15),
        nodeResponseHover = vec.New(0.13, 0.20, 0.28),
        nodeResponseSelected = vec.New(0.16, 0.29, 0.40),
        responseBorder = vec.New(0.22, 0.28, 0.34),
        responsePlaceholderFill = vec.New(0.035, 0.045, 0.055),
        responsePlaceholderText = vec.New(0.30, 0.33, 0.37),
        responsePlaceholderBorder = vec.New(0.12, 0.15, 0.18),
        edge = vec.New(0.36, 0.64, 0.88),
        field = vec.New(0.075, 0.09, 0.105),
        panel = vec.New(0.09, 0.105, 0.12),
        activeTab = vec.New(0.14, 0.25, 0.35),
        danger = vec.New(0.52, 0.20, 0.22),
    },
    state = {
        controller = GossipMenuController.New(),
        model = nil,
        rootMenuID = nil,
        rootLayouts = { },
        reachableMenus = { },
        reachableEdges = { },
        menuDepth = { },
        diagnostics = { },
        nodeViews = { },
        nodePositions = { },
        baseNodeSizes = { },
        nodeSizes = { },
        filteredMenuItems = { },
        panActive = false,
        panMouseStart = vec.New(0, 0),
        panOffsetStart = vec.New(0, 0),
        dragMenuID = nil,
        dragGrabOffset = vec.New(0, 0),
        responseDrag = nil,
        pendingSnapshot = false,
        pendingGraphRebuild = false,
        pendingRootMenuCreation = nil,
        flowSave = nil,
        menuDuplicate = nil,
        inspectorPickers = { },
        optionParameterErrors = { },
        localizedText = {
            root = nil,
            dialog = nil,
            title = nil,
            internalNameInput = nil,
            englishInput = nil,
            translatorContext = "",
            translationRows = { },
            translationInputs = { },
            baselineTranslations = { },
            statusText = nil,
            saveButton = nil,
            cancelButton = nil,
            editingID = nil,
            activeTextID = nil,
            targetKind = nil,
            targetID = nil,
            pendingRequestID = 0i,
            awaitingCreate = false,
            operations = { },
            operationIndex = 0,
            isOpen = false,
        },
        inspectorTab = 1i,
        visible = false,
    },
    inspectorTabs = {
        content = 1i,
        logic = 2i,
        metadata = 3i,
    },
    refs = {
        viewport = nil,
        picker = nil,
        sidebarSearch = nil,
        sidebarList = nil,
        reachableCountText = nil,
        diagnosticsBanner = nil,
        diagnosticsText = nil,
        inspectorHost = nil,
        newRootButton = nil,
        flowDirtyText = nil,
        revertFlowButton = nil,
        applyFlowButton = nil,
        inspectorView = nil,
        incomingMenuSelector = nil,
        inspectorTitle = nil,
        inspectorStateText = nil,
        dirtyIndicator = nil,
        zoomText = nil,
    },
};

editor.geometry.toolbarWidth = editor.geometry.windowSize.x - editor.geometry.toolbarSideInset * 2;
editor.geometry.toolbarPosition = vec.New(editor.geometry.toolbarSideInset, editor.geometry.windowSize.y - editor.geometry.toolbarRegionHeight + (editor.geometry.toolbarRegionHeight - editor.geometry.toolbarHeight - editor.geometry.bodyTopGap) / 2);
editor.geometry.bodyPosition = vec.New(editor.geometry.bodyEdgeInset, editor.geometry.bodyEdgeInset);
editor.geometry.bodySize = vec.New(editor.geometry.windowSize.x - editor.geometry.bodyEdgeInset * 2, editor.geometry.windowSize.y - editor.geometry.toolbarRegionHeight - editor.geometry.bodyTopGap - editor.geometry.bodyEdgeInset);
editor.geometry.sidebarInnerWidth = editor.geometry.sidebarWidth - editor.geometry.sidebarPadding * 2;
editor.geometry.sidebarListContentWidth = editor.geometry.sidebarInnerWidth - editor.geometry.sidebarScrollbarWidth;
editor.geometry.viewportSize = vec.New(editor.geometry.bodySize.x - editor.geometry.sidebarWidth - editor.geometry.inspectorWidth, editor.geometry.bodySize.y);
editor.geometry.sidebarListHeight = editor.geometry.viewportSize.y - editor.geometry.sidebarPadding * 2 - editor.geometry.sidebarHeaderHeight - editor.geometry.sidebarPickerHeight - editor.geometry.sidebarSearchHeight - editor.geometry.sidebarFooterHeight - editor.geometry.sidebarSpacing * 4;
editor.geometry.nodeResponseHeight = editor.geometry.nodeResponsePrimaryHeight + editor.geometry.nodeResponseSecondaryHeight + editor.geometry.nodeResponsePaddingY * 2;
editor.geometry.graphCenter = editor.geometry.viewportSize / 2;
editor.geometry.graphControlsPosition = vec.New(10, editor.geometry.viewportSize.y - 44);
editor.geometry.inspectorInnerWidth = editor.geometry.inspectorWidth - editor.geometry.inspectorPadding * 2;
editor.geometry.inspectorResponseListHeight = editor.geometry.viewportSize.y - 350;

editor.state.canvas = UI.GetCanvas("GossipFlowEditor", vec.New(0, 0), vec.New(1920, 1080), false);
editor.state.snapshot = editor.state.controller:GetSnapshot();

local deleteModal = GossipFlowEditorDeleteModal;
if deleteModal == nil then
    deleteModal = Modal.New(editor.state.canvas, vec.New(0, 0), vec.New(450, 175), 1900i, {
        name = "GossipFlowEditorDelete",
        title = "Delete gossip data",
        message = "",
        confirmText = "Delete",
        cancelText = "Cancel",
    });
    GossipFlowEditorDeleteModal = deleteModal;
end

local revertFlowModal = GossipFlowEditorRevertFlowModal;
if revertFlowModal == nil then
    revertFlowModal = Modal.New(editor.state.canvas, vec.New(0, 0), vec.New(450, 175), 1900i, {
        name = "GossipFlowEditorRevertFlow",
        title = "Revert flow changes",
        message = "",
        confirmText = "Revert",
        cancelText = "Cancel",
    });
    GossipFlowEditorRevertFlowModal = revertFlowModal;
end

local closeWarningModal = GossipFlowEditorCloseWarningModal;
if closeWarningModal == nil then
    closeWarningModal = Modal.New(editor.state.canvas, vec.New(0, 0), vec.New(470, 185), 1900i, {
        name = "GossipFlowEditorCloseWarning",
        title = "Unsaved flow changes",
        message = "",
        confirmText = "Close",
        cancelText = "Keep Editing",
    });
    GossipFlowEditorCloseWarningModal = closeWarningModal;
end

local creatureReferencesModal = GossipFlowEditorCreatureReferencesModal;
if creatureReferencesModal == nil then
    creatureReferencesModal = Modal.New(editor.state.canvas, vec.New(0, 0), vec.New(720, 500), 1900i, {
        name = "GossipFlowEditorCreatureReferences",
        title = "Creature references",
        message = "",
        showActions = false,
    });
    creatureReferencesModal.message:SetColor(editor.colors.muted);
    creatureReferencesModal.message:SetFontSize(9);

    local header = creatureReferencesModal.dialog:NewPanel(vec.New(14, 358), vec.New(692, 26), 2i, "DebugDarkGrey");
    header:SetColor(editor.colors.panel);
    header:SetBorderColor(editor.colors.responseBorder);
    header:SetBorderSize(1);
    local function AddHeader(text : string, x : number, width : number)
        local label = header:NewText(TextUtil.TruncateToWidth(text, "DefaultButtonText", width, 8), vec.New(x, 7), 2i, "DefaultButtonText");
        label:SetFontSize(8);
        label:SetColor(editor.colors.muted);
        label:SetInteractable(false);
    end
    AddHeader("CREATURE ID", 8, 86);
    AddHeader("NAME", 98, 270);
    AddHeader("SUBNAME", 372, 222);
    AddHeader("ROOT", 598, 84);

    creatureReferencesModal.referenceList = VirtualList.New(creatureReferencesModal.dialog, vec.New(14, 58), vec.New(692, 298), 2i, {
        rowHeight = 40,
        scrollBox = { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = 692, contentSizeY = 298, verticalScrollBar = true, scrollBarWidth = 8, alpha = 0 },
        createRow = function(rowParent)
            local row = Button.New(rowParent, vec.New(0, 0), vec.New(684, 36), 1i, { text = "", panelTemplate = "DebugDarkGrey", color = editor.colors.field, hoverColor = editor.colors.field, borderColor = editor.colors.responseBorder, borderSize = 1 });
            row:SetAnchor(vec.New(0, 1));
            row:SetRelativePoint(vec.New(0, 1));
            row:SetInteractable(false);
            local function AddValue(x : number, width : number, color : vec?)
                local value = row.root:NewText("", vec.New(x, 10), 2i, "DefaultButtonText");
                value:SetFontSize(9);
                if color ~= nil then value:SetColor(color); end
                value:SetInteractable(false);
                return value;
            end
            row.idText = AddValue(8, 86, editor.colors.muted);
            row.nameText = AddValue(98, 270, nil);
            row.subnameText = AddValue(372, 222, editor.colors.muted);
            row.rootText = AddValue(598, 76, editor.colors.selectedBorder);
            row.listViewport = rowParent;
            return row;
        end,
        bindRow = function(row, _, item)
            local rowWidth = row.listViewport:GetWidth();
            row:SetSize(vec.New(rowWidth, 36));
            row.idText:SetText(`#${item.creatureTemplateID}`);
            row.nameText:SetText(TextUtil.TruncateToWidth(item.name, "DefaultButtonText", 264, 9));
            row.subnameText:SetText(TextUtil.TruncateToWidth(item.subname, "DefaultButtonText", 216, 9));
            row.rootText:SetText(`#${item.rootMenuID}`);
        end,
    });
    local emptyMessage = "No creatures reference this root flow.";
    local emptyMessageWidth = UI.CalculateTextSize(emptyMessage, "DefaultButtonText", 10).x;
    creatureReferencesModal.emptyText = creatureReferencesModal.dialog:NewText(emptyMessage, vec.New((720 - emptyMessageWidth) / 2, 198), 3i, "DefaultButtonText");
    creatureReferencesModal.emptyText:SetFontSize(10);
    creatureReferencesModal.emptyText:SetColor(editor.colors.muted);
    creatureReferencesModal.emptyText:SetInteractable(false);
    local footerText = creatureReferencesModal.dialog:NewText("Assignment editing lives in the Creature Interaction Editor.", vec.New(14, 18), 2i, "DefaultButtonText");
    footerText:SetFontSize(9);
    footerText:SetColor(editor.colors.muted);
    footerText:SetInteractable(false);
    local closeButton = Button.New(creatureReferencesModal.dialog, vec.New(-14, 12), vec.New(76, 28), 2i, { text = "Close", textTemplate = "DefaultButtonText" });
    closeButton:SetAnchor(vec.New(1, 0));
    closeButton:SetRelativePoint(vec.New(1, 0));
    closeButton:SetFontSize(10);
    closeButton:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i then creatureReferencesModal:Close(); end end);
    GossipFlowEditorCreatureReferencesModal = creatureReferencesModal;
end

local RefreshInspector : () -> () = function() end;
local RebuildNodes : () -> () = function() end;
local ApplySnapshot : () -> () = function() end;
local CreateInspectorResponseList : any;
local OpenLocalizedTextModal : (integer?, string, integer) -> () = function() end;
local RefreshFlowActions : () -> () = function() end;
local RequestCloseEditor : () -> () = function() end;

local function FindMenu(menuID : integer?)
    if menuID == nil then return nil; end
    if editor.state.model ~= nil then return editor.state.model:GetMenuDraft(menuID); end
    for _, menu in editor.state.snapshot.gossipMenus do
        if menu.id == menuID then return menu; end
    end
    return nil;
end

local function FindOption(optionID : integer?)
    if optionID == nil then return nil; end
    if editor.state.model ~= nil then return editor.state.model:GetOptionDraft(optionID); end
    for _, option in editor.state.snapshot.gossipMenuOptions do
        if option.id == optionID then return option; end
    end
    return nil;
end

local function GetCreatureReferences(rootMenuID : integer?) : { any }
    local references = { };
    if rootMenuID == nil then return references; end
    local templatesByID = { };
    for _, template in editor.state.snapshot.creatureTemplates or { } do templatesByID[template.id] = template; end
    for _, binding in editor.state.snapshot.creatureTemplateGossip or { } do
        if binding.rootMenuID == rootMenuID then
            local template = templatesByID[binding.creatureTemplateID];
            table.insert(references, {
                creatureTemplateID = binding.creatureTemplateID,
                rootMenuID = binding.rootMenuID,
                name = if template == nil or template.name == "" then `Creature #{binding.creatureTemplateID}` else template.name,
                subname = if template == nil or template.subname == "" then "-" else template.subname,
            });
        end
    end
    table.sort(references, function(left, right)
        local leftName = string.lower(left.name);
        local rightName = string.lower(right.name);
        if leftName == rightName then return integer.lt(left.creatureTemplateID, right.creatureTemplateID); end
        return leftName < rightName;
    end);
    return references;
end

local function OpenCreatureReferences()
    if editor.state.rootMenuID == nil then return; end
    local references = GetCreatureReferences(editor.state.rootMenuID);
    local rootMenu = FindMenu(editor.state.rootMenuID);
    local rootName = if rootMenu == nil or rootMenu.internalName == "" then `Menu #{editor.state.rootMenuID}` else rootMenu.internalName;
    creatureReferencesModal:SetTitle(`Creature references - {rootName}`);
    creatureReferencesModal:SetMessage(`{#references} creature{if #references == 1 then "" else "s"} use root menu #{editor.state.rootMenuID}. Editing this flow changes their gossip interaction.`);
    creatureReferencesModal.referenceList:SetItems(references);
    creatureReferencesModal.emptyText:SetVisible(#references == 0);
    creatureReferencesModal:Open();
end

local function FindDescriptor(actionType : integer?)
    if actionType == nil then return nil; end
    for _, descriptor in editor.state.snapshot.gossipActionDescriptors do
        if descriptor.type == actionType then return descriptor; end
    end
    return nil;
end

local function GetMenuName(menuID : integer?) : string
    local menu = FindMenu(menuID);
    return if menu then menu.internalName else `Missing menu #{menuID}`;
end

local function GetGreeting(menu : any) : string
    if menu == nil then return "Missing menu"; end
    for _, text in editor.state.snapshot.texts do
        if text.id == menu.greetingTextID then return text.englishValue; end
    end
    return `Missing text #{menu.greetingTextID}`;
end

local function GetTextPickerItems(includeNone : boolean?) : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { };
    if includeNone then table.insert(items, { value = 0i, primary = "None", secondary = "No localized text" }); end
    for _, text in editor.state.snapshot.texts do
        if text.id ~= 0i then
            local internalName = if text.internalName == nil or text.internalName == "" then `Text #{text.id}` else text.internalName;
            table.insert(items, { value = text.id, primary = internalName, secondary = text.englishValue, search = `{text.id} {internalName} {text.englishValue}` });
        end
    end
    table.sort(items, function(left, right)
        if left.value == 0i and right.value == 0i then return false; end
        if left.value == 0i then return true; end
        if right.value == 0i then return false; end
        return string.lower(left.primary) < string.lower(right.primary);
    end);
    return items;
end

local function GetConditionPickerItems() : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { { value = 0i, primary = "Always", secondary = "No condition set" } };
    for _, conditionSet in editor.state.snapshot.conditionSets do
        if conditionSet.id ~= 0i then table.insert(items, { value = conditionSet.id, primary = conditionSet.internalName, secondary = `Condition set #{conditionSet.id}`, search = `{conditionSet.id} {conditionSet.internalName}` }); end
    end
    table.sort(items, function(left, right)
        if left.value == 0i and right.value == 0i then return false; end
        if left.value == 0i then return true; end
        if right.value == 0i then return false; end
        return string.lower(left.primary) < string.lower(right.primary);
    end);
    return items;
end

local function GetMenuPickerItems() : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { };
    if editor.state.model == nil then return items; end
    for _, menuID in editor.state.model:GetMenuIDs() do table.insert(items, { value = menuID, primary = GetMenuName(menuID), secondary = `Menu #{menuID}`, search = `{menuID} {GetMenuName(menuID)}` }); end
    return items;
end

local function GetActionPickerItems() : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { };
    for _, descriptor in editor.state.snapshot.gossipActionDescriptors do table.insert(items, { value = descriptor.type, primary = editor.state.controller:FormatIdentifier(descriptor.name), secondary = `Action type {descriptor.type}`, search = `{descriptor.type} {descriptor.name}` }); end
    return items;
end

local function TrackInspectorPicker(picker : SearchPicker.SearchPicker) : SearchPicker.SearchPicker
    table.insert(editor.state.inspectorPickers, picker);
    return picker;
end

local function CloseInspectorPickers()
    for _, picker in editor.state.inspectorPickers do picker:Close(); end
    editor.state.inspectorPickers = { };
end

local function RefreshRootPickerItems()
    if editor.state.model == nil or editor.refs.picker == nil then return; end
    local items : { SearchPicker.Item } = { };
    for _, menuID in editor.state.model:GetMenuIDs() do table.insert(items, { value = menuID, primary = GetMenuName(menuID), secondary = `Menu #{menuID}`, search = `{menuID} {GetMenuName(menuID)}` }); end
    editor.refs.picker:SetItems(items);
    editor.refs.picker:SetValue(editor.state.rootMenuID, false);
end

local function GetActionTarget(option : any) : (integer?, boolean, string)
    local descriptor = FindDescriptor(option.actionType);
    if descriptor == nil then return nil, false, "Unknown action"; end
    for index, parameter in descriptor.parameters do
        if parameter.kind == 2i then
            local target = option.actionParameters[index];
            if target == nil or target == 0i then return target, true, "Missing destination"; end
            return target, true, `Open Menu -> {GetMenuName(target)}`;
        end
    end
    return nil, false, editor.state.controller:FormatIdentifier(descriptor.name);
end

local function BuildReachableGraph()
    editor.state.reachableMenus = { };
    editor.state.reachableEdges = { };
    editor.state.menuDepth = { };
    editor.state.diagnostics = { };
    if editor.state.model == nil or editor.state.rootMenuID == nil or FindMenu(editor.state.rootMenuID) == nil then return; end

    local visited : { [integer] : boolean } = { };
    local queue = { editor.state.rootMenuID };
    visited[editor.state.rootMenuID] = true;
    editor.state.menuDepth[editor.state.rootMenuID] = 0;
    local cursor = 1;
    while cursor <= #queue do
        local menuID = queue[cursor];
        cursor += 1;
        table.insert(editor.state.reachableMenus, menuID);
        local optionIDs = editor.state.model:GetOrderedOptionIDs(menuID);
        for _, optionID in optionIDs do
            local option = FindOption(optionID);
            if option ~= nil then
                local target, isMenuReference, _ = GetActionTarget(option);
                if isMenuReference then
                    if target ~= nil and FindMenu(target) ~= nil then
                        table.insert(editor.state.reachableEdges, { from = menuID, to = target, optionID = option.id });
                        if not visited[target] then
                            visited[target] = true;
                            editor.state.menuDepth[target] = editor.state.menuDepth[menuID] + 1;
                            table.insert(queue, target);
                        end
                    else
                        table.insert(editor.state.diagnostics, `Option #{option.id} references missing menu #{target}`);
                    end
                end
            end
        end
    end
end

local function GetNodeSize(menuID : integer) : vec
    local responseCount = if editor.state.model ~= nil then #editor.state.model:GetOrderedOptionIDs(menuID) else 0;
    local height = editor.geometry.nodeHeaderHeight + editor.geometry.nodeGreetingHeight + responseCount * (editor.geometry.nodeSeparatorHeight + editor.geometry.nodeResponseHeight) + editor.geometry.nodeBottomPadding;
    return vec.New(editor.geometry.nodeWidth, height);
end

local function ScaledSize(size : vec) : vec
    return size * editor.state.model:GetZoom();
end

local function ScaledFontSize(size : number) : number
    return math.max(6, size * editor.state.model:GetZoom());
end

local function EnsureNodePositions()
    if editor.state.model == nil then return; end
    local columns : { [number] : { integer } } = { };
    local columnWidths : { [number] : number } = { };
    local maximumDepth = 0;
    for _, menuID in editor.state.reachableMenus do
        local depth = editor.state.menuDepth[menuID] or 0;
        if columns[depth] == nil then columns[depth] = { }; end
        table.insert(columns[depth], menuID);
        columnWidths[depth] = math.max(columnWidths[depth] or 0, editor.state.baseNodeSizes[menuID].x);
        maximumDepth = math.max(maximumDepth, depth);
    end

    local columnCenters : { [number] : number } = { };
    local totalWidth = 0;
    for depth = 0, maximumDepth do totalWidth += columnWidths[depth] or 0; end
    if maximumDepth > 0 then totalWidth += editor.geometry.nodeColumnGap * maximumDepth; end
    local cursorX = -totalWidth / 2;
    for depth = 0, maximumDepth do
        local width = columnWidths[depth] or 0;
        columnCenters[depth] = cursorX + width / 2;
        cursorX += width + editor.geometry.nodeColumnGap;
    end

    local columnPositions : { [number] : { [integer] : vec } } = { };
    for depth = 0, maximumDepth do
        local column = columns[depth] or { };
        local totalHeight = 0;
        for _, menuID in column do totalHeight += editor.state.baseNodeSizes[menuID].y; end
        if #column > 1 then totalHeight += editor.geometry.nodeRowGap * (#column - 1); end
        local cursorY = totalHeight / 2;
        columnPositions[depth] = { };
        for _, menuID in column do
            local size = editor.state.baseNodeSizes[menuID];
            cursorY -= size.y / 2;
            columnPositions[depth][menuID] = vec.New(columnCenters[depth], cursorY);
            cursorY -= size.y / 2 + editor.geometry.nodeRowGap;
        end
    end

    for _, menuID in editor.state.reachableMenus do
        local position = editor.state.model:GetNodePosition(menuID);
        if position == nil then
            local depth = editor.state.menuDepth[menuID] or 0;
            position = columnPositions[depth][menuID];
            editor.state.model:SetNodePosition(menuID, position);
        end
        editor.state.nodePositions[menuID] = position;
    end
end

local function GraphToLocal(position : vec) : vec
    local pan = editor.state.model:GetPanOffset();
    return editor.geometry.graphCenter + pan + position * editor.state.model:GetZoom();
end

local function LocalToGraph(position : vec) : vec
    local pan = editor.state.model:GetPanOffset();
    return (position - editor.geometry.graphCenter - pan) / editor.state.model:GetZoom();
end

local function GetViewportLocal(mousePosition : vec) : vec
    return vec.New(mousePosition.x - editor.refs.viewport:GetWorldPosX(), mousePosition.y - editor.refs.viewport:GetWorldPosY());
end

local function ClipSegmentToViewport(from : vec, to : vec) : (vec?, vec?)
    local delta = to - from;
    local first = 0.0;
    local last = 1.0;
    local function Clip(p : number, q : number) : boolean
        if p == 0 then return q >= 0; end
        local ratio = q / p;
        if p < 0 then
            if ratio > last then return false; end
            if ratio > first then first = ratio; end
        else
            if ratio < first then return false; end
            if ratio < last then last = ratio; end
        end
        return true;
    end
    if not Clip(-delta.x, from.x) or not Clip(delta.x, editor.geometry.viewportSize.x - from.x) or
        not Clip(-delta.y, from.y) or not Clip(delta.y, editor.geometry.viewportSize.y - from.y) then return nil, nil; end
    return from + delta * first, from + delta * last;
end

local function SegmentHitsRect(from : vec, to : vec, minimum : vec, maximum : vec) : boolean
    local delta = to - from;
    local first = 0.0;
    local last = 1.0;
    local function Clip(p : number, q : number) : boolean
        if p == 0 then return q >= 0; end
        local ratio = q / p;
        if p < 0 then
            if ratio > last then return false; end
            if ratio > first then first = ratio; end
        else
            if ratio < first then return false; end
            if ratio < last then last = ratio; end
        end
        return true;
    end
    return Clip(-delta.x, from.x - minimum.x) and Clip(delta.x, maximum.x - from.x) and
        Clip(-delta.y, from.y - minimum.y) and Clip(delta.y, maximum.y - from.y);
end

local function SegmentClearOfOtherNodes(from : vec, to : vec, sourceID : integer, targetID : integer) : boolean
    for _, menuID in editor.state.reachableMenus do
        if menuID ~= sourceID and menuID ~= targetID then
            local halfSize = editor.state.nodeSizes[menuID] / 2;
            local center = GraphToLocal(editor.state.nodePositions[menuID]);
            if SegmentHitsRect(from, to, center - halfSize, center + halfSize) then return false; end
        end
    end
    return true;
end

local function ApplyNodePositions()
    if editor.state.model == nil then return; end
    for _, menuID in editor.state.reachableMenus do
        local nodeView = editor.state.nodeViews[menuID];
        local position = editor.state.nodePositions[menuID];
        if nodeView ~= nil and position ~= nil then
            local size = editor.state.nodeSizes[menuID];
            nodeView.root:SetSize(size);
            nodeView.root:SetPos(GraphToLocal(position) - size / 2);
        end
    end
end

local function DrawEdges()
    if not editor.state.visible or editor.refs.viewport == nil or editor.state.model == nil or editor.state.localizedText.isOpen or FrameManager.IsRoleOpen("Modal") then return; end
    local viewportWorld = vec.New(editor.refs.viewport:GetWorldPosX(), editor.refs.viewport:GetWorldPosY());
    local drawnTargetOutlets : { [integer] : boolean } = { };
    local function DrawOutlet(point : vec)
        if point.x < 0 or point.y < 0 or point.x > editor.geometry.viewportSize.x or point.y > editor.geometry.viewportSize.y then return; end
        local worldPoint = viewportWorld + point;
        for offset = -2, 2 do UI.DrawLine2D(worldPoint + vec.New(-2, offset), worldPoint + vec.New(2, offset), editor.colors.edge); end
    end
    for _, edge in editor.state.reachableEdges do
        local sourceDepth = editor.state.menuDepth[edge.from];
        local targetDepth = editor.state.menuDepth[edge.to];
        local sourceView = editor.state.nodeViews[edge.from];
        local targetView = editor.state.nodeViews[edge.to];
        local sourceButton = if sourceView ~= nil then sourceView:Get(`optionButton{edge.optionID}`) else nil;
        local targetHeader = if targetView ~= nil then targetView:Get("header") else nil;
        if sourceDepth ~= nil and targetDepth ~= nil and targetDepth > sourceDepth and sourceButton ~= nil and targetHeader ~= nil then
            local scale = editor.state.model:GetZoom();
            local fromPoint = vec.New(sourceButton.root:GetWorldPosX() + sourceButton.root:GetWidth() + editor.geometry.nodeResponseInset * scale,
                sourceButton.root:GetWorldPosY() + sourceButton.root:GetHeight() / 2) - viewportWorld;
            local toPoint = vec.New(targetHeader:GetWorldPosX(), targetHeader:GetWorldPosY()) - viewportWorld;
            if not SegmentClearOfOtherNodes(fromPoint, toPoint, edge.from, edge.to) then continue; end
            local clippedFrom, clippedTo = ClipSegmentToViewport(fromPoint, toPoint);
            if clippedFrom ~= nil and clippedTo ~= nil and not SegmentHitsRect(clippedFrom, clippedTo, editor.geometry.graphControlsPosition, editor.geometry.graphControlsPosition + editor.geometry.graphControlsSize) then
                UI.DrawLine2D(viewportWorld + clippedFrom, viewportWorld + clippedTo, editor.colors.edge);
                DrawOutlet(fromPoint);
                if not drawnTargetOutlets[edge.to] then DrawOutlet(toPoint); drawnTargetOutlets[edge.to] = true; end
            end
        end
    end
end

local function RefreshSidebarSelection(revealSelection : boolean)
    if editor.refs.sidebarList == nil or editor.state.model == nil then return; end
    editor.refs.sidebarList:Refresh();
    if not revealSelection then return; end

    local selectedMenuID = editor.state.model:GetSession().selectedMenuID;
    for index, item in editor.state.filteredMenuItems do
        if item.menuID == selectedMenuID then
            editor.refs.sidebarList:ScrollToIndex(index);
            return;
        end
    end
end

local function RefreshSidebar(preserveScroll : boolean?, revealSelection : boolean?)
    if editor.refs.reachableCountText ~= nil then editor.refs.reachableCountText:SetText(`{#editor.state.reachableMenus} menus`); end

    editor.state.filteredMenuItems = { };
    if editor.state.model ~= nil then
        local query = if editor.refs.sidebarSearch ~= nil then editor.refs.sidebarSearch:GetText() else "";
        for _, menuID in editor.state.reachableMenus do
            local menu = FindMenu(menuID);
            if menu ~= nil then
                local name = if menu.internalName == nil or menu.internalName == "" then `Menu #{menuID}` else menu.internalName;
                local greeting = GetGreeting(menu);
                local searchable = `#{menuID} {menuID} {name} {greeting}`;
                if query == "" or TextUtil.ContainsIgnoreCase(searchable, query) then
                    local responseCount = #editor.state.model:GetOrderedOptionIDs(menuID);
                    table.insert(editor.state.filteredMenuItems, {
                        menuID = menuID,
                        primary = name,
                        secondary = `#{menuID} - {responseCount} responses`,
                        isRoot = menuID == editor.state.rootMenuID,
                    });
                end
            end
        end
    end

    if editor.refs.sidebarList ~= nil then
        editor.refs.sidebarList:SetItems(editor.state.filteredMenuItems, preserveScroll == true);
        RefreshSidebarSelection(revealSelection == true);
    end

    if editor.refs.diagnosticsBanner ~= nil and editor.refs.diagnosticsText ~= nil then
        local hasDiagnostics = #editor.state.diagnostics > 0;
        editor.refs.diagnosticsBanner:SetVisible(hasDiagnostics);
        if hasDiagnostics then
            local suffix = if #editor.state.diagnostics > 1 then ` (+{#editor.state.diagnostics - 1} more)` else "";
            editor.refs.diagnosticsText:SetText(`Warning: {editor.state.diagnostics[1]}{suffix}`);
        end
    end
end

local function RefreshNavigation(preserveScroll : boolean?, revealSelection : boolean?)
    RefreshSidebar(preserveScroll, revealSelection);
end

local function RefreshZoomText()
    if editor.refs.zoomText == nil or editor.state.model == nil then return; end
    editor.refs.zoomText:SetText(`{math.floor(editor.state.model:GetZoom() * 100 + 0.5)}%`);
end

local function SaveRootLayout()
    if editor.state.model == nil or editor.state.rootMenuID == nil or FindMenu(editor.state.rootMenuID) == nil then return; end
    local nodePositions = { };
    for _, menuID in editor.state.reachableMenus do
        local position = editor.state.model:GetNodePosition(menuID);
        if position ~= nil then nodePositions[menuID] = position; end
    end
    editor.state.rootLayouts[editor.state.rootMenuID] = {
        nodePositions = nodePositions,
        panOffset = editor.state.model:GetPanOffset(),
        zoom = editor.state.model:GetZoom(),
    };
end

local function LoadRootLayout(rootMenuID : integer)
    if editor.state.model == nil then return; end
    editor.state.model:ResetNodePositions();
    local layout = editor.state.rootLayouts[rootMenuID];
    if layout == nil then
        editor.state.model:SetPanOffset(vec.New(0, 0));
        editor.state.model:SetZoom(1);
        return;
    end
    for menuID, position in layout.nodePositions do
        if FindMenu(menuID) ~= nil then editor.state.model:SetNodePosition(menuID, position); end
    end
    editor.state.model:SetPanOffset(layout.panOffset);
    editor.state.model:SetZoom(layout.zoom);
end

local function SwitchRootMenu(rootMenuID : integer, rebuild : boolean?)
    if editor.state.model == nil or FindMenu(rootMenuID) == nil then return; end
    if editor.state.rootMenuID ~= rootMenuID then
        SaveRootLayout();
        editor.state.rootMenuID = rootMenuID;
        LoadRootLayout(rootMenuID);
    end
    editor.state.model:SetSelectedMenu(rootMenuID);
    editor.state.model:SetSelectedOption(nil);
    editor.state.controller:SelectMenu(rootMenuID);
    if rebuild ~= false then
        RebuildNodes();
        RefreshZoomText();
    end
end

local function RefreshNodeSelection()
    if editor.state.model == nil then return; end
    local selectedMenuID = editor.state.model:GetSession().selectedMenuID;
    for menuID, nodeView in editor.state.nodeViews do
        nodeView:Get("outline"):SetBorderColor(if menuID == editor.state.rootMenuID then (if menuID == selectedMenuID then editor.colors.rootSelectedBorder else editor.colors.rootBorder) elseif menuID == selectedMenuID then editor.colors.selectedBorder else editor.colors.nodeBorder);
    end
end

local function RefreshResponseSelection()
    if editor.state.model == nil then return; end
    local selectedOptionID = editor.state.model:GetSession().selectedOptionID;
    for menuID, nodeView in editor.state.nodeViews do
        for _, optionID in editor.state.model:GetOrderedOptionIDs(menuID) do
            local optionButton = nodeView:Get(`optionButton{optionID}`);
            if optionButton ~= nil then optionButton:SetHighlighted(optionID == selectedOptionID); end
        end
    end
end

local function SelectMenu(menuID : integer)
    if editor.state.model == nil or FindMenu(menuID) == nil then return; end
    editor.state.model:SetSelectedMenu(menuID);
    editor.state.model:SetSelectedOption(nil);
    editor.state.controller:SelectMenu(menuID);
    RefreshNodeSelection();
    RefreshResponseSelection();
    RefreshSidebarSelection(true);
    RefreshInspector();
end

local function SelectOption(menuID : integer, optionID : integer)
    if editor.state.model == nil or FindOption(optionID) == nil then return; end
    editor.state.model:SetSelectedMenu(menuID);
    editor.state.model:SetSelectedOption(optionID);
    editor.state.controller:SelectMenu(menuID);
    editor.state.controller:SelectOption(optionID);
    RefreshNodeSelection();
    RefreshResponseSelection();
    RefreshSidebarSelection(true);
    RefreshInspector();
end

local function GetManagedFlowMenuIDs() : { integer }
    if editor.state.model == nil or editor.state.rootMenuID == nil or FindMenu(editor.state.rootMenuID) == nil then return { }; end
    local authoritativeOptionsByMenuID : { [integer] : { any } } = { };
    for _, option in editor.state.snapshot.gossipMenuOptions do
        local options = authoritativeOptionsByMenuID[option.menuID];
        if options == nil then options = { }; authoritativeOptionsByMenuID[option.menuID] = options; end
        table.insert(options, option);
    end

    local result = { };
    local queue = { editor.state.rootMenuID };
    local seen : { [integer] : boolean } = { [editor.state.rootMenuID] = true };
    local cursor = 1;
    while cursor <= #queue do
        local menuID = queue[cursor];
        cursor += 1;
        table.insert(result, menuID);
        local options = { };
        for _, optionID in editor.state.model:GetOrderedOptionIDs(menuID) do
            local option = editor.state.model:GetOptionDraft(optionID);
            if option ~= nil then table.insert(options, option); end
        end
        for _, option in authoritativeOptionsByMenuID[menuID] or { } do table.insert(options, option); end
        for _, option in options do
            local target, isMenuReference, _ = GetActionTarget(option);
            if isMenuReference and target ~= nil and not seen[target] and FindMenu(target) ~= nil then
                seen[target] = true;
                table.insert(queue, target);
            end
        end
    end
    return result;
end

local function GetFlowDirtyCount() : number
    if editor.state.model == nil then return 0; end
    local count = 0;
    for _, menuID in GetManagedFlowMenuIDs() do
        if editor.state.model:IsMenuDirty(menuID) then count += 1; end
        for _, optionID in editor.state.model:GetOrderedOptionIDs(menuID) do
            if editor.state.model:IsOptionDirty(optionID) then count += 1; end
        end
    end
    return count;
end

local function GetGlobalDirtyCount() : number
    if editor.state.model == nil then return 0; end
    local count = 0;
    for _, menuID in editor.state.model:GetMenuIDs() do
        if editor.state.model:IsMenuDirty(menuID) then count += 1; end
        for _, optionID in editor.state.model:GetOrderedOptionIDs(menuID) do
            if editor.state.model:IsOptionDirty(optionID) then count += 1; end
        end
    end
    return count;
end

RefreshFlowActions = function()
    if editor.refs.flowDirtyText == nil or editor.refs.revertFlowButton == nil or editor.refs.applyFlowButton == nil then return; end
    local dirtyCount = GetFlowDirtyCount();
    local busy = editor.state.flowSave ~= nil or editor.state.menuDuplicate ~= nil or editor.state.controller:GetPendingRequestID() ~= 0i;
    local ready = editor.state.controller:IsReady() and not busy and editor.state.rootMenuID ~= nil;
    local message = if editor.state.flowSave ~= nil then `Saving {editor.state.flowSave.completed}/{editor.state.flowSave.total}`
        elseif dirtyCount > 0 then `{dirtyCount} unsaved`
        elseif editor.state.rootMenuID == nil then "No flow"
        else "Saved";
    editor.refs.flowDirtyText:SetText(message);
    editor.refs.flowDirtyText:SetColor(if dirtyCount > 0 then editor.colors.warning else editor.colors.muted);
    editor.refs.revertFlowButton:SetInteractable(ready and dirtyCount > 0);
    editor.refs.applyFlowButton:SetInteractable(ready and dirtyCount > 0);
end

local function GetInspectorState(menuID : integer?, optionID : integer?) : (string, vec)
    local statusMessage, statusLevel = editor.state.controller:GetStatus();
    if editor.state.flowSave ~= nil then return `Saving flow ({editor.state.flowSave.completed}/{editor.state.flowSave.total})`, editor.colors.muted; end
    if editor.state.menuDuplicate ~= nil then return `Duplicating menu ({editor.state.menuDuplicate.completed}/{#editor.state.menuDuplicate.options} responses)`, editor.colors.muted; end
    if editor.state.controller:GetPendingRequestID() ~= 0i then return statusMessage, editor.colors.muted; end
    local dirtyCount = GetFlowDirtyCount();
    if dirtyCount > 0 then return `{dirtyCount} unsaved change{if dirtyCount == 1 then "" else "s"} in this flow`, editor.colors.warning; end
    if statusLevel == STATUS.Success then return statusMessage, editor.colors.success; end
    if statusLevel == STATUS.Warning then return statusMessage, editor.colors.warning; end
    if statusLevel == STATUS.Error then return statusMessage, editor.colors.danger; end
    return "Menu matches authoritative data", editor.colors.muted;
end

local function RefreshMenuDraftVisuals(menuID : integer)
    if editor.state.model == nil then return; end
    local menu = editor.state.model:GetMenuDraft(menuID);
    if menu == nil then return; end
    local nodeView = editor.state.nodeViews[menuID];
    if nodeView ~= nil then
        local headerTitle = nodeView:Get("headerTitle");
        local greeting = nodeView:Get("greeting");
        if headerTitle ~= nil then headerTitle:SetText(if menu.internalName == "" then `Menu #{menuID}` else menu.internalName); end
        if greeting ~= nil then greeting:SetText(GetGreeting(menu)); end
    end
    RefreshSidebar(true, false);
    RefreshRootPickerItems();
    local dirty = editor.state.model:IsMenuDirty(menuID);
    if editor.refs.dirtyIndicator ~= nil then
        editor.refs.dirtyIndicator:SetText(if dirty then "UNSAVED" else "SAVED");
        editor.refs.dirtyIndicator:SetColor(if dirty then editor.colors.warning else editor.colors.muted);
    end
    if editor.refs.inspectorTitle ~= nil then editor.refs.inspectorTitle:SetText(if menu.internalName == "" then `Menu #{menuID}` else menu.internalName); end
    if editor.refs.inspectorStateText ~= nil then
        local message, color = GetInspectorState(menuID, nil);
        editor.refs.inspectorStateText:SetText(message);
        editor.refs.inspectorStateText:SetColor(color);
    end
    RefreshFlowActions();
end

local function RefreshOptionDraftState(optionID : integer)
    if editor.state.model == nil then return; end
    local option = editor.state.model:GetOptionDraft(optionID);
    if option == nil then return; end
    local dirty = editor.state.model:IsOptionDirty(optionID);
    if editor.refs.dirtyIndicator ~= nil then
        editor.refs.dirtyIndicator:SetText(if dirty then "UNSAVED" else "SAVED");
        editor.refs.dirtyIndicator:SetColor(if dirty then editor.colors.warning else editor.colors.muted);
    end
    if editor.refs.inspectorStateText ~= nil then
        local message, color = GetInspectorState(option.menuID, optionID);
        editor.refs.inspectorStateText:SetText(message);
        editor.refs.inspectorStateText:SetColor(color);
    end
    RefreshFlowActions();
end

local function ValidateFlowSave() : string?
    for _, message in editor.state.optionParameterErrors do return message; end
    for _, menuID in GetManagedFlowMenuIDs() do
        local menu = editor.state.model:GetMenuDraft(menuID);
        if menu ~= nil and editor.state.model:IsMenuDirty(menuID) then
            if menu.internalName == nil or string.match(menu.internalName, "%S") == nil then return `Menu #{menuID} needs a non-empty internal name.`; end
            if editor.state.controller:FindText(menu.greetingTextID) == nil then return `Menu #{menuID} needs a valid greeting text.`; end
        end
        for _, optionID in editor.state.model:GetOrderedOptionIDs(menuID) do
            local option = editor.state.model:GetOptionDraft(optionID);
            if option ~= nil and editor.state.model:IsOptionDirty(optionID) then
                if editor.state.controller:FindText(option.textID) == nil then return `Response #{optionID} needs a valid localized text.`; end
                local descriptor = editor.state.controller:FindActionDescriptor(option.actionType);
                if descriptor == nil then return `Response #{optionID} needs a valid behavior.`; end
                for index, parameter in descriptor.parameters do
                    if parameter.kind ~= 0i then
                        local value = option.actionParameters[index];
                        if value == nil or integer.lt(value, parameter.minimum) or integer.gt(value, parameter.maximum) then
                            return `Response #{optionID} has an invalid {editor.state.controller:FormatIdentifier(parameter.name)} value.`;
                        end
                        if parameter.kind == 2i and editor.state.model:GetMenuDraft(value) == nil then return `Response #{optionID} references a missing destination menu.`; end
                    end
                end
            end
        end
    end
    return nil;
end

local function DispatchNextFlowSave()
    local save = editor.state.flowSave;
    if save == nil or editor.state.model == nil or not editor.state.controller:IsReady() then return; end
    while save.cursor <= #save.tasks do
        local task = save.tasks[save.cursor];
        save.cursor += 1;
        local dirty = if task.kind == "menu" then editor.state.model:IsMenuDirty(task.id) else editor.state.model:IsOptionDirty(task.id);
        if dirty then
            save.active = task;
            local requestID = 0i;
            if task.kind == "menu" then
                local menu = editor.state.model:GetMenuDraft(task.id);
                if menu ~= nil then requestID = InteractionEditor.UpdateGossipMenu(menu.id, menu.internalName, menu.greetingTextID, menu.flags); end
            else
                local option = editor.state.model:GetOptionDraft(task.id);
                if option ~= nil then
                    local parameters = option.actionParameters;
                    requestID = if editor.state.model:IsMenuOrderDirty(option.menuID)
                        then InteractionEditor.ReorderGossipMenuOption(option.id, option.menuID, option.orderIndex, option.textID, option.icon, option.flags,
                            option.visibilityConditionSetID, option.enabledConditionSetID, option.disabledReasonTextID, option.actionType,
                            parameters[1], parameters[2], parameters[3], parameters[4])
                        else InteractionEditor.UpdateGossipMenuOption(option.id, option.menuID, option.orderIndex, option.textID, option.icon, option.flags,
                            option.visibilityConditionSetID, option.enabledConditionSetID, option.disabledReasonTextID, option.actionType,
                            parameters[1], parameters[2], parameters[3], parameters[4]);
                end
            end
            editor.state.controller:Submit(requestID);
            if requestID == 0i then editor.state.flowSave = nil; end
            RefreshInspector();
            return;
        end
        save.completed += 1;
    end
    local total = save.total;
    editor.state.flowSave = nil;
    editor.state.controller:SetStatus(`Saved {total} changed record{if total == 1 then "" else "s"} in this flow.`, STATUS.Success);
    RefreshInspector();
end

local function ContinueFlowSave()
    local save = editor.state.flowSave;
    if save == nil or editor.state.controller:GetPendingRequestID() ~= 0i then return; end
    local _, statusLevel = editor.state.controller:GetStatus();
    if statusLevel == STATUS.Error then editor.state.flowSave = nil; RefreshInspector(); return; end
    if save.active ~= nil then
        local active = save.active;
        local dirty = if active.kind == "menu" then editor.state.model:IsMenuDirty(active.id) else editor.state.model:IsOptionDirty(active.id);
        if dirty then return; end
        save.active = nil;
        save.completed += 1;
    end
    DispatchNextFlowSave();
end

local function ApplyFlow()
    if editor.state.model == nil or not editor.state.controller:IsReady() or editor.state.flowSave ~= nil then return; end
    BuildReachableGraph();
    local validationError = ValidateFlowSave();
    if validationError ~= nil then editor.state.controller:SetStatus(validationError, STATUS.Error); RefreshInspector(); return; end
    local tasks = { };
    for _, menuID in GetManagedFlowMenuIDs() do
        if editor.state.model:IsMenuDirty(menuID) then table.insert(tasks, { kind = "menu", id = menuID }); end
        for _, optionID in editor.state.model:GetOrderedOptionIDs(menuID) do
            if editor.state.model:IsOptionDirty(optionID) then table.insert(tasks, { kind = "option", id = optionID }); end
        end
    end
    if #tasks == 0 then editor.state.controller:SetStatus("This flow has no unsaved changes.", STATUS.Muted); RefreshInspector(); return; end
    editor.state.flowSave = { tasks = tasks, cursor = 1, active = nil, completed = 0, total = #tasks };
    DispatchNextFlowSave();
end

local function RevertFlow()
    if editor.state.model == nil or not editor.state.controller:IsReady() or editor.state.flowSave ~= nil then return; end
    local dirtyCount = GetFlowDirtyCount();
    if dirtyCount == 0 then return; end
    editor.state.model:RevertMenus(GetManagedFlowMenuIDs());
    editor.state.controller:SetStatus(`Reverted {dirtyCount} unsaved change{if dirtyCount == 1 then "" else "s"} in this flow.`, STATUS.Success);
    RebuildNodes();
end

local function ConfirmRevertFlow()
    local dirtyCount = GetFlowDirtyCount();
    if dirtyCount == 0 then return; end
    revertFlowModal:Open({
        message = `Discard {dirtyCount} unsaved menu or response change{if dirtyCount == 1 then "" else "s"} in this root flow? Graph layout will be preserved.`,
        onConfirm = RevertFlow,
    });
end

local function CreateRootMenu()
    if editor.state.model == nil or not editor.state.controller:IsReady() then return; end
    local textOptions = editor.state.controller:TextOptions(false);
    if #textOptions == 0 then
        editor.state.controller:SetStatus("Create at least one localized text before creating a root flow.", STATUS.Error);
        RefreshInspector();
        return;
    end
    local existingNames : { [string] : boolean } = { };
    local existingMenuIDs : { [integer] : boolean } = { };
    for _, menuID in editor.state.model:GetMenuIDs() do
        existingMenuIDs[menuID] = true;
        local menu = editor.state.model:GetMenuDraft(menuID);
        if menu ~= nil and menu.internalName ~= nil then existingNames[menu.internalName] = true; end
    end
    local suffix = 1;
    while existingNames[`gossip_menu_{suffix}`] do suffix += 1; end
    editor.state.pendingRootMenuCreation = existingMenuIDs;
    editor.state.controller:Submit(InteractionEditor.CreateGossipMenu(`gossip_menu_{suffix}`, textOptions[1].value, 0i));
    if editor.refs.newRootButton ~= nil then editor.refs.newRootButton:SetInteractable(editor.state.controller:IsReady()); end
    RefreshInspector();
end

local function CreateResponse(menuID : integer)
    if editor.state.model == nil or not editor.state.controller:IsReady() then return; end
    if editor.state.model:IsMenuOrderDirty(menuID) then
        editor.state.controller:SetStatus("Apply the pending response reorder before adding another response.", STATUS.Warning);
        RefreshInspector();
        return;
    end
    local menu = editor.state.model:GetMenuDraft(menuID);
    local textOptions = editor.state.controller:TextOptions(false);
    local action = editor.state.controller:DefaultAction();
    if menu == nil or #textOptions == 0 or action == nil then
        editor.state.controller:SetStatus("A localized text and response behavior are required before adding a response.", STATUS.Error);
        RefreshInspector();
        return;
    end
    local order = 0;
    for _, option in editor.state.snapshot.gossipMenuOptions do
        if option.menuID == menuID then order = math.max(order, integer.tonumber(option.orderIndex) + 1); end
    end
    if order > 32767 then
        editor.state.controller:SetStatus("This menu has exhausted the response-order range.", STATUS.Error);
        RefreshInspector();
        return;
    end
    local parameters = { 0i, 0i, 0i, 0i };
    for index = 1, 4 do parameters[index] = action.parameters[index].minimum; end
    editor.state.controller:QueueSelection(menuID, nil);
    editor.state.controller:Submit(InteractionEditor.CreateGossipMenuOption(menuID, integer.create(order), textOptions[1].value, 0i, 0i, 0i, 0i, 0i, action.type,
        parameters[1], parameters[2], parameters[3], parameters[4]));
    RefreshInspector();
end

local function CanMutateSelection() : boolean
    if editor.state.model == nil or not editor.state.controller:IsReady() or editor.state.flowSave ~= nil or editor.state.menuDuplicate ~= nil then return false; end
    if GetFlowDirtyCount() > 0 then
        editor.state.controller:SetStatus("Apply the flow's unsaved changes before deleting or duplicating records.", STATUS.Warning);
        RefreshInspector();
        return false;
    end
    return true;
end

local function ConfirmDeleteResponse(optionID : integer)
    if not CanMutateSelection() then return; end
    local option = editor.state.model:GetOptionDraft(optionID);
    if option == nil then return; end
    local text = editor.state.controller:FindText(option.textID);
    local label = if text ~= nil then TextUtil.Truncate(text.englishValue, 48) else `Response #{optionID}`;
    deleteModal:Open({
        title = "Delete response",
        message = `Delete "{label}" from this menu? The localized text itself will not be deleted.`,
        onConfirm = function()
            if not editor.state.controller:IsReady() then return; end
            editor.state.controller:Submit(InteractionEditor.DeleteGossipMenuOption(optionID));
            RefreshInspector();
        end,
    });
end

local function ConfirmDeleteMenu(menuID : integer)
    if not CanMutateSelection() then return; end
    local menu = editor.state.model:GetMenuDraft(menuID);
    if menu == nil then return; end
    deleteModal:Open({
        title = "Delete menu",
        message = `Delete "{menu.internalName}" and all of its responses? Incoming menu links or creature assignments may prevent deletion. Localized texts will not be deleted.`,
        onConfirm = function()
            if not editor.state.controller:IsReady() then return; end
            editor.state.controller:Submit(InteractionEditor.DeleteGossipMenu(menuID));
            RefreshInspector();
        end,
    });
end

local function DuplicateResponse(optionID : integer)
    if not CanMutateSelection() then return; end
    local option = editor.state.model:GetOptionDraft(optionID);
    if option == nil then return; end
    local order = 0;
    for _, existing in editor.state.snapshot.gossipMenuOptions do
        if existing.menuID == option.menuID then order = math.max(order, integer.tonumber(existing.orderIndex) + 1); end
    end
    if order > 32767 then editor.state.controller:SetStatus("This menu has exhausted the response-order range.", STATUS.Error); RefreshInspector(); return; end
    local parameters = option.actionParameters;
    editor.state.controller:Submit(InteractionEditor.CreateGossipMenuOption(option.menuID, integer.create(order), option.textID, option.icon, option.flags,
        option.visibilityConditionSetID, option.enabledConditionSetID, option.disabledReasonTextID, option.actionType,
        parameters[1], parameters[2], parameters[3], parameters[4]));
    RefreshInspector();
end

local function DuplicateMenu(menuID : integer)
    if not CanMutateSelection() then return; end
    local menu = editor.state.model:GetMenuDraft(menuID);
    if menu == nil then return; end
    local existingNames : { [string] : boolean } = { };
    local existingMenuIDs : { [integer] : boolean } = { };
    for _, existingID in editor.state.model:GetMenuIDs() do
        existingMenuIDs[existingID] = true;
        local existing = editor.state.model:GetMenuDraft(existingID);
        if existing ~= nil then existingNames[existing.internalName] = true; end
    end
    local baseName = `{menu.internalName}_copy`;
    local internalName = baseName;
    local suffix = 2;
    while existingNames[internalName] do internalName = `{baseName}_{suffix}`; suffix += 1; end
    local options = { };
    for _, optionID in editor.state.model:GetOrderedOptionIDs(menuID) do
        local option = editor.state.model:GetOptionDraft(optionID);
        if option ~= nil then table.insert(options, option); end
    end
    editor.state.menuDuplicate = {
        sourceMenuID = menuID,
        existingMenuIDs = existingMenuIDs,
        options = options,
        createdMenuID = nil,
        cursor = 1,
        active = false,
        completed = 0,
    };
    editor.state.controller:Submit(InteractionEditor.CreateGossipMenu(internalName, menu.greetingTextID, menu.flags));
    RefreshInspector();
end

local function ContinueMenuDuplicate()
    local duplicate = editor.state.menuDuplicate;
    if duplicate == nil or editor.state.model == nil or editor.state.controller:GetPendingRequestID() ~= 0i then return; end
    local _, statusLevel = editor.state.controller:GetStatus();
    if statusLevel == STATUS.Error then editor.state.menuDuplicate = nil; RefreshInspector(); return; end
    if duplicate.createdMenuID == nil then
        local selectedMenuID = editor.state.controller:GetSelectedMenuID();
        if selectedMenuID == nil or duplicate.existingMenuIDs[selectedMenuID] or editor.state.model:GetMenuDraft(selectedMenuID) == nil then return; end
        duplicate.createdMenuID = selectedMenuID;
        SwitchRootMenu(selectedMenuID);
    elseif duplicate.active then
        duplicate.active = false;
        duplicate.completed += 1;
    end

    if duplicate.cursor <= #duplicate.options then
        local option = duplicate.options[duplicate.cursor];
        local orderIndex = integer.create(duplicate.cursor - 1);
        duplicate.cursor += 1;
        local parameters = table.clone(option.actionParameters);
        local descriptor = editor.state.controller:FindActionDescriptor(option.actionType);
        if descriptor ~= nil then
            for index, parameter in descriptor.parameters do
                if parameter.kind == 2i and parameters[index] == duplicate.sourceMenuID then parameters[index] = duplicate.createdMenuID; end
            end
        end
        duplicate.active = true;
        editor.state.model:SetSelectedMenu(duplicate.createdMenuID);
        editor.state.controller:SelectMenu(duplicate.createdMenuID);
        local requestID = InteractionEditor.CreateGossipMenuOption(duplicate.createdMenuID, orderIndex, option.textID, option.icon, option.flags,
            option.visibilityConditionSetID, option.enabledConditionSetID, option.disabledReasonTextID, option.actionType,
            parameters[1], parameters[2], parameters[3], parameters[4]);
        editor.state.controller:Submit(requestID);
        if requestID == 0i then editor.state.menuDuplicate = nil; end
        RefreshInspector();
        return;
    end

    local createdMenuID = duplicate.createdMenuID;
    local responseCount = #duplicate.options;
    editor.state.menuDuplicate = nil;
    SwitchRootMenu(createdMenuID, false);
    editor.state.controller:SetStatus(`Duplicated menu with {responseCount} response{if responseCount == 1 then "" else "s"}. Localized texts remain shared.`, STATUS.Success);
    RebuildNodes();
end

local function MoveOptionInOrder(order : { integer }, optionID : integer, targetIndex : number) : { integer }
    local result = { };
    for _, currentOptionID in order do
        if currentOptionID ~= optionID then table.insert(result, currentOptionID); end
    end
    table.insert(result, math.clamp(targetIndex, 1, #result + 1), optionID);
    return result;
end

local function CommitResponseDrag()
    local drag = editor.state.responseDrag;
    if drag == nil then return; end
    editor.state.responseDrag = nil;
    if drag.nodeRoot ~= nil then drag.nodeRoot:SetClipChildren(true); end
    drag.surface:SetPos(drag.restPosition);
    local changed = drag.targetIndex ~= (table.find(drag.originalOrder, drag.optionID) or drag.targetIndex);
    if changed then editor.state.model:ReorderOptions(drag.menuID, drag.previewOrder); end
    if editor.state.pendingSnapshot then
        editor.state.pendingSnapshot = false;
        ApplySnapshot();
    else
        RebuildNodes();
    end
    if changed then SelectOption(drag.menuID, drag.optionID); end
end

local function GetGraphResponseTargetIndex(drag : any, mousePosition : vec) : number
    local targetIndex = 1;
    local nodeView = editor.state.nodeViews[drag.menuID];
    for _, currentOptionID in drag.originalOrder do
        if currentOptionID ~= drag.optionID then
            local slot = nodeView:Get(`optionSlot{currentOptionID}`);
            if slot ~= nil and mousePosition.y < slot:GetWorldPosY() + slot:GetHeight() / 2 then targetIndex += 1; end
        end
    end
    return math.clamp(targetIndex, 1, #drag.originalOrder);
end

local function BeginGraphResponseDrag(menuID : integer, optionID : integer, surface : any, mousePosition : vec)
    if not editor.state.controller:IsReady() then return; end
    local nodeView = editor.state.nodeViews[menuID];
    local layout = nodeView:Get("responsesLayout");
    local slot = nodeView:Get(`optionSlot{optionID}`);
    local order = editor.state.model:GetOrderedOptionIDs(menuID);
    local currentIndex = table.find(order, optionID) or 1;
    nodeView.root:SetClipChildren(false);
    editor.state.responseDrag = {
        menuID = menuID,
        optionID = optionID,
        originalOrder = order,
        previewOrder = order,
        targetIndex = currentIndex,
        startWorld = surface:GetWorldPos(),
        restPosition = surface:GetPos(),
        slot = slot,
        surface = surface,
        layout = layout,
        nodeRoot = nodeView.root,
        inspector = false,
    };
end

local function PreviewGraphResponseDrag(mousePosition : vec, delta : vec)
    local drag = editor.state.responseDrag;
    if drag == nil or drag.inspector then return; end
    local desiredWorld = drag.startWorld + delta;
    local targetIndex = GetGraphResponseTargetIndex(drag, mousePosition);
    drag.surface:SetWorldPos(desiredWorld);
    if targetIndex ~= drag.targetIndex then
        drag.targetIndex = targetIndex;
        drag.previewOrder = MoveOptionInOrder(drag.originalOrder, drag.optionID, targetIndex);
        drag.layout:Move(drag.slot, targetIndex + 1);
        drag.layout:Refresh();
    end
    drag.surface:SetWorldPos(desiredWorld);
end

local function GetInspectorResponseTargetIndex(list : any, activeRow : any, mousePosition : vec) : number
    local rows = { };
    local firstIndex = math.huge;
    local lastIndex = 0;
    for index0, row in list.realized do
        local index = index0 + 1;
        firstIndex = math.min(firstIndex, index);
        lastIndex = math.max(lastIndex, index);
        if row ~= activeRow then table.insert(rows, row); end
    end
    table.sort(rows, function(left, right) return left:GetWorldPosY() > right:GetWorldPosY(); end);
    local targetIndex = firstIndex;
    for _, row in rows do
        if mousePosition.y < row:GetWorldPosY() + row:GetHeight() / 2 then targetIndex += 1; end
    end
    return math.clamp(targetIndex, firstIndex, lastIndex);
end

local function PreviewInspectorResponseDrag(mousePosition : vec, delta : vec)
    local drag = editor.state.responseDrag;
    if drag == nil or not drag.inspector then return; end
    local desiredWorld = drag.startWorld + delta;
    local targetIndex = GetInspectorResponseTargetIndex(drag.list, drag.row, mousePosition);
    drag.surface:SetWorldPos(desiredWorld);
    if targetIndex ~= drag.targetIndex then
        drag.targetIndex = targetIndex;
        drag.previewOrder = MoveOptionInOrder(drag.originalOrder, drag.optionID, targetIndex);
        local firstIndex = math.huge;
        for index0, _ in drag.list.realized do firstIndex = math.min(firstIndex, index0 + 1); end
        for _, row in drag.list.realized do
            local previewIndex = table.find(drag.previewOrder, row.boundOptionID);
            if previewIndex ~= nil then row:SetPosY(-((previewIndex - firstIndex) * drag.list.rowHeight) - 1); end
        end
    end
    drag.surface:SetWorldPos(desiredWorld);
end

local function MakeInspectorTabs()
    local children = { };
    for _, tab in {
        { id = editor.inspectorTabs.content, label = "Content", width = 78 },
        { id = editor.inspectorTabs.logic, label = "Logic", width = 70 },
        { id = editor.inspectorTabs.metadata, label = "Metadata", width = 82 },
    } do
        local tabID = tab.id;
        table.insert(children, Declarative.Button(tab.label, {
            width = tab.width,
            height = 28,
            fontSize = 10,
            panelTemplate = "DebugDarkGrey",
            color = editor.colors.panel,
            hoverColor = editor.colors.activeTab,
            highlightColor = editor.colors.activeTab,
            borderColor = if editor.state.inspectorTab == tabID then editor.colors.selectedBorder else editor.colors.responseBorder,
            borderSize = 1,
            setup = function(button) button:SetHighlighted(editor.state.inspectorTab == tabID); end,
            onClick = function() editor.state.inspectorTab = tabID; RefreshInspector(); end,
        }));
    end
    return Declarative.Horizontal({ width = editor.geometry.inspectorInnerWidth, height = 28, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, table.unpack(children));
end

local function MakeInspectorValueCard(primary : string, secondary : string?, height : number)
    if secondary == nil then
        return Declarative.Panel({ width = editor.geometry.inspectorInnerWidth, height = height, template = "DebugDarkGrey", color = editor.colors.field, borderColor = vec.New(0.24, 0.28, 0.32), borderSize = 1 },
            Declarative.Text(primary, { position = vec.New(7, 0), width = editor.geometry.inspectorInnerWidth - 14, height = height, anchor = Declarative.Anchor.Left, relativePoint = Declarative.Anchor.Left, fontSize = 10, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText", interactable = false })
        );
    end
    return Declarative.Panel({ width = editor.geometry.inspectorInnerWidth, height = height, template = "DebugDarkGrey", color = editor.colors.field, borderColor = vec.New(0.24, 0.28, 0.32), borderSize = 1 },
        Declarative.Text(primary, { position = vec.New(7, -4), width = editor.geometry.inspectorInnerWidth - 14, height = 18, anchor = Declarative.Anchor.TopLeft, relativePoint = Declarative.Anchor.TopLeft, fontSize = 10, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText", interactable = false }),
        Declarative.Text(secondary, { position = vec.New(7, 4), width = editor.geometry.inspectorInnerWidth - 14, height = 16, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText", interactable = false })
    );
end

local function MakeInspectorActions(responseSelected : boolean, menuID : integer?, optionID : integer?)
    local duplicateLabel = if responseSelected then "Duplicate Response" else "Duplicate Menu";
    local duplicateWidth = if responseSelected then 124 else 102;
    local stateMessage, stateColor = GetInspectorState(menuID, optionID);
    local selectionActionReady = editor.state.controller:IsReady() and editor.state.flowSave == nil and editor.state.menuDuplicate == nil and GetFlowDirtyCount() == 0;
    return Declarative.Vertical({ width = editor.geometry.inspectorInnerWidth, height = 55, spacing = 5, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(stateMessage, { id = "inspectorState", width = editor.geometry.inspectorInnerWidth, height = 16, fontSize = 9, color = stateColor, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
        Declarative.Horizontal({ width = editor.geometry.inspectorInnerWidth, height = 34, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Button("Delete", { width = 54, height = 34, fontSize = 10, panelTemplate = "DebugDarkGrey", color = editor.colors.danger, hoverColor = vec.New(0.65, 0.25, 0.27), disabledAlpha = 0.18, borderSize = 0, interactable = selectionActionReady, onClick = function() if responseSelected and optionID ~= nil then ConfirmDeleteResponse(optionID); elseif menuID ~= nil then ConfirmDeleteMenu(menuID); end end }),
            Declarative.Panel({ grow = 1, height = 34, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }),
            Declarative.Button(duplicateLabel, { width = duplicateWidth, height = 34, fontSize = 10, interactable = selectionActionReady, onClick = function() if responseSelected and optionID ~= nil then DuplicateResponse(optionID); elseif menuID ~= nil then DuplicateMenu(menuID); end end })
        )
    );
end

local function MakeInspectorPickerField(id : string, label : string, items : { SearchPicker.Item }, value : any, onValueChanged : (any) -> ())
    return Declarative.Vertical({ width = editor.geometry.inspectorInnerWidth, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = editor.geometry.inspectorInnerWidth, height = 12, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
        Declarative.Component({ id = id, size = vec.New(editor.geometry.inspectorInnerWidth, 42), anchor = Declarative.Anchor.BottomLeft, relativePoint = Declarative.Anchor.BottomLeft, factory = function(parent, position, size, layer)
            return TrackInspectorPicker(SearchPicker.New(parent, editor.state.canvas, position, size, layer, { items = items, value = value, placeholder = "Select...", popupWidth = 390, popupHeight = 420, rowHeight = 40, onValueChanged = onValueChanged,
                button = { panelTemplate = "DebugDarkGrey", textTemplate = "DefaultButtonText", textAlign = "left", textPadX = 8, color = editor.colors.field, hoverColor = editor.colors.nodeResponseHover, borderColor = editor.colors.responseBorder, borderSize = 1 } }));
        end })
    );
end

local function MakeLocalizedTextPickerField(id : string, label : string, textID : integer, targetKind : string, targetID : integer, onValueChanged : (integer) -> ())
    return Declarative.Vertical({ width = editor.geometry.inspectorInnerWidth, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = editor.geometry.inspectorInnerWidth, height = 12, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
        Declarative.Horizontal({ width = editor.geometry.inspectorInnerWidth, height = 42, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Component({ id = id, size = vec.New(editor.geometry.inspectorInnerWidth - 92, 42), anchor = Declarative.Anchor.BottomLeft, relativePoint = Declarative.Anchor.BottomLeft, factory = function(parent, position, size, layer)
                return TrackInspectorPicker(SearchPicker.New(parent, editor.state.canvas, position, size, layer, { items = GetTextPickerItems(false), value = textID, placeholder = "Select localized text", popupWidth = 390, popupHeight = 420, rowHeight = 40, onValueChanged = function(value)
                    if value ~= nil then onValueChanged(value); end
                end, button = { panelTemplate = "DebugDarkGrey", textTemplate = "DefaultButtonText", textAlign = "left", textPadX = 8, color = editor.colors.field, hoverColor = editor.colors.nodeResponseHover, borderColor = editor.colors.responseBorder, borderSize = 1 } }));
            end }),
            Declarative.Button("Edit", { width = 42, height = 42, fontSize = 9, interactable = editor.state.controller:IsReady() and editor.state.controller:FindText(textID) ~= nil, onClick = function() OpenLocalizedTextModal(textID, targetKind, targetID); end }),
            Declarative.Button("New", { width = 42, height = 42, fontSize = 9, interactable = editor.state.controller:IsReady(), onClick = function() OpenLocalizedTextModal(nil, targetKind, targetID); end })
        )
    );
end

local function MakeActionParameterField(option : any, index : number, parameter : any)
    local label = editor.state.controller:FormatIdentifier(parameter.name);
    if parameter.kind == 2i then
        return MakeInspectorPickerField(`actionParameter{index}`, label, GetMenuPickerItems(), option.actionParameters[index], function(value)
            if editor.state.model == nil or value == nil then return; end
            local currentOption = editor.state.model:GetOptionDraft(option.id);
            if currentOption == nil then return; end
            local parameters = table.clone(currentOption.actionParameters);
            parameters[index] = value;
            editor.state.model:SetOptionFields(option.id, { actionParameters = parameters });
            editor.state.pendingGraphRebuild = true;
        end);
    end

    return Declarative.Vertical({ width = editor.geometry.inspectorInnerWidth, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = editor.geometry.inspectorInnerWidth, height = 12, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
        Declarative.Component({ id = `actionParameter{index}`, size = vec.New(editor.geometry.inspectorInnerWidth, 30), anchor = Declarative.Anchor.BottomLeft, relativePoint = Declarative.Anchor.BottomLeft, factory = function(parent, position, size, layer)
            local input = InputField.New(parent, position, size, layer, { defaultText = tostring(option.actionParameters[index]), defaultTextFontSize = 10, placeholderText = "Whole number" });
            input:SetTextVerticallyCentered();
            input:SetOnTextChanged(function(currentInput)
                local value = tonumber(currentInput:GetText());
                local minimum = integer.tonumber(parameter.minimum);
                local maximum = integer.tonumber(parameter.maximum);
                if value == nil or math.floor(value) ~= value or value < minimum or value > maximum then
                    editor.state.optionParameterErrors[index] = `{label} must be a whole number from {parameter.minimum} through {parameter.maximum}.`;
                    if editor.refs.inspectorStateText ~= nil then editor.refs.inspectorStateText:SetText(editor.state.optionParameterErrors[index]); editor.refs.inspectorStateText:SetColor(editor.colors.danger); end
                    return;
                end
                editor.state.optionParameterErrors[index] = nil;
                if editor.state.model == nil then return; end
                local currentOption = editor.state.model:GetOptionDraft(option.id);
                if currentOption == nil then return; end
                local parameters = table.clone(currentOption.actionParameters);
                parameters[index] = integer.create(value);
                editor.state.model:SetOptionFields(option.id, { actionParameters = parameters });
                RefreshOptionDraftState(option.id);
            end);
            return input;
        end })
    );
end

local function GetMenuLinks(menuID : integer, outgoing : boolean) : { any }
    local result = { };
    for _, edge in editor.state.reachableEdges do
        if (outgoing and edge.from == menuID) or (not outgoing and edge.to == menuID) then table.insert(result, edge); end
    end
    table.sort(result, function(left, right)
        local leftID = if outgoing then left.to else left.from;
        local rightID = if outgoing then right.to else right.from;
        if leftID == rightID then return integer.lt(left.optionID, right.optionID); end
        return integer.lt(leftID, rightID);
    end);
    return result;
end

local function MakeIncomingMenuNavigation(menuID : integer) : (any?, number)
    local sourceIDs = { };
    local seen : { [integer] : boolean } = { };
    for _, edge in editor.state.reachableEdges do
        local sourceDepth = editor.state.menuDepth[edge.from];
        local targetDepth = editor.state.menuDepth[edge.to];
        if edge.to == menuID and sourceDepth ~= nil and targetDepth ~= nil and sourceDepth < targetDepth and not seen[edge.from] then
            seen[edge.from] = true;
            table.insert(sourceIDs, edge.from);
        end
    end
    table.sort(sourceIDs, function(left, right) return integer.lt(left, right); end);
    if #sourceIDs == 0 then return nil, 0; end
    if #sourceIDs == 1 then
        local sourceID = sourceIDs[1];
        return Declarative.Panel({ width = editor.geometry.inspectorInnerWidth, height = 22, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Button("", { position = vec.New(0, 0), size = vec.New(editor.geometry.inspectorInnerWidth, 22), panelTemplate = "DebugDarkGrey", color = editor.colors.panel, hoverColor = editor.colors.nodeResponseHover, borderSize = 0, onClick = function() SelectMenu(sourceID); end }),
            Declarative.Text(`< {GetMenuName(sourceID)}`, { position = vec.New(6, 0), width = editor.geometry.inspectorInnerWidth - 12, height = 22, layer = 2i, anchor = Declarative.Anchor.Left, relativePoint = Declarative.Anchor.Left, fontSize = 9, color = editor.colors.selectedBorder, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText", interactable = false })
        ), 22;
    end

    local options = { };
    for _, sourceID in sourceIDs do table.insert(options, { label = GetMenuName(sourceID), value = sourceID }); end
    return Declarative.Component({
        id = "incomingMenuSelector",
        size = vec.New(editor.geometry.inspectorInnerWidth, 26),
        anchor = Declarative.Anchor.BottomLeft,
        relativePoint = Declarative.Anchor.BottomLeft,
        factory = function(parent, position, size, layer)
            local dropdown = Dropdown.New(parent, editor.state.canvas, position, size, layer, {
                options = options,
                placeholder = `{#sourceIDs} incoming menus`,
                previewMaxCharacters = 34,
                menuLayer = 140i,
                menuWidth = size.x,
                itemHeight = 24,
                button = { panelTemplate = "DebugDarkGrey", color = editor.colors.panel, hoverColor = editor.colors.nodeResponseHover, borderColor = editor.colors.responseBorder, borderSize = 1 },
                onValueChanged = function(value) SelectMenu(value); return true; end,
            });
            dropdown.button:SetFontSize(9);
            return dropdown;
        end,
    }), 26;
end

local function MakeMenuLinkSection(label : string, menuID : integer, outgoing : boolean)
    local links = GetMenuLinks(menuID, outgoing);
    local children = { Declarative.Text(label, { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }) };
    if #links == 0 then
        table.insert(children, MakeInspectorValueCard(if outgoing then "No outgoing menu links" else "No incoming menu links", "Within the selected root flow", 40));
    else
        for index = 1, math.min(2, #links) do
            local edge = links[index];
            local linkedMenuID = if outgoing then edge.to else edge.from;
            local option = FindOption(edge.optionID);
            local text = if option == nil then nil else editor.state.controller:FindText(option.textID);
            local response = if text == nil then `Response #{edge.optionID}` else text.englishValue;
            local primary = if outgoing then response else GetMenuName(linkedMenuID);
            local secondary = if outgoing then `Open Menu -> {GetMenuName(linkedMenuID)}` else response;
            table.insert(children, MakeInspectorValueCard(primary, secondary, 40));
        end
        if #links > 2 then table.insert(children, MakeInspectorValueCard(`+{#links - 2} more links`, nil, 28)); end
    end
    return Declarative.Vertical({ width = editor.geometry.inspectorInnerWidth, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, table.unpack(children));
end

local function MakeMenuInspectorBody(menu : any, title : string, responseListHeight : number) : { any }
    if editor.state.inspectorTab == editor.inspectorTabs.logic then
        local depth = editor.state.menuDepth[menu.id];
        local reachability = if menu.id == editor.state.rootMenuID then "Root of the active flow" elseif depth ~= nil then `Reachable at depth {depth}` else "Not reachable from the selected root";
        return {
            MakeMenuLinkSection("INCOMING LINKS", menu.id, false),
            Declarative.Panel({ width = editor.geometry.inspectorInnerWidth, height = 8, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }),
            MakeMenuLinkSection("OUTGOING LINKS", menu.id, true),
            Declarative.Panel({ width = editor.geometry.inspectorInnerWidth, height = 8, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }),
            Declarative.Text("REACHABILITY", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            MakeInspectorValueCard(reachability, `Selected root menu #{editor.state.rootMenuID}`, 42),
        };
    elseif editor.state.inspectorTab == editor.inspectorTabs.metadata then
        local diagnostics = if #editor.state.diagnostics == 0 then "No graph diagnostics" else `{#editor.state.diagnostics} graph diagnostics`;
        local diagnosticDetail = if #editor.state.diagnostics == 0 then "All reachable menu references resolve" else editor.state.diagnostics[1];
        local creatureReferenceCount = #GetCreatureReferences(editor.state.rootMenuID);
        return {
            Declarative.Text("INTERNAL NAME + ID", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            MakeInspectorValueCard(title, `#{menu.id}`, 42),
            Declarative.Text("ROOT REFERENCES", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            MakeInspectorValueCard(if menu.id == editor.state.rootMenuID then "Selected root flow" else `Active flow root #{editor.state.rootMenuID}`, `{creatureReferenceCount} creature{if creatureReferenceCount == 1 then "" else "s"} use this root flow`, 48),
            Declarative.Button(`Show Creature References ({creatureReferenceCount})`, { width = editor.geometry.inspectorInnerWidth, height = 30, fontSize = 10, onClick = OpenCreatureReferences }),
            Declarative.Text("DIAGNOSTICS", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            MakeInspectorValueCard(diagnostics, diagnosticDetail, 48),
        };
    end

    local optionIDs = editor.state.model:GetOrderedOptionIDs(menu.id);
    return {
        Declarative.Text("MENU CONTENT", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
        Declarative.Text("Internal name", { width = editor.geometry.inspectorInnerWidth, height = 12, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
        Declarative.Component({ id = "menuNameInput", size = vec.New(editor.geometry.inspectorInnerWidth, 30), anchor = Declarative.Anchor.BottomLeft, relativePoint = Declarative.Anchor.BottomLeft, factory = function(parent, position, size, layer)
            local input = InputField.New(parent, position, size, layer, { defaultText = menu.internalName or "", defaultTextFontSize = 10, placeholderText = "Internal name" });
            input:SetTextVerticallyCentered();
            input:SetOnTextChanged(function(currentInput)
                if editor.state.model == nil then return; end
                editor.state.model:SetMenuFields(menu.id, { internalName = currentInput:GetText() });
                RefreshMenuDraftVisuals(menu.id);
            end);
            return input;
        end }),
        MakeLocalizedTextPickerField("greetingPicker", "Opening localized text", menu.greetingTextID, "menu", menu.id, function(value)
                if editor.state.model == nil then return; end
                editor.state.model:SetMenuFields(menu.id, { greetingTextID = value });
                RefreshMenuDraftVisuals(menu.id);
            end),
        Declarative.Text(`RESPONSES ({#optionIDs})`, { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
        Declarative.Component({ id = "inspectorResponses", size = vec.New(editor.geometry.inspectorInnerWidth, responseListHeight), anchor = Declarative.Anchor.BottomLeft, relativePoint = Declarative.Anchor.BottomLeft, factory = function(parent, position, size, layer)
            local list = CreateInspectorResponseList(parent, position, size, layer, menu.id);
            list:SetItems(optionIDs);
            return list;
        end }),
        Declarative.Button("+ Add Response", { width = editor.geometry.inspectorInnerWidth, height = 28, fontSize = 10, interactable = editor.state.controller:IsReady() and not editor.state.model:IsMenuOrderDirty(menu.id), onClick = function() CreateResponse(menu.id); end }),
    };
end

local function MakeResponseInspectorBody(option : any) : { any }
    if editor.state.inspectorTab == editor.inspectorTabs.logic then
        local body = {
            Declarative.Text("CONDITIONS", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            MakeInspectorPickerField("visibilityCondition", "Visibility condition", GetConditionPickerItems(), option.visibilityConditionSetID, function(value)
                if editor.state.model == nil or value == nil then return; end
                editor.state.model:SetOptionFields(option.id, { visibilityConditionSetID = value });
                RefreshOptionDraftState(option.id);
            end),
            MakeInspectorPickerField("enabledCondition", "Enabled condition", GetConditionPickerItems(), option.enabledConditionSetID, function(value)
                if editor.state.model == nil or value == nil then return; end
                local fields : { [string] : any } = { enabledConditionSetID = value };
                if value == 0i then fields.disabledReasonTextID = 0i; end
                editor.state.model:SetOptionFields(option.id, fields);
                editor.state.pendingGraphRebuild = true;
            end),
        };
        if option.enabledConditionSetID ~= 0i then
            table.insert(body, MakeInspectorPickerField("disabledReasonText", "Disabled reason text", GetTextPickerItems(true), option.disabledReasonTextID, function(value)
                if editor.state.model == nil or value == nil then return; end
                editor.state.model:SetOptionFields(option.id, { disabledReasonTextID = value });
                RefreshOptionDraftState(option.id);
            end));
        end
        table.insert(body, Declarative.Text("Visibility controls whether this response is shown. Enabled state can provide localized disabled-reason text.", { width = editor.geometry.inspectorInnerWidth, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
        return body;
    elseif editor.state.inspectorTab == editor.inspectorTabs.metadata then
        local responseName = if type(option.internalName) == "string" and option.internalName ~= "" then option.internalName else "Gossip response";
        return {
            Declarative.Text("INTERNAL NAME + ID", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            MakeInspectorValueCard(responseName, `#{option.id}`, 42),
            MakeInspectorValueCard("Internal note unavailable", "Current gossip response records do not expose a note field", 48),
        };
    end
    local body = {
        Declarative.Text("PLAYER RESPONSE", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
        MakeLocalizedTextPickerField("responseText", "Localized text", option.textID, "option", option.id, function(value)
            if editor.state.model == nil then return; end
            editor.state.model:SetOptionFields(option.id, { textID = value });
            editor.state.pendingGraphRebuild = true;
        end),
        MakeInspectorPickerField("responseAction", "Behavior", GetActionPickerItems(), option.actionType, function(value)
            if editor.state.model == nil or value == nil then return; end
            local descriptor = editor.state.controller:FindActionDescriptor(value);
            if descriptor == nil then return; end
            local parameters = { 0i, 0i, 0i, 0i };
            for index = 1, 4 do parameters[index] = descriptor.parameters[index].minimum; end
            editor.state.optionParameterErrors = { };
            editor.state.model:SetOptionFields(option.id, { actionType = value, actionParameters = parameters });
            editor.state.pendingGraphRebuild = true;
        end),
    };
    local descriptor = editor.state.controller:FindActionDescriptor(option.actionType);
    local parameterCount = 0;
    if descriptor ~= nil then
        for index, parameter in descriptor.parameters do
            if parameter.kind ~= 0i then parameterCount += 1; table.insert(body, MakeActionParameterField(option, index, parameter)); end
        end
    end
    if parameterCount == 0 then table.insert(body, MakeInspectorValueCard("No destination or parameters", "This behavior completes without additional configuration.", 46)); end
    table.insert(body, Declarative.Text("Response order is controlled by dragging within the menu node or response list.", { width = editor.geometry.inspectorInnerWidth, height = 28, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
    return body;
end

CreateInspectorResponseList = function(parent : any, position : vec, size : vec, layer : integer, menuID : integer)
    local list : any = nil;
    list = VirtualList.New(parent, position, size, layer, {
        rowHeight = editor.geometry.inspectorResponseRowHeight,
        scrollBox = { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = size.x, contentSizeY = size.y, verticalScrollBar = true, scrollBarWidth = 8, alpha = 0 },
        createRow = function(rowParent)
            local row = Button.New(rowParent, vec.New(0, 0), vec.New(size.x, editor.geometry.inspectorResponseRowHeight - 4), 1i, {
                text = "", panelTemplate = "DebugDarkGrey", color = editor.colors.responsePlaceholderFill, hoverColor = editor.colors.responsePlaceholderFill, highlightColor = editor.colors.responsePlaceholderFill, alpha = 0.35, hoverAlpha = 0.35, highlightAlpha = 0.35, borderColor = editor.colors.responsePlaceholderBorder, borderSize = 1,
            });
            row:SetAnchor(vec.New(0, 1));
            row:SetRelativePoint(vec.New(0, 1));

            local primaryHeight = UI.CalculateTextSize("Ag", "DefaultButtonText", 10).y;
            local secondaryHeight = UI.CalculateTextSize("Ag", "DefaultButtonText", 9).y;
            local lineGap = 1;
            local textBottom = math.max(0, (editor.geometry.inspectorResponseRowHeight - 4 - primaryHeight - secondaryHeight - lineGap) / 2);
            local placeholderPrimary = row.root:NewText("", vec.New(10, textBottom + secondaryHeight + lineGap), 1i, "DefaultButtonText");
            placeholderPrimary:SetFontSize(10);
            placeholderPrimary:SetColor(editor.colors.responsePlaceholderText);
            placeholderPrimary:SetAlpha(0.55);
            placeholderPrimary:SetInteractable(false);
            local placeholderSecondary = row.root:NewText("", vec.New(10, textBottom), 1i, "DefaultButtonText");
            placeholderSecondary:SetFontSize(9);
            placeholderSecondary:SetColor(editor.colors.responsePlaceholderText);
            placeholderSecondary:SetAlpha(0.42);
            placeholderSecondary:SetInteractable(false);

            local surface = Button.New(row.root, vec.New(0, 0), vec.New(size.x, editor.geometry.inspectorResponseRowHeight - 4), 2i, {
                text = "", panelTemplate = "DebugDarkGrey", color = editor.colors.panel, hoverColor = editor.colors.nodeResponseHover, highlightColor = editor.colors.nodeResponseSelected, alpha = 0.82, hoverAlpha = 1, highlightAlpha = 1, borderColor = editor.colors.responseBorder, borderSize = 1,
            });
            surface.root:SetClipChildren(true);
            local primary = surface.root:NewText("", vec.New(10, textBottom + secondaryHeight + lineGap), 2i, "DefaultButtonText");
            primary:SetFontSize(10);
            primary:SetInteractable(false);
            local secondary = surface.root:NewText("", vec.New(10, textBottom), 2i, "DefaultButtonText");
            secondary:SetFontSize(9);
            secondary:SetColor(editor.colors.muted);
            secondary:SetInteractable(false);

            local selectionOutline = surface.root:NewPanel(vec.New(1, 1), vec.New(math.max(1, size.x - 2), editor.geometry.inspectorResponseRowHeight - 6), 4i, "DebugDarkGrey");
            selectionOutline:SetAlpha(0);
            selectionOutline:SetBorderColor(editor.colors.selectedBorder);
            selectionOutline:SetBorderSize(1);
            selectionOutline:SetInteractable(false);
            selectionOutline:SetVisible(false);

            row.placeholderPrimary = placeholderPrimary;
            row.placeholderSecondary = placeholderSecondary;
            row.surface = surface;
            row.primaryText = primary;
            row.secondaryText = secondary;
            row.selectionOutline = selectionOutline;
            row.listViewport = rowParent;
            row.boundOptionID = nil;
            row.boundIndex = nil;
            row.suppressClick = false;
            surface:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton ~= 0i then return; end
                if row.suppressClick then row.suppressClick = false; return; end
                if row.boundOptionID ~= nil then SelectOption(menuID, row.boundOptionID); end
            end);
            Draggable.Attach(surface, surface, {
                button = 0i,
                moveTarget = false,
                threshold = 4,
                onBegin = function(_, mousePosition)
                    if row.boundOptionID == nil or row.boundIndex == nil or not editor.state.controller:IsReady() then return; end
                    row.suppressClick = true;
                    local order = editor.state.model:GetOrderedOptionIDs(menuID);
                    editor.state.responseDrag = {
                        menuID = menuID,
                        optionID = row.boundOptionID,
                        originalOrder = order,
                        previewOrder = order,
                        targetIndex = row.boundIndex,
                        startWorld = surface:GetWorldPos(),
                        restPosition = vec.New(0, 0),
                        surface = surface,
                        row = row,
                        list = list,
                        inspector = true,
                    };
                end,
                onMove = function(_, mousePosition, delta) PreviewInspectorResponseDrag(mousePosition, delta); end,
                onEnd = function() CommitResponseDrag(); end,
            });
            return row;
        end,
        bindRow = function(row, index, optionID)
            local option = FindOption(optionID);
            local rowWidth = row.listViewport:GetWidth();
            local textWidth = math.max(0, rowWidth - 20);
            row:SetSize(vec.New(rowWidth, editor.geometry.inspectorResponseRowHeight - 4));
            row.surface:SetSize(vec.New(rowWidth, editor.geometry.inspectorResponseRowHeight - 4));
            row.surface:SetPos(vec.New(0, 0));
            row.selectionOutline:SetSize(vec.New(math.max(1, rowWidth - 2), editor.geometry.inspectorResponseRowHeight - 6));
            local activeDrag = editor.state.responseDrag;
            if activeDrag == nil or not activeDrag.inspector or activeDrag.row ~= row then row.suppressClick = false; end
            row.boundOptionID = optionID;
            row.boundIndex = index;
            local primaryValue;
            local secondaryValue;
            if option == nil then
                primaryValue = "Missing response";
                secondaryValue = `#{optionID}`;
            else
                local text = editor.state.controller:FindText(option.textID);
                local primary = if text == nil then `Missing text #{option.textID}` else text.englishValue;
                local _, _, action = GetActionTarget(option);
                primaryValue = TextUtil.TruncateToWidth(primary, "DefaultButtonText", textWidth, 10);
                secondaryValue = TextUtil.TruncateToWidth(`#{option.id} | {action}`, "DefaultButtonText", textWidth, 9);
            end
            row.placeholderPrimary:SetText(primaryValue);
            row.placeholderSecondary:SetText(secondaryValue);
            row.primaryText:SetText(primaryValue);
            row.secondaryText:SetText(secondaryValue);
            local selected = editor.state.model ~= nil and editor.state.model:GetSession().selectedOptionID == optionID;
            row.surface:SetInteractable(false);
            row.surface:SetInteractable(true);
            row.surface:SetHighlighted(selected);
            row.selectionOutline:SetVisible(selected);
        end,
    });
    local scrollBox = list:GetScrollBox();
    scrollBox._viewport:SetOnMouseScroll(function(event, x, y)
        local drag = editor.state.responseDrag;
        if drag ~= nil and drag.inspector and drag.list == list then return; end
        scrollBox:Scroll(event, x, y);
    end);
    list:SetAnchor(vec.New(0, 0));
    list:SetRelativePoint(vec.New(0, 0));
    return list;
end

RefreshInspector = function()
    if editor.refs.inspectorHost == nil then return; end
    editor.refs.inspectorStateText = nil;
    editor.refs.dirtyIndicator = nil;
    editor.refs.inspectorTitle = nil;
    CloseInspectorPickers();
    editor.state.optionParameterErrors = { };
    if editor.refs.incomingMenuSelector ~= nil then editor.refs.incomingMenuSelector:SetVisible(false); editor.refs.incomingMenuSelector = nil; end
    if editor.refs.inspectorView ~= nil then editor.refs.inspectorView:Destroy(); editor.refs.inspectorView = nil; end

    local menu : any = nil;
    local option : any = nil;
    if editor.state.model ~= nil then
        local session = editor.state.model:GetSession();
        menu = editor.state.model:GetMenuDraft(session.selectedMenuID or editor.state.rootMenuID);
        option = if session.selectedOptionID == nil then nil else editor.state.model:GetOptionDraft(session.selectedOptionID);
    end

    local content;
    if menu == nil then
        content = Declarative.Vertical({ size = vec.New(editor.geometry.inspectorWidth, editor.geometry.viewportSize.y), spacing = editor.geometry.inspectorSpacing, padding = editor.geometry.inspectorPadding, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Text("INSPECTOR", { width = editor.geometry.inspectorInnerWidth, height = 16, fontSize = 10, color = editor.colors.selectedBorder, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            Declarative.Text("No menu selected", { width = editor.geometry.inspectorInnerWidth, height = 28, fontSize = 15, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            Declarative.Text("Choose a root flow, then select a menu or response to inspect its authoritative fields.", { width = editor.geometry.inspectorInnerWidth, height = 52, fontSize = 10, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" })
        );
    elseif option == nil then
        local title = if menu.internalName == nil or menu.internalName == "" then `Menu #{menu.id}` else menu.internalName;
        local children = { };
        local incomingNavigation, incomingNavigationHeight = MakeIncomingMenuNavigation(menu.id);
        local navigationBudget = if incomingNavigation == nil then 0 else incomingNavigationHeight + editor.geometry.inspectorSpacing;
        local responseListHeight = math.max(editor.geometry.inspectorResponseRowHeight, editor.geometry.inspectorResponseListHeight - navigationBudget);
        local body = MakeMenuInspectorBody(menu, title, responseListHeight);
        if incomingNavigation ~= nil then table.insert(children, incomingNavigation); end
        table.insert(children, Declarative.Text(if menu.id == editor.state.rootMenuID then "ROOT MENU NODE" else "MENU NODE", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = if menu.id == editor.state.rootMenuID then editor.colors.rootSelectedBorder else editor.colors.selectedBorder, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }));
        table.insert(children, Declarative.Horizontal({ width = editor.geometry.inspectorInnerWidth, height = 24, spacing = 5, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Text(title, { id = "inspectorTitle", width = editor.geometry.inspectorInnerWidth - 63, height = 24, fontSize = 15, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            Declarative.Text(if editor.state.model:IsMenuDirty(menu.id) then "UNSAVED" else "SAVED", { id = "dirtyIndicator", width = 58, height = 24, anchor = Declarative.Anchor.Right, relativePoint = Declarative.Anchor.Right, fontSize = 8, color = if editor.state.model:IsMenuDirty(menu.id) then editor.colors.warning else editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" })
        ));
        table.insert(children, MakeInspectorTabs());
        for _, child in body do table.insert(children, child); end
        table.insert(children, Declarative.Panel({ grow = 1, width = editor.geometry.inspectorInnerWidth, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }));
        table.insert(children, MakeInspectorActions(false, menu.id, nil));
        content = Declarative.Vertical({ size = vec.New(editor.geometry.inspectorWidth, editor.geometry.viewportSize.y), spacing = editor.geometry.inspectorSpacing, padding = editor.geometry.inspectorPadding, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, table.unpack(children));
    else
        local text = editor.state.controller:FindText(option.textID);
        local responseText = if text == nil then `Missing text #{option.textID}` else text.englishValue;
        local menuTitle = if menu.internalName == nil or menu.internalName == "" then `Menu #{menu.id}` else menu.internalName;
        local body = MakeResponseInspectorBody(option);
        local children = {
            Declarative.Panel({ width = editor.geometry.inspectorInnerWidth, height = 22, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                Declarative.Button("", { position = vec.New(0, 0), size = vec.New(editor.geometry.inspectorInnerWidth, 22), panelTemplate = "DebugDarkGrey", color = editor.colors.panel, hoverColor = editor.colors.nodeResponseHover, borderSize = 0, onClick = function() SelectMenu(menu.id); end }),
                Declarative.Text(`< {menuTitle}`, { position = vec.New(6, 0), width = editor.geometry.inspectorInnerWidth - 12, height = 22, layer = 2i, anchor = Declarative.Anchor.Left, relativePoint = Declarative.Anchor.Left, fontSize = 9, color = editor.colors.selectedBorder, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText", interactable = false })
            ),
            Declarative.Text("PLAYER RESPONSE", { width = editor.geometry.inspectorInnerWidth, height = 14, fontSize = 9, color = editor.colors.selectedBorder, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            Declarative.Horizontal({ width = editor.geometry.inspectorInnerWidth, height = 30, spacing = 5, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                Declarative.Text(responseText, { width = editor.geometry.inspectorInnerWidth - 63, height = 30, fontSize = 13, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
                Declarative.Text(if editor.state.model:IsOptionDirty(option.id) then "UNSAVED" else "SAVED", { id = "dirtyIndicator", width = 58, height = 30, anchor = Declarative.Anchor.Right, relativePoint = Declarative.Anchor.Right, fontSize = 8, color = if editor.state.model:IsOptionDirty(option.id) then editor.colors.warning else editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" })
            ),
            MakeInspectorTabs(),
        };
        for _, child in body do table.insert(children, child); end
        table.insert(children, Declarative.Panel({ grow = 1, width = editor.geometry.inspectorInnerWidth, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }));
        table.insert(children, MakeInspectorActions(true, menu.id, option.id));
        content = Declarative.Vertical({ size = vec.New(editor.geometry.inspectorWidth, editor.geometry.viewportSize.y), spacing = editor.geometry.inspectorSpacing, padding = editor.geometry.inspectorPadding, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, table.unpack(children));
    end

    editor.refs.inspectorView = Declarative.Build(editor.refs.inspectorHost, content);
    editor.refs.incomingMenuSelector = editor.refs.inspectorView:Get("incomingMenuSelector");
    editor.refs.inspectorTitle = editor.refs.inspectorView:Get("inspectorTitle");
    editor.refs.inspectorStateText = editor.refs.inspectorView:Get("inspectorState");
    editor.refs.dirtyIndicator = editor.refs.inspectorView:Get("dirtyIndicator");
    RefreshFlowActions();
end

local function CreateSidebarList(parent : any, position : vec, size : vec, layer : integer)
    local list = VirtualList.New(parent, position, size, layer, {
        rowHeight = editor.geometry.menuRowHeight,
        scrollBox = {
            panelTemplate = "DebugDarkGrey",
            contentPanelTemplate = "DebugDarkGrey",
            contentSizeX = editor.geometry.sidebarListContentWidth,
            contentSizeY = size.y,
            verticalScrollBar = true,
            scrollBarWidth = editor.geometry.sidebarScrollbarWidth,
            alpha = 0,
        },
        createRow = function(rowParent)
            local row = Button.New(rowParent, vec.New(0, 0), vec.New(size.x, editor.geometry.menuRowHeight - 2), 1i, {
                text = "",
                panelTemplate = "DebugDarkGrey",
                textTemplate = "DefaultButtonText",
                textAlign = "left",
                textPadX = 10,
                color = vec.New(0.08, 0.095, 0.11),
                hoverColor = vec.New(0.12, 0.18, 0.24),
                highlightColor = vec.New(0.13, 0.24, 0.32),
                alpha = 0.88,
                hoverAlpha = 1,
                highlightAlpha = 1,
            });
            row:SetAnchor(vec.New(0, 1));
            row:SetRelativePoint(vec.New(0, 1));
            row:SetFontSize(10);
            row.root:SetClipChildren(true);
            row.text:SetAnchor(vec.New(0, 1));
            row.text:SetRelativePoint(vec.New(0, 1));
            row.text:SetPos(vec.New(10, -5));

            local secondary = row.root:NewText("", vec.New(10, -23), 2i, "DefaultButtonText");
            secondary:SetAnchor(vec.New(0, 1));
            secondary:SetRelativePoint(vec.New(0, 1));
            secondary:SetFontSize(9);
            secondary:SetColor(editor.colors.muted);
            secondary:SetInteractable(false);

            local rootMarker = row.root:NewPanel(vec.New(0, 0), vec.New(3, editor.geometry.menuRowHeight - 8), 3i, "DebugDarkGrey");
            rootMarker:SetAnchor(vec.New(0, 0.5));
            rootMarker:SetRelativePoint(vec.New(0, 0.5));
            rootMarker:SetColor(editor.colors.rootBorder);
            rootMarker:SetBorderSize(0);
            rootMarker:SetInteractable(false);

            row.secondaryText = secondary;
            row.rootMarker = rootMarker;
            row.listViewport = rowParent;
            row.boundItem = nil;
            row:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton ~= 0i or row.boundItem == nil then return; end
                SelectMenu(row.boundItem.menuID);
            end);
            return row;
        end,
        bindRow = function(row, _, item)
            local rowWidth = row.listViewport:GetWidth();
            local textWidth = math.max(0, rowWidth - 20);
            row:SetSize(vec.New(rowWidth, editor.geometry.menuRowHeight - 2));
            row.boundItem = item;
            row:SetText(TextUtil.TruncateToWidth(item.primary, "DefaultButtonText", textWidth, 10));
            row.secondaryText:SetText(TextUtil.TruncateToWidth(item.secondary, "DefaultButtonText", textWidth, 9));
            row.rootMarker:SetVisible(item.isRoot);
            row:SetHighlighted(editor.state.model ~= nil and editor.state.model:GetSession().selectedMenuID == item.menuID);
        end,
    });
    list:SetAnchor(vec.New(0, 0));
    list:SetRelativePoint(vec.New(0, 0));
    return list;
end

local function MakeResponseNode(menuID : integer, optionID : integer, contentWidth : number, responseOffset : number, responseWidth : number)
    local option = FindOption(optionID);
    local scale = editor.state.model:GetZoom();
    local rowHeight = editor.geometry.nodeResponseHeight * scale;
    local separatorHeight = editor.geometry.nodeSeparatorHeight * scale;
    local slotHeight = rowHeight + separatorHeight;
    if option == nil then return Declarative.Text("Missing response", { id = `optionSlot{optionID}`, width = contentWidth, height = slotHeight, fontSize = ScaledFontSize(9), overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }); end
    local target, isMenuReference, action = GetActionTarget(option);
    local text = editor.state.controller:FindText(option.textID);
    local primary = if text then text.englishValue else `Missing text #{option.textID}`;
    local secondary = if isMenuReference then action else `[{action}]`;
    if target ~= nil and FindMenu(target) == nil then secondary = `Missing menu #{target}`; end
    return Declarative.Panel({
        id = `optionSlot{optionID}`,
        size = vec.New(contentWidth, slotHeight),
        template = "DebugDarkGrey",
        alpha = 0,
        borderSize = 0,
    },
        Declarative.Panel({ position = vec.New(responseOffset, rowHeight), size = vec.New(responseWidth, separatorHeight), template = "DebugDarkGrey", color = editor.colors.responsePlaceholderBorder, alpha = 0.42, borderSize = 0, interactable = false }),
        Declarative.Panel({ position = vec.New(responseOffset, 0), size = vec.New(responseWidth, rowHeight), template = "DebugDarkGrey", color = editor.colors.responsePlaceholderFill, alpha = 0.35, borderSize = 0, interactable = false }),
        Declarative.Text(primary, {
            position = vec.New(responseOffset + editor.geometry.nodeResponseTextInset * scale, (editor.geometry.nodeResponsePaddingY + editor.geometry.nodeResponseSecondaryHeight) * scale),
            width = responseWidth - editor.geometry.nodeResponseTextInset * 2 * scale,
            height = editor.geometry.nodeResponsePrimaryHeight * scale,
            layer = 1i,
            fontSize = ScaledFontSize(10),
            color = editor.colors.responsePlaceholderText,
            alpha = 0.55,
            overflow = Declarative.TextOverflow.Truncate,
            template = "DefaultButtonText",
            interactable = false,
        }),
        Declarative.Text(secondary, {
            position = vec.New(responseOffset + editor.geometry.nodeResponseTextInset * scale, editor.geometry.nodeResponsePaddingY * scale),
            width = responseWidth - editor.geometry.nodeResponseTextInset * 2 * scale,
            height = editor.geometry.nodeResponseSecondaryHeight * scale,
            layer = 1i,
            fontSize = ScaledFontSize(9),
            color = editor.colors.responsePlaceholderText,
            alpha = 0.42,
            overflow = Declarative.TextOverflow.Truncate,
            template = "DefaultButtonText",
            interactable = false,
        }),
        Declarative.Component({
            id = `optionButton{optionID}`,
            position = vec.New(responseOffset, 0),
            size = vec.New(responseWidth, rowHeight),
            layer = 2i,
            anchor = Declarative.Anchor.BottomLeft,
            relativePoint = Declarative.Anchor.BottomLeft,
            factory = function(parent, position, size, layer)
                local surface = Button.New(parent, position, size, layer, {
                    text = "", panelTemplate = "DebugDarkGrey", color = editor.colors.nodeResponseFill, hoverColor = editor.colors.nodeResponseHover, highlightColor = editor.colors.nodeResponseSelected, alpha = 0.82, hoverAlpha = 1, highlightAlpha = 1, borderSize = 0,
                });
                surface.root:SetClipChildren(true);
                local textWidth = math.max(0, responseWidth - editor.geometry.nodeResponseTextInset * 2 * scale);
                local primaryText = surface.root:NewText(TextUtil.TruncateToWidth(primary, "DefaultButtonText", textWidth, ScaledFontSize(10)), vec.New(editor.geometry.nodeResponseTextInset * scale, (editor.geometry.nodeResponsePaddingY + editor.geometry.nodeResponseSecondaryHeight) * scale), 2i, "DefaultButtonText");
                primaryText:SetFontSize(ScaledFontSize(10));
                primaryText:SetInteractable(false);
                local secondaryText = surface.root:NewText(TextUtil.TruncateToWidth(secondary, "DefaultButtonText", textWidth, ScaledFontSize(9)), vec.New(editor.geometry.nodeResponseTextInset * scale, editor.geometry.nodeResponsePaddingY * scale), 2i, "DefaultButtonText");
                secondaryText:SetFontSize(ScaledFontSize(9));
                secondaryText:SetColor(editor.colors.muted);
                secondaryText:SetInteractable(false);
                surface:SetHighlighted(editor.state.model:GetSession().selectedOptionID == optionID);
                local suppressClick = false;
                surface:SetOnMouseUp(function(_, _, mouseButton)
                    if mouseButton ~= 0i then return; end
                    if suppressClick then suppressClick = false; return; end
                    SelectOption(menuID, optionID);
                end);
                Draggable.Attach(surface, surface, {
                    button = 0i,
                    moveTarget = false,
                    threshold = 4,
                    onBegin = function(_, mousePosition) suppressClick = true; BeginGraphResponseDrag(menuID, optionID, surface, mousePosition); end,
                    onMove = function(_, mousePosition, delta) PreviewGraphResponseDrag(mousePosition, delta); end,
                    onEnd = function() CommitResponseDrag(); end,
                });
                return surface;
            end,
        })
    );
end

local function MakeNode(menuID : integer)
    local menu = FindMenu(menuID);
    local nodeSize = editor.state.nodeSizes[menuID];
    local scale = editor.state.model:GetZoom();
    local contentInset = editor.geometry.nodeGreetingInset * scale;
    local contentWidth = nodeSize.x - contentInset * 2;
    local responseOffset = (editor.geometry.nodeResponseInset - editor.geometry.nodeGreetingInset) * scale;
    local responseWidth = nodeSize.x - editor.geometry.nodeResponseInset * 2 * scale;
    local headerHeight = editor.geometry.nodeHeaderHeight * scale;
    local headerInset = editor.geometry.nodeContentInset * scale;
    local headerIDWidth = editor.geometry.nodeHeaderIDWidth * scale;
    local headerGap = editor.geometry.nodeHeaderGap * scale;
    local headerTitleWidth = math.max(1, nodeSize.x - headerInset * 2 - headerIDWidth - headerGap);
    local outlineSize = vec.New(math.max(1, nodeSize.x - 2), math.max(1, nodeSize.y - 2));
    local headerTitle = if menu.internalName == nil or menu.internalName == "" then `Menu #{menuID}` else menu.internalName;
    local selected = editor.state.model:GetSession().selectedMenuID == menuID;
    local bodyChildren = { Declarative.Text(GetGreeting(menu), { id = "greeting", width = contentWidth, height = editor.geometry.nodeGreetingHeight * scale, fontSize = ScaledFontSize(12), overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }) };
    for _, optionID in editor.state.model:GetOrderedOptionIDs(menuID) do
        table.insert(bodyChildren, MakeResponseNode(menuID, optionID, contentWidth, responseOffset, responseWidth));
    end
    return Declarative.Panel({
        id = `menu{menuID}`,
        size = nodeSize,
        template = "DebugDarkGrey",
        color = editor.colors.nodeFill,
        borderSize = 0,
        clipChildren = true,
        setup = function(panel) panel:SetOnMouseUp(function() SelectMenu(menuID); end); end,
    },
        Declarative.Panel({ id = "header", position = vec.New(0, nodeSize.y - headerHeight), size = vec.New(nodeSize.x, headerHeight), template = "DebugDarkGrey", color = if menuID == editor.state.rootMenuID then vec.New(0.20, 0.18, 0.13) else vec.New(0.12, 0.16, 0.20), borderSize = 0 },
            Declarative.Text(headerTitle, { id = "headerTitle", position = vec.New(headerInset, 0), width = headerTitleWidth, height = headerHeight, anchor = Declarative.Anchor.Left, relativePoint = Declarative.Anchor.Left, fontSize = ScaledFontSize(11), overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            Declarative.Text(`#{menuID}`, { position = vec.New(-headerInset, 0), width = headerIDWidth, height = headerHeight, anchor = Declarative.Anchor.Right, relativePoint = Declarative.Anchor.Right, fontSize = ScaledFontSize(9), color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" })
        ),
        Declarative.Vertical({ id = "responsesLayout", position = vec.New(contentInset, editor.geometry.nodeBottomPadding * scale), size = vec.New(contentWidth, nodeSize.y - headerHeight - editor.geometry.nodeBottomPadding * scale), spacing = 0, padding = 0, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, table.unpack(bodyChildren)),
        Declarative.Panel({ id = "outline", position = vec.New(1, 1), size = outlineSize, layer = 10i, template = "DebugDarkGrey", alpha = 0, borderColor = if menuID == editor.state.rootMenuID then (if selected then editor.colors.rootSelectedBorder else editor.colors.rootBorder) elseif selected then editor.colors.selectedBorder else editor.colors.nodeBorder, borderSize = 1, interactable = false })
    );
end

RebuildNodes = function()
    for _, nodeView in editor.state.nodeViews do nodeView:Destroy(); end
    editor.state.nodeViews = { };
    editor.state.nodePositions = { };
    editor.state.baseNodeSizes = { };
    editor.state.nodeSizes = { };
    if editor.state.model == nil then return; end
    BuildReachableGraph();
    for _, menuID in editor.state.reachableMenus do
        editor.state.baseNodeSizes[menuID] = GetNodeSize(menuID);
        editor.state.nodeSizes[menuID] = ScaledSize(editor.state.baseNodeSizes[menuID]);
    end
    EnsureNodePositions();
    for _, menuID in editor.state.reachableMenus do
        local nodeID = menuID;
        local nodeView = Declarative.Build(editor.refs.viewport, MakeNode(nodeID));
        editor.state.nodeViews[nodeID] = nodeView;
        local header = nodeView:Get("header");
        if header == nil or nodeView:Get("outline") == nil or nodeView:Get("responsesLayout") == nil then error("GossipFlowEditor: node refs were not built"); end
        Draggable.Attach(nodeView.root, header, {
            button = 0i,
            moveTarget = false,
            onBegin = function(_, mousePosition)
                SelectMenu(nodeID);
                local currentPosition = editor.state.model:GetNodePosition(nodeID) or vec.New(0, 0);
                editor.state.dragMenuID = nodeID;
                editor.state.dragGrabOffset = LocalToGraph(GetViewportLocal(mousePosition)) - currentPosition;
            end,
            onMove = function(_, mousePosition)
                if editor.state.dragMenuID ~= nodeID or editor.state.model == nil then return; end
                local nextPosition = LocalToGraph(GetViewportLocal(mousePosition)) - editor.state.dragGrabOffset;
                editor.state.model:SetNodePosition(nodeID, nextPosition);
                editor.state.nodePositions[nodeID] = nextPosition;
                ApplyNodePositions();
            end,
            onEnd = function(_, _, _)
                if editor.state.dragMenuID == nodeID then
                    editor.state.dragMenuID = nil;
                end
            end,
        });
    end
    ApplyNodePositions();
    RefreshResponseSelection();
    RefreshNavigation(true, true);
    RefreshInspector();
end

ApplySnapshot = function()
    local nextSnapshot = editor.state.controller:Refresh();
    editor.state.snapshot = nextSnapshot;
    if editor.refs.newRootButton ~= nil then editor.refs.newRootButton:SetInteractable(editor.state.controller:IsReady()); end
    if nextSnapshot.dataState ~= "Ready" or editor.state.controller:IsAwaitingSnapshot() then
        return;
    end
    if editor.state.model == nil then editor.state.model = GossipMenuModel.New(nextSnapshot); else editor.state.model:Reload(nextSnapshot); end
    local ids = editor.state.model:GetMenuIDs();
    local controllerMenuID = editor.state.controller:GetSelectedMenuID();
    local controllerOptionID = editor.state.controller:GetSelectedOptionID();
    if controllerOptionID ~= nil and FindOption(controllerOptionID) ~= nil then
        editor.state.model:SetSelectedOption(controllerOptionID);
    elseif controllerMenuID ~= nil and FindMenu(controllerMenuID) ~= nil then
        editor.state.model:SetSelectedMenu(controllerMenuID);
    end
    if editor.state.pendingRootMenuCreation ~= nil then
        local createdMenuID = if controllerMenuID ~= nil and not editor.state.pendingRootMenuCreation[controllerMenuID] and FindMenu(controllerMenuID) ~= nil then controllerMenuID else nil;
        if createdMenuID ~= nil then
            SwitchRootMenu(createdMenuID, false);
            editor.state.pendingRootMenuCreation = nil;
        else
            local _, statusLevel = editor.state.controller:GetStatus();
            if editor.state.controller:GetPendingRequestID() == 0i and statusLevel == STATUS.Error then editor.state.pendingRootMenuCreation = nil; end
        end
    end
    if editor.state.rootMenuID == nil or FindMenu(editor.state.rootMenuID) == nil then
        if ids[1] ~= nil then
            SwitchRootMenu(ids[1], false);
        else
            editor.state.rootMenuID = nil;
            editor.state.model:ResetNodePositions();
            editor.state.model:SetPanOffset(vec.New(0, 0));
            editor.state.model:SetZoom(1);
        end
    end
    local selectedMenuID = editor.state.model:GetSession().selectedMenuID;
    if selectedMenuID == nil or FindMenu(selectedMenuID) == nil then
        if editor.state.rootMenuID ~= nil then editor.state.model:SetSelectedMenu(editor.state.rootMenuID); editor.state.controller:SelectMenu(editor.state.rootMenuID); end
    end
    RefreshRootPickerItems();
    RebuildNodes();
    RefreshZoomText();
end

local view = Declarative.Build(editor.state.canvas,
    Declarative.Panel({ id = "root", position = editor.geometry.windowPosition, size = editor.geometry.windowSize, template = "DialogBox", clipChildren = true },
        Declarative.Horizontal({ id = "toolbar", position = editor.geometry.toolbarPosition, size = vec.New(editor.geometry.toolbarWidth, editor.geometry.toolbarHeight), spacing = 6, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Panel({ id = "dragHandle", grow = 1, height = editor.geometry.toolbarHeight, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                Declarative.Text("Gossip Editor", { height = editor.geometry.toolbarHeight, anchor = Declarative.Anchor.Left, relativePoint = Declarative.Anchor.Left, fontSize = 15, template = "DefaultButtonText" })
            ),
            Declarative.Panel({ width = 88, height = editor.geometry.toolbarHeight, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                Declarative.Text("No flow", { id = "flowDirty", width = 88, height = editor.geometry.toolbarHeight, anchor = Declarative.Anchor.Right, relativePoint = Declarative.Anchor.Right, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" })
            ),
            Declarative.Button("Revert Flow", { id = "revertFlow", size = vec.New(82, editor.geometry.toolbarHeight), fontSize = 9, interactable = false, onClick = ConfirmRevertFlow }),
            Declarative.Button("Apply Flow", { id = "applyFlow", size = vec.New(82, editor.geometry.toolbarHeight), fontSize = 9, panelTemplate = "DebugDarkGrey", color = editor.colors.activeTab, hoverColor = vec.New(0.18, 0.34, 0.47), alpha = 1, disabledAlpha = 0.16, borderSize = 0, interactable = false, onClick = ApplyFlow }),
            Declarative.Panel({ width = 40, height = editor.geometry.toolbarHeight, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }),
            Declarative.Button("Refresh", { id = "refresh", size = vec.New(76, editor.geometry.toolbarHeight), fontSize = 11, onClick = function() editor.state.controller:RequestSnapshot(); end }),
            Declarative.Button("Close", { id = "close", size = vec.New(62, editor.geometry.toolbarHeight), fontSize = 11, onClick = function() RequestCloseEditor(); end })
        ),
        Declarative.Horizontal({ id = "body", position = editor.geometry.bodyPosition, size = editor.geometry.bodySize, spacing = 0, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Vertical({ id = "menus", width = editor.geometry.sidebarWidth, height = editor.geometry.viewportSize.y, spacing = editor.geometry.sidebarSpacing, padding = editor.geometry.sidebarPadding, template = "DebugDarkGrey", alpha = 0.72, events = { mouseDown = function() end } },
                Declarative.Horizontal({ width = editor.geometry.sidebarInnerWidth, height = editor.geometry.sidebarHeaderHeight, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                    Declarative.Text("ROOT FLOW", { grow = 1, height = editor.geometry.sidebarHeaderHeight, fontSize = 11, color = editor.colors.selectedBorder, template = "DefaultButtonText" }),
                    Declarative.Panel({ width = 72, height = editor.geometry.sidebarHeaderHeight, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                        Declarative.Text("0 menus", { id = "reachableCount", width = 72, height = editor.geometry.sidebarHeaderHeight, anchor = Declarative.Anchor.Right, relativePoint = Declarative.Anchor.Right, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" })
                    )
                ),
                Declarative.Component({ id = "rootPicker", width = editor.geometry.sidebarInnerWidth, height = editor.geometry.sidebarPickerHeight, anchor = Declarative.Anchor.BottomLeft, relativePoint = Declarative.Anchor.BottomLeft, factory = function(parent, position, size, layer)
                    return SearchPicker.New(parent, editor.state.canvas, position, size, layer, { items = { }, placeholder = "Select root menu", popupWidth = 320, popupHeight = 360, rowHeight = 32, onValueChanged = function(value)
                        if value ~= nil then SwitchRootMenu(value); end
                    end, button = { textTemplate = "DefaultButtonText" } });
                end }),
                Declarative.Component({ id = "sidebarSearch", width = editor.geometry.sidebarInnerWidth, height = editor.geometry.sidebarSearchHeight, anchor = Declarative.Anchor.BottomLeft, relativePoint = Declarative.Anchor.BottomLeft, factory = function(parent, position, size, layer)
                    local input = InputBox.New(parent, position, size, layer, { panelTemplate = "DialogBox", textTemplate = "DefaultButtonText", defaultTextFontSize = 10, placeholderText = "Find menu in this flow...", placeholderTextColor = editor.colors.muted });
                    input:SetTextVerticallyCentered();
                    input:SetOnTextChanged(function() RefreshSidebar(false, false); end);
                    return input;
                end }),
                Declarative.Panel({ width = editor.geometry.sidebarInnerWidth, height = editor.geometry.sidebarListHeight, template = "DebugDarkGrey", clipChildren = true },
                    Declarative.Component({ id = "sidebarList", position = vec.New(0, 0), size = vec.New(editor.geometry.sidebarInnerWidth, editor.geometry.sidebarListHeight), anchor = Declarative.Anchor.BottomLeft, relativePoint = Declarative.Anchor.BottomLeft, factory = CreateSidebarList }),
                    Declarative.Panel({ id = "diagnosticsBanner", position = vec.New(0, 0), size = vec.New(editor.geometry.sidebarListContentWidth, 20), layer = 5i, template = "DebugDarkGrey", color = vec.New(0.22, 0.15, 0.07), borderColor = editor.colors.warning, borderSize = 1, visible = false, events = { mouseDown = function() end } },
                        Declarative.Text("", { id = "diagnosticsText", position = vec.New(6, 0), width = editor.geometry.sidebarListContentWidth - 12, height = 20, anchor = Declarative.Anchor.Left, relativePoint = Declarative.Anchor.Left, fontSize = 9, color = editor.colors.warning, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" })
                    )
                ),
                Declarative.Button("+ New Root Flow", { id = "newRoot", width = editor.geometry.sidebarInnerWidth, height = editor.geometry.sidebarFooterHeight, fontSize = 10, interactable = editor.state.controller:IsReady(), onClick = CreateRootMenu })
            ),
            Declarative.Panel({ id = "viewport", width = editor.geometry.viewportSize.x, height = editor.geometry.viewportSize.y, template = "DebugDarkGrey", clipChildren = true, setup = function(panel)
                editor.refs.viewport = panel;
                panel:SetOnMouseDown(function(_, _, button, mouseX, mouseY)
                    if button ~= 2i or editor.state.model == nil then return; end
                    editor.state.panActive = true; editor.state.panMouseStart = vec.New(mouseX, mouseY); editor.state.panOffsetStart = editor.state.model:GetPanOffset();
                end);
                panel:SetOnMouseHeld(function(_, _, mouseX, mouseY)
                    if not editor.state.panActive or editor.state.model == nil then return; end
                    editor.state.model:SetPanOffset(editor.state.panOffsetStart + vec.New(mouseX, mouseY) - editor.state.panMouseStart); ApplyNodePositions();
                end);
                panel:SetOnMouseRelease(function(_, _, button) if button == 2i then editor.state.panActive = false; end end);
                panel:SetOnMouseScroll(function(_, _, _, scrollY)
                    if editor.state.model == nil or scrollY == 0 then return; end
                    local mousePosition = UI.GetMousePos();
                    local localPosition = GetViewportLocal(mousePosition);
                    local graphUnderCursor = LocalToGraph(localPosition);
                    local nextZoom = math.clamp(editor.state.model:GetZoom() * (if scrollY > 0 then 1.2 else 1 / 1.2), 0.55, 2.2);
                    editor.state.model:SetZoom(nextZoom);
                    editor.state.model:SetPanOffset(localPosition - editor.geometry.graphCenter - graphUnderCursor * nextZoom);
                    RebuildNodes();
                    RefreshZoomText();
                end);
            end },
            Declarative.Horizontal({ id = "graphControls", position = editor.geometry.graphControlsPosition, size = editor.geometry.graphControlsSize, layer = 20i, spacing = 4, padding = 4, template = "DebugDarkGrey", alpha = 0.94 },
                Declarative.Button("Reset View", { id = "resetView", size = vec.New(82, 26), fontSize = 10, onClick = function() if editor.state.model ~= nil then editor.state.model:SetPanOffset(vec.New(0, 0)); editor.state.model:SetZoom(1); RebuildNodes(); RefreshZoomText(); end end }),
                Declarative.Button("Auto Layout", { id = "autoLayout", size = vec.New(88, 26), fontSize = 10, onClick = function() if editor.state.model ~= nil then editor.state.model:ResetNodePositions(); RebuildNodes(); RefreshZoomText(); end end }),
                Declarative.Text("100%", { id = "zoom", width = 58, height = 26, fontSize = 10, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" })
            )),
            Declarative.Panel({ id = "inspectorHost", width = editor.geometry.inspectorWidth, height = editor.geometry.viewportSize.y, template = "DebugDarkGrey", alpha = 0.72, clipChildren = true, events = { mouseDown = function() end } })
        )
    )
);

GossipFlowEditorView = view;
local dragHandle = view:Get("dragHandle");
editor.refs.picker = view:Get("rootPicker");
editor.refs.sidebarSearch = view:Get("sidebarSearch");
editor.refs.sidebarList = view:Get("sidebarList");
editor.refs.reachableCountText = view:Get("reachableCount");
editor.refs.diagnosticsBanner = view:Get("diagnosticsBanner");
editor.refs.diagnosticsText = view:Get("diagnosticsText");
editor.refs.inspectorHost = view:Get("inspectorHost");
editor.refs.newRootButton = view:Get("newRoot");
editor.refs.flowDirtyText = view:Get("flowDirty");
editor.refs.revertFlowButton = view:Get("revertFlow");
editor.refs.applyFlowButton = view:Get("applyFlow");
editor.refs.zoomText = view:Get("zoom");
if dragHandle == nil or editor.refs.picker == nil or editor.refs.sidebarSearch == nil or editor.refs.sidebarList == nil or editor.refs.reachableCountText == nil or editor.refs.diagnosticsBanner == nil or editor.refs.diagnosticsText == nil or editor.refs.inspectorHost == nil or editor.refs.newRootButton == nil or editor.refs.flowDirtyText == nil or editor.refs.revertFlowButton == nil or editor.refs.applyFlowButton == nil or editor.refs.zoomText == nil or editor.refs.viewport == nil then error("GossipFlowEditor: declarative shell refs were not built"); end
Draggable.Attach(view.root, dragHandle);
Draggable.Attach(view.root, view.root);

local localizedText = editor.state.localizedText;
local localizedTextView = Declarative.Build(editor.state.canvas, Declarative.Panel({ position = vec.New(0, 0), size = vec.New(1920, 1080), layer = 1800i, anchor = Declarative.Anchor.TopLeft, relativePoint = Declarative.Anchor.TopLeft, template = "DebugDarkGrey", color = vec.New(0.02, 0.025, 0.03), alpha = 0.55, borderSize = 0, interactable = true, enabled = false, events = { mouseDown = function() end, mouseScroll = function() end } }));
localizedText.root = localizedTextView.root;
GossipFlowLocalizedTextModalView = localizedTextView;

local localizedDialog = localizedText.root:NewPanel(vec.New(0, 0), vec.New(760, 388), 1i, "DialogBox");
localizedDialog:SetAnchor(vec.New(0.5, 0.5));
localizedDialog:SetRelativePoint(vec.New(0.5, 0.5));
localizedText.dialog = localizedDialog;
local localizedEyebrow = localizedDialog:NewText("LOCALIZED TEXT", vec.New(12, -9), 2i, "DefaultButtonText");
localizedEyebrow:SetAnchor(vec.New(0, 1));
localizedEyebrow:SetRelativePoint(vec.New(0, 1));
localizedEyebrow:SetFontSize(9);
localizedEyebrow:SetColor(editor.colors.selectedBorder);
localizedText.title = localizedDialog:NewText("New text", vec.New(12, -25), 2i, "DefaultButtonText");
localizedText.title:SetAnchor(vec.New(0, 1));
localizedText.title:SetRelativePoint(vec.New(0, 1));
localizedText.title:SetFontSize(12);
local headerSeparator = localizedDialog:NewPanel(vec.New(0, -54), vec.New(760, 1), 2i, "DebugDarkGrey");
headerSeparator:SetAnchor(vec.New(0, 1));
headerSeparator:SetRelativePoint(vec.New(0, 1));
headerSeparator:SetColor(editor.colors.responseBorder);
headerSeparator:SetBorderSize(0);
local footerSeparator = localizedDialog:NewPanel(vec.New(0, 46), vec.New(760, 1), 2i, "DebugDarkGrey");
footerSeparator:SetAnchor(vec.New(0, 0));
footerSeparator:SetRelativePoint(vec.New(0, 0));
footerSeparator:SetColor(editor.colors.responseBorder);
footerSeparator:SetBorderSize(0);
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
local sourceLabel = NewLocalizedLabel("enUS *", vec.New(12, -128));
sourceLabel:SetColor(editor.colors.selectedBorder);
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
localizedText.saveButton = Button.New(localizedDialog, vec.New(-12, 10), vec.New(84, 28), 2i, { text = "Save Text", textTemplate = "DefaultButtonText", color = editor.colors.activeTab, hoverColor = vec.New(0.18, 0.34, 0.47) });
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
        if not valid then SetLocalizedTextStatus("Internal key and enUS source are required.", editor.colors.danger);
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
    if textID == nil or textID == 0i then SetLocalizedTextStatus("The saved localized-text ID is unavailable.", editor.colors.danger); return; end
    local assigned = false;
    if localizedText.targetKind == "menu" then
        if editor.state.model ~= nil and editor.state.model:GetMenuDraft(localizedText.targetID) ~= nil then editor.state.model:SetMenuFields(localizedText.targetID, { greetingTextID = textID }); assigned = true; end
    elseif localizedText.targetKind == "option" then
        if editor.state.model ~= nil and editor.state.model:GetOptionDraft(localizedText.targetID) ~= nil then editor.state.model:SetOptionFields(localizedText.targetID, { textID = textID }); assigned = true; end
    end
    editor.state.controller:SetStatus(if assigned then "Localized text saved and assigned." else "Localized text saved, but its target is no longer available.", if assigned then STATUS.Success else STATUS.Warning);
    editor.state.pendingGraphRebuild = true;
    SetLocalizedTextOpen(false);
end

local function SubmitNextLocalizedTextOperation()
    localizedText.operationIndex += 1;
    local operation = localizedText.operations[localizedText.operationIndex];
    if operation == nil then CompleteLocalizedTextSave(); return; end
    local textID = localizedText.activeTextID;
    if textID == nil or textID == 0i then RefreshLocalizedTextActions(); SetLocalizedTextStatus("The localized-text ID is unavailable.", editor.colors.danger); return; end
    local requestID = 0i;
    if operation.kind == "updateText" then requestID = InteractionEditor.UpdateText(textID, operation.internalName, operation.englishValue, operation.translatorContext);
    elseif operation.kind == "createTranslation" then requestID = InteractionEditor.CreateTranslation(textID, operation.locale, operation.value);
    elseif operation.kind == "updateTranslation" then requestID = InteractionEditor.UpdateTranslation(textID, operation.locale, operation.value);
    elseif operation.kind == "deleteTranslation" then requestID = InteractionEditor.DeleteTranslation(textID, operation.locale); end
    localizedText.pendingRequestID = requestID;
    RefreshLocalizedTextActions();
    if requestID == 0i then SetLocalizedTextStatus("The localized-text mutation could not be submitted.", editor.colors.danger); end
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
        if localizedText.pendingRequestID == 0i then SetLocalizedTextStatus("The localized text could not be created.", editor.colors.danger); end
    else
        localizedText.activeTextID = localizedText.editingID;
        SubmitNextLocalizedTextOperation();
    end
end

local function ApplySuccessfulLocalizedOperation(operation : any)
    if operation == nil then return; end
    if operation.kind == "updateText" then localizedText.baselineText = { internalName = operation.internalName, englishValue = operation.englishValue, translatorContext = operation.translatorContext };
    elseif operation.kind == "deleteTranslation" then localizedText.baselineTranslations[operation.locale] = "";
    else localizedText.baselineTranslations[operation.locale] = operation.value; end
end

local function UpdateLocalizedTextSave()
    if localizedText.pendingRequestID == 0i then return; end
    local result = InteractionEditor.TakeMutationResult(localizedText.pendingRequestID);
    if result == nil then return; end
    localizedText.pendingRequestID = 0i;
    if not result.succeeded then
        localizedText.awaitingCreate = false;
        localizedText.operations = { };
        RefreshLocalizedTextActions();
        SetLocalizedTextStatus(if result.response ~= "" then result.response else "The localized-text mutation failed.", editor.colors.danger);
        return;
    end

    if localizedText.awaitingCreate then
        localizedText.awaitingCreate = false;
        localizedText.activeTextID = result.artifactID;
        localizedText.editingID = result.artifactID;
        localizedText.baselineText = { internalName = localizedText.internalNameInput:GetText(), englishValue = localizedText.englishInput:GetText(), translatorContext = localizedText.translatorContext };
    else
        ApplySuccessfulLocalizedOperation(localizedText.operations[localizedText.operationIndex]);
    end
    SubmitNextLocalizedTextOperation();
end

OpenLocalizedTextModal = function(textID : integer?, targetKind : string, targetID : integer)
    if localizedText.pendingRequestID ~= 0i then return; end
    local text = if textID == nil then nil else editor.state.controller:FindText(textID);
    localizedText.editingID = if text == nil then nil else text.id;
    localizedText.activeTextID = localizedText.editingID;
    localizedText.targetKind = targetKind;
    localizedText.targetID = targetID;
    localizedText.awaitingCreate = false;
    localizedText.operations = { };
    localizedText.operationIndex = 0;
    localizedText.baselineText = if text == nil then nil else { internalName = text.internalName, englishValue = text.englishValue, translatorContext = text.translatorContext };
    localizedText.title:SetText(if text == nil then "New localized text" else text.internalName);
    localizedText.internalNameInput:SetText(if text == nil then "" else text.internalName);
    localizedText.englishInput:SetText(if text == nil then "" else text.englishValue);
    localizedText.translatorContext = if text == nil then "" else text.translatorContext;
    BuildLocalizedTranslationRows(localizedText.editingID);
    RefreshLocalizedTextActions();
    SetLocalizedTextOpen(true);
end

localizedText.internalNameInput:SetOnTextChanged(function() RefreshLocalizedTextActions(); end);
localizedText.englishInput:SetOnTextChanged(function() RefreshLocalizedTextActions(); end);
localizedText.saveButton:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i then BeginLocalizedTextSave(); end end);
localizedText.cancelButton:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i and localizedText.pendingRequestID == 0i then SetLocalizedTextOpen(false); end end);
localizedText.closeButton:SetOnMouseUp(function(_, _, mouseButton) if mouseButton == 0i and localizedText.pendingRequestID == 0i then SetLocalizedTextOpen(false); end end);

local function HideEditor()
    editor.state.visible = false;
    SetLocalizedTextOpen(false);
    deleteModal:Close();
    revertFlowModal:Close();
    creatureReferencesModal:Close();
    editor.refs.picker:Close();
    CloseInspectorPickers();
    if editor.refs.incomingMenuSelector ~= nil then editor.refs.incomingMenuSelector:SetVisible(false); end
    view.root:SetVisible(false);
end

RequestCloseEditor = function()
    if not editor.state.visible or closeWarningModal:IsOpen() then return; end
    if editor.state.flowSave ~= nil or editor.state.menuDuplicate ~= nil or editor.state.controller:GetPendingRequestID() ~= 0i or localizedText.pendingRequestID ~= 0i then
        editor.state.controller:SetStatus("Wait for the active editor operation to finish before closing.", STATUS.Warning);
        RefreshInspector();
        return;
    end
    local dirtyCount = GetGlobalDirtyCount();
    if dirtyCount == 0 then HideEditor(); return; end
    closeWarningModal:Open({
        message = `Close with {dirtyCount} unsaved change{if dirtyCount == 1 then "" else "s"} across the editor? Drafts remain available if you reopen it during this client session, but they are not saved to the server.`,
        onConfirm = HideEditor,
    });
end

local methods = { };
function methods:Show()
    editor.state.visible = true;
    view.root:SetVisible(true);
    editor.state.snapshot = editor.state.controller:GetSnapshot();
    if editor.state.snapshot.dataState == "Ready" then ApplySnapshot(); else editor.state.controller:RequestSnapshot(); end
end
function methods:Hide()
    RequestCloseEditor();
end
function methods:IsVisible() return view.root:IsVisible(); end

EditorRegistry.Register("GossipFlowEditor", methods);
view.root:SetVisible(false);

RegisterEvent(GameEvent.Updated, function()
    if not editor.state.visible then return; end
    UpdateLocalizedTextSave();
    if editor.state.controller:Update() then
        if editor.state.responseDrag ~= nil then editor.state.pendingSnapshot = true; else ApplySnapshot(); end
    end
    ContinueFlowSave();
    ContinueMenuDuplicate();
    if editor.state.pendingGraphRebuild and editor.state.responseDrag == nil then
        editor.state.pendingGraphRebuild = false;
        RebuildNodes();
    end
    DrawEdges();
end);

if editor.state.snapshot.dataState == "Ready" then ApplySnapshot(); else RefreshNavigation(false, false); RefreshInspector(); end
