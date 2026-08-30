export type Snapshot = { [string] : any };

export type QuestModel =
{
    Reload : (self : QuestModel, snapshot : Snapshot) -> (),
    Revert : (self : QuestModel) -> (),
    IsDirty : (self : QuestModel) -> boolean,
    IsQuestDirty : (self : QuestModel, questID : integer) -> boolean,
    GetQuestIDs : (self : QuestModel) -> { integer },
    GetQuest : (self : QuestModel, questID : integer) -> any?,
    GetPrerequisites : (self : QuestModel, questID : integer) -> { any },
    GetAssociations : (self : QuestModel, questID : integer) -> { any },
    GetObjectives : (self : QuestModel, questID : integer) -> { any },
    GetTargets : (self : QuestModel, objectiveID : integer) -> { any },
    GetRewardGroups : (self : QuestModel, questID : integer) -> { any },
    GetRewards : (self : QuestModel, rewardGroupID : integer) -> { any },
    ReorderObjectives : (self : QuestModel, questID : integer, orderedObjectiveIDs : { integer }) -> (),
    ReorderRewards : (self : QuestModel, rewardGroupID : integer, orderedRewards : { any }) -> (),
    SetQuestFields : (self : QuestModel, questID : integer, fields : { [string] : any }) -> (),
    SetPrerequisiteFields : (self : QuestModel, prerequisiteID : integer, fields : { [string] : any }) -> (),
    SetAssociationFields : (self : QuestModel, associationID : integer, fields : { [string] : any }) -> (),
    SetObjectiveFields : (self : QuestModel, objectiveID : integer, fields : { [string] : any }) -> (),
    SetTargetFields : (self : QuestModel, targetID : integer, fields : { [string] : any }) -> (),
    SetRewardGroupFields : (self : QuestModel, rewardGroupID : integer, fields : { [string] : any }) -> (),
    SetRewardFields : (self : QuestModel, rewardKind : string, rewardID : integer, fields : { [string] : any }) -> (),
    AddQuest : (self : QuestModel, internalName : string) -> integer,
    DuplicateQuest : (self : QuestModel, questID : integer, internalName : string) -> integer,
    AddPrerequisite : (self : QuestModel, questID : integer, prerequisiteQuestID : integer) -> integer,
    AddAssociation : (self : QuestModel, questID : integer, creatureTemplateID : integer) -> integer,
    AddObjective : (self : QuestModel, questID : integer, objectiveType : integer) -> integer,
    AddTarget : (self : QuestModel, objectiveID : integer, targetID : integer) -> integer,
    AddRewardGroup : (self : QuestModel, questID : integer) -> integer,
    AddReward : (self : QuestModel, rewardGroupID : integer, rewardKind : string, targetID : integer?) -> integer,
    RemoveQuest : (self : QuestModel, questID : integer) -> (),
    RemovePrerequisite : (self : QuestModel, prerequisiteID : integer) -> (),
    RemoveAssociation : (self : QuestModel, associationID : integer) -> (),
    RemoveObjective : (self : QuestModel, objectiveID : integer) -> (),
    RemoveTarget : (self : QuestModel, targetID : integer) -> (),
    RemoveRewardGroup : (self : QuestModel, rewardGroupID : integer) -> (),
    RemoveReward : (self : QuestModel, rewardKind : string, rewardID : integer) -> (),
    ResolveTemporaryID : (self : QuestModel, kind : string, temporaryID : integer, authoritativeID : integer) -> (),
    GetOperations : (self : QuestModel, questID : integer) -> { any },
};

export type QuestModelAPI = { New : (snapshot : Snapshot) -> QuestModel };

local QUEST_FIELDS = { internalName = true, titleTextID = true, summaryTextID = true, detailsTextID = true, logTextID = true, progressTextID = true, completionTextID = true, visibilityConditionSetID = true, availabilityConditionSetID = true, unavailableReasonTextID = true, level = true, flags = true };
local PREREQUISITE_FIELDS = { prerequisiteQuestID = true };
local ASSOCIATION_FIELDS = { creatureTemplateID = true, roleFlags = true, priority = true, visibilityConditionSetID = true, enabledConditionSetID = true, flags = true };
local OBJECTIVE_FIELDS = { priority = true, objectiveType = true, requiredCount = true, textID = true, flags = true };
local TARGET_FIELDS = { priority = true, targetID = true, contribution = true };
local REWARD_GROUP_FIELDS = { priority = true, selectionCount = true, flags = true };
local REWARD_FIELDS = { priority = true, itemTemplateID = true, quantity = true, currencyID = true, factionID = true, amount = true, flags = true };
local REWARD_COLLECTIONS = { Item = "rewardItemsByID", Currency = "rewardCurrenciesByID", Reputation = "rewardReputationsByID", Experience = "rewardExperienceByID" };

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
    local required = { "creatureTemplateInteractions", "creatureTemplateGossip", "creatureTemplateVendor", "questTemplates", "questPrerequisites", "creatureTemplateQuestGivers", "creatureTemplateQuests", "questObjectives", "questObjectiveTargets", "questRewardGroups", "questRewardItems", "questRewardCurrencies", "questRewardReputations", "questRewardExperience" };
    if type(snapshot) ~= "table" then
        error("QuestModel: snapshot must be a table", 3);
    end
    for _, key in required do
        if type(snapshot[key]) ~= "table" then
            error(`QuestModel: snapshot collection '{key}' must be a table`, 3);
        end
    end
end

local function SortRecords(records : { any })
    table.sort(records, function(left : any, right : any)
        local leftPriority = left.priority or 0i;
        local rightPriority = right.priority or 0i;
        if leftPriority == rightPriority then
            return integer.lt(left.id, right.id);
        end

        return integer.gt(leftPriority, rightPriority);
    end);
end

local function BuildDraft(snapshot : Snapshot) : any
    local draft = {
        providersByCreatureID = { }, otherCapabilitiesByCreatureID = { }, questsByID = { }, prerequisitesByID = { }, questGiversByCreatureID = { }, associationsByID = { }, objectivesByID = { }, targetsByID = { }, rewardGroupsByID = { },
        rewardItemsByID = { }, rewardCurrenciesByID = { }, rewardReputationsByID = { }, rewardExperienceByID = { },
    };
    for _, provider in snapshot.creatureTemplateInteractions do
        draft.providersByCreatureID[provider.creatureTemplateID] = Clone(provider);
    end
    for _, gossip in snapshot.creatureTemplateGossip do
        draft.otherCapabilitiesByCreatureID[gossip.creatureTemplateID] = true;
    end
    for _, vendor in snapshot.creatureTemplateVendor do
        draft.otherCapabilitiesByCreatureID[vendor.creatureTemplateID] = true;
    end
    for _, quest in snapshot.questTemplates do
        draft.questsByID[quest.id] = Clone(quest);
    end
    for _, prerequisite in snapshot.questPrerequisites do
        draft.prerequisitesByID[prerequisite.id] = Clone(prerequisite);
    end
    for _, questGiver in snapshot.creatureTemplateQuestGivers do
        draft.questGiversByCreatureID[questGiver.creatureTemplateID] = Clone(questGiver);
    end
    for _, association in snapshot.creatureTemplateQuests do
        draft.associationsByID[association.id] = Clone(association);
    end
    for _, objective in snapshot.questObjectives do
        draft.objectivesByID[objective.id] = Clone(objective);
    end
    for _, target in snapshot.questObjectiveTargets do
        draft.targetsByID[target.id] = Clone(target);
    end
    for _, group in snapshot.questRewardGroups do
        draft.rewardGroupsByID[group.id] = Clone(group);
    end
    for _, reward in snapshot.questRewardItems do
        draft.rewardItemsByID[reward.id] = Clone(reward);
    end
    for _, reward in snapshot.questRewardCurrencies do
        draft.rewardCurrenciesByID[reward.id] = Clone(reward);
    end
    for _, reward in snapshot.questRewardReputations do
        draft.rewardReputationsByID[reward.id] = Clone(reward);
    end
    for _, reward in snapshot.questRewardExperience do
        draft.rewardExperienceByID[reward.id] = Clone(reward);
    end

    return draft;
end

local function ApplyFields(target : any, fields : { [string] : any }, allowed : { [string] : boolean }, label : string)
    if target == nil then
        error(`QuestModel: unknown {label}`, 3);
    end
    for key, value in fields do
        if not allowed[key] then
            error(`QuestModel: {label} field '{key}' is not draft-editable`, 3);
        end
        target[key] = Clone(value);
    end
end

local modelAPI : QuestModelAPI = { };

function modelAPI.New(snapshot : Snapshot) : QuestModel
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

    local function RecordsFor(field : string, parentField : string, parentID : integer) : { any }
        local records = { };
        for _, record in draft[field] do
            if record[parentField] == parentID then
                table.insert(records, record);
            end
        end
        SortRecords(records);
        return records;
    end

    local function Replace(nextSnapshot : Snapshot)
        ValidateSnapshot(nextSnapshot);
        draft = BuildDraft(nextSnapshot);
        baseline = Clone(draft);
    end

    local function RemoveRewardsForGroup(rewardGroupID : integer)
        for _, field in REWARD_COLLECTIONS do
            for id, reward in draft[field] do
                if reward.rewardGroupID == rewardGroupID then
                    draft[field][id] = nil;
                end
            end
        end
    end

    local function RemoveUnusedQuestGiver(creatureTemplateID : integer)
        for _, association in draft.associationsByID do
            if association.creatureTemplateID == creatureTemplateID then
                return;
            end
        end

        draft.questGiversByCreatureID[creatureTemplateID] = nil;
        if not draft.otherCapabilitiesByCreatureID[creatureTemplateID] then
            draft.providersByCreatureID[creatureTemplateID] = nil;
        end
    end

    local function RemoveChildrenForQuest(questID : integer)
        for id, prerequisite in draft.prerequisitesByID do
            if prerequisite.questID == questID then
                draft.prerequisitesByID[id] = nil;
            end
        end
        for id, association in draft.associationsByID do
            if association.questID == questID then
                local creatureTemplateID = association.creatureTemplateID;
                draft.associationsByID[id] = nil;
                RemoveUnusedQuestGiver(creatureTemplateID);
            end
        end
        for id, objective in draft.objectivesByID do
            if objective.questID == questID then
                for targetID, target in draft.targetsByID do
                    if target.objectiveID == id then
                        draft.targetsByID[targetID] = nil;
                    end
                end
                draft.objectivesByID[id] = nil;
            end
        end
        for id, group in draft.rewardGroupsByID do
            if group.questID == questID then
                RemoveRewardsForGroup(id);
                draft.rewardGroupsByID[id] = nil;
            end
        end
    end

    function model:Reload(nextSnapshot : Snapshot)
        Replace(nextSnapshot);
    end

    function model:Revert()
        draft = Clone(baseline);
    end

    function model:IsDirty() : boolean
        return not Equal(draft, baseline);
    end

    function model:IsQuestDirty(questID : integer) : boolean
        if not Equal(draft.questsByID[questID], baseline.questsByID[questID]) then
            return true;
        end

        return #self:GetOperations(questID) > 0;
    end

    function model:GetQuestIDs() : { integer }
        local ids = { };
        for id in draft.questsByID do
            table.insert(ids, id);
        end
        table.sort(ids, function(left : integer, right : integer)
            local leftQuest = draft.questsByID[left];
            local rightQuest = draft.questsByID[right];
            local leftName = string.lower(leftQuest.internalName);
            local rightName = string.lower(rightQuest.internalName);
            if leftName == rightName then
                return integer.lt(left, right);
            end

            return leftName < rightName;
        end);
        return ids;
    end

    function model:GetQuest(questID : integer) : any?
        return draft.questsByID[questID];
    end

    function model:GetPrerequisites(questID : integer) : { any }
        return RecordsFor("prerequisitesByID", "questID", questID);
    end

    function model:GetAssociations(questID : integer) : { any }
        return RecordsFor("associationsByID", "questID", questID);
    end

    function model:GetObjectives(questID : integer) : { any }
        return RecordsFor("objectivesByID", "questID", questID);
    end

    function model:GetTargets(objectiveID : integer) : { any }
        return RecordsFor("targetsByID", "objectiveID", objectiveID);
    end

    function model:GetRewardGroups(questID : integer) : { any }
        return RecordsFor("rewardGroupsByID", "questID", questID);
    end

    function model:GetRewards(rewardGroupID : integer) : { any }
        local rewards = { };
        for kind, field in REWARD_COLLECTIONS do
            for _, reward in draft[field] do
                if reward.rewardGroupID == rewardGroupID then
                    table.insert(rewards, { kind = kind, value = reward });
                end
            end
        end
        table.sort(rewards, function(left : any, right : any)
            if left.value.priority == right.value.priority then
                if left.kind == right.kind then
                    return integer.lt(left.value.id, right.value.id);
                end

                return left.kind < right.kind;
            end

            return integer.gt(left.value.priority, right.value.priority);
        end);
        return rewards;
    end

    function model:ReorderObjectives(questID : integer, orderedObjectiveIDs : { integer })
        local objectives = self:GetObjectives(questID);
        if #orderedObjectiveIDs ~= #objectives then
            error(`QuestModel: objective order for quest {questID} must contain every objective`, 3);
        end

        local remaining : { [integer] : any } = { };
        for _, objective in objectives do remaining[objective.id] = objective; end
        for index, objectiveID in orderedObjectiveIDs do
            local objective = remaining[objectiveID];
            if objective == nil then
                error(`QuestModel: objective order for quest {questID} contains an unknown or duplicate objective ID {objectiveID}`, 3);
            end

            objective.priority = integer.create((#orderedObjectiveIDs - index + 1) * 100);
            remaining[objectiveID] = nil;
        end
    end

    function model:ReorderRewards(rewardGroupID : integer, orderedRewards : { any })
        local rewards = self:GetRewards(rewardGroupID);
        if #orderedRewards ~= #rewards then
            error(`QuestModel: reward order for group {rewardGroupID} must contain every reward`, 3);
        end

        local remaining : { [string] : any } = { };
        for _, reward in rewards do remaining[`{reward.kind}:{reward.value.id}`] = reward.value; end
        for index, reward in orderedRewards do
            local key = `{reward.kind}:{reward.id}`;
            local value = remaining[key];
            if value == nil then
                error(`QuestModel: reward order for group {rewardGroupID} contains an unknown or duplicate reward '{key}'`, 3);
            end

            value.priority = integer.create((#orderedRewards - index + 1) * 100);
            remaining[key] = nil;
        end
    end

    function model:SetQuestFields(questID : integer, fields : { [string] : any })
        ApplyFields(draft.questsByID[questID], fields, QUEST_FIELDS, "quest");
    end

    function model:SetPrerequisiteFields(prerequisiteID : integer, fields : { [string] : any })
        ApplyFields(draft.prerequisitesByID[prerequisiteID], fields, PREREQUISITE_FIELDS, "prerequisite");
    end

    function model:SetAssociationFields(associationID : integer, fields : { [string] : any })
        local association = draft.associationsByID[associationID];
        local previousCreatureTemplateID = if association ~= nil then association.creatureTemplateID else nil;
        local nextCreatureTemplateID = fields.creatureTemplateID or previousCreatureTemplateID;
        if nextCreatureTemplateID ~= nil and draft.providersByCreatureID[nextCreatureTemplateID] == nil then
            error(`QuestModel: creature template {nextCreatureTemplateID} is not an interaction provider`, 3);
        end
        ApplyFields(association, fields, ASSOCIATION_FIELDS, "association");
        if association.creatureTemplateID ~= previousCreatureTemplateID then
            if draft.questGiversByCreatureID[association.creatureTemplateID] == nil then
                draft.questGiversByCreatureID[association.creatureTemplateID] = { creatureTemplateID = association.creatureTemplateID, flags = 0i };
            end
            if previousCreatureTemplateID ~= nil then
                RemoveUnusedQuestGiver(previousCreatureTemplateID);
            end
        end
    end

    function model:SetObjectiveFields(objectiveID : integer, fields : { [string] : any })
        ApplyFields(draft.objectivesByID[objectiveID], fields, OBJECTIVE_FIELDS, "objective");
    end

    function model:SetTargetFields(targetID : integer, fields : { [string] : any })
        ApplyFields(draft.targetsByID[targetID], fields, TARGET_FIELDS, "objective target");
    end

    function model:SetRewardGroupFields(rewardGroupID : integer, fields : { [string] : any })
        ApplyFields(draft.rewardGroupsByID[rewardGroupID], fields, REWARD_GROUP_FIELDS, "reward group");
    end

    function model:SetRewardFields(rewardKind : string, rewardID : integer, fields : { [string] : any })
        local collection = REWARD_COLLECTIONS[rewardKind];
        if collection == nil then
            error(`QuestModel: unknown reward kind '{rewardKind}'`, 3);
        end

        ApplyFields(draft[collection][rewardID], fields, REWARD_FIELDS, "reward");
    end

    function model:AddQuest(internalName : string) : integer
        local id = AllocateTemporaryID();
        draft.questsByID[id] = { id = id, internalName = internalName, titleTextID = 0i, summaryTextID = 0i, detailsTextID = 0i, logTextID = 0i, progressTextID = 0i, completionTextID = 0i, visibilityConditionSetID = 0i, availabilityConditionSetID = 0i, unavailableReasonTextID = 0i, level = 1i, flags = 0i };
        return id;
    end

    function model:DuplicateQuest(questID : integer, internalName : string) : integer
        local source = draft.questsByID[questID];
        if source == nil then
            error(`QuestModel: unknown quest ID {questID}`, 3);
        end

        local id = AllocateTemporaryID();
        draft.questsByID[id] = Clone(source);
        draft.questsByID[id].id = id;
        draft.questsByID[id].internalName = internalName;
        for _, prerequisite in self:GetPrerequisites(questID) do
            local copyID = self:AddPrerequisite(id, prerequisite.prerequisiteQuestID);
            self:SetPrerequisiteFields(copyID, { prerequisiteQuestID = prerequisite.prerequisiteQuestID });
        end
        for _, objective in self:GetObjectives(questID) do
            local copyID = self:AddObjective(id, objective.objectiveType);
            self:SetObjectiveFields(copyID, { priority = objective.priority, objectiveType = objective.objectiveType, requiredCount = objective.requiredCount, textID = objective.textID, flags = objective.flags });
            for _, target in self:GetTargets(objective.id) do
                local targetCopyID = self:AddTarget(copyID, target.targetID);
                self:SetTargetFields(targetCopyID, { priority = target.priority, targetID = target.targetID, contribution = target.contribution });
            end
        end
        for _, group in self:GetRewardGroups(questID) do
            local copyID = self:AddRewardGroup(id);
            self:SetRewardGroupFields(copyID, { priority = group.priority, selectionCount = group.selectionCount, flags = group.flags });
            for _, reward in self:GetRewards(group.id) do
                local rewardCopyID = self:AddReward(copyID, reward.kind);
                local fields = { priority = reward.value.priority, amount = reward.value.amount, flags = reward.value.flags, itemTemplateID = reward.value.itemTemplateID, quantity = reward.value.quantity, currencyID = reward.value.currencyID, factionID = reward.value.factionID };
                self:SetRewardFields(reward.kind, rewardCopyID, fields);
            end
        end
        for _, association in self:GetAssociations(questID) do
            local copyID = self:AddAssociation(id, association.creatureTemplateID);
            self:SetAssociationFields(copyID, { creatureTemplateID = association.creatureTemplateID, roleFlags = association.roleFlags, priority = association.priority, visibilityConditionSetID = association.visibilityConditionSetID, enabledConditionSetID = association.enabledConditionSetID, flags = association.flags });
        end

        return id;
    end

    function model:AddPrerequisite(questID : integer, prerequisiteQuestID : integer) : integer
        local id = AllocateTemporaryID();
        draft.prerequisitesByID[id] = { id = id, questID = questID, prerequisiteQuestID = prerequisiteQuestID };
        return id;
    end

    function model:AddAssociation(questID : integer, creatureTemplateID : integer) : integer
        if draft.providersByCreatureID[creatureTemplateID] == nil then
            error(`QuestModel: creature template {creatureTemplateID} is not an interaction provider`, 3);
        end

        local id = AllocateTemporaryID();
        draft.associationsByID[id] = { id = id, creatureTemplateID = creatureTemplateID, questID = questID, roleFlags = 1i, priority = 0i, visibilityConditionSetID = 0i, enabledConditionSetID = 0i, flags = 0i };
        if draft.questGiversByCreatureID[creatureTemplateID] == nil then
            draft.questGiversByCreatureID[creatureTemplateID] = { creatureTemplateID = creatureTemplateID, flags = 0i };
        end
        return id;
    end

    function model:AddObjective(questID : integer, objectiveType : integer) : integer
        local id = AllocateTemporaryID();
        draft.objectivesByID[id] = { id = id, questID = questID, priority = 0i, objectiveType = objectiveType, requiredCount = 1i, textID = 0i, flags = 0i };
        return id;
    end

    function model:AddTarget(objectiveID : integer, targetID : integer) : integer
        local id = AllocateTemporaryID();
        draft.targetsByID[id] = { id = id, objectiveID = objectiveID, priority = 0i, targetID = targetID, contribution = 1i };
        return id;
    end

    function model:AddRewardGroup(questID : integer) : integer
        local id = AllocateTemporaryID();
        draft.rewardGroupsByID[id] = { id = id, questID = questID, priority = 0i, selectionCount = 0i, flags = 0i };
        return id;
    end

    function model:AddReward(rewardGroupID : integer, rewardKind : string, targetID : integer?) : integer
        local collection = REWARD_COLLECTIONS[rewardKind];
        if collection == nil then
            error(`QuestModel: unknown reward kind '{rewardKind}'`, 3);
        end

        local id = AllocateTemporaryID();
        local reward = { id = id, rewardGroupID = rewardGroupID, priority = 0i, flags = 0i };
        if rewardKind == "Item" then
            reward.itemTemplateID = targetID or 0i;
            reward.quantity = 1i;
        elseif rewardKind == "Currency" then
            reward.currencyID = targetID or 0i;
            reward.amount = 1i;
        elseif rewardKind == "Reputation" then
            reward.factionID = targetID or 0i;
            reward.amount = 1i;
        else
            reward.amount = 1i;
        end
        draft[collection][id] = reward;
        return id;
    end

    function model:RemoveQuest(questID : integer)
        RemoveChildrenForQuest(questID);
        draft.questsByID[questID] = nil;
    end

    function model:RemovePrerequisite(prerequisiteID : integer)
        draft.prerequisitesByID[prerequisiteID] = nil;
    end

    function model:RemoveAssociation(associationID : integer)
        local association = draft.associationsByID[associationID];
        draft.associationsByID[associationID] = nil;
        if association ~= nil then
            RemoveUnusedQuestGiver(association.creatureTemplateID);
        end
    end

    function model:RemoveObjective(objectiveID : integer)
        for id, target in draft.targetsByID do
            if target.objectiveID == objectiveID then
                draft.targetsByID[id] = nil;
            end
        end
        draft.objectivesByID[objectiveID] = nil;
    end

    function model:RemoveTarget(targetID : integer)
        draft.targetsByID[targetID] = nil;
    end

    function model:RemoveRewardGroup(rewardGroupID : integer)
        RemoveRewardsForGroup(rewardGroupID);
        draft.rewardGroupsByID[rewardGroupID] = nil;
    end

    function model:RemoveReward(rewardKind : string, rewardID : integer)
        local collection = REWARD_COLLECTIONS[rewardKind];
        if collection ~= nil then
            draft[collection][rewardID] = nil;
        end
    end

    function model:ResolveTemporaryID(kind : string, temporaryID : integer, authoritativeID : integer)
        local collections = { Quest = "questsByID", Prerequisite = "prerequisitesByID", Association = "associationsByID", Objective = "objectivesByID", Target = "targetsByID", RewardGroup = "rewardGroupsByID", RewardItem = "rewardItemsByID", RewardCurrency = "rewardCurrenciesByID", RewardReputation = "rewardReputationsByID", RewardExperience = "rewardExperienceByID" };
        local collection = collections[kind];
        if collection == nil or draft[collection][temporaryID] == nil then
            return;
        end

        local record = draft[collection][temporaryID];
        draft[collection][temporaryID] = nil;
        record.id = authoritativeID;
        draft[collection][authoritativeID] = record;
        if kind == "Quest" then
            for _, childCollection in { draft.prerequisitesByID, draft.associationsByID, draft.objectivesByID, draft.rewardGroupsByID } do
                for _, child in childCollection do
                    if child.questID == temporaryID then
                        child.questID = authoritativeID;
                    end
                end
            end
        elseif kind == "Objective" then
            for _, target in draft.targetsByID do
                if target.objectiveID == temporaryID then
                    target.objectiveID = authoritativeID;
                end
            end
        elseif kind == "RewardGroup" then
            for _, field in REWARD_COLLECTIONS do
                for _, reward in draft[field] do
                    if reward.rewardGroupID == temporaryID then
                        reward.rewardGroupID = authoritativeID;
                    end
                end
            end
        end
    end

    local function AppendRecordOperations(operations : { any }, kind : string, collection : string, parentField : string, parentID : integer)
        for id, record in baseline[collection] do
            if record[parentField] == parentID and draft[collection][id] == nil then
                table.insert(operations, { kind = `Delete{kind}`, value = Clone(record) });
            end
        end
        for id, record in draft[collection] do
            if record[parentField] == parentID then
                if IsTemporaryID(id) then
                    table.insert(operations, { kind = `Create{kind}`, temporaryID = id, value = Clone(record) });
                elseif not Equal(record, baseline[collection][id]) then
                    table.insert(operations, { kind = `Update{kind}`, value = Clone(record) });
                end
            end
        end
    end

    function model:GetOperations(questID : integer) : { any }
        local operations = { };
        local quest = draft.questsByID[questID];
        if quest == nil then
            return operations;
        end

        if IsTemporaryID(questID) then
            table.insert(operations, { kind = "CreateQuest", temporaryID = questID, value = Clone(quest) });
        elseif not Equal(quest, baseline.questsByID[questID]) then
            table.insert(operations, { kind = "UpdateQuest", value = Clone(quest) });
        end

        AppendRecordOperations(operations, "Prerequisite", "prerequisitesByID", "questID", questID);
        AppendRecordOperations(operations, "Objective", "objectivesByID", "questID", questID);
        for _, objective in self:GetObjectives(questID) do
            if IsTemporaryID(objective.id) then
                for _, operation in operations do
                    if operation.kind == "CreateObjective" and operation.temporaryID == objective.id then
                        operation.targets = Clone(self:GetTargets(objective.id));
                        break;
                    end
                end
            else
                AppendRecordOperations(operations, "Target", "targetsByID", "objectiveID", objective.id);
            end
        end
        AppendRecordOperations(operations, "RewardGroup", "rewardGroupsByID", "questID", questID);
        for _, group in self:GetRewardGroups(questID) do
            if IsTemporaryID(group.id) then
                for _, operation in operations do
                    if operation.kind == "CreateRewardGroup" and operation.temporaryID == group.id then
                        operation.rewards = Clone(self:GetRewards(group.id));
                        break;
                    end
                end
            else
                AppendRecordOperations(operations, "RewardItem", "rewardItemsByID", "rewardGroupID", group.id);
                AppendRecordOperations(operations, "RewardCurrency", "rewardCurrenciesByID", "rewardGroupID", group.id);
                AppendRecordOperations(operations, "RewardReputation", "rewardReputationsByID", "rewardGroupID", group.id);
                AppendRecordOperations(operations, "RewardExperience", "rewardExperienceByID", "rewardGroupID", group.id);
            end
        end

        local associatedCreatureIDs : { [integer] : boolean } = { };
        for _, association in self:GetAssociations(questID) do
            associatedCreatureIDs[association.creatureTemplateID] = true;
        end
        for creatureTemplateID, questGiver in draft.questGiversByCreatureID do
            if associatedCreatureIDs[creatureTemplateID] and baseline.questGiversByCreatureID[creatureTemplateID] == nil then
                table.insert(operations, { kind = "CreateQuestGiver", value = Clone(questGiver) });
            end
        end
        AppendRecordOperations(operations, "Association", "associationsByID", "questID", questID);
        for creatureTemplateID, questGiver in baseline.questGiversByCreatureID do
            if draft.questGiversByCreatureID[creatureTemplateID] == nil then
                if baseline.providersByCreatureID[creatureTemplateID] ~= nil and draft.providersByCreatureID[creatureTemplateID] == nil then
                    table.insert(operations, { kind = "DeleteProvider", value = Clone(baseline.providersByCreatureID[creatureTemplateID]) });
                else
                    table.insert(operations, { kind = "DeleteQuestGiver", value = Clone(questGiver) });
                end
            end
        end
        return operations;
    end

    return model;
end

return modelAPI;
