-- If a run crashes, set this to the index after the last NOVUS_CVAR_BEGIN
-- marker before executing the script again.
local START_INDEX = 1
local SETTLE_FRAMES = 10
local CAMERA_DWELL_SECONDS = 5
local CAMERA_WATCHDOG_SECONDS = 300

local FLAG_READ_ONLY = 4
local AUTOMATION_CONTROL_CVARS = {
    cl_consoleDuplicateToTerminal = true,
    cl_consoleEnabled = true,
}
local BLACKLISTED_CVARS = {
    cl_r_joltEnabled = "known_game_hang",
}
local LIVE_DERIVED_CVARS = {
    cl_r_fogBlendBegin = "sky_time_simulation",
    cl_r_fogBlendEnd = "sky_time_simulation",
    cl_r_fogColor = "sky_time_simulation",
}

-- Keep callable references even while cl_developerMode temporarily removes
-- developer-only methods from the global Editor table.
local GetCVars = Editor.GetCVars
local SetCVar = Editor.SetCVar

local function Hex(value: string): string
    local result = ""
    for index = 1, #value do
        result ..= string.format("%02X", string.byte(value, index))
    end
    return result
end

local function ValuesEqual(left, right, typeName): boolean
    if typeName == "Float Vector" or typeName == "Integer Vector" then
        return left.x == right.x and left.y == right.y and left.z == right.z and left.w == right.w
    end
    return left == right
end

local function FormatValue(value, typeName): string
    if typeName == "Float Vector" or typeName == "Integer Vector" then
        return table.concat({
            tostring(value.x),
            tostring(value.y),
            tostring(value.z),
            tostring(value.w),
        }, ",")
    end
    return tostring(value)
end

local function CopyValue(value, typeName)
    if typeName == "Float Vector" or typeName == "Integer Vector" then
        return {
            x = value.x,
            y = value.y,
            z = value.z,
            w = value.w,
        }
    end
    return value
end

local function AddUnique(values, value, typeName)
    for _, existing in values do
        if ValuesEqual(existing, value, typeName) then
            return
        end
    end
    table.insert(values, value)
end

local function NumericDescriptionChoices(cvar)
    local result = {}
    local description = cvar.description

    -- Explicitly documented enum spellings such as "0 = Off, 1 = Static".
    for token in string.gmatch(description, "([%-]?%d+)%s*=") do
        AddUnique(result, tonumber(token), cvar.type)
    end

    -- Small, finite integer ranges can be enumerated completely.
    if cvar.type == "Integer" then
        local lowerText, upperText = string.match(description, "^%s*%[([%-]?%d+)%s*,%s*([%-]?%d+)%]")
        local lower = tonumber(lowerText)
        local upper = tonumber(upperText)
        if lower ~= nil and upper ~= nil and upper >= lower and upper - lower <= 16 then
            for value = lower, upper do
                AddUnique(result, value, cvar.type)
            end
        end
    elseif cvar.type == "Float" then
        -- A continuous range has infinitely many values. Exercise both documented
        -- boundaries, plus current/default below.
        local lowerText, upperText = string.match(
            description,
            "^%s*%[([%-]?%d+%.?%d*)%s*,%s*([%-]?%d+%.?%d*)%]")
        local lower = tonumber(lowerText)
        local upper = tonumber(upperText)
        if lower ~= nil and upper ~= nil and upper >= lower then
            AddUnique(result, lower, cvar.type)
            AddUnique(result, upper, cvar.type)
        end
    end

    return result
end

local function ChoicesFor(cvar)
    local result = {}
    if cvar.type == "Boolean" then
        table.insert(result, false)
        table.insert(result, true)
    elseif cvar.type == "Integer" or cvar.type == "Float" then
        result = NumericDescriptionChoices(cvar)
        AddUnique(result, CopyValue(cvar.defaultValue, cvar.type), cvar.type)
        AddUnique(result, CopyValue(cvar.value, cvar.type), cvar.type)
    else
        -- Strings and vectors have no finite option set in the CVar API.
        AddUnique(result, CopyValue(cvar.defaultValue, cvar.type), cvar.type)
        AddUnique(result, CopyValue(cvar.value, cvar.type), cvar.type)
    end
    return result
end

local function Apply(cvar, value)
    if cvar.type == "Float Vector" or cvar.type == "Integer Vector" then
        SetCVar(cvar.category, cvar.name, value.x, value.y, value.z, value.w)
    else
        SetCVar(cvar.category, cvar.name, value)
    end
end

local function StartSweep()
local cvars = GetCVars()
table.sort(cvars, function(left, right)
    return left.qualifiedName < right.qualifiedName
end)

local originals = {}
local choices = {}
local developerModeIndex = nil
for index, cvar in cvars do
    originals[index] = CopyValue(cvar.value, cvar.type)
    choices[index] = ChoicesFor(cvar)
    if cvar.qualifiedName == "cl_developerMode" then
        developerModeIndex = index
    end
end

print("NOVUS_CVAR_SNAPSHOT_BEGIN count=" .. tostring(#cvars))
for index, cvar in cvars do
    print(table.concat({
        "NOVUS_CVAR_SNAPSHOT",
        tostring(index),
        tostring(cvar.category),
        tostring(cvar.flags),
        Hex(cvar.type),
        Hex(cvar.name),
        Hex(cvar.qualifiedName),
        Hex(cvar.description),
        Hex(FormatValue(originals[index], cvar.type)),
    }, "|"))
end
print("NOVUS_CVAR_SNAPSHOT_END count=" .. tostring(#cvars))

local cvarIndex = START_INDEX
local choiceIndex = 1
local beganCurrent = false
local finished = false

print(
    "NOVUS_CVAR_SWEEP_BEGIN" ..
    " startIndex=" .. tostring(START_INDEX) ..
    " count=" .. tostring(#cvars) ..
    " settleFrames=" .. tostring(SETTLE_FRAMES))

local Advance: () -> ()
Advance = function()
    if finished then
        return
    end

    while cvarIndex <= #cvars do
        local cvar = cvars[cvarIndex]
        local blacklistReason = BLACKLISTED_CVARS[cvar.qualifiedName]
        if blacklistReason ~= nil then
            print(
                "NOVUS_CVAR_SKIP" ..
                " index=" .. tostring(cvarIndex) ..
                " name=" .. cvar.qualifiedName ..
                " reason=blacklisted" ..
                " detail=" .. blacklistReason)
            cvarIndex += 1
            choiceIndex = 1
            beganCurrent = false
        elseif cvar.qualifiedName == "cl_developerMode" then
            print(
                "NOVUS_CVAR_DEFER" ..
                " index=" .. tostring(cvarIndex) ..
                " name=" .. cvar.qualifiedName ..
                " reason=terminal_luau_reload_test")
            cvarIndex += 1
            choiceIndex = 1
            beganCurrent = false
        elseif bit32.band(cvar.flags, FLAG_READ_ONLY) ~= 0 then
            print(
                "NOVUS_CVAR_SKIP" ..
                " index=" .. tostring(cvarIndex) ..
                " name=" .. cvar.qualifiedName ..
                " reason=read_only")
            cvarIndex += 1
            choiceIndex = 1
            beganCurrent = false
        elseif #choices[cvarIndex] == 0 then
            print(
                "NOVUS_CVAR_SKIP" ..
                " index=" .. tostring(cvarIndex) ..
                " name=" .. cvar.qualifiedName ..
                " reason=no_finite_choices")
            cvarIndex += 1
            choiceIndex = 1
            beganCurrent = false
        else
            if not beganCurrent then
                print(
                    "NOVUS_CVAR_BEGIN" ..
                    " index=" .. tostring(cvarIndex) ..
                    " name=" .. cvar.qualifiedName ..
                    " type=" .. string.gsub(cvar.type, " ", "_") ..
                    " choices=" .. tostring(#choices[cvarIndex]) ..
                    " original=" .. FormatValue(originals[cvarIndex], cvar.type))
                beganCurrent = true
            end

            if choiceIndex <= #choices[cvarIndex] then
                local value = choices[cvarIndex][choiceIndex]
                print(
                    "NOVUS_CVAR_SET" ..
                    " index=" .. tostring(cvarIndex) ..
                    " choice=" .. tostring(choiceIndex) ..
                    " name=" .. cvar.qualifiedName ..
                    " value=" .. FormatValue(value, cvar.type))
                Apply(cvar, value)

                -- These CVars control the console and stdout path used to drive
                -- and observe this script. Exercise the requested value, but
                -- restore it before yielding so the harness cannot disable
                -- its own next update or log stream.
                if AUTOMATION_CONTROL_CVARS[cvar.qualifiedName] then
                    Apply(cvar, originals[cvarIndex])
                    print(
                        "NOVUS_CVAR_CONTROL_RESTORED" ..
                        " index=" .. tostring(cvarIndex) ..
                        " name=" .. cvar.qualifiedName ..
                        " restored=" .. FormatValue(originals[cvarIndex], cvar.type))
                end

                choiceIndex += 1
                Scheduler.AfterFrames(SETTLE_FRAMES, Advance)
                return
            end

            Apply(cvar, originals[cvarIndex])
            print(
                "NOVUS_CVAR_END" ..
                " index=" .. tostring(cvarIndex) ..
                " name=" .. cvar.qualifiedName ..
                " restored=" .. FormatValue(originals[cvarIndex], cvar.type))
            cvarIndex += 1
            choiceIndex = 1
            beganCurrent = false
            Scheduler.AfterFrames(SETTLE_FRAMES, Advance)
            return
        end
    end

    local current = GetCVars()
    local currentByName = {}
    for _, cvar in current do
        currentByName[cvar.qualifiedName] = cvar
    end

    local mismatches = 0
    local liveDrifts = 0
    for index, originalCVar in cvars do
        local actual = currentByName[originalCVar.qualifiedName]
        if actual == nil or not ValuesEqual(actual.value, originals[index], originalCVar.type) then
            local liveDerivedReason = LIVE_DERIVED_CVARS[originalCVar.qualifiedName]
            if actual ~= nil and liveDerivedReason ~= nil then
                liveDrifts += 1
                print(
                    "NOVUS_CVAR_LIVE_DRIFT" ..
                    " index=" .. tostring(index) ..
                    " name=" .. originalCVar.qualifiedName ..
                    " expected=" .. FormatValue(originals[index], originalCVar.type) ..
                    " actual=" .. FormatValue(actual.value, actual.type) ..
                    " reason=" .. liveDerivedReason)
            else
                mismatches += 1
                print(
                    "NOVUS_CVAR_RESTORE_MISMATCH" ..
                    " index=" .. tostring(index) ..
                    " name=" .. originalCVar.qualifiedName ..
                    " expected=" .. FormatValue(originals[index], originalCVar.type) ..
                    " actual=" .. (actual and FormatValue(actual.value, actual.type) or "<missing>"))
            end
        end
    end

    -- Changing developer mode schedules a Luau state reload on the next update,
    -- which would remove this callback. Exercise every choice synchronously as
    -- the final CVar and restore it before yielding.
    if developerModeIndex ~= nil then
        local developerMode = cvars[developerModeIndex]
        local original = originals[developerModeIndex]
        print(
            "NOVUS_CVAR_BEGIN" ..
            " index=" .. tostring(developerModeIndex) ..
            " name=" .. developerMode.qualifiedName ..
            " type=" .. string.gsub(developerMode.type, " ", "_") ..
            " choices=2" ..
            " original=" .. FormatValue(original, developerMode.type) ..
            " mode=terminal_luau_reload_test")

        print(
            "NOVUS_CVAR_SET" ..
            " index=" .. tostring(developerModeIndex) ..
            " choice=1" ..
            " name=" .. developerMode.qualifiedName ..
            " value=false")
        Apply(developerMode, false)

        print(
            "NOVUS_CVAR_SET" ..
            " index=" .. tostring(developerModeIndex) ..
            " choice=2" ..
            " name=" .. developerMode.qualifiedName ..
            " value=true")
        Apply(developerMode, true)
        Apply(developerMode, original)

        print(
            "NOVUS_CVAR_END" ..
            " index=" .. tostring(developerModeIndex) ..
            " name=" .. developerMode.qualifiedName ..
            " restored=" .. FormatValue(original, developerMode.type) ..
            " mode=terminal_luau_reload_test")
    end

    finished = true
    print(
        "NOVUS_CVAR_SWEEP_END" ..
        " count=" .. tostring(#cvars) ..
        " restoreMismatches=" .. tostring(mismatches) ..
        " liveDrifts=" .. tostring(liveDrifts) ..
        " status=" .. (mismatches == 0 and "restored" or "restore_failed"))
    print(
        "NOVUS_CVAR_SEQUENCE_COMPLETE" ..
        " count=" .. tostring(#cvars) ..
        " restoreMismatches=" .. tostring(mismatches) ..
        " liveDrifts=" .. tostring(liveDrifts) ..
        " status=" .. (mismatches == 0 and "restored" or "restore_failed"))
end

Advance()
end

local cameraWatchdogHandle = nil
local cameraLoadRequested = false
local cameraLoadFinished = false
local sequenceFailed = false

local function FailSequence(reason: string)
    if sequenceFailed then
        return
    end

    sequenceFailed = true
    if cameraWatchdogHandle ~= nil then
        Scheduler.Cancel(cameraWatchdogHandle)
        cameraWatchdogHandle = nil
    end

    print("NOVUS_CVAR_SEQUENCE_FAILED reason=" .. reason)
end

local function StartCameraLoad()
    if cameraLoadRequested or sequenceFailed then
        return
    end

    local cameraSaves = Camera.GetSaveNames()
    table.sort(cameraSaves)

    local saveName = nil
    for _, candidate in cameraSaves do
        if candidate ~= "Default" then
            saveName = candidate
            break
        end
    end

    if saveName == nil then
        FailSequence("no-user-camera-save")
        return
    end

    cameraLoadRequested = true
    print("NOVUS_CVAR_SEQUENCE event=camera-started cameraSave=" .. saveName)
    if not Camera.LoadSave(saveName) then
        FailSequence("camera-request-rejected cameraSave=" .. saveName)
        return
    end

    cameraWatchdogHandle = Scheduler.AfterSeconds(CAMERA_WATCHDOG_SECONDS, function()
        if not cameraLoadFinished then
            FailSequence("camera-watchdog cameraSave=" .. saveName)
        end
    end)
end

RegisterEvent(GameEvent.MapLoaded, function(_eventID: Event, data: any)
    if sequenceFailed or not cameraLoadRequested or cameraLoadFinished or not data.isLoaded then
        return
    end

    cameraLoadFinished = true
    if cameraWatchdogHandle ~= nil then
        Scheduler.Cancel(cameraWatchdogHandle)
        cameraWatchdogHandle = nil
    end

    print(
        "NOVUS_CVAR_SEQUENCE event=camera-loaded" ..
        " map=" .. data.mapInternalName ..
        " dwellSeconds=" .. tostring(CAMERA_DWELL_SECONDS))
    Scheduler.AfterSeconds(CAMERA_DWELL_SECONDS, function()
        if sequenceFailed then
            return
        end

        print("NOVUS_CVAR_SEQUENCE event=sweep-started map=" .. data.mapInternalName)
        StartSweep()
    end)
end)

if Game.IsLoaded() then
    -- Let any buffered startup map event drain before requesting the camera save.
    Scheduler.AfterSeconds(0, StartCameraLoad)
else
    RegisterEvent(GameEvent.Loaded, function()
        Scheduler.AfterSeconds(0, StartCameraLoad)
    end)
end
