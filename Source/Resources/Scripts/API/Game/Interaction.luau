export type Option =
{
    token : integer,
    icon : integer,
    enabled : boolean,
    text : string,
    disabledReason : string,
}

export type Session =
{
    sessionID : integer,
    revision : integer,
    sourceUnitID : integer,
    surfaceType : integer,
    greeting : string,
    options : { Option },
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
    activeSession : Session?,
    lastClose : CloseState?,
    lastResult : ResultState?,
}

local interactionAPI = { };

function interactionAPI.GetState() : State
    return Interaction.GetState() :: State;
end

function interactionAPI.Open(unitID : integer) : boolean
    return Interaction.Open(unitID);
end

function interactionAPI.Select(sessionID : integer, revision : integer, optionToken : integer) : boolean
    return Interaction.Select(sessionID, revision, optionToken);
end

function interactionAPI.Close(sessionID : integer) : boolean
    return Interaction.Close(sessionID);
end

return interactionAPI;
