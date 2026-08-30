local Declarative = require("@src/API/UI/Declarative");
local Button = require("@src/API/UI/Button");
local CurrencyPicker = require("@src/API/Editor/UI/CurrencyPicker");
local Dropdown = require("@src/API/UI/Dropdown");
local InputBox = require("@src/API/UI/InputBox");
local InputField = require("@src/API/Editor/UI/InputField");
local ItemPicker = require("@src/API/Editor/UI/ItemPicker");
local Modal = require("@src/API/UI/Modal");
local SearchPicker = require("@src/API/UI/SearchPicker");
local ScrollBox = require("@src/API/UI/ScrollBox");
local TextUtil = require("@src/API/UI/TextUtil");
local VirtualList = require("@src/API/UI/VirtualList");
local Draggable = require("@src/API/UI/Draggable");
local GameTooltip = require("@src/API/Game/UI/GameTooltip");
local ItemUtil = require("@src/API/Game/ItemUtil");
local EditorRegistry = require("@src/API/Editor/Registry");
local VendorListController = require("@src/API/Editor/VendorListController");
local VendorListModel = require("@src/API/Editor/VendorListModel");

local STATUS = VendorListController.StatusLevel;
local ARTIFACT_VENDOR_LIST = 15i;
local ARTIFACT_VENDOR_LIST_ITEM = 16i;
local MAX_INTERNAL_NAME_LENGTH = 128;
local MAX_COST_COMPONENTS = 8;
local MAX_VENDOR_ITEMS = 256;
local SLOT_WIDTH = 180;
local SLOT_HEIGHT = 70;
local SLOT_ICON_SIZE = 34;
local SLOT_GAP = 5;
local PREVIEW_TOP = 76;
local HORIZONTAL_SCROLL_HEIGHT = 12;
local UNKNOWN_ITEM_ICON = "texture/ui/icons/question.png";

if VendorEditorView ~= nil then
    VendorEditorView:Destroy();
end

local editor : any =
{
    geometry = { windowPosition = vec.New(220, 88), windowSize = vec.New(1480, 820), toolbarHeight = 52, sidebarWidth = 230, inspectorWidth = 300, footerHeight = 42 },
    colors =
    {
        muted = vec.New(0.56, 0.60, 0.67), accent = vec.New(0.42, 0.72, 0.96), warning = vec.New(0.94, 0.72, 0.30), success = vec.New(0.42, 0.78, 0.50), error = vec.New(0.94, 0.38, 0.38),
        danger = vec.New(0.62, 0.20, 0.23), selected = vec.New(0.14, 0.28, 0.39), slot = vec.New(0.075, 0.09, 0.105), emptySlot = vec.New(0.045, 0.052, 0.06),
    },
    state =
    {
        controller = VendorListController.New(), model = nil, snapshot = nil, visible = false, rows = 6i, columns = 2i, page = 1, search = "", selectedListID = nil,
        selectedItemID = nil, cardDrag = nil, pendingSnapshot = false, pickerMode = "add", pickerCostItemComponentID = nil, applyOperations = nil, applyIndex = 0, applyIDMap = { }, applyingListID = nil, applyingItemID = nil,
        inspectorView = nil, slotWidgets = { }, horizontalDragStart = 0,
    },
    refs = { },
};

local bodyHeight = editor.geometry.windowSize.y - editor.geometry.toolbarHeight;
local centerWidth = editor.geometry.windowSize.x - editor.geometry.sidebarWidth - editor.geometry.inspectorWidth;
local previewHeight = bodyHeight - PREVIEW_TOP - editor.geometry.footerHeight - HORIZONTAL_SCROLL_HEIGHT;
local inspectorContentWidth = editor.geometry.inspectorWidth - 28;
local inspectorInputWidth = (inspectorContentWidth - 10) / 2;
local inspectorActionWidth = (inspectorContentWidth - 6) / 2;
editor.state.canvas = UI.GetCanvas("VendorEditor", vec.New(0, 0), vec.New(1920, 1080), false);
editor.state.snapshot = editor.state.controller:GetSnapshot();

local deleteModal = VendorEditorDeleteModal;
if deleteModal == nil then
    deleteModal = Modal.New(editor.state.canvas, vec.New(0, 0), vec.New(450, 175), 1900i, { name = "VendorEditorDelete", title = "Delete vendor data", message = "", confirmText = "Delete", cancelText = "Cancel" });
    VendorEditorDeleteModal = deleteModal;
end
local emptyCostModal = VendorEditorEmptyCostModal;
if emptyCostModal == nil then
    emptyCostModal = Modal.New(editor.state.canvas, vec.New(0, 0), vec.New(500, 195), 1900i, { name = "VendorEditorEmptyCost", title = "Delete empty costs", message = "", confirmText = "Delete and Apply", cancelText = "Cancel" });
    VendorEditorEmptyCostModal = emptyCostModal;
end
local closeModal = VendorEditorCloseModal;
if closeModal == nil then
    closeModal = Modal.New(editor.state.canvas, vec.New(0, 0), vec.New(470, 180), 1900i, { name = "VendorEditorClose", title = "Unsaved vendor changes", message = "", confirmText = "Close", cancelText = "Keep Editing" });
    VendorEditorCloseModal = closeModal;
end

local itemPicker = VendorEditorSharedItemPicker;
if itemPicker == nil then
    itemPicker = ItemPicker.New(editor.state.canvas, 1900i, { name = "VendorEditorItemCatalog", title = "Select Item", width = 560, height = 590 });
    VendorEditorSharedItemPicker = itemPicker;
end

local currencyPicker = VendorEditorSharedCurrencyPicker;
if currencyPicker == nil then
    currencyPicker = CurrencyPicker.New(editor.state.canvas, 1900i, { name = "VendorEditorCurrencyCatalog", width = 560, height = 590 });
    VendorEditorSharedCurrencyPicker = currencyPicker;
end

local RefreshAll : (boolean?) -> () = function() end;
local RefreshInspector : () -> () = function() end;
local RefreshPreview : () -> () = function() end;
local UpdateToolbar : () -> () = function() end;
local ApplySnapshot : () -> () = function() end;
local UniqueListName : (string) -> string = function(value)
    return value;
end;
local HandleItemPicked : (integer) -> () = function() end;
local OpenItemPicker : (string, integer?) -> () = function() end;
local SelectList : (integer?) -> () = function() end;
local view : any;
local editorTooltip : any = nil;

local function FindItemTemplate(itemTemplateID : integer?)
    if itemTemplateID == nil then
        return nil;
    end

    for _, item in editor.state.snapshot.itemTemplates do
        if item.id == itemTemplateID then
            return item;
        end
    end

    return nil;
end

local function ItemName(itemTemplateID : integer?) : string
    local item = FindItemTemplate(itemTemplateID);
    return if item ~= nil then item.name else `Unknown item #{itemTemplateID}`;
end

local function ItemIconTexture(itemTemplateID : integer?) : string
    local item = FindItemTemplate(itemTemplateID);
    if item == nil or item.iconID == 0i then
        return UNKNOWN_ITEM_ICON;
    end

    local icon = Item.GetIconInfo(item.iconID);
    return if icon ~= nil then icon.Texture else UNKNOWN_ITEM_ICON;
end

local function CurrencyName(currencyID : integer?) : string
    for _, currency in editor.state.snapshot.currencies do
        if currency.id == currencyID then
            return currency.name;
        end
    end

    return `Unknown currency #{currencyID}`;
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

local function GetItemPickerItems(costID : integer?, currentItemTemplateID : integer?) : { any }
    local items : { any } = { };
    local used : { [integer] : boolean } = { };
    if costID ~= nil and editor.state.model ~= nil then
        local _, components = editor.state.model:GetCostComponents(costID);
        for _, component in components do
            if component.itemTemplateID ~= currentItemTemplateID then
                used[component.itemTemplateID] = true;
            end
        end
    end

    for _, item in editor.state.snapshot.itemTemplates do
        if not used[item.id] then
            table.insert(items, { value = item.id, primary = item.name, secondary = "Item template", search = `{item.id} {item.name}`, icon = ItemIconTexture(item.id), rarity = item.rarity, trailing = tostring(item.id) });
        end
    end

    table.sort(items, function(left, right)
        return integer.lt(left.value, right.value);
    end);
    return items;
end

local function GetCurrencyPickerItems(costID : integer?, currentCurrencyID : integer?) : { any }
    local items : { any } = { };
    local used : { [integer] : boolean } = { };
    if costID ~= nil and editor.state.model ~= nil then
        local components = editor.state.model:GetCostComponents(costID);
        for _, component in components do
            if component.currencyID ~= currentCurrencyID then
                used[component.currencyID] = true;
            end
        end
    end

    for _, currency in editor.state.snapshot.currencies do
        if not used[currency.id] then
            table.insert(items, { value = currency.id, primary = currency.name, secondary = "Currency", search = `{currency.id} {currency.name}`, trailing = tostring(currency.id) });
        end
    end

    table.sort(items, function(left, right)
        local leftName = string.lower(left.primary);
        local rightName = string.lower(right.primary);
        return if leftName == rightName then integer.lt(left.value, right.value) else leftName < rightName;
    end);
    return items;
end

local function GetConditionPickerItems() : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { { value = 0i, primary = "Always", secondary = "No condition set" } };
    for _, condition in editor.state.snapshot.conditionSets do
        if condition.id ~= 0i then
            table.insert(items, { value = condition.id, primary = condition.internalName, secondary = `Condition set #{condition.id}`, search = `{condition.id} {condition.internalName}` });
        end
    end

    table.sort(items, function(left, right)
        if left.value == 0i then
            return right.value ~= 0i;
        end

        if right.value == 0i then
            return false;
        end

        return string.lower(left.primary) < string.lower(right.primary);
    end);
    return items;
end

local function GetTextPickerItems() : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { { value = 0i, primary = "None", secondary = "No disabled reason" } };
    for _, text in editor.state.snapshot.texts do
        if text.id ~= 0i then
            table.insert(items, { value = text.id, primary = text.internalName, secondary = text.englishValue, search = `{text.id} {text.internalName} {text.englishValue}` });
        end
    end
    return items;
end

local function GetCostPickerItems() : { SearchPicker.Item }
    local items : { SearchPicker.Item } = { { value = 0i, primary = "Free", secondary = "No cost definition" } };
    if editor.state.model ~= nil then
        for _, cost in editor.state.model:GetDraft().costsByID do
            table.insert(items, { value = cost.id, primary = cost.internalName, secondary = if integer.lt(cost.id, 0i) then "Unsaved cost" else `Cost #{cost.id}`, search = `{cost.id} {cost.internalName}` });
        end
    end

    table.sort(items, function(left, right)
        if left.value == 0i then
            return right.value ~= 0i;
        end

        if right.value == 0i then
            return false;
        end

        return string.lower(left.primary) < string.lower(right.primary);
    end);
    return items;
end

local function FormatCost(costID : integer) : string
    if costID == 0i then
        return "Free";
    end

    local cost = editor.state.model:GetCost(costID);
    if cost == nil then
        return `Unknown cost #{costID}`;
    end

    local currencies, items = editor.state.model:GetCostComponents(costID);
    local parts = { };
    for _, component in currencies do
        table.insert(parts, tostring(component.amount) .. " " .. CurrencyName(component.currencyID));
    end

    for _, component in items do
        table.insert(parts, `{component.amount}x {ItemName(component.itemTemplateID)}`);
    end

    return if #parts == 0 then `{cost.internalName} (incomplete)` else table.concat(parts, " + ");
end

local function CreateCostPreview(parent : any, costID : integer, position : vec, maximumWidth : number, tooltipsEnabled : boolean) : { any }
    local widgets = { };
    local currencies, items = editor.state.model:GetCostComponents(costID);
    local components = { };
    for _, component in currencies do
        table.insert(components, { kind = 0i, value = component });
    end

    for _, component in items do
        table.insert(components, { kind = 1i, value = component });
    end

    local x = position.x;
    local function AddText(text : string)
        local remainingWidth = math.max(0, position.x + maximumWidth - x);
        local displayedText = TextUtil.TruncateToWidth(text, "DefaultButtonText", remainingWidth, 8);
        local widget = parent:NewText(displayedText, vec.New(x, position.y), 2i, "DefaultButtonText");
        widget:SetAnchor(vec.New(0, 1));
        widget:SetRelativePoint(vec.New(0, 1));
        widget:SetFontSize(8);
        widget:SetColor(editor.colors.warning);
        widget:SetInteractable(false);
        x += UI.CalculateTextSize(displayedText, "DefaultButtonText", 8).x;
        table.insert(widgets, widget);
    end

    if costID == 0i then
        AddText("Free");
        return widgets;
    end
    if #components == 0 then
        AddText("Incomplete price");
        return widgets;
    end

    for index, entry in components do
        if index > 1 then
            AddText(" + ");
        end

        if entry.kind == 0i then
            AddText(`{entry.value.amount} {CurrencyName(entry.value.currencyID)}`);
        else
            AddText(`{entry.value.amount}x `);
            if x + 14 > position.x + maximumWidth then
                break;
            end

            local component = entry.value;
            local icon = parent:NewPanel(vec.New(x, position.y + 1), vec.New(14, 14), 3i, "DebugDarkGrey");
            icon:SetAnchor(vec.New(0, 1));
            icon:SetRelativePoint(vec.New(0, 1));
            icon:SetBackground(ItemIconTexture(component.itemTemplateID));
            icon:SetColor(vec.New(1, 1, 1));
            icon:SetBorderSize(0);
            if tooltipsEnabled then
                AttachItemTooltip(icon, component.itemTemplateID);
            else
                icon:SetInteractable(false);
            end

            x += 14;
            table.insert(widgets, icon);
        end

        if x >= position.x + maximumWidth then
            break;
        end
    end

    return widgets;
end

local function DestroySlots()
    if editorTooltip ~= nil then
        editorTooltip.ClearLines();
        editorTooltip.Hide();
    end

    if editor.state.cardDrag ~= nil and editor.state.cardDrag.dragSurface ~= nil then
        UI.DestroyWidget(editor.state.cardDrag.dragSurface.root);
    end

    editor.state.cardDrag = nil;
    for _, widget in editor.state.slotWidgets do
        UI.DestroyWidget(widget.root);
    end

    editor.state.slotWidgets = { };
end

local function UpdateHorizontalScrollbar()
    if editor.refs.slotHost == nil or editor.refs.horizontalScrollTrack == nil or editor.refs.horizontalScrollHandle == nil then
        return;
    end

    local viewportWidth = editor.refs.slotHost:GetViewportSize().x;
    local contentWidth = editor.refs.slotHost.content:GetWidth();
    local scrollRange = math.max(0, contentWidth - viewportWidth);
    local trackWidth = editor.refs.horizontalScrollTrack:GetWidth();
    editor.refs.horizontalScrollTrack:SetVisible(scrollRange > 0);
    if scrollRange <= 0 then
        return;
    end

    local handleWidth = math.max(30, trackWidth * viewportWidth / contentWidth);
    local handleRange = math.max(1, trackWidth - handleWidth);
    local offsetX = math.clamp(editor.refs.slotHost:GetOffset().x, -scrollRange, 0);
    editor.refs.horizontalScrollHandle:SetWidth(handleWidth);
    editor.refs.horizontalScrollHandle:SetPosX((-offsetX / scrollRange) * handleRange);
end

local function ReadUnsigned(input : any, maximum : number) : integer?
    local value = tonumber(input:GetText());
    if value == nil or value < 0 or value > maximum or math.floor(value) ~= value then
        return nil;
    end

    return integer.create(value);
end

local function SubmitOperation(operation : any)
    local value = operation.value;
    local function ResolveID(id : integer) : integer
        return if integer.lt(id, 0i) then (editor.state.applyIDMap[id] or 0i) else id;
    end

    local requestID = 0i;
    if operation.kind == "CreateList" then
        local items = { };
        for _, item in operation.items do
            table.insert(items, { priority = item.priority, itemTemplateID = item.itemTemplateID, purchaseQuantity = item.purchaseQuantity, costID = ResolveID(item.costID), visibilityConditionSetID = item.visibilityConditionSetID, enabledConditionSetID = item.enabledConditionSetID, disabledReasonTextID = item.disabledReasonTextID, flags = item.flags });
        end
        requestID = InteractionEditor.CreateVendorList(value.internalName, value.flags, items);
    elseif operation.kind == "UpdateList" then
        requestID = InteractionEditor.UpdateVendorList(value.id, value.internalName, value.flags);
    elseif operation.kind == "CreateCost" then
        requestID = InteractionEditor.CreateCost(value.internalName, operation.currencyComponents, operation.itemComponents);
    elseif operation.kind == "CreateCurrency" then
        requestID = InteractionEditor.CreateCostCurrencyComponent(ResolveID(value.costID), value.priority, value.currencyID, value.amount);
    elseif operation.kind == "CreateCostItem" then
        requestID = InteractionEditor.CreateCostItemComponent(ResolveID(value.costID), value.priority, value.itemTemplateID, value.amount);
    elseif operation.kind == "CreateItem" then
        requestID = InteractionEditor.CreateVendorListItem(value.vendorListID, value.priority, value.itemTemplateID, value.purchaseQuantity, ResolveID(value.costID), value.visibilityConditionSetID, value.enabledConditionSetID, value.disabledReasonTextID, value.flags);
    elseif operation.kind == "UpdateItem" then
        requestID = InteractionEditor.UpdateVendorListItem(value.id, value.vendorListID, value.priority, value.itemTemplateID, value.purchaseQuantity, ResolveID(value.costID), value.visibilityConditionSetID, value.enabledConditionSetID, value.disabledReasonTextID, value.flags);
    elseif operation.kind == "UpdateCost" then
        requestID = InteractionEditor.UpdateCost(value.id, value.internalName);
    elseif operation.kind == "UpdateCurrency" then
        requestID = InteractionEditor.UpdateCostCurrencyComponent(value.id, value.costID, value.priority, value.currencyID, value.amount);
    elseif operation.kind == "DeleteCurrency" then
        requestID = InteractionEditor.DeleteCostCurrencyComponent(value.id);
    elseif operation.kind == "UpdateCostItem" then
        requestID = InteractionEditor.UpdateCostItemComponent(value.id, value.costID, value.priority, value.itemTemplateID, value.amount);
    elseif operation.kind == "DeleteCostItem" then
        requestID = InteractionEditor.DeleteCostItemComponent(value.id);
    elseif operation.kind == "DeleteCost" then
        requestID = InteractionEditor.DeleteCost(value.id);
    end

    editor.state.controller:Submit(requestID);
end

local function GetEmptyReferencedCosts(listID : integer) : { any }
    local costs = { };
    local checked : { [integer] : boolean } = { };
    for _, itemID in editor.state.model:GetOrderedItemIDs(listID) do
        local item = editor.state.model:GetItem(itemID);
        if item ~= nil and item.costID ~= 0i and not checked[item.costID] then
            checked[item.costID] = true;
            local currencies, items = editor.state.model:GetCostComponents(item.costID);
            if #currencies + #items == 0 then
                local cost = editor.state.model:GetCost(item.costID);
                if cost ~= nil then
                    table.insert(costs, cost);
                end
            end
        end
    end

    table.sort(costs, function(left, right)
        return integer.lt(left.id, right.id);
    end);
    return costs;
end

local function IsCostReferencedOutsideList(costID : integer, listID : integer) : boolean
    for _, item in editor.state.snapshot.vendorListItems do
        if item.costID == costID and item.vendorListID ~= listID then
            return true;
        end
    end

    for _, item in editor.state.model:GetDraft().itemsByID do
        if item.costID == costID and item.vendorListID ~= listID then
            return true;
        end
    end

    return false;
end

local function GetApplyValidationError(listID : integer) : string?
    if editor.state.model == nil then
        return "Vendor data is unavailable.";
    end

    local list = editor.state.model:GetList(listID);
    if list == nil then
        return "Select a vendor list to apply.";
    end

    if list.internalName == "" or #list.internalName > MAX_INTERNAL_NAME_LENGTH then
        return `The vendor-list internal name must contain between 1 and {MAX_INTERNAL_NAME_LENGTH} characters.`;
    end

    if list.flags ~= 0i then
        return "Vendor-list flags are not currently supported.";
    end

    local itemIDs = editor.state.model:GetOrderedItemIDs(listID);
    if #itemIDs == 0 or #itemIDs > MAX_VENDOR_ITEMS then
        return `A vendor list must contain between 1 and {MAX_VENDOR_ITEMS} items.`;
    end

    local checkedCosts : { [integer] : boolean } = { };
    for _, itemID in itemIDs do
        local item = editor.state.model:GetItem(itemID);
        if item == nil or item.itemTemplateID == 0i then
            return "Every vendor entry must reference an item.";
        end

        if item.purchaseQuantity == 0i then
            return `Vendor entry #{itemID} must have a purchase quantity greater than zero.`;
        end

        if item.flags ~= 0i then
            return `Vendor entry #{itemID} uses unsupported flags.`;
        end

        if item.disabledReasonTextID ~= 0i and item.enabledConditionSetID == 0i then
            return `Vendor entry #{itemID} has a disabled reason but no enabled condition.`;
        end

        if item.costID ~= 0i and not checkedCosts[item.costID] then
            checkedCosts[item.costID] = true;
            local cost = editor.state.model:GetCost(item.costID);
            if cost == nil then
                return `Vendor entry #{itemID} references an unknown cost.`;
            end

            if cost.internalName == "" or #cost.internalName > MAX_INTERNAL_NAME_LENGTH then
                return `Cost #{cost.id} must have an internal name containing between 1 and {MAX_INTERNAL_NAME_LENGTH} characters.`;
            end

            local currencies, items = editor.state.model:GetCostComponents(item.costID);
            if #currencies + #items == 0 and IsCostReferencedOutsideList(cost.id, listID) then
                return `Cost '{cost.internalName}' cannot be deleted because it is referenced by another vendor list.`;
            end

            if #currencies + #items > MAX_COST_COMPONENTS then
                return `Cost #{cost.id} cannot contain more than {MAX_COST_COMPONENTS} components.`;
            end

            local seenCurrencies : { [integer] : boolean } = { };
            for _, component in currencies do
                if component.currencyID == 0i or component.amount == 0i then
                    return `Every currency component in cost #{cost.id} must have a currency and an amount greater than zero.`;
                end

                if seenCurrencies[component.currencyID] then
                    return `Cost #{cost.id} contains {CurrencyName(component.currencyID)} more than once.`;
                end

                seenCurrencies[component.currencyID] = true;
            end

            local seenItems : { [integer] : boolean } = { };
            for _, component in items do
                if component.itemTemplateID == 0i or component.amount == 0i then
                    return `Every item component in cost #{cost.id} must have an item and an amount greater than zero.`;
                end

                if seenItems[component.itemTemplateID] then
                    return `Cost #{cost.id} contains {ItemName(component.itemTemplateID)} more than once.`;
                end

                seenItems[component.itemTemplateID] = true;
            end
        end
    end
    return nil;
end

local function GetPersistedVendorItemCount(listID : integer) : number
    local count = 0;
    for _, item in editor.state.snapshot.vendorListItems do
        if item.vendorListID == listID then
            count += 1;
        end
    end

    return count;
end

local function ApplyList(emptyCosts : { any })
    local listID = editor.state.selectedListID;
    if editor.state.model == nil or listID == nil or not editor.state.controller:IsReady() then
        return;
    end

    local emptyCostIDs : { [integer] : boolean } = { };
    for _, cost in emptyCosts do
        emptyCostIDs[cost.id] = true;
    end
    for _, itemID in editor.state.model:GetOrderedItemIDs(listID) do
        local item = editor.state.model:GetItem(itemID);
        if item ~= nil and emptyCostIDs[item.costID] then
            editor.state.model:SetItemFields(itemID, { costID = 0i });
        end
    end
    for _, cost in emptyCosts do
        if integer.lt(cost.id, 0i) then
            editor.state.model:RemoveTemporaryCost(cost.id);
        end
    end

    local operations = editor.state.model:GetDirtyOperations(listID);
    for _, cost in emptyCosts do
        if not integer.lt(cost.id, 0i) then
            table.insert(operations, { kind = "DeleteCost", value = cost });
        end
    end
    if #operations == 0 then
        RefreshInspector();
        RefreshPreview();
        UpdateToolbar();
        return;
    end

    editor.state.applyOperations = operations;
    editor.state.applyIndex = 1;
    editor.state.applyIDMap = { };
    editor.state.applyingListID = listID;
    editor.state.applyingItemID = editor.state.selectedItemID;
    RefreshInspector();
    RefreshPreview();
    UpdateToolbar();
    SubmitOperation(operations[1]);
end

local function BeginApply()
    if editor.state.model == nil or editor.state.selectedListID == nil or not editor.state.controller:IsReady() then
        return;
    end

    local listID = editor.state.selectedListID;
    local validationError = GetApplyValidationError(listID);
    if validationError ~= nil then
        editor.state.controller:SetStatus(validationError, STATUS.Warning);
        UpdateToolbar();
        return;
    end

    local emptyCosts = GetEmptyReferencedCosts(listID);
    if #emptyCosts == 0 then
        ApplyList(emptyCosts);
        return;
    end

    local names = { };
    for _, cost in emptyCosts do
        table.insert(names, `'{cost.internalName}'`);
    end

    local explanation = if #emptyCosts == 1 then "This cost has no components and will be deleted:" else "These costs have no components and will be deleted:";
    emptyCostModal:Open({
        message = `{explanation}\n{table.concat(names, ", ")}\n\nItems using them in this vendor list will become Free. Continue?`,
        onConfirm = function()
            ApplyList(emptyCosts);
        end
    });
end

local function ContinueApply(result : any)
    if editor.state.applyOperations == nil then
        return;
    end
    if not result.succeeded then
        editor.state.applyOperations = nil;
        editor.state.applyIndex = 0;
        editor.state.applyIDMap = { };
        editor.state.applyingListID = nil;
        editor.state.applyingItemID = nil;
        return;
    end

    local completedOperation = editor.state.applyOperations[editor.state.applyIndex];
    if completedOperation.temporaryID ~= nil then
        editor.state.applyIDMap[completedOperation.temporaryID] = result.artifactID;
        local draft = editor.state.model:GetDraft();
        if completedOperation.kind == "CreateCost" then
            editor.state.model:ResolveTemporaryCost(completedOperation.temporaryID, result.artifactID);
        elseif completedOperation.kind == "CreateCurrency" and draft.currencyComponentsByID[completedOperation.temporaryID] ~= nil then
            editor.state.model:RemoveCurrencyComponent(completedOperation.temporaryID);
        elseif completedOperation.kind == "CreateCostItem" and draft.itemComponentsByID[completedOperation.temporaryID] ~= nil then
            editor.state.model:RemoveItemComponent(completedOperation.temporaryID);
        end
        RefreshInspector();
        RefreshPreview();
        UpdateToolbar();
    end

    editor.state.applyIndex += 1;
    local operation = editor.state.applyOperations[editor.state.applyIndex];
    if operation ~= nil then
        SubmitOperation(operation);
        return;
    end

    local draftListID = editor.state.applyingListID;
    local selectedListID = draftListID;
    if selectedListID ~= nil and integer.lt(selectedListID, 0i) then
        selectedListID = editor.state.applyIDMap[selectedListID];
    end
    local selectedItemID = editor.state.applyingItemID;
    if selectedItemID ~= nil and integer.lt(selectedItemID, 0i) then
        selectedItemID = editor.state.applyIDMap[selectedItemID];
    end

    editor.state.applyOperations = nil;
    editor.state.applyIndex = 0;
    editor.state.applyIDMap = { };
    editor.state.applyingListID = nil;
    editor.state.applyingItemID = nil;
    if draftListID ~= nil and selectedListID ~= nil then
        editor.state.model:AcceptAppliedList(draftListID, selectedListID);
    end
    editor.state.selectedListID = selectedListID;
    editor.state.controller:SelectList(selectedListID);
    editor.state.selectedItemID = selectedItemID;
    editor.state.controller:SelectItem(selectedItemID);
    editor.state.controller:RequestSnapshot();
end

local function AddVendorItem(itemTemplateID : integer)
    local listID = editor.state.selectedListID;
    if listID == nil or editor.state.model == nil then
        return;
    end

    local ids = editor.state.model:GetOrderedItemIDs(listID);
    if #ids >= MAX_VENDOR_ITEMS then
        editor.state.controller:SetStatus("A vendor list cannot contain more than 256 items.", STATUS.Warning);
        return;
    end

    local itemID = editor.state.model:AddItem(listID, itemTemplateID);
    editor.state.selectedItemID = itemID;
    editor.state.controller:SelectItem(itemID);
    RefreshAll(true);
end

HandleItemPicked = function(resourceID : integer)
    local selectedItemID = editor.state.selectedItemID;
    if editor.state.pickerMode == "add" then
        AddVendorItem(resourceID);
    elseif editor.state.pickerMode == "replace" and selectedItemID ~= nil then
        editor.state.model:SetItemFields(selectedItemID, { itemTemplateID = resourceID });
        RefreshAll(true);
    elseif editor.state.pickerMode == "costItem" and selectedItemID ~= nil then
        local item = editor.state.model:GetItem(selectedItemID);
        if item ~= nil and item.costID ~= 0i then
            editor.state.model:AddItemComponent(item.costID, resourceID);
            RefreshInspector();
            RefreshPreview();
            UpdateToolbar();
        end
    elseif editor.state.pickerMode == "replaceCostItem" and editor.state.pickerCostItemComponentID ~= nil then
        editor.state.model:SetItemComponentFields(editor.state.pickerCostItemComponentID, { itemTemplateID = resourceID });
        RefreshInspector();
        RefreshPreview();
        UpdateToolbar();
    end
end

local function StartDuplicate()
    local listID = editor.state.selectedListID;
    if listID == nil or editor.state.model == nil or not editor.state.controller:IsReady() then
        return;
    end

    local list = editor.state.model:GetList(listID);
    local duplicateID = editor.state.model:DuplicateList(listID, UniqueListName(list.internalName .. " Copy"));
    SelectList(duplicateID);
end

local function GetSelectedList()
    return if editor.state.model ~= nil and editor.state.selectedListID ~= nil then editor.state.model:GetList(editor.state.selectedListID) else nil;
end

UniqueListName = function(baseName : string) : string
    local used : { [string] : boolean } = { };
    if editor.state.model ~= nil then
        for _, listID in editor.state.model:GetListIDs() do
            used[string.lower(editor.state.model:GetList(listID).internalName)] = true;
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

local function UniqueCostName(baseName : string) : string
    local used : { [string] : boolean } = { };
    if editor.state.model ~= nil then
        for _, cost in editor.state.model:GetDraft().costsByID do
            used[string.lower(cost.internalName)] = true;
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

SelectList = function(listID : integer?)
    editor.state.selectedListID = listID;
    editor.state.selectedItemID = nil;
    editor.state.page = 1;
    editor.state.controller:SelectList(listID);
    if editor.refs.slotHost ~= nil then
        editor.refs.slotHost:SetOffset(vec.New(0, 0));
    end

    RefreshAll(false);
end

local function SelectItem(itemID : integer?)
    editor.state.selectedItemID = itemID;
    editor.state.controller:SelectItem(itemID);
    RefreshPreview();
    RefreshInspector();
end

local function RefreshSidebar(preserveScroll : boolean?)
    local rows = { };
    if editor.state.model ~= nil then
        local query = string.lower(editor.state.search);
        for _, listID in editor.state.model:GetListIDs() do
            local list = editor.state.model:GetList(listID);
            if query == "" or string.find(string.lower(list.internalName), query, 1, true) ~= nil or string.find(tostring(listID), query, 1, true) ~= nil then
                table.insert(rows, { id = listID, list = list, itemCount = #editor.state.model:GetOrderedItemIDs(listID) });
            end
        end
    end
    editor.refs.listCount:SetText(`{#rows} list{if #rows == 1 then "" else "s"}`);
    editor.refs.sidebarList:SetItems(rows, preserveScroll == true);
end

UpdateToolbar = function()
    local list = GetSelectedList();
    local dirty = list ~= nil and (editor.state.model:IsListDirty(list.id) or #GetEmptyReferencedCosts(list.id) > 0);
    local anyDirty = editor.state.model ~= nil and editor.state.model:IsDirty();
    local statusMessage, statusLevel = editor.state.controller:GetStatus();
    local validationError = if dirty then GetApplyValidationError(list.id) else nil;
    if validationError ~= nil then
        statusMessage = validationError;
        statusLevel = STATUS.Warning;
    end

    local statusColor = if statusLevel == STATUS.Success then editor.colors.success elseif statusLevel == STATUS.Error then editor.colors.error elseif statusLevel == STATUS.Warning then editor.colors.warning else editor.colors.muted;
    editor.refs.status:SetText(statusMessage);
    editor.refs.status:SetColor(statusColor);
    editor.refs.dirty:SetText(if list == nil then "NO LIST" elseif dirty then "UNSAVED" else "SAVED");
    editor.refs.dirty:SetColor(if dirty then editor.colors.warning else editor.colors.muted);
    editor.refs.apply:SetInteractable(dirty and validationError == nil and editor.state.controller:IsReady() and editor.state.applyOperations == nil);
    editor.refs.revert:SetInteractable(anyDirty and editor.state.applyOperations == nil);
end

local function MakePickerNode(id : string, label : string, items : { SearchPicker.Item }, value : any, onChanged : (any) -> ())
    return Declarative.Vertical({ width = inspectorContentWidth, height = 54, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = inspectorContentWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
        Declarative.Component({ id = id, width = inspectorContentWidth, height = 32, factory = function(parent, position, size, layer)
            return SearchPicker.New(parent, editor.state.canvas, position, size, layer, { items = items, value = value, popupWidth = 360, popupHeight = 360, rowHeight = 40, onValueChanged = function(nextValue)
                onChanged(nextValue);
            end });
        end })
    );
end

local function MakeNumberInputNode(id : string, label : string, value : integer, maximum : number, onChanged : (integer) -> ())
    return Declarative.Vertical({ width = inspectorInputWidth, height = 54, spacing = 3, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
        Declarative.Text(label, { width = inspectorInputWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
        Declarative.Component({ id = id, width = inspectorInputWidth, height = 32, factory = function(parent, position, size, layer)
            local input = InputField.New(parent, position, size, layer, { defaultText = tostring(value), defaultTextFontSize = 10 });
            input:SetTextVerticallyCentered();
            input:SetOnTextChanged(function()
                local parsed = ReadUnsigned(input, maximum);
                if parsed ~= nil then
                    onChanged(parsed);
                end
            end);
            return input;
        end })
    );
end

local function BuildCostComponentNodes(item : any) : ({ any }, { any })
    local currencyNodes = { };
    local itemNodes = { };
    if item.costID == 0i then
        return currencyNodes, itemNodes;
    end

    local currencies, items = editor.state.model:GetCostComponents(item.costID);
    for _, component in currencies do
        table.insert(currencyNodes, Declarative.Horizontal({ width = inspectorContentWidth, height = 32, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Component({ width = inspectorContentWidth - 108, height = 30, factory = function(parent, position, size, layer)
                local button = Button.New(parent, position, size, layer, { text = TextUtil.TruncateToWidth(CurrencyName(component.currencyID), "DefaultButtonText", size.x - 16, 9), textTemplate = "DefaultButtonText", textAlign = "left", textPadX = 8 });
                button:SetFontSize(9);
                button:SetOnMouseUp(function(_, _, mouseButton)
                    if mouseButton ~= 0i then return; end
                    currencyPicker:Open({ title = "Replace Cost Currency", items = GetCurrencyPickerItems(component.costID, component.currencyID), onSelect = function(value)
                        editor.state.model:SetCurrencyComponentFields(component.id, { currencyID = value });
                        RefreshInspector();
                        RefreshPreview();
                        UpdateToolbar();
                    end });
                end);
                return button;
            end }),
            Declarative.Component({ width = 70, height = 30, factory = function(parent, position, size, layer)
                local input = InputField.New(parent, position, size, layer, { defaultText = tostring(component.amount), defaultTextFontSize = 9 });
                input:SetTextVerticallyCentered();
                input:SetOnTextChanged(function()
                    local value = ReadUnsigned(input, 9007199254740991);
                    if value ~= nil and integer.gt(value, 0i) then
                        editor.state.model:SetCurrencyComponentFields(component.id, { amount = value });
                        UpdateToolbar();
                        RefreshPreview();
                    end
                end);
                return input;
            end }),
            Declarative.Button("x", { width = 30, height = 30, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
                editor.state.model:RemoveCurrencyComponent(component.id);
                RefreshInspector();
                RefreshPreview();
                UpdateToolbar();
            end })
        ));
    end
    for _, component in items do
        table.insert(itemNodes, Declarative.Horizontal({ width = inspectorContentWidth, height = 32, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Component({ width = inspectorContentWidth - 108, height = 30, factory = function(parent, position, size, layer)
                local picker = Button.New(parent, position, size, layer, { text = ItemName(component.itemTemplateID), textTemplate = "DefaultButtonText", textAlign = "left", textPadX = 8 });
                picker:SetFontSize(9);
                picker:SetOnMouseUp(function(_, _, mouseButton)
                    if mouseButton ~= 0i then
                        return;
                    end

                    editorTooltip.ClearLines();
                    editorTooltip.Hide();
                    OpenItemPicker("replaceCostItem", component.id);
                end);
                local itemTemplate = FindItemTemplate(component.itemTemplateID);
                if itemTemplate ~= nil then
                    picker.text:SetColor(ItemUtil.GetRarityColor(itemTemplate.rarity));
                end

                AttachItemTooltip(picker, component.itemTemplateID);
                return picker;
            end }),
            Declarative.Component({ width = 70, height = 30, factory = function(parent, position, size, layer)
                local input = InputField.New(parent, position, size, layer, { defaultText = tostring(component.amount), defaultTextFontSize = 9 });
                input:SetTextVerticallyCentered();
                input:SetOnTextChanged(function()
                    local value = ReadUnsigned(input, 4294967295);
                    if value ~= nil and integer.gt(value, 0i) then
                        editor.state.model:SetItemComponentFields(component.id, { amount = value });
                        UpdateToolbar();
                        RefreshPreview();
                    end
                end);
                return input;
            end }),
            Declarative.Button("x", { width = 30, height = 30, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
                editor.state.model:RemoveItemComponent(component.id);
                RefreshInspector();
                RefreshPreview();
                UpdateToolbar();
            end })
        ));
    end
    return currencyNodes, itemNodes;
end

RefreshInspector = function()
    if editor.state.inspectorView ~= nil then
        editor.state.inspectorView:Destroy();
        editor.state.inspectorView = nil;
    end

    local list = GetSelectedList();
    local body = { };
    if list == nil then
        table.insert(body, Declarative.Text("Select or create a vendor list.", { width = inspectorContentWidth, height = 60, fontSize = 11, color = editor.colors.muted, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
    elseif editor.state.selectedItemID == nil then
        table.insert(body, Declarative.Text("VENDOR LIST", { width = inspectorContentWidth, height = 20, fontSize = 10, color = editor.colors.warning, template = "DefaultButtonText" }));
        table.insert(body, Declarative.Text(`List #{list.id} - {list.internalName}\n- Items : {#editor.state.model:GetOrderedItemIDs(list.id)}`, { width = inspectorContentWidth, height = 52, fontSize = 12, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
        table.insert(body, Declarative.Text("Internal name", { width = inspectorContentWidth, height = 17, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }));
        table.insert(body, Declarative.Component({ width = inspectorContentWidth, height = 32, factory = function(parent, position, size, layer)
            local input = InputField.New(parent, position, size, layer, { defaultText = list.internalName, defaultTextFontSize = 10 });
            input:SetTextVerticallyCentered();
            input:SetOnTextChanged(function()
                editor.state.model:SetListFields(list.id, { internalName = input:GetText() });
                RefreshSidebar(true);
                UpdateToolbar();
            end);
            return input;
        end }));
        local referenceCount = 0;
        for _, binding in editor.state.snapshot.creatureTemplateVendor do
            if binding.vendorListID == list.id then
                referenceCount += 1;
            end
        end

        table.insert(body, Declarative.Text(`Used by {referenceCount} creature template{if referenceCount == 1 then "" else "s"}.`, { width = inspectorContentWidth, height = 28, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }));
        table.insert(body, Declarative.Button("Duplicate List", { width = inspectorContentWidth, height = 32, fontSize = 10, onClick = StartDuplicate }));
        table.insert(body, Declarative.Button("Delete List", { width = inspectorContentWidth, height = 32, fontSize = 10, panelTemplate = "DangerButtonPanel", onClick = function()
            deleteModal:Open({ message = `Delete '{list.internalName}' and all {#editor.state.model:GetOrderedItemIDs(list.id)} vendor entries?`, onConfirm = function()
                if integer.lt(list.id, 0i) then
                    editor.state.model:RemoveTemporaryList(list.id);
                    SelectList(editor.state.model:GetListIDs()[1]);
                elseif editor.state.controller:IsReady() then
                    editor.state.controller:Submit(InteractionEditor.DeleteVendorList(list.id));
                end
            end });
        end }));
    else
        local item = editor.state.model:GetItem(editor.state.selectedItemID);
        if item == nil then
            editor.state.selectedItemID = nil;
            RefreshInspector();
            return;
        end

        table.insert(body, Declarative.Text("VENDOR ITEM", { width = inspectorContentWidth, height = 20, fontSize = 10, color = editor.colors.warning, template = "DefaultButtonText" }));
        table.insert(body, Declarative.Component({ width = inspectorContentWidth, height = 48, factory = function(parent, position, size, layer)
            local summary = parent:NewPanel(position, size, layer, "DebugDarkGrey");
            summary:SetAlpha(0.0);
            local icon = summary:NewPanel(vec.New(0, 0), vec.New(40, 40), 1i, "DebugDarkGrey");
            icon:SetAnchor(vec.New(0.0, 0.5));
            icon:SetRelativePoint(vec.New(0.0, 0.5));
            icon:SetBackground(ItemIconTexture(item.itemTemplateID));
            icon:SetColor(vec.New(1, 1, 1));
            icon:SetBorderColor(editor.colors.muted);
            icon:SetBorderSize(1);
            AttachItemTooltip(icon, item.itemTemplateID);
            local name = summary:NewText(TextUtil.Truncate(ItemName(item.itemTemplateID), 29), vec.New(50, -11), 1i, "DefaultButtonText");
            name:SetAnchor(vec.New(0.0, 1.0));
            name:SetRelativePoint(vec.New(0.0, 1.0));
            name:SetFontSize(11);
            name:SetInteractable(false);
            local itemTemplate = FindItemTemplate(item.itemTemplateID);
            if itemTemplate ~= nil then
                name:SetColor(ItemUtil.GetRarityColor(itemTemplate.rarity));
            end

            local metadata = summary:NewText(`Entry #{item.id} - Item #{item.itemTemplateID}`, vec.New(50, -31), 1i, "DefaultButtonText");
            metadata:SetAnchor(vec.New(0.0, 1.0));
            metadata:SetRelativePoint(vec.New(0.0, 1.0));
            metadata:SetFontSize(8);
            metadata:SetColor(editor.colors.muted);
            metadata:SetInteractable(false);
            return summary;
        end }));
        table.insert(body, Declarative.Horizontal({ width = inspectorContentWidth, height = 54, spacing = 10, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            MakeNumberInputNode("purchaseQuantity", "Purchase quantity", item.purchaseQuantity, 4294967295, function(value)
                if integer.gt(value, 0i) then
                    editor.state.model:SetItemFields(item.id, { purchaseQuantity = value });
                    UpdateToolbar();
                    RefreshPreview();
                end
            end),
            MakeNumberInputNode("flags", "Flags", item.flags, 4294967295, function(value)
                editor.state.model:SetItemFields(item.id, { flags = value });
                UpdateToolbar();
            end)
        ));
        table.insert(body, MakePickerNode("visibilityCondition", "Visibility condition", GetConditionPickerItems(), item.visibilityConditionSetID, function(value)
            editor.state.model:SetItemFields(item.id, { visibilityConditionSetID = value });
            UpdateToolbar();
        end));
        table.insert(body, MakePickerNode("enabledCondition", "Enabled condition", GetConditionPickerItems(), item.enabledConditionSetID, function(value)
            editor.state.model:SetItemFields(item.id, { enabledConditionSetID = value });
            UpdateToolbar();
        end));
        table.insert(body, MakePickerNode("disabledReason", "Disabled reason text", GetTextPickerItems(), item.disabledReasonTextID, function(value)
            editor.state.model:SetItemFields(item.id, { disabledReasonTextID = value });
            UpdateToolbar();
        end));
        table.insert(body, MakePickerNode("cost", "Cost definition", GetCostPickerItems(), item.costID, function(value)
            editor.state.model:SetItemFields(item.id, { costID = value });
            UpdateToolbar();
            RefreshPreview();
            RefreshInspector();
        end));
        table.insert(body, Declarative.Text(FormatCost(item.costID), { width = inspectorContentWidth, height = 38, fontSize = 9, color = editor.colors.warning, overflow = Declarative.TextOverflow.Wrap, template = "DefaultButtonText" }));
        if item.costID == 0i then
            table.insert(body, Declarative.Button("+ New Compound Cost", { width = inspectorContentWidth, height = 30, fontSize = 9, onClick = function()
                local costID = editor.state.model:AddCost(UniqueCostName(`{ItemName(item.itemTemplateID)} Cost`));
                editor.state.model:SetItemFields(item.id, { costID = costID });
                RefreshInspector();
                RefreshPreview();
                UpdateToolbar();
            end }));
        else
            local cost = editor.state.model:GetCost(item.costID);
            if cost ~= nil then
                table.insert(body, Declarative.Component({ width = inspectorContentWidth, height = 30, factory = function(parent, position, size, layer)
                    local input = InputField.New(parent, position, size, layer, { defaultText = cost.internalName, defaultTextFontSize = 9 });
                    input:SetTextVerticallyCentered();
                    input:SetOnTextChanged(function()
                        editor.state.model:SetCostFields(cost.id, { internalName = input:GetText() });
                        UpdateToolbar();
                    end);
                    return input;
                end }));
            end
            local currencyNodes, itemNodes = BuildCostComponentNodes(item);
            table.insert(body, Declarative.Text("CURRENCIES", { width = inspectorContentWidth, height = 18, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }));
            if #currencyNodes == 0 then
                table.insert(body, Declarative.Text("No currency components.", { width = inspectorContentWidth, height = 22, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }));
            else
                for _, node in currencyNodes do
                    table.insert(body, node);
                end
            end
            table.insert(body, Declarative.Button("+ Currency", { width = inspectorContentWidth, height = 30, fontSize = 9, onClick = function()
                currencyPicker:Open({ title = "Add Cost Currency", items = GetCurrencyPickerItems(item.costID, nil), onSelect = function(currencyID)
                    local currencies, costItems = editor.state.model:GetCostComponents(item.costID);
                    if #currencies + #costItems >= MAX_COST_COMPONENTS then
                        editor.state.controller:SetStatus(`A cost cannot contain more than {MAX_COST_COMPONENTS} components.`, STATUS.Warning);
                        UpdateToolbar();
                        return;
                    end

                    editor.state.model:AddCurrencyComponent(item.costID, currencyID);
                    RefreshInspector();
                    RefreshPreview();
                    UpdateToolbar();
                end });
            end }));
            table.insert(body, Declarative.Text("ITEMS", { width = inspectorContentWidth, height = 18, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }));
            if #itemNodes == 0 then
                table.insert(body, Declarative.Text("No item components.", { width = inspectorContentWidth, height = 22, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }));
            else
                for _, node in itemNodes do
                    table.insert(body, node);
                end
            end
            table.insert(body, Declarative.Button("+ Item", { width = inspectorContentWidth, height = 30, fontSize = 9, onClick = function()
                local currencies, costItems = editor.state.model:GetCostComponents(item.costID);
                if #currencies + #costItems >= MAX_COST_COMPONENTS then
                    editor.state.controller:SetStatus(`A cost cannot contain more than {MAX_COST_COMPONENTS} components.`, STATUS.Warning);
                    UpdateToolbar();
                    return;
                end

                if #GetItemPickerItems(item.costID, nil) == 0 then
                    editor.state.controller:SetStatus("No unused items are available for this cost.", STATUS.Warning);
                    UpdateToolbar();
                    return;
                end

                OpenItemPicker("costItem");
            end }));
        end
        table.insert(body, Declarative.Panel({ grow = 1, width = inspectorContentWidth, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }));
        table.insert(body, Declarative.Horizontal({ width = inspectorContentWidth, height = 32, spacing = 6, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Button("Replace Item", { width = inspectorActionWidth, height = 32, fontSize = 9, onClick = function()
                OpenItemPicker("replace");
            end }),
            Declarative.Button("Remove", { width = inspectorActionWidth, height = 32, fontSize = 9, panelTemplate = "DangerButtonPanel", onClick = function()
                if integer.lt(item.id, 0i) then
                    editor.state.model:RemoveTemporaryItem(item.id);
                    editor.state.selectedItemID = nil;
                    editor.state.controller:SelectItem(nil);
                    RefreshAll(true);
                else
                    if GetPersistedVendorItemCount(item.vendorListID) <= 1 then
                        editor.state.controller:SetStatus("Apply a replacement entry before deleting the final persisted vendor item.", STATUS.Warning);
                        UpdateToolbar();
                    else
                        deleteModal:Open({ message = `Remove {ItemName(item.itemTemplateID)} from this vendor list?`, onConfirm = function()
                            if editor.state.controller:IsReady() then
                                editor.state.controller:Submit(InteractionEditor.DeleteVendorListItem(item.id));
                            end
                        end });
                    end
                end
            end })
        ));
        table.insert(body, Declarative.Panel({ width = inspectorContentWidth, height = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }));
    end
    local contentHeight = 12 + math.max(0, #body - 1) * 7;
    for _, node in body do
        local options = node.options or node;
        contentHeight += options.height or 0;
    end
    contentHeight = math.max(bodyHeight, contentHeight);
    editor.refs.inspectorHost:SetContentHeight(contentHeight);
    editor.state.inspectorView = Declarative.Build(editor.refs.inspectorHost.content, Declarative.Vertical({ position = vec.New(14, -4), width = inspectorContentWidth, height = contentHeight - 12, spacing = 7, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, table.unpack(body)));
end

local function MoveItemInOrder(order : { integer }, itemID : integer, targetIndex : number) : { integer }
    local result = { };
    for _, currentItemID in order do
        if currentItemID ~= itemID then
            table.insert(result, currentItemID);
        end
    end

    table.insert(result, math.clamp(targetIndex, 1, #result + 1), itemID);
    return result;
end

local function GetCardPosition(drag : any, absoluteIndex : number) : vec
    local localIndex = absoluteIndex - drag.firstSlot;
    local row = math.floor(localIndex / drag.columnCount);
    local column = localIndex % drag.columnCount;

    return vec.New(drag.margin + column * (SLOT_WIDTH + SLOT_GAP), -(drag.gridTop + row * (SLOT_HEIGHT + SLOT_GAP)));
end

local function GetVendorCardTargetIndex(drag : any, mousePosition : vec) : number?
    local hostPosition = editor.refs.slotHost.content:GetWorldPos();
    local relativeX = mousePosition.x - hostPosition.x - drag.margin;
    local relativeY = hostPosition.y + editor.refs.slotHost.content:GetHeight() - drag.gridTop - mousePosition.y;
    if relativeX < 0 or relativeY < 0 then
        return nil;
    end

    local column = math.floor(relativeX / (SLOT_WIDTH + SLOT_GAP));
    local row = math.floor(relativeY / (SLOT_HEIGHT + SLOT_GAP));
    if relativeX - column * (SLOT_WIDTH + SLOT_GAP) >= SLOT_WIDTH or relativeY - row * (SLOT_HEIGHT + SLOT_GAP) >= SLOT_HEIGHT then
        return nil;
    end

    local localIndex = row * drag.columnCount + column + 1;
    local targetIndex = drag.firstSlot + localIndex - 1;
    if localIndex < 1 or localIndex > drag.visibleSlots or drag.originalOrder[targetIndex] == nil then
        return nil;
    end

    return targetIndex;
end

local function PreviewVendorCardDrag(mousePosition : vec, delta : vec)
    local drag = editor.state.cardDrag;
    if drag == nil then
        return;
    end

    local desiredWorld = drag.startWorld + delta;
    local targetIndex = GetVendorCardTargetIndex(drag, mousePosition);
    if targetIndex ~= drag.targetIndex then
        drag.targetIndex = targetIndex;
        drag.previewOrder = if targetIndex ~= nil then MoveItemInOrder(drag.originalOrder, drag.itemID, targetIndex) else drag.originalOrder;
        for _, widget in editor.state.slotWidgets do
            if widget.itemID ~= nil then
                local previewIndex = table.find(drag.previewOrder, widget.itemID);
                if previewIndex ~= nil then
                    widget.root:SetPos(GetCardPosition(drag, previewIndex));
                end
            end
        end
    end

    drag.dragSurface:SetPos(desiredWorld - drag.hostWorld);
end

local function CommitVendorCardDrag()
    local drag = editor.state.cardDrag;
    if drag == nil then
        return;
    end

    editor.state.cardDrag = nil;
    UI.DestroyWidget(drag.dragSurface.root);
    drag.sourceSurface:SetVisible(true);

    local changed = drag.targetIndex ~= nil and drag.targetIndex ~= drag.sourceIndex;
    if changed then
        editor.state.model:ReorderItems(drag.listID, drag.sourceIndex, drag.targetIndex);
    end

    if editor.state.pendingSnapshot then
        editor.state.pendingSnapshot = false;
        ApplySnapshot();
    else
        RefreshPreview();
    end

    if changed then
        SelectItem(drag.itemID);
    else
        RefreshInspector();
    end

    UpdateToolbar();
end

local function CreateVendorCard(parent : any, layer : integer, item : any?, selected : boolean, placeholder : boolean)
    local button = Button.New(parent, vec.New(0, 0), vec.New(SLOT_WIDTH, SLOT_HEIGHT), layer, {
        text = "",
        panelTemplate = "DebugDarkGrey",
        color = if item ~= nil then editor.colors.slot else editor.colors.emptySlot,
        hoverColor = if item ~= nil then vec.New(0.10, 0.13, 0.15) else vec.New(0.075, 0.09, 0.105),
        highlightColor = editor.colors.slot,
        alpha = if placeholder then 0.28 else 1,
        hoverAlpha = if placeholder then 0.28 else 1,
        highlightAlpha = if placeholder then 0.28 else 1,
    });
    button:SetAnchor(vec.New(0, 1));
    button:SetRelativePoint(vec.New(0, 1));
    button:SetHighlighted(selected and not placeholder);
    button.root:SetBorderColor(if selected and not placeholder then editor.colors.warning else vec.New(0.22, 0.28, 0.32));
    button.root:SetBorderSize(1);
    button.root:SetClipChildren(true);

    local iconPanel = button.root:NewPanel(vec.New(6, -6), vec.New(SLOT_ICON_SIZE, SLOT_ICON_SIZE), 2i, "DebugDarkGrey");
    iconPanel:SetAnchor(vec.New(0, 1));
    iconPanel:SetRelativePoint(vec.New(0, 1));
    iconPanel:SetBorderColor(editor.colors.muted);
    iconPanel:SetBorderSize(1);
    iconPanel:SetInteractable(false);
    if item ~= nil then
        iconPanel:SetBackground(ItemIconTexture(item.itemTemplateID));
        iconPanel:SetColor(vec.New(1, 1, 1));
    else
        iconPanel:SetColor(editor.colors.emptySlot);
    end
    if placeholder then
        iconPanel:SetAlpha(0.38);
    end

    local iconText = iconPanel:NewText(if item ~= nil then "" else "+", vec.New(0, 0), 1i, "DefaultButtonText");
    iconText:SetAnchor(vec.New(0.5, 0.5));
    iconText:SetRelativePoint(vec.New(0.5, 0.5));
    iconText:SetFontSize(14);
    iconText:SetColor(editor.colors.muted);
    iconText:SetInteractable(false);

    local textX = 47;
    local nameText = button.root:NewText(if item ~= nil then TextUtil.Truncate(ItemName(item.itemTemplateID), 18) else "Empty vendor slot", vec.New(textX, -4), 2i, "DefaultButtonText");
    nameText:SetAnchor(vec.New(0, 1));
    nameText:SetRelativePoint(vec.New(0, 1));
    nameText:SetFontSize(9);
    nameText:SetInteractable(false);

    local costWidgets = if item ~= nil then CreateCostPreview(button.root, item.costID, vec.New(textX, -16), SLOT_WIDTH - textX - 6, not placeholder) else { };
    if item == nil then
        local costText = button.root:NewText("Click to select an item", vec.New(textX, -16), 2i, "DefaultButtonText");
        costText:SetAnchor(vec.New(0, 1));
        costText:SetRelativePoint(vec.New(0, 1));
        costText:SetFontSize(8);
        costText:SetColor(editor.colors.muted);
        costText:SetInteractable(false);
        table.insert(costWidgets, costText);
    end

    local secondary = if item == nil then "" elseif integer.gt(item.purchaseQuantity, 1i) then `Quantity {item.purchaseQuantity}` elseif item.visibilityConditionSetID ~= 0i then `Condition {item.visibilityConditionSetID}` else "";
    local secondaryText = button.root:NewText(secondary, vec.New(textX, -41), 2i, "DefaultButtonText");
    secondaryText:SetAnchor(vec.New(0, 1));
    secondaryText:SetRelativePoint(vec.New(0, 1));
    secondaryText:SetFontSize(7);
    secondaryText:SetColor(editor.colors.muted);
    secondaryText:SetInteractable(false);
    if placeholder then
        nameText:SetAlpha(0.55);
        for _, widget in costWidgets do
            widget:SetAlpha(0.42);
        end

        secondaryText:SetAlpha(0.42);
        button:SetInteractable(false);
    end
    if item ~= nil and not placeholder then
        AttachItemTooltip(button, item.itemTemplateID);
    end

    return button;
end

RefreshPreview = function()
    DestroySlots();
    local list = GetSelectedList();
    if list == nil then
        editor.refs.previewTitle:SetText("Select a vendor list");
        editor.refs.pageText:SetText("");
        return;
    end

    local ids = editor.state.model:GetOrderedItemIDs(list.id);
    local perPage = integer.tonumber(editor.state.rows) * integer.tonumber(editor.state.columns);
    local pageCount = math.max(1, math.ceil(MAX_VENDOR_ITEMS / perPage));
    editor.state.page = math.clamp(editor.state.page, 1, pageCount);
    local firstSlot = (editor.state.page - 1) * perPage + 1;
    local visibleSlots = math.min(perPage, MAX_VENDOR_ITEMS - firstSlot + 1);
    editor.refs.previewTitle:SetText(`{list.internalName}  -  {#ids} / {MAX_VENDOR_ITEMS} items`);
    editor.refs.pageText:SetText(`Page {editor.state.page} / {pageCount} - {visibleSlots} slots`);
    editor.refs.previousPage:SetInteractable(editor.state.page > 1);
    editor.refs.nextPage:SetInteractable(editor.state.page < pageCount);

    local columnCount = integer.tonumber(editor.state.columns);
    local rowCount = integer.tonumber(editor.state.rows);
    local gridWidth = columnCount * SLOT_WIDTH + (columnCount - 1) * SLOT_GAP;
    local gridHeight = rowCount * SLOT_HEIGHT + (rowCount - 1) * SLOT_GAP;
    local contentHeight = math.max(previewHeight, gridHeight);
    editor.refs.slotHost:SetContentHeight(contentHeight);
    local viewportWidth = editor.refs.slotHost:GetViewportSize().x;
    local contentWidth = math.max(viewportWidth, gridWidth + 36);
    local margin = math.max(18, (contentWidth - gridWidth) / 2);
    local gridTop = math.max(0, (contentHeight - gridHeight) / 2);
    editor.refs.slotHost.content:SetWidth(contentWidth);
    local currentOffset = editor.refs.slotHost:GetOffset();
    editor.refs.slotHost:SetOffset(vec.New(math.clamp(currentOffset.x, math.min(0, viewportWidth - contentWidth), 0), currentOffset.y));
    UpdateHorizontalScrollbar();

    for localIndex = 1, visibleSlots do
        local absoluteIndex = firstSlot + localIndex - 1;
        local row = math.floor((localIndex - 1) / columnCount);
        local column = (localIndex - 1) % columnCount;
        local position = vec.New(margin + column * (SLOT_WIDTH + SLOT_GAP), -(gridTop + row * (SLOT_HEIGHT + SLOT_GAP)));
        local itemID = ids[absoluteIndex];
        local item = if itemID ~= nil then editor.state.model:GetItem(itemID) else nil;
        local selected = itemID ~= nil and itemID == editor.state.selectedItemID;
        local slot = editor.refs.slotHost.content:NewPanel(position, vec.New(SLOT_WIDTH, SLOT_HEIGHT), 1i, "DebugDarkGrey");
        slot:SetAnchor(vec.New(0, 1));
        slot:SetRelativePoint(vec.New(0, 1));
        slot:SetAlpha(0);
        slot:SetBorderSize(0);
        slot:SetInteractable(false);
        if item ~= nil then
            CreateVendorCard(slot, 1i, item, false, true);
        end

        local button = CreateVendorCard(slot, 3i, item, selected, false);
        button.itemID = itemID;
        button.suppressClick = false;
        button:SetOnMouseUp(function(_, _, mouseButton)
            if mouseButton ~= 0i then
                return;
            end

            if button.suppressClick then
                button.suppressClick = false;
                return;
            end

            if itemID == nil then
                OpenItemPicker("add");
            else
                SelectItem(itemID);
            end
        end);
        if itemID ~= nil then
            Draggable.Attach(button, button, {
                button = 0i,
                moveTarget = false,
                threshold = 4,
                onBegin = function(_, mousePosition)
                    if not editor.state.controller:IsReady() then
                        return;
                    end

                    editorTooltip.ClearLines();
                    editorTooltip.Hide();
                    button.suppressClick = true;
                    local startWorld = button:GetWorldPos();
                    local hostWorld = editor.refs.dragHost:GetWorldPos();
                    local dragSurface = CreateVendorCard(editor.refs.dragHost, 1i, item, selected, false);
                    dragSurface:SetAnchor(vec.New(0, 0));
                    dragSurface:SetRelativePoint(vec.New(0, 0));
                    dragSurface.root:SetInteractable(false);
                    dragSurface:SetPos(startWorld - hostWorld);
                    button:SetVisible(false);
                    editor.state.cardDrag = { listID = list.id, itemID = itemID, sourceIndex = absoluteIndex, targetIndex = absoluteIndex, originalOrder = ids, previewOrder = ids, startWorld = startWorld, hostWorld = hostWorld, sourceSurface = button, dragSurface = dragSurface, slot = slot, firstSlot = firstSlot, visibleSlots = visibleSlots, columnCount = columnCount, margin = margin, gridTop = gridTop };
                end,
                onMove = function(_, mousePosition, delta)
                    PreviewVendorCardDrag(mousePosition, delta);
                end,
                onEnd = function()
                    CommitVendorCardDrag();
                end,
            });
        end

        table.insert(editor.state.slotWidgets, { root = slot, surface = button, itemID = itemID });
    end
end

RefreshAll = function(preserveScroll : boolean?)
    RefreshSidebar(preserveScroll);
    RefreshPreview();
    RefreshInspector();
    UpdateToolbar();
end

ApplySnapshot = function()
    editor.state.snapshot = editor.state.controller:Refresh();
    if editor.state.snapshot.dataState ~= "Ready" or editor.state.controller:IsAwaitingSnapshot() then
        UpdateToolbar();
        return;
    end

    if editor.state.model == nil then
        editor.state.model = VendorListModel.New(editor.state.snapshot);
    else
        editor.state.model:Reload(editor.state.snapshot);
    end

    local selectedListID = editor.state.controller:GetSelectedListID() or editor.state.selectedListID;
    if selectedListID == nil or editor.state.model:GetList(selectedListID) == nil then
        selectedListID = editor.state.model:GetListIDs()[1];
    end

    local selectedItemID = editor.state.controller:GetSelectedItemID() or editor.state.selectedItemID;
    editor.state.selectedListID = selectedListID;
    editor.state.controller:SelectList(selectedListID);
    if selectedItemID ~= nil then
        local selectedItem = editor.state.model:GetItem(selectedItemID);
        if selectedItem == nil or selectedItem.vendorListID ~= selectedListID then
            selectedItemID = nil;
        end
    end

    editor.state.selectedItemID = selectedItemID;
    editor.state.controller:SelectItem(selectedItemID);
    RefreshAll(true);
end

local function CreateSidebarList(parent, position, size, layer)
    return VirtualList.New(parent, position, size, layer, {
        rowHeight = 42,
        scrollBox = { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = size.x - 10, contentSizeY = size.y, verticalScrollBar = true, scrollBarWidth = 10, alpha = 0 },
        createRow = function(rowParent)
            local row = Button.New(rowParent, vec.New(0, 0), vec.New(size.x - 10, 40), 1i, { text = "", panelTemplate = "DebugDarkGrey", textTemplate = "DefaultButtonText", textAlign = "left", textPadX = 8, color = editor.colors.slot, highlightColor = editor.colors.selected });
            row:SetFontSize(9);
            row:SetAnchor(vec.New(0.0, 1.0));
            row:SetRelativePoint(vec.New(0.0, 1.0));
            row.bound = nil;
            row:SetOnMouseUp(function(_, _, mouseButton)
                if mouseButton == 0i and row.bound ~= nil then
                    SelectList(row.bound.id);
                end
            end);

            return row;
        end,
        bindRow = function(row, _, value)
            row.bound = value;
            row:SetText(`{value.list.internalName}\nList #{value.id} - {value.itemCount} items`);
            row:SetHighlighted(value.id == editor.state.selectedListID);
        end,
    });
end

OpenItemPicker = function(mode : string, costItemComponentID : integer?)
    editor.state.pickerMode = mode;
    editor.state.pickerCostItemComponentID = costItemComponentID;
    local selectedItem = if editor.state.model ~= nil and editor.state.selectedItemID ~= nil then editor.state.model:GetItem(editor.state.selectedItemID) else nil;
    local costID = if selectedItem ~= nil and selectedItem.costID ~= 0i then selectedItem.costID else nil;
    local currentItemTemplateID = nil;
    if mode == "replaceCostItem" and costID ~= nil and costItemComponentID ~= nil then
        local _, components = editor.state.model:GetCostComponents(costID);
        for _, component in components do
            if component.id == costItemComponentID then
                currentItemTemplateID = component.itemTemplateID;
                break;
            end
        end
    end

    local items = if mode == "costItem" then GetItemPickerItems(costID, nil) elseif mode == "replaceCostItem" then GetItemPickerItems(costID, currentItemTemplateID) else GetItemPickerItems(nil, nil);
    itemPicker:Open({ title = if mode == "replace" then "Replace Vendor Item" elseif mode == "replaceCostItem" then "Replace Cost Item" elseif mode == "costItem" then "Add Cost Item" else "Add Vendor Item", items = items, onSelect = HandleItemPicked });
end

view = Declarative.Build(editor.state.canvas,
    Declarative.Panel({ id = "root", position = editor.geometry.windowPosition, size = editor.geometry.windowSize, template = "DialogBox", clipChildren = true },
        Declarative.Horizontal({ id = "topbar", position = vec.New(14, editor.geometry.windowSize.y - 44), size = vec.New(editor.geometry.windowSize.x - 28, 34), spacing = 6, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Panel({ id = "dragHandle", grow = 1, height = 34, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, Declarative.Text("Vendor Editor", { position = vec.New(0, 0), width = 180, height = 20, anchor = Declarative.Anchor.Left, relativePoint = Declarative.Anchor.Left, fontSize = 15, template = "DefaultButtonText" })),
            Declarative.Text("", { id = "status", width = 300, height = 34, fontSize = 9, color = editor.colors.muted, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
            Declarative.Text("NO LIST", { id = "dirty", width = 76, height = 34, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
            Declarative.Button("Revert All", { id = "revert", width = 86, height = 34, fontSize = 9, interactable = false, onClick = function()
                if editor.state.model == nil then
                    return;
                end

                editor.state.model:Revert();
                if editor.state.selectedListID ~= nil and editor.state.model:GetList(editor.state.selectedListID) == nil then
                    SelectList(editor.state.model:GetListIDs()[1]);
                else
                    RefreshAll(false);
                end
            end }),
            Declarative.Button("Apply List", { id = "apply", width = 86, height = 34, fontSize = 9, color = editor.colors.selected, interactable = false, onClick = BeginApply }),
            Declarative.Panel({ width = 20, height = 34, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }),
            Declarative.Button("Refresh", { width = 76, height = 34, fontSize = 10, onClick = function()
                editor.state.controller:RequestSnapshot();
            end }),
            Declarative.Button("Close", { width = 62, height = 34, fontSize = 10, onClick = function()
                local dirty = editor.state.model ~= nil and editor.state.model:IsDirty();
                local function hide()
                    itemPicker:Close();
                    currencyPicker:Close();
                    editor.state.visible = false;
                    view.root:SetVisible(false);
                end

                if dirty then
                    closeModal:Open({ message = "Close with unsaved vendor-list changes? The local draft will remain available until scripts reload.", onConfirm = hide });
                else
                    hide();
                end
            end })
        ),
        Declarative.Horizontal({ position = vec.New(1, 1), size = vec.New(editor.geometry.windowSize.x - 2, bodyHeight), spacing = 0, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
            Declarative.Vertical({ width = editor.geometry.sidebarWidth, height = bodyHeight, spacing = 7, padding = 10, template = "DebugDarkGrey", alpha = 0.72 },
                Declarative.Horizontal({ width = editor.geometry.sidebarWidth - 20, height = 20, spacing = 4, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                    Declarative.Text("VENDOR LISTS", { grow = 1, height = 20, fontSize = 10, color = editor.colors.accent, template = "DefaultButtonText" }),
                    Declarative.Text("0 lists", { id = "listCount", width = 64, height = 20, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" })
                ),
                Declarative.Component({ id = "search", width = editor.geometry.sidebarWidth - 20, height = 30, factory = function(parent, position, size, layer)
                    local input = InputBox.New(parent, position, size, layer, { placeholderText = "Find vendor list...", defaultTextFontSize = 10 });
                    input:SetTextVerticallyCentered();
                    input:SetOnTextChanged(function()
                        editor.state.search = input:GetText();
                        RefreshSidebar(false);
                    end);

                    return input;
                end }),
                Declarative.Component({ id = "sidebarList", width = editor.geometry.sidebarWidth - 20, height = bodyHeight - 127, factory = CreateSidebarList }),
                Declarative.Button("+ New Vendor List", { width = editor.geometry.sidebarWidth - 20, height = 30, fontSize = 10, onClick = function()
                    if editor.state.model == nil or not editor.state.controller:IsReady() then
                        return;
                    end

                    SelectList(editor.state.model:AddList(UniqueListName("New Vendor List"), 0i));
                end })
            ),
            Declarative.Panel({ width = centerWidth, height = bodyHeight, template = "DebugDarkGrey", alpha = 0.35, clipChildren = true },
                Declarative.Horizontal({ position = vec.New(14, -10), size = vec.New(centerWidth - 28, 34), anchor = Declarative.Anchor.TopLeft, relativePoint = Declarative.Anchor.TopLeft, spacing = 6, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                    Declarative.Text("Rows", { width = 36, height = 34, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
                    Declarative.Component({ id = "rows", width = 58, height = 30, factory = function(parent, position, size, layer)
                        local dropdown = Dropdown.New(parent, editor.state.canvas, position, size, layer, {
                            options = { { label = "6", value = 6i }, { label = "7", value = 7i }, { label = "8", value = 8i } },
                            value = editor.state.rows,
                            itemHeight = 22,
                            itemFontSize = 9,
                            onValueChanged = function(value)
                                editor.state.rows = value;
                                editor.state.page = 1;
                                editor.refs.slotHost:SetOffset(vec.New(0, 0));
                                RefreshPreview();
                            end,
                        });
                        dropdown.button:SetFontSize(9);

                        return dropdown;
                    end }),
                    Declarative.Text("Columns", { width = 48, height = 34, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }),
                    Declarative.Component({ id = "columns", width = 58, height = 30, factory = function(parent, position, size, layer)
                        local options = { };
                        for value = 2, 8 do
                            table.insert(options, { label = tostring(value), value = integer.create(value) });
                        end

                        local dropdown = Dropdown.New(parent, editor.state.canvas, position, size, layer, {
                            options = options,
                            value = editor.state.columns,
                            itemHeight = 22,
                            itemFontSize = 9,
                            onValueChanged = function(value)
                                editor.state.columns = value;
                                editor.state.page = 1;
                                editor.refs.slotHost:SetOffset(vec.New(0, 0));
                                RefreshPreview();
                            end,
                        });
                        dropdown.button:SetFontSize(9);

                        return dropdown;
                    end }),
                    Declarative.Panel({ grow = 1, height = 34, template = "DebugDarkGrey", alpha = 0, borderSize = 0 }, Declarative.Text("Select an empty slot to add; drag populated slots to insert/reorder.", { width = 430, height = 34, anchor = Declarative.Anchor.Right, relativePoint = Declarative.Anchor.Right, fontSize = 9, color = editor.colors.muted, template = "DefaultButtonText" }))
                ),
                Declarative.Text("Select a vendor list", { id = "previewTitle", position = vec.New(18, -50), width = centerWidth - 36, height = 20, anchor = Declarative.Anchor.TopLeft, relativePoint = Declarative.Anchor.TopLeft, fontSize = 11, color = editor.colors.warning, overflow = Declarative.TextOverflow.Truncate, template = "DefaultButtonText" }),
                Declarative.Component({ id = "slotHost", position = vec.New(0, -PREVIEW_TOP), size = vec.New(centerWidth, previewHeight), anchor = Declarative.Anchor.TopLeft, relativePoint = Declarative.Anchor.TopLeft, factory = function(parent, position, size, layer)
                    return ScrollBox.New(parent, position, size, layer, { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = size.x - 10, contentSizeY = size.y, verticalScrollBar = true, horizontalScrollBar = true, scrollBarWidth = 10, alpha = 0 });
                end }),
                Declarative.Panel({ id = "horizontalScrollTrack", position = vec.New(0, editor.geometry.footerHeight + 1), size = vec.New(centerWidth, 10), template = "DebugDarkGrey", color = vec.New(0.18, 0.20, 0.22), borderSize = 0, visible = false },
                    Declarative.Panel({ id = "horizontalScrollHandle", position = vec.New(0, 0), size = vec.New(40, 10), template = "DebugDarkGrey", color = vec.New(0.56, 0.58, 0.60), borderSize = 0 })
                ),
                Declarative.Horizontal({ position = vec.New(centerWidth / 2 - 110, 7), size = vec.New(220, 30), spacing = 6, anchor = Declarative.Anchor.BottomLeft, relativePoint = Declarative.Anchor.BottomLeft, template = "DebugDarkGrey", alpha = 0, borderSize = 0 },
                    Declarative.Button("<", { id = "previousPage", width = 42, height = 30, fontSize = 10, onClick = function()
                        editor.state.page -= 1;
                        RefreshPreview();
                    end }),
                    Declarative.Text("", { id = "pageText", width = 124, height = 30, fontSize = 9, color = editor.colors.muted, align = "center", verticalAlign = "center", inset = 0, template = "DefaultButtonText" }),
                    Declarative.Button(">", { id = "nextPage", width = 42, height = 30, fontSize = 10, onClick = function()
                        editor.state.page += 1;
                        RefreshPreview();
                    end })
                )
            ),
            Declarative.Component({ id = "inspectorHost", width = editor.geometry.inspectorWidth, height = bodyHeight, factory = function(parent, position, size, layer)
                return ScrollBox.New(parent, position, size, layer, { panelTemplate = "DebugDarkGrey", contentPanelTemplate = "DebugDarkGrey", contentSizeX = editor.geometry.inspectorWidth - 10, contentSizeY = bodyHeight, verticalScrollBar = true, scrollBarWidth = 10, alpha = 0.72 });
            end })
        ),
        Declarative.Panel({ id = "dragHost", position = vec.New(0, 0), size = editor.geometry.windowSize, layer = 100i, template = "DebugDarkGrey", alpha = 0, borderSize = 0, interactable = false })
    )
);

VendorEditorView = view;
editor.refs = view.refs;
Draggable.Attach(view.root, view:Get("dragHandle"));
Draggable.Attach(view.root, view:Get("topbar"));
editorTooltip = GameTooltip.New(view.root);
editor.refs.slotHost:SetOnScroll(UpdateHorizontalScrollbar);
Draggable.Attach(editor.refs.horizontalScrollHandle, editor.refs.horizontalScrollHandle, {
    moveTarget = false,
    onBegin = function()
        editor.state.horizontalDragStart = editor.refs.slotHost:GetOffset().x;
    end,
    onMove = function(_, _, delta)
        local viewportWidth = editor.refs.slotHost:GetViewportSize().x;
        local contentWidth = editor.refs.slotHost.content:GetWidth();
        local scrollRange = math.max(0, contentWidth - viewportWidth);
        local handleRange = math.max(1, editor.refs.horizontalScrollTrack:GetWidth() - editor.refs.horizontalScrollHandle:GetWidth());
        local offset = editor.refs.slotHost:GetOffset();
        editor.refs.slotHost:SetOffset(vec.New(math.clamp(editor.state.horizontalDragStart - delta.x * scrollRange / handleRange, -scrollRange, 0), offset.y));
    end,
});
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
    local dirty = editor.state.model ~= nil and editor.state.model:IsDirty();
    local function hide()
        itemPicker:Close();
        currencyPicker:Close();
        editor.state.visible = false;
        view.root:SetVisible(false);
    end

    if dirty then
        closeModal:Open({ message = "Close with unsaved vendor-list changes?", onConfirm = hide });
    else
        hide();
    end
end

function methods:IsVisible()
    return view.root:IsVisible();
end

EditorRegistry.Register("VendorEditor", methods);

RegisterEvent(GameEvent.Updated, function()
    if not editor.state.visible then
        return;
    end

    if editor.state.controller:Update() then
        if editor.state.cardDrag ~= nil then
            editor.state.pendingSnapshot = true;
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
    elseif result.succeeded and result.artifact == ARTIFACT_VENDOR_LIST then
        editor.state.selectedListID = result.artifactID;
        editor.state.selectedItemID = nil;
        editor.state.controller:SelectList(result.artifactID);
    elseif result.succeeded and result.artifact == ARTIFACT_VENDOR_LIST_ITEM then
        editor.state.selectedItemID = result.artifactID;
        editor.state.controller:SelectItem(result.artifactID);
    end
end);

if editor.state.snapshot.dataState == "Ready" then
    ApplySnapshot();
else
    RefreshInspector();
    UpdateToolbar();
end
