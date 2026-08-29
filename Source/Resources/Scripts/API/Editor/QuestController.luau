local MUTATION_CREATE = 0i;
local ARTIFACT_QUEST_TEMPLATE = 18i;

export type Snapshot = { [string] : any };
export type StatusLevelValue = { [string] : any };
export type StatusLevelTable = { Muted : StatusLevelValue, Success : StatusLevelValue, Warning : StatusLevelValue, Error : StatusLevelValue };

export type QuestController =
{
    GetSnapshot : (self : QuestController) -> Snapshot,
    Refresh : (self : QuestController) -> Snapshot,
    Update : (self : QuestController) -> boolean,
    RequestSnapshot : (self : QuestController) -> boolean,
    Submit : (self : QuestController, requestID : integer) -> (),
    TakeCompletedResult : (self : QuestController) -> any?,
    SelectQuest : (self : QuestController, questID : integer?) -> (),
    GetSelectedQuestID : (self : QuestController) -> integer?,
    SetStatus : (self : QuestController, message : string, level : StatusLevelValue?) -> (),
    GetStatus : (self : QuestController) -> (string, StatusLevelValue),
    IsReady : (self : QuestController) -> boolean,
    IsAwaitingSnapshot : (self : QuestController) -> boolean,
};

export type QuestControllerAPI = { StatusLevel : StatusLevelTable, New : () -> QuestController };

local function EmptySnapshot() : Snapshot
    return {
        dataState = "Unavailable", revision = 0i,
        texts = { }, translations = { }, locales = { }, conditionSets = { }, creatureTemplates = { }, itemTemplates = { }, currencies = { }, factions = { }, questObjectiveTypes = { },
        creatureTemplateInteractions = { }, creatureTemplateGossip = { }, creatureTemplateVendor = { },
        questTemplates = { }, questPrerequisites = { }, creatureTemplateQuestGivers = { }, creatureTemplateQuests = { }, questObjectives = { }, questObjectiveTargets = { },
        questRewardGroups = { }, questRewardItems = { }, questRewardCurrencies = { }, questRewardReputations = { }, questRewardExperience = { },
    };
end

local function FindQuest(snapshot : Snapshot, questID : integer?) : any?
    if questID == nil then
        return nil;
    end
    for _, quest in snapshot.questTemplates do
        if quest.id == questID then
            return quest;
        end
    end

    return nil;
end

local statusLevels : StatusLevelTable = { Muted = { }, Success = { }, Warning = { }, Error = { } };
local validStatusLevels : { [any] : boolean } = { [statusLevels.Muted] = true, [statusLevels.Success] = true, [statusLevels.Warning] = true, [statusLevels.Error] = true };
local controllerAPI : QuestControllerAPI = { StatusLevel = statusLevels };

local function RequireStatusLevel(level : StatusLevelValue?) : StatusLevelValue
    if level == nil then
        return statusLevels.Muted;
    end
    if not validStatusLevels[level] then
        error("QuestController: invalid status level", 3);
    end

    return level;
end

function controllerAPI.New() : QuestController
    local controller : any = { };
    local snapshot = EmptySnapshot();
    local selectedQuestID : integer? = nil;
    local pendingRequestID = 0i;
    local completedResult : any? = nil;
    local awaitingSnapshot = false;
    local recoveryRequested = false;
    local statusMessage = "Select a quest to edit it.";
    local statusLevel = statusLevels.Muted;
    local lastState = "Unavailable";
    local lastRevision = 0i;

    function controller:GetSnapshot() : Snapshot
        return snapshot;
    end

    function controller:Refresh() : Snapshot
        snapshot = InteractionEditor.GetSnapshot();
        local state = InteractionEditor.GetState();
        if awaitingSnapshot and snapshot.dataState == "Ready" then
            awaitingSnapshot = false;
            recoveryRequested = false;
            statusMessage = "Authoritative quest data refreshed.";
            statusLevel = statusLevels.Success;
        elseif awaitingSnapshot and snapshot.dataState == "Failed" then
            awaitingSnapshot = false;
            statusMessage = "The authoritative quest data refresh failed.";
            statusLevel = statusLevels.Error;
        end

        if snapshot.dataState == "Ready" and state == "Ready" and FindQuest(snapshot, selectedQuestID) == nil then
            selectedQuestID = nil;
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
                    if result.mutationType == MUTATION_CREATE and result.artifact == ARTIFACT_QUEST_TEMPLATE then
                        selectedQuestID = result.artifactID;
                    end
                    local confirmation = if result.response ~= "" then result.response else "The server confirmed the quest mutation.";
                    statusMessage = `{confirmation} Revision {result.revision}.`;
                    statusLevel = statusLevels.Success;
                else
                    statusMessage = if result.response ~= "" then result.response else "The server rejected the quest mutation.";
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
            statusMessage = if awaitingSnapshot then "Recovering authoritative quest data..." else "The quest change-set stream failed and recovery could not start.";
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
            statusMessage = "Requesting authoritative quest data...";
            statusLevel = statusLevels.Muted;
        else
            statusMessage = "The quest snapshot request could not be submitted.";
            statusLevel = statusLevels.Error;
        end

        return requested;
    end

    function controller:Submit(requestID : integer)
        pendingRequestID = requestID;
        completedResult = nil;
        if requestID == 0i then
            statusMessage = "The quest mutation could not be submitted.";
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

    function controller:SelectQuest(questID : integer?)
        selectedQuestID = questID;
    end

    function controller:GetSelectedQuestID() : integer?
        return selectedQuestID;
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

    function controller:IsAwaitingSnapshot() : boolean
        return awaitingSnapshot;
    end

    controller:Refresh();
    return controller;
end

return controllerAPI;
