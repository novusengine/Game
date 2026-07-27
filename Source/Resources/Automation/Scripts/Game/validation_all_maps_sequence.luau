-- Deterministic all-map validation sequence for Vulkan validation runs.
-- Run once through automation_run after the Debug client has started.

local DWELL_SECONDS = 5
local WATCHDOG_SECONDS = 300

type ValidationAction = {
    kind: string,
    label: string,
    value: string?,
    expectedMap: string?,
    expectLoaded: boolean,
}

local actions: { ValidationAction } = {}
local actionIndex = 0
local activeAction: ValidationAction? = nil
local watchdogHandle: number? = nil
local sequenceStarted = false
local sequenceFinished = false
local skippedActions = 0

local function emit(event: string, detail: string)
    print("NOVUS_VALIDATION_SEQUENCE event=" .. event .. " " .. detail)
end

local function cancelWatchdog()
    if watchdogHandle ~= nil then
        Scheduler.Cancel(watchdogHandle)
        watchdogHandle = nil
    end
end

local function fail(reason: string)
    if sequenceFinished then
        return
    end

    sequenceFinished = true
    cancelWatchdog()
    emit("failed", "reason=" .. reason)
    print("NOVUS_VALIDATION_SEQUENCE_FAILED reason=" .. reason)
end

local advance: () -> ()

local function beginDwell(action: ValidationAction, mapInternalName: string)
    cancelWatchdog()
    emit(
        "completed",
        "step=" .. tostring(actionIndex) ..
        " label=" .. action.label ..
        " map=" .. mapInternalName)

    Scheduler.AfterSeconds(DWELL_SECONDS, function()
        if sequenceFinished then
            return
        end

        emit("dwell-complete", "step=" .. tostring(actionIndex) .. " label=" .. action.label)
        activeAction = nil
        advance()
    end)
end

local function executeAction(action: ValidationAction): boolean
    if action.kind == "camera" then
        return Camera.LoadSave(action.value :: string)
    elseif action.kind == "load" then
        return Map.Load(action.value :: string)
    elseif action.kind == "unload" then
        return Map.Unload()
    end

    return false
end

advance = function()
    if sequenceFinished then
        return
    end

    actionIndex += 1
    activeAction = actions[actionIndex]
    if activeAction == nil then
        sequenceFinished = true
        emit("complete", "steps=" .. tostring(#actions) .. " skipped=" .. tostring(skippedActions))
        print(
            "NOVUS_VALIDATION_SEQUENCE_COMPLETE" ..
            " steps=" .. tostring(#actions) ..
            " skipped=" .. tostring(skippedActions))
        return
    end

    local action = activeAction :: ValidationAction
    emit("started", "step=" .. tostring(actionIndex) .. " label=" .. action.label)

    if not executeAction(action) then
        fail("request-rejected step=" .. tostring(actionIndex) .. " label=" .. action.label)
        return
    end

    local watchedIndex = actionIndex
    watchdogHandle = Scheduler.AfterSeconds(WATCHDOG_SECONDS, function()
        if sequenceFinished or actionIndex ~= watchedIndex then
            return
        end
        fail("watchdog step=" .. tostring(watchedIndex) .. " label=" .. action.label)
    end)
end

local function onMapLoaded(_eventID: Event, data: any)
    local action = activeAction
    if sequenceFinished or action == nil then
        return
    end

    local typedAction = action :: ValidationAction
    if data.isLoaded ~= typedAction.expectLoaded then
        return
    end
    if typedAction.expectedMap ~= nil and data.mapInternalName ~= typedAction.expectedMap then
        return
    end

    beginDwell(typedAction, data.mapInternalName)
end

local function onMapLoadFailed(_eventID: Event, data: any)
    local action = activeAction
    if sequenceFinished or action == nil then
        return
    end

    local typedAction = action :: ValidationAction
    if typedAction.kind == "camera" then
        fail(
            "camera-map-load-failed" ..
            " step=" .. tostring(actionIndex) ..
            " label=" .. typedAction.label ..
            " map=" .. tostring(data.mapInternalName) ..
            " detail=" .. tostring(data.reason))
        return
    end

    if typedAction.kind ~= "load" or typedAction.expectedMap ~= data.mapInternalName then
        return
    end

    cancelWatchdog()
    skippedActions += 1
    emit(
        "skipped",
        "step=" .. tostring(actionIndex) ..
        " label=" .. typedAction.label ..
        " map=" .. tostring(data.mapInternalName) ..
        " reason=" .. tostring(data.reason))

    local followingAction = actions[actionIndex + 1]
    if followingAction ~= nil and followingAction.kind == "unload" then
        skippedActions += 1
        emit(
            "skipped",
            "step=" .. tostring(actionIndex + 1) ..
            " label=" .. followingAction.label ..
            " reason=corresponding-load-failed")
        actionIndex += 1
    end

    activeAction = nil
    Scheduler.AfterFrames(1, advance)
end

local function buildActions(): boolean
    local cameraSaves = Camera.GetSaveNames()
    table.sort(cameraSaves)
    for _, saveName in cameraSaves do
        if saveName ~= "Default" then
            table.insert(actions, {
                kind = "camera",
                label = "camera:" .. saveName,
                value = saveName,
                expectedMap = nil,
                expectLoaded = true,
            })
        end
    end

    local maps = Map.GetList()
    table.sort(maps, function(left, right): boolean
        if left.internalName == right.internalName then
            return left.id < right.id
        end
        return left.internalName < right.internalName
    end)

    if #maps == 0 then
        fail("no-maps-found")
        return false
    end

    for _, map in maps do
        if map.internalName == "" then
            fail("invalid-map-record id=" .. tostring(map.id) .. " reason=empty-internal-name")
            return false
        end

        table.insert(actions, {
            kind = "load",
            label = "map:" .. map.internalName,
            value = map.internalName,
            expectedMap = map.internalName,
            expectLoaded = true,
        })
        table.insert(actions, {
            kind = "unload",
            label = "unload:" .. map.internalName,
            value = nil,
            expectedMap = nil,
            expectLoaded = false,
        })
    end

    emit(
        "discovered",
        "camera-saves=" .. tostring(#actions - (#maps * 2)) ..
        " maps=" .. tostring(#maps))

    return true
end

local function startSequence()
    if sequenceStarted or sequenceFinished then
        return
    end
    sequenceStarted = true

    if not buildActions() then
        return
    end

    emit("ready", "steps=" .. tostring(#actions))
    advance()
end

RegisterEvent(GameEvent.MapLoaded, onMapLoaded)
RegisterEvent(GameEvent.MapLoadFailed, onMapLoadFailed)

if Game.IsLoaded() then
    -- Deferring through the native timer lets any already-buffered map event drain
    -- before the first validation request is issued.
    Scheduler.AfterSeconds(0, startSequence)
else
    RegisterEvent(GameEvent.Loaded, function()
        Scheduler.AfterSeconds(0, startSequence)
    end)
end
