export type ScriptEntry =
{
    name : string,
    path : string,
    revision : integer,
    sourceAvailable : boolean,
    locked : boolean,
    lockedByRequester : boolean,
}

export type Inspection =
{
    guid : string,
    templateID : integer,
    guidScriptName : string,
    templateScriptName : string,
    effectiveScriptName : string,
    effectiveScope : string,
}

export type State =
{
    version : integer,
    status : string,
    busy : boolean,
    scripts : { ScriptEntry },
    inspection : Inspection?,
}

local CreatureAI = { };

function CreatureAI.GetState() : State
    return CreatureAIEditor.GetState() :: State;
end

function CreatureAI.RequestCatalog() : boolean
    return CreatureAIEditor.RequestCatalog();
end

function CreatureAI.InspectUnit(unitID : integer) : boolean
    return CreatureAIEditor.InspectUnit(unitID);
end

function CreatureAI.ClearInspection()
    CreatureAIEditor.ClearInspection();
end

function CreatureAI.View(scriptName : string) : boolean
    return CreatureAIEditor.View(scriptName);
end

function CreatureAI.Edit(scriptName : string) : boolean
    return CreatureAIEditor.Edit(scriptName);
end

function CreatureAI.Create(scriptName : string, relativePath : string, source : string) : boolean
    return CreatureAIEditor.Create(scriptName, relativePath, source);
end

function CreatureAI.Duplicate(sourceScriptName : string, scriptName : string, relativePath : string) : boolean
    return CreatureAIEditor.Duplicate(sourceScriptName, scriptName, relativePath);
end

function CreatureAI.FinishEditing(scriptName : string) : boolean
    return CreatureAIEditor.FinishEditing(scriptName);
end

function CreatureAI.Link(scope : "GUID" | "Template", scriptName : string) : boolean
    return CreatureAIEditor.Link(scope == "GUID", scriptName);
end

function CreatureAI.Unlink(scope : "GUID" | "Template") : boolean
    return CreatureAIEditor.Unlink(scope == "GUID");
end

return CreatureAI;
