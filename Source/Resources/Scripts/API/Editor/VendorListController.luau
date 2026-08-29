local MUTATION_CREATE = 0i;
local ARTIFACT_VENDOR_LIST = 15i;
local ARTIFACT_VENDOR_LIST_ITEM = 16i;

export type Snapshot = { [string] : any };
export type StatusLevelValue = { [string] : any };
export type StatusLevelTable = { Muted : StatusLevelValue, Success : StatusLevelValue, Warning : StatusLevelValue, Error : StatusLevelValue };

export type VendorListController =
{
    GetSnapshot : (self : VendorListController) -> Snapshot,
    Refresh : (self : VendorListController) -> Snapshot,
    Update : (self : VendorListController) -> boolean,
    RequestSnapshot : (self : VendorListController) -> boolean,
    Submit : (self : VendorListController, requestID : integer) -> (),
    TakeCompletedResult : (self : VendorListController) -> any?,
    SelectList : (self : VendorListController, listID : integer?) -> (),
    SelectItem : (self : VendorListController, itemID : integer?) -> (),
    GetSelectedListID : (self : VendorListController) -> integer?,
    GetSelectedItemID : (self : VendorListController) -> integer?,
    SetStatus : (self : VendorListController, message : string, level : StatusLevelValue?) -> (),
    GetStatus : (self : VendorListController) -> (string, StatusLevelValue),
    IsReady : (self : VendorListController) -> boolean,
    GetPendingRequestID : (self : VendorListController) -> integer,
    IsAwaitingSnapshot : (self : VendorListController) -> boolean,
};

export type VendorListControllerAPI = { StatusLevel : StatusLevelTable, New : () -> VendorListController };

local function EmptySnapshot() : Snapshot
    return {
        dataState = "Unavailable",
        revision = 0i,
        texts = { },
        conditionSets = { },
        costs = { },
        costCurrencyComponents = { },
        costItemComponents = { },
        vendorLists = { },
        vendorListItems = { },
        creatureTemplateVendor = { },
        itemTemplates = { },
        currencies = { }
    };
end

local function FindByID(values : { any }, id : integer?)
    if id == nil then
        return nil;
    end

    for _, value in values do
        if value.id == id then
            return value;
        end
    end

    return nil;
end

local statusLevels : StatusLevelTable = { Muted = { }, Success = { }, Warning = { }, Error = { } };
local validStatusLevels : { [any] : boolean } = { [statusLevels.Muted] = true, [statusLevels.Success] = true, [statusLevels.Warning] = true, [statusLevels.Error] = true };
local controllerAPI : VendorListControllerAPI = { StatusLevel = statusLevels };

local function RequireStatusLevel(level : StatusLevelValue?) : StatusLevelValue
    if level == nil then
        return statusLevels.Muted;
    end

    if not validStatusLevels[level] then
        error("VendorListController: invalid status level", 3);
    end

    return level;
end

function controllerAPI.New() : VendorListController
    local controller : any = { };
    local snapshot = EmptySnapshot();
    local selectedListID : integer? = nil;
    local selectedItemID : integer? = nil;
    local pendingRequestID = 0i;
    local completedResult : any? = nil;
    local awaitingSnapshot = false;
    local statusMessage = "Select a vendor list to edit it.";
    local statusLevel = statusLevels.Muted;
    local lastState = "Unavailable";
    local lastRevision = 0i;
    local recoveryRequested = false;

    function controller:GetSnapshot() : Snapshot
        return snapshot;
    end

    function controller:Refresh() : Snapshot
        snapshot = InteractionEditor.GetSnapshot();
        local state = InteractionEditor.GetState();
        if awaitingSnapshot and snapshot.dataState == "Ready" then
            awaitingSnapshot = false;
            recoveryRequested = false;
            statusMessage = "Authoritative vendor data refreshed.";
            statusLevel = statusLevels.Success;
        elseif awaitingSnapshot and snapshot.dataState == "Failed" then
            awaitingSnapshot = false;
            statusMessage = "The authoritative vendor data refresh failed.";
            statusLevel = statusLevels.Error;
        end

        if snapshot.dataState == "Ready" and state == "Ready" then
            if FindByID(snapshot.vendorLists, selectedListID) == nil then
                selectedListID = nil;
                selectedItemID = nil;
            end

            local selectedItem = FindByID(snapshot.vendorListItems, selectedItemID);
            if selectedItem == nil or selectedItem.vendorListID ~= selectedListID then
                selectedItemID = nil;
            end
        end

        lastState = state;
        lastRevision = InteractionEditor.GetRevision();
        return snapshot;
    end

    function controller:Update() : boolean
        local shouldRefresh = false;
        if pendingRequestID ~= 0i then
            local result = InteractionEditor.TakeMutationResult(pendingRequestID);
            if result ~= nil then
                pendingRequestID = 0i;
                completedResult = result;
                if result.succeeded then
                    if result.mutationType == MUTATION_CREATE and result.artifact == ARTIFACT_VENDOR_LIST then
                        selectedListID = result.artifactID;
                        selectedItemID = nil;
                    elseif result.mutationType == MUTATION_CREATE and result.artifact == ARTIFACT_VENDOR_LIST_ITEM then
                        selectedItemID = result.artifactID;
                    end

                    local confirmation = if result.response ~= "" then result.response else "The server confirmed the mutation.";
                    statusMessage = `{confirmation} Revision {result.revision}.`;
                    statusLevel = statusLevels.Success;
                else
                    statusMessage = if result.response ~= "" then result.response else "The server rejected the mutation.";
                    statusLevel = statusLevels.Error;
                end
                shouldRefresh = true;
            end
        end

        local state = InteractionEditor.GetState();
        local revision = InteractionEditor.GetRevision();
        if revision ~= lastRevision then
            lastRevision = revision;
            shouldRefresh = true;
        end

        if state == "Failed" and not awaitingSnapshot and not recoveryRequested then
            recoveryRequested = true;
            awaitingSnapshot = InteractionEditor.RequestSnapshot();
            statusMessage = if awaitingSnapshot then "Recovering authoritative vendor data..." else "The vendor change-set stream failed and recovery could not start.";
            statusLevel = if awaitingSnapshot then statusLevels.Warning else statusLevels.Error;
        end

        if state ~= lastState or (awaitingSnapshot and snapshot.dataState == "Ready") then
            lastState = state;
            shouldRefresh = true;
        end

        return shouldRefresh;
    end

    function controller:RequestSnapshot() : boolean
        local requested = InteractionEditor.RequestSnapshot();
        if requested then
            awaitingSnapshot = true;
            recoveryRequested = false;
            statusMessage = "Requesting authoritative vendor data...";
            statusLevel = statusLevels.Muted;
        else
            statusMessage = "The vendor snapshot request could not be submitted.";
            statusLevel = statusLevels.Error;
        end

        return requested;
    end

    function controller:Submit(requestID : integer)
        pendingRequestID = requestID;
        completedResult = nil;
        if requestID == 0i then
            statusMessage = "The mutation could not be submitted.";
            statusLevel = statusLevels.Error;
        else
            statusMessage = `Waiting for server response (request #{requestID})...`;
            statusLevel = statusLevels.Muted;
        end
    end

    function controller:TakeCompletedResult()
        local result = completedResult;
        completedResult = nil;
        return result;
    end

    function controller:SelectList(listID : integer?)
        selectedListID = listID;
        selectedItemID = nil;
    end

    function controller:SelectItem(itemID : integer?)
        selectedItemID = itemID;
    end

    function controller:GetSelectedListID() : integer?
        return selectedListID;
    end

    function controller:GetSelectedItemID() : integer?
        return selectedItemID;
    end

    function controller:SetStatus(message : string, level : StatusLevelValue?)
        statusMessage = message;
        statusLevel = RequireStatusLevel(level);
    end

    function controller:GetStatus() : (string, StatusLevelValue)
        return statusMessage, statusLevel;
    end

    function controller:IsReady() : boolean
        return snapshot.dataState == "Ready" and pendingRequestID == 0i and not awaitingSnapshot;
    end

    function controller:GetPendingRequestID() : integer
        return pendingRequestID;
    end

    function controller:IsAwaitingSnapshot() : boolean
        return awaitingSnapshot;
    end

    controller:Refresh();
    return controller;
end

return controllerAPI;
