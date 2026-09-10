-- Permanent animation counters and reproducible pose/capture output.
local Button=require("@src/API/UI/Button")
local InputBox=require("@src/API/UI/InputBox")
local Registry=require("@src/API/Editor/Registry")
local Session=require("@src/API/Editor/AnimationPreviewSession")
local Window=require("@src/API/Editor/UI/Window")
local canvas=UI.GetCanvas("AnimationDiagnostics",vec.New(0,0),vec.New(1920,1080),false)
local window=Window.New(canvas,vec.New(16,-420),vec.New(660,580),1140i,{title="Animation Diagnostics",titleHeight=22,padding=10,windowColor=vec.New(0.055,0.063,0.078),titleColor=vec.New(0.035,0.04,0.055),alpha=0.98})
local root=window.root
local function Anchor(w) w:SetAnchor(vec.New(0,1)); w:SetRelativePoint(vec.New(0,1)); return w end
local function Text(s,x,y)
    local t=Anchor(root:NewText(s,vec.New(x,y),2i,"DevToolsText")); t:SetFontSize(10); t:SetColor(vec.New(0.72,0.78,0.84)); t:SetInteractable(false); return t
end
local function Btn(s,x,y,w,fn)
    local b=Anchor(Button.New(root,vec.New(x,y),vec.New(w,25),2i,{text=s,textTemplate="DevToolsText",borderColor=vec.New(0.22,0.27,0.34),borderSize=1})); b:SetOnMouseUp(fn); return b
end
local function Input(s,x,y,w)
    local t=Anchor(InputBox.New(root,vec.New(x,y),vec.New(w,25),2i,{panelTemplate="DebugDarkGrey",textTemplate="DevToolsText",defaultText=s})); t:SetTextVerticallyCentered(); return t
end
local page=0
local failuresOnly=false
local generation=0
local rows={}
local status=Text("Counters are sampled only while this window is open.",12,-39)
local category=Text("",12,-69)
for i=1,16 do rows[i]={label=Text("",12,-101-(i-1)*22),value=Text("",500,-101-(i-1)*22)} end
local function Refresh()
    local stats=Asset.GetRenderAssetStats()
    local keys={}
    for key,value in stats do if string.sub(key,1,9)=="animation" and type(value)~="table" then table.insert(keys,key) end end
    table.sort(keys)
    if failuresOnly then
        local failures=stats.animationAssetFailureDetails or {}
        page=math.clamp(page,0,math.max(0,math.ceil(#failures/16)-1))
        category:SetText(string.format("Asset failures / asset ID / reason   |   %d entries",#failures))
        for i,row in rows do row.label:SetText(string.sub(failures[page*16+i] or "",1,98)); row.value:SetText("") end
        if #failures==0 then rows[1].label:SetText("No resident asset failures") end
        return
    end
    page=math.clamp(page,0,math.max(0,math.ceil(#keys/16)-1))
    category:SetText(string.format("Runtime / memory / queues / fallbacks   |   page %d of %d",page+1,math.ceil(#keys/16)))
    for i,row in rows do
        local key=keys[page*16+i]
        local name=key and string.sub(key,10) or ""
        name=string.gsub(name,"(%l)(%u)","%1 %2")
        row.label:SetText(name); row.value:SetText(key and tostring(stats[key]) or "")
    end
end
Btn("Previous",472,-38,80,function() page=math.max(0,page-1); Refresh() end)
Btn("Next",558,-38,90,function() page+=1; Refresh() end)
local captureName=Input("animation-inspection",12,-466,226)
local fps=Input("20",244,-466,50)
local seconds=Input("15",300,-466,50)
local recording=false
local captureGeneration=0
local restorePlayback=nil
local function FinishCapture(message)
    recording=false; captureGeneration+=1
    if restorePlayback then restorePlayback(); restorePlayback=nil end
    status:SetText(message)
end
Btn("Dump pose",356,-466,92,function()
    local entity=Session.GetSnapshot().entity
    if not entity then status:SetText("No preview actor"); return end
    local ok,result=Asset.AnimationDump(entity,captureName:GetText())
    status:SetText(ok and "Pose and controller dump written to animation-diagnostics" or result)
end)
Btn("Screenshot",454,-466,94,function()
    local ok,error=RenderTarget.Dump("SceneColor","animation-captures/"..captureName:GetText()..".png")
    status:SetText(ok and "Screenshot queued" or error)
end)
local function StartCapture(name,rate,duration)
    if recording then status:SetText("A capture is already running"); return end
    rate=math.clamp(rate,1,60); duration=math.clamp(duration,1,60)
    if not string.match(name,"^[%w_-]+$") then status:SetText("Use a simple capture name"); return end
    local snapshot=Session.GetSnapshot()
    if not snapshot.entity then status:SetText("No preview actor"); return end
    local timeScale=nil
    for _,cvar in Editor.GetCVars() do if cvar.name=="animationControllerTimeScale" then timeScale=cvar; break end end
    if not timeScale then status:SetText("Animation time control unavailable"); return end
    local direct=Asset.GetAnimationState(snapshot.entity)
    Editor.SetCVar(timeScale.category,timeScale.name,0)
    if direct then Asset.AnimationPause(snapshot.entity) end
    restorePlayback=function()
        Editor.SetCVar(timeScale.category,timeScale.name,timeScale.value)
        if direct and direct.playing then Asset.AnimationPlay(snapshot.entity) end
    end
    recording=true; captureGeneration+=1
    local current=captureGeneration
    local count=math.floor(rate*duration)
    local frame=0
    local function Capture()
        if current~=captureGeneration then return end
        if not RenderTarget.IsIdle() then Scheduler.AfterFrames(1,Capture); return end
        if frame>=count then FinishCapture("Sequence complete: "..tostring(count).." frames"); return end
        local currentPreview=Session.GetSnapshot()
        if currentPreview.entity~=snapshot.entity or currentPreview.mode~=snapshot.mode then FinishCapture("Capture stopped: preview changed"); return end
        if direct then
            local time=direct.time+frame/rate*direct.playbackRate
            if direct.looping and direct.duration>0 then time=time%direct.duration else time=math.clamp(time,0,direct.duration) end
            if not Asset.AnimationSeek(snapshot.entity,time) then FinishCapture("Preview no longer available"); return end
        else
            local steps=math.ceil(10/rate)
            for _=1,steps do
                if not Asset.StepAnimationControllers(1/rate/steps) then FinishCapture("Animation step failed"); return end
            end
        end
        Scheduler.AfterFrames(3,function()
            if current~=captureGeneration then return end
            local ok,error=RenderTarget.Dump("SceneColor",string.format("animation-captures/%s/frame-%04d.png",name,frame))
            if not ok then FinishCapture(error); return end
            frame+=1; status:SetText(string.format("Recording %d / %d",frame,count))
            Scheduler.AfterFrames(1,Capture)
        end)
    end
    Capture()
end
Btn("Sequence",554,-466,94,function() StartCapture(captureName:GetText(),tonumber(fps:GetText()) or 20,tonumber(seconds:GetText()) or 15) end)
Text("Capture name                         FPS     Seconds",12,-505)
Btn("Stop capture",12,-536,106,function() FinishCapture("Capture stopped") end)
Btn("Graph",124,-536,80,function() Registry.Toggle("AnimationGraph") end)
Btn("Assets",210,-536,80,function() Registry.Toggle("AnimationAssets") end)
Btn("Preview",296,-536,80,function() Registry.Toggle("AnimationDebug") end)
Btn("Failures / stats",382,-536,142,function() failuresOnly=not failuresOnly; page=0; Refresh() end)
local methods={}
function methods:Capture(name,rate,duration) StartCapture(name,rate,duration) end
function methods:StopCapture() FinishCapture("Capture stopped") end
function methods:IsRecording() return recording end
function methods:Show()
    root:SetVisible(true); generation+=1
    local current=generation
    local function Tick() if generation~=current or not root:IsVisible() then return end; Refresh(); Scheduler.AfterSeconds(0.25,Tick) end
    Tick()
end
function methods:Hide() root:SetVisible(false); generation+=1 end
function methods:IsVisible() return root:IsVisible() end
window.closeButton:SetOnMouseUp(function() methods:Hide() end)
Registry.Register("AnimationDiagnostics",methods)
