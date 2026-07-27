local completed = false

local function finish(status: string, detail: string)
    if completed then
        return
    end

    completed = true
    print("NOVUS_CAPTURE_SMOKE_COMPLETE status=" .. status .. " detail=" .. detail)
end

local function queueCaptures()
    local sceneQueued, sceneError =
        RenderTarget.Dump("SceneColor", "validation-smoke/scene-color.png")
    if not sceneQueued then
        finish("failed", "scene-color:" .. sceneError)
        return
    end

    Scheduler.AfterFrames(10, function()
        local visibilityQueued, visibilityError =
            RenderTarget.Dump("VisibilityBuffer", "validation-smoke/visibility-buffer.png")
        if not visibilityQueued then
            finish("failed", "visibility-buffer:" .. visibilityError)
            return
        end

        Scheduler.AfterFrames(10, function()
            if not RenderDoc.IsAvailable() then
                finish("failed", "renderdoc-unavailable")
                return
            end

            local renderDocQueued, renderDocError =
                RenderDoc.CaptureNextFrame("validation-smoke/frame.rdc")
            if not renderDocQueued then
                finish("failed", "renderdoc:" .. renderDocError)
                return
            end

            Scheduler.AfterFrames(120, function()
                finish("queued", "render-targets-and-renderdoc")
            end)
        end)
    end)
end

local function start()
    local cameraSaves = Camera.GetSaveNames()
    table.sort(cameraSaves)

    local cameraSave = nil
    for _, saveName in cameraSaves do
        if saveName ~= "Default" then
            cameraSave = saveName
            break
        end
    end

    if cameraSave == nil then
        finish("failed", "no-user-camera-save")
        return
    end

    local mapLoaded = false
    RegisterEvent(GameEvent.MapLoaded, function()
        if mapLoaded then
            return
        end

        mapLoaded = true
        Scheduler.AfterSeconds(5, queueCaptures)
    end)

    if not Camera.LoadSave(cameraSave) then
        finish("failed", "camera-load-rejected")
    end
end

if Game.IsLoaded() then
    Scheduler.AfterSeconds(0, start)
else
    RegisterEvent(GameEvent.Loaded, function()
        Scheduler.AfterSeconds(0, start)
    end)
end
