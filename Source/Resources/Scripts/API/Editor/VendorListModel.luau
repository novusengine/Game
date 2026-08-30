export type VendorListRecord =
{
    id : integer,
    internalName : string,
    flags : integer,
};

export type VendorListItemRecord =
{
    id : integer,
    vendorListID : integer,
    priority : integer,
    itemTemplateID : integer,
    purchaseQuantity : integer,
    costID : integer,
    visibilityConditionSetID : integer,
    enabledConditionSetID : integer,
    disabledReasonTextID : integer,
    flags : integer,
};

export type CostRecord =
{
    id : integer,
    internalName : string,
};

export type CostCurrencyComponentRecord =
{
    id : integer,
    costID : integer,
    priority : integer,
    currencyID : integer,
    amount : integer,
};

export type CostItemComponentRecord =
{
    id : integer,
    costID : integer,
    priority : integer,
    itemTemplateID : integer,
    amount : integer,
};

export type Snapshot =
{
    vendorLists : { VendorListRecord },
    vendorListItems : { VendorListItemRecord },
    costs : { CostRecord },
    costCurrencyComponents : { CostCurrencyComponentRecord },
    costItemComponents : { CostItemComponentRecord },
};

export type Draft =
{
    listsByID : { [integer] : VendorListRecord },
    itemsByID : { [integer] : VendorListItemRecord },
    itemIDsByListID : { [integer] : { integer } },
    costsByID : { [integer] : CostRecord },
    currencyComponentsByID : { [integer] : CostCurrencyComponentRecord },
    itemComponentsByID : { [integer] : CostItemComponentRecord },
};

export type VendorListModel =
{
    GetDraft : (self : VendorListModel) -> Draft,
    Reload : (self : VendorListModel, snapshot : Snapshot) -> (),
    Revert : (self : VendorListModel) -> (),
    IsDirty : (self : VendorListModel) -> boolean,
    IsListDirty : (self : VendorListModel, listID : integer) -> boolean,
    GetListIDs : (self : VendorListModel) -> { integer },
    GetList : (self : VendorListModel, listID : integer) -> VendorListRecord?,
    GetItem : (self : VendorListModel, itemID : integer) -> VendorListItemRecord?,
    GetOrderedItemIDs : (self : VendorListModel, listID : integer) -> { integer },
    GetCost : (self : VendorListModel, costID : integer) -> CostRecord?,
    GetCostComponents : (self : VendorListModel, costID : integer) -> ({ CostCurrencyComponentRecord }, { CostItemComponentRecord }),
    SetListFields : (self : VendorListModel, listID : integer, fields : { [string] : any }) -> (),
    SetItemFields : (self : VendorListModel, itemID : integer, fields : { [string] : any }) -> (),
    SetCostFields : (self : VendorListModel, costID : integer, fields : { [string] : any }) -> (),
    SetCurrencyComponentFields : (self : VendorListModel, componentID : integer, fields : { [string] : any }) -> (),
    SetItemComponentFields : (self : VendorListModel, componentID : integer, fields : { [string] : any }) -> (),
    AddList : (self : VendorListModel, internalName : string, flags : integer) -> integer,
    DuplicateList : (self : VendorListModel, listID : integer, internalName : string) -> integer,
    AddItem : (self : VendorListModel, listID : integer, itemTemplateID : integer) -> integer,
    AddCost : (self : VendorListModel, internalName : string) -> integer,
    AddCurrencyComponent : (self : VendorListModel, costID : integer, currencyID : integer) -> integer,
    AddItemComponent : (self : VendorListModel, costID : integer, itemTemplateID : integer) -> integer,
    RemoveTemporaryList : (self : VendorListModel, listID : integer) -> (),
    RemoveTemporaryItem : (self : VendorListModel, itemID : integer) -> (),
    RemoveCurrencyComponent : (self : VendorListModel, componentID : integer) -> (),
    RemoveItemComponent : (self : VendorListModel, componentID : integer) -> (),
    RemoveTemporaryCost : (self : VendorListModel, costID : integer) -> (),
    ResolveTemporaryCost : (self : VendorListModel, temporaryCostID : integer, costID : integer) -> (),
    AcceptAppliedList : (self : VendorListModel, draftListID : integer, authoritativeListID : integer) -> (),
    ReorderItems : (self : VendorListModel, listID : integer, sourceIndex : number, destinationIndex : number) -> (),
    GetDirtyOperations : (self : VendorListModel, listID : integer) -> { any },
};

export type VendorListModelAPI =
{
    New : (snapshot : Snapshot) -> VendorListModel,
};

local LIST_FIELDS = { internalName = true, flags = true };
local ITEM_FIELDS = { priority = true, itemTemplateID = true, purchaseQuantity = true, costID = true, visibilityConditionSetID = true, enabledConditionSetID = true, disabledReasonTextID = true, flags = true };
local COST_FIELDS = { internalName = true };
local CURRENCY_COMPONENT_FIELDS = { priority = true, currencyID = true, amount = true };
local ITEM_COMPONENT_FIELDS = { priority = true, itemTemplateID = true, amount = true };

local function IsTemporaryID(id : integer) : boolean
    return integer.lt(id, 0i);
end

local function Clone(value : any, seen : { [any] : any }?) : any
    if type(value) ~= "table" then
        return value;
    end

    local previous = if seen then seen[value] else nil;
    if previous ~= nil then
        return previous;
    end

    local result : { [any] : any } = { };
    local visited = seen or { };
    visited[value] = result;
    for key, child in value do
        result[Clone(key, visited)] = Clone(child, visited);
    end
    return result;
end

local function Equal(left : any, right : any) : boolean
    if left == right then
        return true;
    end

    if type(left) ~= "table" or type(right) ~= "table" then
        return false;
    end

    for key, value in left do
        if not Equal(value, right[key]) then
            return false;
        end
    end
    for key in right do
        if left[key] == nil then
            return false;
        end
    end
    return true;
end

local function ValidateSnapshot(snapshot : Snapshot)
    if type(snapshot) ~= "table" then
        error("VendorListModel: snapshot must be a table", 3);
    end
    if type(snapshot.vendorLists) ~= "table" or type(snapshot.vendorListItems) ~= "table" or type(snapshot.costs) ~= "table" or type(snapshot.costCurrencyComponents) ~= "table" or type(snapshot.costItemComponents) ~= "table" then
        error("VendorListModel: snapshot vendor and cost collections must be tables", 3);
    end
end

local function BuildDraft(snapshot : Snapshot) : Draft
    local draft : Draft = { listsByID = { }, itemsByID = { }, itemIDsByListID = { }, costsByID = { }, currencyComponentsByID = { }, itemComponentsByID = { } };
    for _, list in snapshot.vendorLists do
        draft.listsByID[list.id] = Clone(list);
    end

    for _, item in snapshot.vendorListItems do
        draft.itemsByID[item.id] = Clone(item);
        local ids = draft.itemIDsByListID[item.vendorListID];
        if ids == nil then
            ids = { };
            draft.itemIDsByListID[item.vendorListID] = ids;
        end
        table.insert(ids, item.id);
    end

    for _, ids in draft.itemIDsByListID do
        table.sort(ids, function(leftID, rightID)
            local left = draft.itemsByID[leftID];
            local right = draft.itemsByID[rightID];
            if left.priority == right.priority then
                return integer.lt(left.id, right.id);
            end
            return integer.gt(left.priority, right.priority);
        end);
    end

    for _, cost in snapshot.costs do
        draft.costsByID[cost.id] = Clone(cost);
    end
    for _, component in snapshot.costCurrencyComponents do
        draft.currencyComponentsByID[component.id] = Clone(component);
    end
    for _, component in snapshot.costItemComponents do
        draft.itemComponentsByID[component.id] = Clone(component);
    end
    return draft;
end

local function ApplyFields(target : any, fields : { [string] : any }, allowed : { [string] : boolean }, label : string)
    for key, value in fields do
        if not allowed[key] then
            error(`VendorListModel: {label} field '{key}' is not draft-editable`, 3);
        end
        target[key] = Clone(value);
    end
end

local function ReapplyFields(previous : any, baseline : any, target : any, allowed : { [string] : boolean })
    if previous == nil or baseline == nil or target == nil then
        return;
    end
    for field in allowed do
        if not Equal(previous[field], baseline[field]) then
            target[field] = Clone(previous[field]);
        end
    end
end

local modelAPI : VendorListModelAPI = { };

function modelAPI.New(snapshot : Snapshot) : VendorListModel
    ValidateSnapshot(snapshot);
    local model : any = { };
    local draft = BuildDraft(snapshot);
    local baseline = Clone(draft);
    local nextTemporaryID = -1i;

    local function AllocateTemporaryID() : integer
        local id = nextTemporaryID;
        nextTemporaryID = integer.sub(nextTemporaryID, 1i);
        return id;
    end

    local function RequireRecord(records : { [integer] : any }, id : integer, label : string) : any
        local record = records[id];
        if record == nil then
            error(`VendorListModel: unknown {label} ID {id}`, 3);
        end
        return record;
    end

    local function Replace(nextSnapshot : Snapshot, rebase : boolean)
        ValidateSnapshot(nextSnapshot);
        local previous = draft;
        local previousBaseline = baseline;
        draft = BuildDraft(nextSnapshot);
        baseline = Clone(draft);
        if not rebase then
            return;
        end

        for id, record in previous.listsByID do
            if IsTemporaryID(id) then
                draft.listsByID[id] = Clone(record);
                draft.itemIDsByListID[id] = { };
            else
                ReapplyFields(record, previousBaseline.listsByID[id], draft.listsByID[id], LIST_FIELDS);
            end
        end
        for id, record in previous.itemsByID do
            if IsTemporaryID(id) then
                draft.itemsByID[id] = Clone(record);
                local ids = draft.itemIDsByListID[record.vendorListID];
                if ids == nil then
                    ids = { };
                    draft.itemIDsByListID[record.vendorListID] = ids;
                end
                if table.find(ids, id) == nil then
                    table.insert(ids, id);
                end
            else
                ReapplyFields(record, previousBaseline.itemsByID[id], draft.itemsByID[id], ITEM_FIELDS);
            end
        end
        for id, record in previous.costsByID do
            if IsTemporaryID(id) then
                draft.costsByID[id] = Clone(record);
            else
                ReapplyFields(record, previousBaseline.costsByID[id], draft.costsByID[id], COST_FIELDS);
            end
        end
        for id, record in previous.currencyComponentsByID do
            if IsTemporaryID(id) then
                local authoritativeMatch = false;
                for authoritativeID, authoritative in draft.currencyComponentsByID do
                    local newlyAuthoritative = not IsTemporaryID(authoritativeID) and previousBaseline.currencyComponentsByID[authoritativeID] == nil;
                    if newlyAuthoritative and authoritative.costID == record.costID and authoritative.currencyID == record.currencyID then
                        authoritativeMatch = true;
                        break;
                    end
                end
                if not authoritativeMatch then
                    draft.currencyComponentsByID[id] = Clone(record);
                end
            else
                ReapplyFields(record, previousBaseline.currencyComponentsByID[id], draft.currencyComponentsByID[id], CURRENCY_COMPONENT_FIELDS);
            end
        end
        for id, record in previous.itemComponentsByID do
            if IsTemporaryID(id) then
                local authoritativeMatch = false;
                for authoritativeID, authoritative in draft.itemComponentsByID do
                    local newlyAuthoritative = not IsTemporaryID(authoritativeID) and previousBaseline.itemComponentsByID[authoritativeID] == nil;
                    if newlyAuthoritative and authoritative.costID == record.costID and authoritative.itemTemplateID == record.itemTemplateID then
                        authoritativeMatch = true;
                        break;
                    end
                end
                if not authoritativeMatch then
                    draft.itemComponentsByID[id] = Clone(record);
                end
            else
                ReapplyFields(record, previousBaseline.itemComponentsByID[id], draft.itemComponentsByID[id], ITEM_COMPONENT_FIELDS);
            end
        end
        for id in previousBaseline.currencyComponentsByID do
            if previous.currencyComponentsByID[id] == nil then
                draft.currencyComponentsByID[id] = nil;
            end
        end
        for id in previousBaseline.itemComponentsByID do
            if previous.itemComponentsByID[id] == nil then
                draft.itemComponentsByID[id] = nil;
            end
        end

        for listID, previousIDs in previous.itemIDsByListID do
            local baselineIDs = previousBaseline.itemIDsByListID[listID] or { };
            local nextIDs = draft.itemIDsByListID[listID];
            if nextIDs ~= nil and not Equal(previousIDs, baselineIDs) then
                local available : { [integer] : boolean } = { };
                local rebased = { };
                for _, itemID in nextIDs do
                    available[itemID] = true;
                end
                for _, itemID in previousIDs do
                    if available[itemID] then
                        table.insert(rebased, itemID);
                        available[itemID] = nil;
                    end
                end
                for _, itemID in nextIDs do
                    if available[itemID] then
                        table.insert(rebased, itemID);
                    end
                end
                draft.itemIDsByListID[listID] = rebased;
            end
        end
    end

    function model:GetDraft() : Draft
        return Clone(draft);
    end

    function model:Reload(nextSnapshot : Snapshot)
        Replace(nextSnapshot, true);
    end

    function model:Revert()
        draft = Clone(baseline);
    end

    function model:IsDirty() : boolean
        return not Equal(draft, baseline);
    end

    function model:IsListDirty(listID : integer) : boolean
        if not Equal(draft.listsByID[listID], baseline.listsByID[listID]) or not Equal(draft.itemIDsByListID[listID] or { }, baseline.itemIDsByListID[listID] or { }) then
            return true;
        end

        for _, itemID in draft.itemIDsByListID[listID] or { } do
            local item = draft.itemsByID[itemID];
            if not Equal(item, baseline.itemsByID[itemID]) then
                return true;
            end
            if item.costID ~= 0i then
                if not Equal(draft.costsByID[item.costID], baseline.costsByID[item.costID]) then
                    return true;
                end
                for componentID, component in draft.currencyComponentsByID do
                    if component.costID == item.costID and not Equal(component, baseline.currencyComponentsByID[componentID]) then
                        return true;
                    end
                end
                for componentID, component in draft.itemComponentsByID do
                    if component.costID == item.costID and not Equal(component, baseline.itemComponentsByID[componentID]) then
                        return true;
                    end
                end
                for componentID, component in baseline.currencyComponentsByID do
                    if component.costID == item.costID and draft.currencyComponentsByID[componentID] == nil then
                        return true;
                    end
                end
                for componentID, component in baseline.itemComponentsByID do
                    if component.costID == item.costID and draft.itemComponentsByID[componentID] == nil then
                        return true;
                    end
                end
            end
        end
        return false;
    end

    function model:GetListIDs() : { integer }
        local ids = { };
        for id in draft.listsByID do
            table.insert(ids, id);
        end

        table.sort(ids, function(left, right)
            local leftName = string.lower(draft.listsByID[left].internalName);
            local rightName = string.lower(draft.listsByID[right].internalName);
            return if leftName == rightName then integer.lt(left, right) else leftName < rightName;
        end);
        return ids;
    end

    function model:GetList(listID : integer) : VendorListRecord?
        local value = draft.listsByID[listID];
        return if value then Clone(value) else nil;
    end

    function model:GetItem(itemID : integer) : VendorListItemRecord?
        local value = draft.itemsByID[itemID];
        return if value then Clone(value) else nil;
    end

    function model:GetOrderedItemIDs(listID : integer) : { integer }
        return Clone(draft.itemIDsByListID[listID] or { });
    end

    function model:GetCost(costID : integer) : CostRecord?
        local value = draft.costsByID[costID];
        return if value then Clone(value) else nil;
    end

    function model:GetCostComponents(costID : integer) : ({ CostCurrencyComponentRecord }, { CostItemComponentRecord })
        local currencies = { };
        local items = { };
        for _, component in draft.currencyComponentsByID do
            if component.costID == costID then
                table.insert(currencies, Clone(component));
            end
        end
        for _, component in draft.itemComponentsByID do
            if component.costID == costID then
                table.insert(items, Clone(component));
            end
        end

        local function Sort(left, right)
            return if left.priority == right.priority then integer.lt(left.id, right.id) else integer.gt(left.priority, right.priority);
        end

        table.sort(currencies, Sort);
        table.sort(items, Sort);
        return currencies, items;
    end

    function model:SetListFields(listID : integer, fields : { [string] : any })
        ApplyFields(RequireRecord(draft.listsByID, listID, "vendor list"), fields, LIST_FIELDS, "vendor list");
    end

    function model:SetItemFields(itemID : integer, fields : { [string] : any })
        ApplyFields(RequireRecord(draft.itemsByID, itemID, "vendor item"), fields, ITEM_FIELDS, "vendor item");
    end

    function model:SetCostFields(costID : integer, fields : { [string] : any })
        ApplyFields(RequireRecord(draft.costsByID, costID, "cost"), fields, COST_FIELDS, "cost");
    end

    function model:SetCurrencyComponentFields(componentID : integer, fields : { [string] : any })
        local component = RequireRecord(draft.currencyComponentsByID, componentID, "currency component");
        if fields.currencyID ~= nil then
            for otherID, other in draft.currencyComponentsByID do
                if otherID ~= componentID and other.costID == component.costID and other.currencyID == fields.currencyID then
                    error(`VendorListModel: cost {component.costID} already contains currency {fields.currencyID}`, 2);
                end
            end
        end

        ApplyFields(component, fields, CURRENCY_COMPONENT_FIELDS, "currency component");
    end

    function model:SetItemComponentFields(componentID : integer, fields : { [string] : any })
        local component = RequireRecord(draft.itemComponentsByID, componentID, "item component");
        if fields.itemTemplateID ~= nil then
            for otherID, other in draft.itemComponentsByID do
                if otherID ~= componentID and other.costID == component.costID and other.itemTemplateID == fields.itemTemplateID then
                    error(`VendorListModel: cost {component.costID} already contains item {fields.itemTemplateID}`, 2);
                end
            end
        end

        ApplyFields(component, fields, ITEM_COMPONENT_FIELDS, "item component");
    end

    function model:AddList(internalName : string, flags : integer) : integer
        local id = AllocateTemporaryID();
        draft.listsByID[id] = { id = id, internalName = internalName, flags = flags };
        draft.itemIDsByListID[id] = { };
        return id;
    end

    function model:DuplicateList(listID : integer, internalName : string) : integer
        local source = RequireRecord(draft.listsByID, listID, "vendor list");
        local duplicateID = model:AddList(internalName, source.flags);
        local duplicateIDs = draft.itemIDsByListID[duplicateID] :: { integer };
        for _, itemID in draft.itemIDsByListID[listID] or { } do
            local duplicateItem = Clone(RequireRecord(draft.itemsByID, itemID, "vendor item"));
            duplicateItem.id = AllocateTemporaryID();
            duplicateItem.vendorListID = duplicateID;
            draft.itemsByID[duplicateItem.id] = duplicateItem;
            table.insert(duplicateIDs, duplicateItem.id);
        end
        return duplicateID;
    end

    function model:AddItem(listID : integer, itemTemplateID : integer) : integer
        RequireRecord(draft.listsByID, listID, "vendor list");
        local ids = draft.itemIDsByListID[listID];
        if ids == nil then
            ids = { };
            draft.itemIDsByListID[listID] = ids;
        end

        local priority = if #ids == 0 then 100i else integer.sub(draft.itemsByID[ids[#ids]].priority, 100i);
        local id = AllocateTemporaryID();
        draft.itemsByID[id] = { id = id, vendorListID = listID, priority = priority, itemTemplateID = itemTemplateID, purchaseQuantity = 1i, costID = 0i, visibilityConditionSetID = 0i, enabledConditionSetID = 0i, disabledReasonTextID = 0i, flags = 0i };
        table.insert(ids, id);
        return id;
    end

    function model:RemoveTemporaryList(listID : integer)
        if not IsTemporaryID(listID) then
            error("VendorListModel: only temporary vendor lists can be removed locally", 2);
        end

        RequireRecord(draft.listsByID, listID, "vendor list");
        local possibleUnusedCosts : { [integer] : boolean } = { };
        for _, itemID in draft.itemIDsByListID[listID] or { } do
            local item = draft.itemsByID[itemID];
            if item ~= nil and IsTemporaryID(item.costID) then
                possibleUnusedCosts[item.costID] = true;
            end

            draft.itemsByID[itemID] = nil;
        end
        draft.itemIDsByListID[listID] = nil;
        draft.listsByID[listID] = nil;

        for costID in possibleUnusedCosts do
            local used = false;
            for _, item in draft.itemsByID do
                if item.costID == costID then
                    used = true;
                    break;
                end
            end

            if not used then
                draft.costsByID[costID] = nil;
                for componentID, component in draft.currencyComponentsByID do
                    if component.costID == costID then
                        draft.currencyComponentsByID[componentID] = nil;
                    end
                end

                for componentID, component in draft.itemComponentsByID do
                    if component.costID == costID then
                        draft.itemComponentsByID[componentID] = nil;
                    end
                end
            end
        end
    end

    function model:AddCost(internalName : string) : integer
        local id = AllocateTemporaryID();
        draft.costsByID[id] = { id = id, internalName = internalName };
        return id;
    end

    function model:AddCurrencyComponent(costID : integer, currencyID : integer) : integer
        RequireRecord(draft.costsByID, costID, "cost");
        local currencies, items = model:GetCostComponents(costID);
        for _, component in currencies do
            if component.currencyID == currencyID then
                error(`VendorListModel: cost {costID} already contains currency {currencyID}`, 2);
            end
        end

        local id = AllocateTemporaryID();
        draft.currencyComponentsByID[id] = { id = id, costID = costID, priority = integer.create((#currencies + #items + 1) * -100), currencyID = currencyID, amount = 1i };
        return id;
    end

    function model:AddItemComponent(costID : integer, itemTemplateID : integer) : integer
        RequireRecord(draft.costsByID, costID, "cost");
        local currencies, items = model:GetCostComponents(costID);
        for _, component in items do
            if component.itemTemplateID == itemTemplateID then
                error(`VendorListModel: cost {costID} already contains item {itemTemplateID}`, 2);
            end
        end

        local id = AllocateTemporaryID();
        draft.itemComponentsByID[id] = { id = id, costID = costID, priority = integer.create((#currencies + #items + 1) * -100), itemTemplateID = itemTemplateID, amount = 1i };
        return id;
    end

    function model:RemoveTemporaryItem(itemID : integer)
        if not IsTemporaryID(itemID) then
            error("VendorListModel: only temporary vendor items can be removed locally", 2);
        end

        local item = RequireRecord(draft.itemsByID, itemID, "vendor item");
        local ids = draft.itemIDsByListID[item.vendorListID] or { };
        local index = table.find(ids, itemID);
        if index ~= nil then
            table.remove(ids, index);
        end

        draft.itemsByID[itemID] = nil;
    end

    function model:RemoveCurrencyComponent(componentID : integer)
        RequireRecord(draft.currencyComponentsByID, componentID, "currency component");
        draft.currencyComponentsByID[componentID] = nil;
    end

    function model:RemoveItemComponent(componentID : integer)
        RequireRecord(draft.itemComponentsByID, componentID, "item component");
        draft.itemComponentsByID[componentID] = nil;
    end

    function model:RemoveTemporaryCost(costID : integer)
        if not IsTemporaryID(costID) then
            error("VendorListModel: only temporary costs can be removed locally", 2);
        end

        RequireRecord(draft.costsByID, costID, "cost");
        for _, item in draft.itemsByID do
            if item.costID == costID then
                error(`VendorListModel: temporary cost {costID} is still referenced`, 2);
            end
        end

        draft.costsByID[costID] = nil;
        for componentID, component in draft.currencyComponentsByID do
            if component.costID == costID then
                draft.currencyComponentsByID[componentID] = nil;
            end
        end

        for componentID, component in draft.itemComponentsByID do
            if component.costID == costID then
                draft.itemComponentsByID[componentID] = nil;
            end
        end
    end

    function model:ResolveTemporaryCost(temporaryCostID : integer, costID : integer)
        if not IsTemporaryID(temporaryCostID) or IsTemporaryID(costID) then
            error("VendorListModel: resolving a cost requires a temporary source ID and authoritative destination ID", 2);
        end

        RequireRecord(draft.costsByID, temporaryCostID, "temporary cost");
        RequireRecord(draft.costsByID, costID, "authoritative cost");

        for _, item in draft.itemsByID do
            if item.costID == temporaryCostID then
                item.costID = costID;
            end
        end

        draft.costsByID[temporaryCostID] = nil;
        for componentID, component in draft.currencyComponentsByID do
            if component.costID == temporaryCostID then
                draft.currencyComponentsByID[componentID] = nil;
            end
        end

        for componentID, component in draft.itemComponentsByID do
            if component.costID == temporaryCostID then
                draft.itemComponentsByID[componentID] = nil;
            end
        end
    end

    function model:AcceptAppliedList(draftListID : integer, authoritativeListID : integer)
        local authoritativeList = RequireRecord(baseline.listsByID, authoritativeListID, "authoritative vendor list");
        if IsTemporaryID(draftListID) then
            model:RemoveTemporaryList(draftListID);
        else
            for itemID, item in draft.itemsByID do
                if item.vendorListID == draftListID then
                    draft.itemsByID[itemID] = nil;
                end
            end
            draft.itemIDsByListID[draftListID] = nil;
        end

        draft.listsByID[authoritativeListID] = Clone(authoritativeList);
        draft.itemIDsByListID[authoritativeListID] = Clone(baseline.itemIDsByListID[authoritativeListID] or { });
        for _, itemID in draft.itemIDsByListID[authoritativeListID] do
            draft.itemsByID[itemID] = Clone(baseline.itemsByID[itemID]);
        end
    end

    function model:ReorderItems(listID : integer, sourceIndex : number, destinationIndex : number)
        local ids = draft.itemIDsByListID[listID] or { };
        if sourceIndex < 1 or sourceIndex > #ids or destinationIndex < 1 or destinationIndex > #ids or sourceIndex == destinationIndex then
            return;
        end

        local movedID = table.remove(ids, sourceIndex);
        table.insert(ids, destinationIndex, movedID);
        for index, itemID in ids do
            draft.itemsByID[itemID].priority = integer.create((#ids - index + 1) * 100);
        end
    end

    function model:GetDirtyOperations(listID : integer) : { any }
        local operations = { };
        local list = draft.listsByID[listID];
        if list == nil then
            return operations;
        end

        local temporaryList = IsTemporaryID(listID);
        if not temporaryList and not Equal(list, baseline.listsByID[listID]) then
            table.insert(operations, { kind = "UpdateList", value = Clone(list) });
        end

        local relevantCosts : { [integer] : boolean } = { };
        for _, itemID in draft.itemIDsByListID[listID] or { } do
            local item = draft.itemsByID[itemID];
            if item.costID ~= 0i then
                relevantCosts[item.costID] = true;
            end
        end

        for costID in relevantCosts do
            local cost = draft.costsByID[costID];
            if cost ~= nil and IsTemporaryID(costID) then
                local currencyComponents = { };
                local itemComponents = { };
                for _, component in draft.currencyComponentsByID do
                    if component.costID == costID then
                        table.insert(currencyComponents, Clone(component));
                    end
                end
                for _, component in draft.itemComponentsByID do
                    if component.costID == costID then
                        table.insert(itemComponents, Clone(component));
                    end
                end
                table.sort(currencyComponents, function(a : any, b : any)
                    return integer.gt(a.priority, b.priority);
                end);
                table.sort(itemComponents, function(a : any, b : any)
                    return integer.gt(a.priority, b.priority);
                end);
                table.insert(operations, { kind = "CreateCost", temporaryID = costID, value = Clone(cost), currencyComponents = currencyComponents, itemComponents = itemComponents });
            else
                if cost ~= nil and not Equal(cost, baseline.costsByID[costID]) then
                    table.insert(operations, { kind = "UpdateCost", value = Clone(cost) });
                end
                for componentID, component in baseline.currencyComponentsByID do
                    if component.costID == costID and draft.currencyComponentsByID[componentID] == nil then
                        table.insert(operations, { kind = "DeleteCurrency", value = Clone(component) });
                    end
                end
                for componentID, component in baseline.itemComponentsByID do
                    if component.costID == costID and draft.itemComponentsByID[componentID] == nil then
                        table.insert(operations, { kind = "DeleteCostItem", value = Clone(component) });
                    end
                end
                for componentID, component in draft.currencyComponentsByID do
                    if component.costID == costID and IsTemporaryID(componentID) then
                        table.insert(operations, { kind = "CreateCurrency", temporaryID = componentID, value = Clone(component) });
                    elseif component.costID == costID and not Equal(component, baseline.currencyComponentsByID[componentID]) then
                        table.insert(operations, { kind = "UpdateCurrency", value = Clone(component) });
                    end
                end
                for componentID, component in draft.itemComponentsByID do
                    if component.costID == costID and IsTemporaryID(componentID) then
                        table.insert(operations, { kind = "CreateCostItem", temporaryID = componentID, value = Clone(component) });
                    elseif component.costID == costID and not Equal(component, baseline.itemComponentsByID[componentID]) then
                        table.insert(operations, { kind = "UpdateCostItem", value = Clone(component) });
                    end
                end
            end
        end

        if temporaryList then
            local items = { };
            for _, itemID in draft.itemIDsByListID[listID] or { } do
                table.insert(items, Clone(draft.itemsByID[itemID]));
            end
            table.insert(operations, { kind = "CreateList", temporaryID = listID, value = Clone(list), items = items });
        else
            for _, itemID in draft.itemIDsByListID[listID] or { } do
                local item = draft.itemsByID[itemID];
                if IsTemporaryID(itemID) then
                    table.insert(operations, { kind = "CreateItem", temporaryID = itemID, value = Clone(item) });
                elseif not Equal(item, baseline.itemsByID[itemID]) then
                    table.insert(operations, { kind = "UpdateItem", value = Clone(item) });
                end
            end
        end

        return operations;
    end

    return model;
end

return modelAPI;
