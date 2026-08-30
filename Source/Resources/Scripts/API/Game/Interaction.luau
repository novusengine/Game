export type Option =
{
    token : integer,
    icon : integer,
    kind : integer,
    enabled : boolean,
    text : string,
    disabledReason : string,
    itemTemplateID : integer,
    quantity : integer,
    costID : integer,
    costDataAvailable : boolean,
    costCurrencyComponents : { { currencyID : integer, name : string, amount : integer } },
    costItemComponents : { { itemTemplateID : integer, amount : integer } },
}

export type Session =
{
    sessionID : integer,
    revision : integer,
    sourceUnitID : integer,
    surfaceType : integer,
    questMask : integer,
    heading : string,
    greeting : string,
    options : { Option },
    objectives : { { text : string, displayText : string, progress : integer, required : integer, optional : boolean } },
    rewards : { { groupID : integer, selectionID : integer, type : integer, resourceID : integer, amount : integer, amountNegative : boolean, selectionCount : integer, flags : integer, selected : boolean } },
}

export type CloseState =
{
    sessionID : integer,
    reason : integer,
}

export type ResultState =
{
    sessionID : integer,
    revision : integer,
    result : integer,
}

export type State =
{
    version : integer,
    questLog : { revision : integer, quests : { { questID : integer, state : integer, title : string, logText : string, objectives : { { objectiveID : integer, text : string, displayText : string, progress : integer, required : integer, optional : boolean } } } } },
    activeSession : Session?,
    lastClose : CloseState?,
    lastResult : ResultState?,
    lastQuestCommand : { questID : integer, result : integer }?,
}

export type InteractionAPI =
{
    GetVersion : () -> integer,
    GetState : () -> State,
    Open : (unitID : integer) -> boolean,
    Select : (sessionID : integer, revision : integer, optionToken : integer) -> boolean,
    Purchase : (sessionID : integer, revision : integer, purchaseToken : integer, bundleCount : integer) -> boolean,
    ToggleReward : (selectionID : integer) -> boolean,
    CompleteQuest : (sessionID : integer, revision : integer, completionToken : integer) -> boolean,
    AbandonQuest : (questID : integer) -> boolean,
    Close : (sessionID : integer) -> boolean,
}

local interactionAPI : InteractionAPI = { };

function interactionAPI.GetVersion() : integer
    return Interaction.GetVersion();
end

function interactionAPI.GetState() : State
    return Interaction.GetState() :: State;
end

function interactionAPI.Open(unitID : integer) : boolean
    return Interaction.Open(unitID);
end

function interactionAPI.Select(sessionID : integer, revision : integer, optionToken : integer) : boolean
    return Interaction.Select(sessionID, revision, optionToken);
end

function interactionAPI.Purchase(sessionID : integer, revision : integer, purchaseToken : integer, bundleCount : integer) : boolean
    return Interaction.Purchase(sessionID, revision, purchaseToken, bundleCount);
end

function interactionAPI.ToggleReward(selectionID : integer) : boolean
    return Interaction.ToggleReward(selectionID);
end

function interactionAPI.CompleteQuest(sessionID : integer, revision : integer, completionToken : integer) : boolean
    return Interaction.CompleteQuest(sessionID, revision, completionToken);
end

function interactionAPI.AbandonQuest(questID : integer) : boolean
    return Interaction.AbandonQuest(questID);
end

function interactionAPI.Close(sessionID : integer) : boolean
    return Interaction.Close(sessionID);
end

return interactionAPI;
