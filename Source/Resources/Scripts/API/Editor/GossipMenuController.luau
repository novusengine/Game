local TextUtil = require("@src/API/UI/TextUtil");

local MUTATION_CREATE = 0i;
local ARTIFACT_LOCALIZED_TEXT = 0i;
local ARTIFACT_LOCALIZED_TEXT_TRANSLATION = 1i;
local ARTIFACT_GOSSIP_MENU = 7i;
local ARTIFACT_GOSSIP_MENU_OPTION = 8i;

export type Snapshot = { [string] : any };

export type StatusLevelValue = { [string] : any };

export type StatusLevelTable =
{
    Muted : StatusLevelValue,
    Success : StatusLevelValue,
    Warning : StatusLevelValue,
    Error : StatusLevelValue,
};

export type GossipMenuController = {
    GetSnapshot : (self : GossipMenuController) -> Snapshot,
    Refresh : (self : GossipMenuController) -> Snapshot,
    Update : (self : GossipMenuController) -> boolean,
    RequestSnapshot : (self : GossipMenuController) -> boolean,
    Submit : (self : GossipMenuController, requestID : integer) -> (),
    QueueSelection : (self : GossipMenuController, menuID : integer?, optionID : integer?) -> (),
    SelectMenu : (self : GossipMenuController, menuID : integer?) -> (),
    SelectOption : (self : GossipMenuController, optionID : integer?) -> (),
    GetSelectedMenuID : (self : GossipMenuController) -> integer?,
    GetSelectedOptionID : (self : GossipMenuController) -> integer?,
    FindMenu : (self : GossipMenuController, menuID : integer?) -> any?,
    FindOption : (self : GossipMenuController, optionID : integer?) -> any?,
    FindText : (self : GossipMenuController, textID : integer?) -> any?,
    FindActionDescriptor : (self : GossipMenuController, actionType : integer?) -> any?,
    FormatIdentifier : (self : GossipMenuController, value : string) -> string,
    TextOptions : (self : GossipMenuController, includeNone : boolean) -> { any },
    ConditionOptions : (self : GossipMenuController) -> { any },
    MenuOptions : (self : GossipMenuController) -> { any },
    ActionOptions : (self : GossipMenuController) -> { any },
    DefaultAction : (self : GossipMenuController) -> any?,
    ReadUnsigned : (self : GossipMenuController, text : string, maximum : number) -> integer?,
    SetStatus : (self : GossipMenuController, message : string, level : StatusLevelValue?) -> (),
    GetStatus : (self : GossipMenuController) -> (string, StatusLevelValue),
    IsReady : (self : GossipMenuController) -> boolean,
    GetPendingRequestID : (self : GossipMenuController) -> integer,
    IsAwaitingSnapshot : (self : GossipMenuController) -> boolean,
};

export type GossipMenuControllerAPI =
{
    StatusLevel : StatusLevelTable,
    New : () -> GossipMenuController,
};

local function EmptySnapshot() : Snapshot
    return { dataState = "Unavailable", revision = 0i, texts = {}, conditionSets = {}, gossipActionDescriptors = {}, gossipMenus = {}, gossipMenuOptions = {}, creatureTemplates = {}, creatureTemplateGossip = {} };
end

local function FindByID(values : { any }, id : integer?)
    if id == nil then return nil; end
    for _, value in values do
        if value.id == id then return value; end
    end
    return nil;
end

local function FormatIdentifier(value : string) : string
    local spaced = string.gsub(value, "(%l)(%u)", "%1 %2");
    spaced = string.gsub(spaced, "_", " ");
    return string.upper(string.sub(spaced, 1, 1)) .. string.sub(spaced, 2);
end

local statusLevels : StatusLevelTable =
{
    Muted = { },
    Success = { },
    Warning = { },
    Error = { },
};

local validStatusLevels : { [any] : boolean } =
{
    [statusLevels.Muted] = true,
    [statusLevels.Success] = true,
    [statusLevels.Warning] = true,
    [statusLevels.Error] = true,
};

local controllerAPI : GossipMenuControllerAPI = { StatusLevel = statusLevels };
local function RequireStatusLevel(level : StatusLevelValue?) : StatusLevelValue
    if level == nil then return statusLevels.Muted; end
    if not validStatusLevels[level] then error("GossipMenuController: status level must be GossipMenuController.StatusLevel.Muted, .Success, .Warning, or .Error", 3); end
    return level;
end

function controllerAPI.New() : GossipMenuController
    local controller : any = { };
    local snapshot = EmptySnapshot();
    local selectedMenuID : integer? = nil;
    local selectedOptionID : integer? = nil;
    local pendingRequestID = 0i;
    local awaitingSnapshot = false;
    local selectMenuAfterSnapshot : integer? = nil;
    local selectOptionAfterSnapshot : integer? = nil;
    local hasQueuedMenuSelection = false;
    local hasQueuedOptionSelection = false;
    local statusMessage = "Select a gossip menu to edit it.";
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
            statusMessage = "Authoritative gossip data refreshed.";
            statusLevel = statusLevels.Success;
        elseif awaitingSnapshot and snapshot.dataState == "Failed" then
            awaitingSnapshot = false;
            statusMessage = "The authoritative gossip data refresh failed.";
            statusLevel = statusLevels.Error;
        end
        if snapshot.dataState == "Ready" and state == "Ready" then
            local queuedMenu = FindByID(snapshot.gossipMenus, selectMenuAfterSnapshot);
            if hasQueuedMenuSelection and (selectMenuAfterSnapshot == nil or queuedMenu ~= nil) then
                selectedMenuID = selectMenuAfterSnapshot; selectMenuAfterSnapshot = nil; hasQueuedMenuSelection = false;
            end
            local queuedOption = FindByID(snapshot.gossipMenuOptions, selectOptionAfterSnapshot);
            if hasQueuedOptionSelection and (selectOptionAfterSnapshot == nil or queuedOption ~= nil) then
                selectedOptionID = selectOptionAfterSnapshot; selectOptionAfterSnapshot = nil; hasQueuedOptionSelection = false;
                if queuedOption ~= nil then selectedMenuID = queuedOption.menuID; end
            end
            if FindByID(snapshot.gossipMenus, selectedMenuID) == nil then selectedMenuID = nil; selectedOptionID = nil; end
            if FindByID(snapshot.gossipMenuOptions, selectedOptionID) == nil then selectedOptionID = nil; end
        end
        lastState = state;
        lastRevision = InteractionEditor.GetRevision();
        return snapshot;
    end

    function controller:Update() : boolean
        local shouldRefresh = false;
        if pendingRequestID ~= 0i then
            local result = InteractionEditor.TakeMutationResult(pendingRequestID);
            if result then
                pendingRequestID = 0i;
                if result.succeeded then
                    if result.artifact == ARTIFACT_LOCALIZED_TEXT or result.artifact == ARTIFACT_LOCALIZED_TEXT_TRANSLATION or
                        result.artifact == ARTIFACT_GOSSIP_MENU or result.artifact == ARTIFACT_GOSSIP_MENU_OPTION then
                        if result.mutationType == MUTATION_CREATE and result.artifact == ARTIFACT_GOSSIP_MENU then
                            selectMenuAfterSnapshot = result.artifactID; selectOptionAfterSnapshot = nil; hasQueuedMenuSelection = true; hasQueuedOptionSelection = true;
                        elseif result.mutationType == MUTATION_CREATE and result.artifact == ARTIFACT_GOSSIP_MENU_OPTION then selectOptionAfterSnapshot = result.artifactID; hasQueuedOptionSelection = true; end
                        local confirmation = if result.response ~= "" then result.response else "The server confirmed the mutation.";
                        statusMessage = `{confirmation} Revision {result.revision}.`;
                        statusLevel = statusLevels.Success;
                    else
                        awaitingSnapshot = InteractionEditor.RequestSnapshot();
                        statusMessage = if awaitingSnapshot then "Mutation accepted; refreshing authoritative data..." else "Mutation accepted, but refresh could not start.";
                        statusLevel = if awaitingSnapshot then statusLevels.Success else statusLevels.Warning;
                    end
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
            statusMessage = if awaitingSnapshot then "Recovering authoritative gossip data..." else "The gossip change-set stream failed and recovery could not start.";
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
            statusMessage = "Requesting authoritative gossip data...";
            statusLevel = statusLevels.Muted;
        else
            statusMessage = "The gossip snapshot request could not be submitted.";
            statusLevel = statusLevels.Error;
        end
        return requested;
    end

    function controller:Submit(requestID : integer)
        pendingRequestID = requestID;
        if requestID == 0i then
            statusMessage = "The mutation could not be submitted.";
            statusLevel = statusLevels.Error;
        else
            statusMessage = `Waiting for server response (request #{requestID})...`;
            statusLevel = statusLevels.Muted;
        end
    end

    function controller:QueueSelection(menuID : integer?, optionID : integer?)
        selectMenuAfterSnapshot = menuID;
        selectOptionAfterSnapshot = optionID;
        hasQueuedMenuSelection = true;
        hasQueuedOptionSelection = true;
    end

    function controller:SelectMenu(menuID : integer?)
        selectedMenuID = menuID;
        selectedOptionID = nil;
    end

    function controller:SelectOption(optionID : integer?)
        selectedOptionID = optionID;
    end

    function controller:GetSelectedMenuID() : integer? return selectedMenuID; end
    function controller:GetSelectedOptionID() : integer? return selectedOptionID; end
    function controller:FindMenu(menuID : integer?) return FindByID(snapshot.gossipMenus, menuID); end
    function controller:FindOption(optionID : integer?) return FindByID(snapshot.gossipMenuOptions, optionID); end
    function controller:FindText(textID : integer?) return FindByID(snapshot.texts, textID); end

    function controller:FindActionDescriptor(actionType : integer?)
        if actionType == nil then return nil; end
        for _, descriptor in snapshot.gossipActionDescriptors do
            if descriptor.type == actionType then return descriptor; end
        end
        return nil;
    end

    function controller:FormatIdentifier(value : string) : string
        return FormatIdentifier(value);
    end

    function controller:TextOptions(includeNone : boolean) : { any }
        local options = {};
        if includeNone then table.insert(options, { label = "None", value = 0i }); end
        for _, text in snapshot.texts do
            if text.id ~= 0i then table.insert(options, { label = `{text.internalName} - {TextUtil.Truncate(text.englishValue, 48)}`, value = text.id }); end
        end
        table.sort(options, function(left, right)
            if left.value == 0i and right.value == 0i then return false; end
            if left.value == 0i then return true; end
            if right.value == 0i then return false; end
            return string.lower(left.label) < string.lower(right.label);
        end);
        return options;
    end

    function controller:ConditionOptions() : { any }
        local options = { { label = "None", value = 0i } };
        for _, set in snapshot.conditionSets do
            if set.id ~= 0i then table.insert(options, { label = set.internalName, value = set.id }); end
        end
        table.sort(options, function(left, right)
            if left.value == 0i and right.value == 0i then return false; end
            if left.value == 0i then return true; end
            if right.value == 0i then return false; end
            return string.lower(left.label) < string.lower(right.label);
        end);
        return options;
    end

    function controller:MenuOptions() : { any }
        local options = {};
        for _, menu in snapshot.gossipMenus do
            if menu.id ~= 0i then table.insert(options, { label = menu.internalName, value = menu.id }); end
        end
        table.sort(options, function(left, right) return string.lower(left.label) < string.lower(right.label); end);
        return options;
    end

    function controller:ActionOptions() : { any }
        local options = {};
        for _, descriptor in snapshot.gossipActionDescriptors do table.insert(options, { label = FormatIdentifier(descriptor.name), value = descriptor.type }); end
        return options;
    end

    function controller:DefaultAction()
        for _, descriptor in snapshot.gossipActionDescriptors do
            if descriptor.name == "Close" then return descriptor; end
        end
        return snapshot.gossipActionDescriptors[1];
    end

    function controller:ReadUnsigned(text : string, maximum : number) : integer?
        local value = tonumber(text);
        if value == nil or value < 0 or value > maximum or math.floor(value) ~= value then return nil; end
        return integer.create(value);
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

    function controller:GetPendingRequestID() : integer return pendingRequestID; end
    function controller:IsAwaitingSnapshot() : boolean return awaitingSnapshot; end

    controller:Refresh();
    return controller;
end

return controllerAPI;
