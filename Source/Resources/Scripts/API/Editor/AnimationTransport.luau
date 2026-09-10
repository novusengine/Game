-- Playback controls stay available while the asset inspector changes context.
local Button=require("@src/API/UI/Button")
local InputBox=require("@src/API/UI/InputBox")
local Dropdown=require("@src/API/UI/Dropdown")
local api={}
local function Anchor(w) w:SetAnchor(vec.New(0,1)); w:SetRelativePoint(vec.New(0,1)); return w end
function api.New(root,canvas,getEntity)
    local self={snap=false,frames=false,fps=30}
    local function Btn(text,x,width,callback)
        local button=Anchor(Button.New(root,vec.New(x,-32),vec.New(width,26),3i,{text=text,textTemplate="DevToolsText",borderSize=0}))
        button:SetOnMouseUp(callback); return button
    end
    local hint=Anchor(root:NewText("",vec.New(580,-39),3i,"DevToolsText"))
    hint:SetFontSize(10); hint:SetColor(vec.New(0.68,0.75,0.82)); hint:SetInteractable(false)
    local function TimeScale()
        for _,cvar in Editor.GetCVars() do if cvar.name=="animationControllerTimeScale" then return cvar end end
        return nil
    end
    function self:GetState()
        local entity=getEntity()
        if not entity then return nil end
        local direct=Asset.GetAnimationState(entity)
        if direct then direct.direct=true; return direct end
        local controller=Asset.GetAnimationControllerState(entity)
        if not controller then return nil end
        local scale=TimeScale()
        local time=(tonumber(tostring(controller.stateTimeUS)) or 0)/1000000
        return {direct=false,time=time,duration=math.max(10,math.ceil(time/10)*10),playing=scale~=nil and scale.value>0,playbackRate=scale and scale.value or 1}
    end
    function self:Pause()
        local state=self:GetState(); if not state then return false end
        if state.direct then return Asset.AnimationPause(getEntity()) end
        local scale=TimeScale(); if not scale then return false end
        Editor.SetCVar(scale.category,scale.name,0); return true
    end
    function self:Play()
        local state=self:GetState(); if not state then return false end
        if state.direct then return Asset.AnimationPlay(getEntity()) end
        local scale=TimeScale(); if not scale then return false end
        Editor.SetCVar(scale.category,scale.name,1); return true
    end
    function self:Seek(seconds)
        local state=self:GetState()
        if not state or not state.direct then return false end
        self:Pause()
        return Asset.AnimationSeek(getEntity(),math.clamp(seconds,0,state.duration))
    end
    function self:Step(direction)
        local state=self:GetState(); if not state then return false end
        self:Pause()
        if state.direct then return self:Seek(state.time+direction/self.fps) end
        if direction<0 then return false end
        Asset.StepAnimationControllers(1/self.fps); return true
    end
    local start=Btn("|<",12,38,function() self:Seek(0) end)
    local back=Btn("<",56,32,function() self:Step(-1) end)
    local play=Btn("Play",94,66,function() local state=self:GetState(); if state and state.playing then self:Pause() else self:Play() end end)
    local step=Btn(">",166,32,function() self:Step(1) end)
    local loop
    loop=Btn("Loop",204,58,function() local state=self:GetState(); if state and state.direct then Asset.AnimationSetLooping(getEntity(),not state.looping) end end)
    local unit=Anchor(Dropdown.New(root,canvas,vec.New(268,-32),vec.New(100,26),3i,{options={{label="Seconds",value=false},{label="Frames",value=true}},value=false,button={textTemplate="DevToolsText",borderSize=0},menuLayer=1190i}))
    unit:SetOnValueChanged(function(value) self.frames=value end)
    local snap
    snap=Btn("Snap: off",374,84,function() self.snap=not self.snap; snap:SetText(self.snap and "Snap: 30fps" or "Snap: off") end)
    local time=Anchor(InputBox.New(root,vec.New(464,-32),vec.New(100,26),3i,{defaultText="0",panelTemplate="DebugDarkGrey",textTemplate="DevToolsText"}))
    time:SetTextVerticallyCentered()
    local editingTime=false
    time:SetOnFocusBegin(function() editingTime=true end)
    time:SetOnFocusEnd(function() editingTime=false end)
    time:SetOnSubmit(function() local value=tonumber(time:GetText()); if value then self:Seek(self.frames and value/self.fps or value) end end)
    function self:Snap(seconds) return self.snap and math.floor(seconds*self.fps+0.5)/self.fps or seconds end
    function self:Label(seconds) return self.frames and string.format("%.0ff",seconds*self.fps) or string.format("%.2fs",seconds) end
    function self:Tick()
        local state=self:GetState()
        play:SetText(state and state.playing and "Pause" or "Play"); play:SetInteractable(state~=nil)
        start:SetInteractable(state~=nil and state.direct); back:SetInteractable(state~=nil and state.direct)
        start:SetAlpha(state and state.direct and 1 or 0.3); back:SetAlpha(state and state.direct and 1 or 0.3)
        step:SetInteractable(state~=nil); loop:SetInteractable(state~=nil and state.direct)
        loop:SetAlpha(state and state.direct and 1 or 0.3); loop:SetHighlighted(state~=nil and state.looping==true)
        time:SetInteractable(state~=nil and state.direct)
        if state and not editingTime then time:SetText(self.frames and string.format("%.0f",state.time*self.fps) or string.format("%.3f",state.time)) end
        hint:SetText(not state and "Create a character in Preview setup." or state.direct and "Clip preview / scrub or enter time / frame display: 30 fps" or "State machine / pause / step forward / elapsed state time")
        return state
    end
    return self
end
return api
