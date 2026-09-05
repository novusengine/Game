--[[
AnimationPreviewSession - shared state for animation development tools.

The playback panel owns the preview actor. Other editor windows can observe that actor
without spawning a duplicate or keeping their own controller handles alive.
]]

export type Snapshot =
{
    entity : integer?,
    mode : string,
    revision : integer,
    inputs : { [string] : number },
}

local entity : integer? = nil
local mode = "states"
local revision = 0i
local inputs : { [string] : number } = {}

local api = {}

function api.SetEntity(nextEntity : integer, nextMode : string)
    entity = nextEntity
    mode = nextMode
    inputs = {}
    revision = integer.add(revision, 1i)
end

function api.Clear(expectedEntity : integer?)
    if expectedEntity ~= nil and entity ~= expectedEntity then return end
    entity = nil
    mode = "states"
    inputs = {}
    revision = integer.add(revision, 1i)
end

function api.SetInput(name : string, value : number)
    inputs[name] = value
end

function api.GetSnapshot() : Snapshot
    return {
        entity = entity,
        mode = mode,
        revision = revision,
        inputs = table.clone(inputs),
    }
end

return api
