Scheduler.AfterFrames(30, function()
    if not RenderDoc.IsAvailable() then
        print("NOVUS_RENDERDOC_VALIDATION status=failed detail=unavailable")
        return
    end

    local queued, errorMessage =
        RenderDoc.CaptureNextFrame("validation-smoke/frame-renderdoc.rdc")
    if queued then
        print("NOVUS_RENDERDOC_VALIDATION status=queued")
    else
        print("NOVUS_RENDERDOC_VALIDATION status=failed detail=" .. errorMessage)
    end
end)
