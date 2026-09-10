-- Animation asset drafts share the preview actor; Apply publishes at the next simulation boundary.
local Button = require("@src/API/UI/Button")
local InputBox = require("@src/API/UI/InputBox")
local Registry = require("@src/API/Editor/Registry")
local Session = require("@src/API/Editor/AnimationPreviewSession")
local Window = require("@src/API/Editor/UI/Window")
local ShapeUtil = require("@src/API/UI/ShapeSurfaceUtil")
local JointHierarchy = require("@src/API/Editor/JointHierarchy")
local Properties = require("@src/API/Editor/AnimationProperties")
local Transport = require("@src/API/Editor/AnimationTransport")
local Dropdown = require("@src/API/UI/Dropdown")
local Modal = require("@src/API/UI/Modal")
local Tooltip = require("@src/API/UI/Tooltip")
local canvas = UI.GetCanvas("AnimationAssets",vec.New(0,0),vec.New(1920,1080),false)
local window = Window.New(canvas,vec.New(16,-80),vec.New(460,730),1130i,{
    title="Animation Properties",titleHeight=26,padding=10,
    windowColor=vec.New(0.055,0.063,0.078),titleColor=vec.New(0.035,0.04,0.055),alpha=0.98,
})
local root=window.root
local tooltip=Tooltip.New(canvas,vec.New(290,36),1201i)
local function Tip(button,message)
    button:SetOnHoverBegin(function()
        tooltip:Clear(); tooltip:AddLine(message,"DevToolsText")
        tooltip:ShowAt(UI.GetMousePos()+vec.New(12,-20))
    end)
    button:SetOnHoverEnd(function() tooltip:Hide() end)
end
local hierarchy
local entity=nil
local picking=false
local chainJoints={}
local jointOverlay=canvas:NewPanel(vec.New(0,0),vec.New(1920,1080),1129i,"TexturePanel")
local jointSurface=UI.GetShapeSurface("AnimationJointSelection",vec.New(1920,1080))
jointOverlay:SetBackground(jointSurface); jointOverlay:SetInteractable(false)
local function PickJoint(point)
    local nearest,distance=nil,14
    for _,joint in entity and Asset.GetAnimationJointScreenPositions(entity) or {} do
        local d=vec.magnitude(point-vec.New(joint.x*1920,joint.y*1080))
        if d<distance then nearest=tonumber(tostring(joint.index)); distance=d end
    end
    if nearest then hierarchy:Select(nearest,true) end
    return nearest
end
local timelineWindow=Window.New(canvas,vec.New(16,-830),vec.New(1884,230),1131i,{
    title="Animation Timeline",titleHeight=26,padding=10,
    windowColor=vec.New(0.055,0.063,0.078),titleColor=vec.New(0.035,0.04,0.055),alpha=0.98,
})
local timeline=timelineWindow.root:NewPanel(vec.New(190,-70),vec.New(1682,90),2i,"TexturePanel")
timeline:SetAnchor(vec.New(0,1)); timeline:SetRelativePoint(vec.New(0,1))
local timelineSurface=UI.GetShapeSurface("AnimationAssetTimeline",vec.New(1682,90))
timeline:SetBackground(timelineSurface); timeline:SetVisible(false)
local timelineEntries={}
local timelineLabels={}
local rulerLabels={}
local cursorLabel
local timelineDuration=1
local transport=Transport.New(timelineWindow.root,canvas,function() return entity end)
local function DrawWindowSpans()
    for _,start in timelineEntries do
        if start.collection=="actionWindows" and string.find(start.field.path,"/startTimeMicroseconds$") then
            for _,finish in timelineEntries do
                if finish.collection==start.collection and finish.index==start.index and string.find(finish.field.path,"/endTimeMicroseconds$") then
                    ShapeUtil.DrawLine(timelineSurface,start.point,finish.point,{width=5,color=vec.New(0.95,0.65,0.25),alpha=0.65})
                end
            end
        end
    end
end
local draggedMarker=nil
local function Anchor(w) w:SetAnchor(vec.New(0,1)); w:SetRelativePoint(vec.New(0,1)); return w end
local function Text(s,x,y,c)
    local w=Anchor(root:NewText(s,vec.New(x,y),2i,"DevToolsText"))
    w:SetFontSize(10); w:SetColor(c or vec.New(0.68,0.73,0.80)); w:SetInteractable(false); return w
end
local function Btn(s,x,y,w,fn)
    local b=Anchor(Button.New(root,vec.New(x,y),vec.New(w,25),2i,{text=s,textTemplate="DevToolsText",borderColor=vec.New(0.22,0.27,0.34),borderSize=1}))
    b:SetOnMouseUp(fn); return b
end
local function Input(s,x,y,w)
    local b=Anchor(InputBox.New(root,vec.New(x,y),vec.New(w,25),2i,{panelTemplate="DebugDarkGrey",textTemplate="DevToolsText",defaultText=s}))
    b:SetTextVerticallyCentered(); return b
end
local types={"Skeleton","Rig family","Clip","Animation set","Bone mask","IK rig"}
local collections={
    {"joints","familyBindings","propagationRules","attachments","hierarchyDepthRanges","hierarchyDepthJointIndices"},
    {"joints","chains","chainJointIndices"},
    {"events","synchronizationMarkers","actionWindows","eventPayloadBytes"},
    {"slots","variants"}, {"jointWeights"}, {"chains","chainJointIndices","jointLimits","solvers"},
}
local paths={"character.skeleton","animation/bipedal_humanoid.rigfamily","orc_idle.animationclip","animations/mixamo/mixamo.animationset","character_upper_body.bonemask","character.ikrig"}
local typeIndex=3
local document=nil
local group="events"
local item=0
local advanced=false
local propertyInspector
local collectionPicker,itemPicker,advancedButton
local addButton,removeButton
local ConfirmAction
local fields={}
local status=Text("Open an asset to edit its properties.",12,-162)
local path=Input(paths[typeIndex],12,-74,334)
local name=Input("animation-asset",12,-126,244)
Text("Saved authoring source",12,-108)
local result=Text("",12,-671,vec.New(1,0.67,0.32))
local publication=Text("",12,-650)
local liveIK=Text("",12,-650,vec.New(0.3,0.8,0.95))
local Refresh,Commit,Command
local function SetCVar(name,value)
    for _,cvar in Editor.GetCVars() do if cvar.name==name then Editor.SetCVar(cvar.category,cvar.name,value); return end end
end
local timelineTick=0
local function Short(s,n) return #s>n and string.sub(s,1,n-3).."..." or s end
local function FieldChoices(field)
    if not field then return nil end
    if string.match(field.path,"^/data/solvers/%d+/type$") then return {"Two-bone IK","Aim","Look at","Iterative chain"} end
    if string.match(field.path,"/goalSpace$") then return {"Model space","World space","Joint space"} end
    if string.match(field.path,"PoseDomainType$") or string.match(field.path,"/poseDomainType$") then return {"Rig family","Skeleton"} end
    if string.match(field.path,"^/data/propagationRules/%d+/type$") then return {"Copy local transform","Copy model transform","Distribute rotation"} end
    return nil
end
Refresh=function()
    entity=Session.GetSnapshot().entity
    document=entity and Asset.AnimationAssetDocument(entity) or nil
    if document and tonumber(tostring(document.type))~=typeIndex-1 then document=nil end
    if not document then status:SetText("Press Open to load the selected asset type"); result:SetText(""); publication:SetText("") end
    liveIK:SetVisible(document~=nil and typeIndex==6)
    publication:SetVisible(typeIndex~=6)
    fields={}
    local prefix=group=="asset" and "/asset/" or "/data/"..group.."/"..tostring(item)
    if document then
        status:SetText(document.dirty and "Draft / unsaved source changes" or "Source loaded / edits stay in the draft until Apply")
        result:SetText(Short(document.status,66)); publication:SetText(Short(document.publication,66))
        for _,field in document.fields do
            if string.sub(field.path,1,#prefix)==prefix and (group=="asset" or #field.path==#prefix or string.sub(field.path,#prefix+1,#prefix+1)=="/") then table.insert(fields,field) end
        end
    end
    if propertyInspector then propertyInspector:SetFields(fields,prefix,document,advanced) end
    if collectionPicker then
        local options={}
        local labels={asset="Asset settings",joints="Joints",familyBindings="Family mapping",propagationRules="Propagation",attachments="Attachments",events="Events",synchronizationMarkers="Sync markers",actionWindows="Action windows",slots="Animation slots",variants="Clip variants",jointWeights="Bone mask",chains="IK chains",jointLimits="Joint limits",solvers="Solvers"}
        for _,collection in collections[typeIndex] do
            if advanced or labels[collection] then table.insert(options,{label=labels[collection] or collection,value=collection}) end
        end
        table.insert(options,{label="Asset settings",value="asset"})
        collectionPicker:SetOptions(options); collectionPicker:SetValue(group,false)
        local items={}
        if document then
            local indices={}
            for _,field in document.fields do
                local index=string.match(field.path,"^/data/"..group.."/(%d+)")
                if index then indices[tonumber(index)]=true end
            end
            local values={}; for _,field in document.fields do values[field.path]=field.value end
            local order={}; for index in indices do table.insert(order,index) end; table.sort(order)
            for _,index in order do
                local label=(labels[group] or group).." "..tostring(index+1)
                if group=="joints" or group=="jointWeights" or group=="jointLimits" then
                    local jointIndex=group=="jointLimits" and tonumber(values["/data/jointLimits/"..index.."/jointIndex"]) or index
                    for _,joint in document.hierarchy or {} do if tonumber(tostring(joint.index))==jointIndex then label=joint.name; break end end
                elseif group=="chains" then
                    local effector=tonumber(values["/data/chains/"..index.."/effectorJointIndex"])
                    for _,joint in document.hierarchy or {} do if tonumber(tostring(joint.index))==effector then label=joint.name.." chain"; break end end
                elseif group=="events" or group=="synchronizationMarkers" or group=="actionWindows" then
                    local time=tonumber(values["/data/"..group.."/"..index.."/timeMicroseconds"] or values["/data/"..group.."/"..index.."/startTimeMicroseconds"])
                    if time then label=label.." / "..string.format("%.2fs",time/1000000) end
                end
                table.insert(items,{label=label,value=index})
            end
        end
        itemPicker:SetOptions(items); itemPicker:SetValue(item,false); itemPicker:SetVisible(group~="asset")
        addButton:SetVisible(group~="asset" and group~="jointWeights")
        removeButton:SetVisible(group~="asset" and group~="jointWeights" and #items>0)
    end
    if hierarchy then
        hierarchy:SetDocument(document,typeIndex-1)
        chainJoints={}
        if document and (typeIndex==6 or typeIndex==2) and (group=="chains" or group=="solvers") then
            local values={}
            for _,field in document.fields do values[field.path]=tonumber(field.value) end
            local chainIndex=group=="solvers" and values["/data/solvers/"..item.."/chainIndex"] or item
            local prefix="/data/chains/"..tostring(chainIndex).."/"
            local offset=values[prefix.."jointIndexOffset"] or 0
            local count=values[prefix.."numJointIndices"] or 0
            for i=0,count-1 do
                local index=values["/data/chainJointIndices/"..tostring(offset+i)]
                if index then chainJoints[index]=i==0 and "Root" or i==count-1 and "Effector" or "Middle" end
            end
        end
        hierarchy:SetChain(chainJoints)
        if document and typeIndex~=3 and typeIndex~=4 and root:IsVisible() then hierarchy:Show() else hierarchy:Hide() end
    end
    timelineWindow.root:SetVisible(root:IsVisible())
    timeline:SetVisible(entity~=nil)
    for _,label in timelineLabels do label:SetVisible(document~=nil and typeIndex==3) end
    for _,label in rulerLabels do label:SetVisible(entity~=nil) end
    cursorLabel:SetVisible(true)
    timelineSurface:Clear(); timelineEntries={}
    if document and typeIndex==3 then
        for _,field in document.fields do
            if field.path=="/asset/durationMicroseconds" then timelineDuration=math.max(1,tonumber(field.value) or 1) end
        end
        for i,label in rulerLabels do label:SetText(string.format("%.2fs",(i-1)/4*timelineDuration/1000000)) end
        local lanes={events=70,synchronizationMarkers=40,actionWindows=10}
        for _,y in lanes do ShapeUtil.DrawLine(timelineSurface,vec.New(12,y),vec.New(1672,y),{width=1,color=vec.New(0.2,0.25,0.32)}) end
        for _,field in document.fields do
            local collection,index,property=string.match(field.path,"^/data/([^/]+)/(%d+)/([^/]+)$")
            if collection and lanes[collection] and (string.find(property,"TimeMicroseconds") or property=="timeMicroseconds") then
                local point=vec.New(12+1660*(tonumber(field.value) or 0)/timelineDuration,lanes[collection])
                timelineSurface:DrawCircle(point,6,vec.New(0.3,0.7,1),1)
                table.insert(timelineEntries,{field=field,point=point,collection=collection,index=tonumber(index)})
            end
        end
        local state=entity and Asset.GetAnimationState(entity)
        DrawWindowSpans()
        if state then
            local x=12+1660*state.time*1000000/timelineDuration
            ShapeUtil.DrawLine(timelineSurface,vec.New(x,3),vec.New(x,87),{width=2,color=vec.New(0.2,0.88,0.48)})
        end
    end

end
timeline:SetOnMouseDown(function(_,_,button,x,y)
    if button~=0i then return end
    local point=ShapeUtil.ScreenToSurface(timeline,timelineSurface,UI.GetMousePos())
    local nearest,distance=nil,14
    for _,entry in timelineEntries do local d=vec.magnitude(point-entry.point); if d<distance then nearest=entry; distance=d end end
    draggedMarker=nearest
    if nearest then
        group=nearest.collection; item=nearest.index; Refresh()
        for _,entry in timelineEntries do if entry.field.path==nearest.field.path then draggedMarker=entry; break end end
    elseif entity then transport:Seek(transport:Snap(math.clamp((point.x-12)/1660,0,1)*timelineDuration/1000000)) end
end)
timeline:SetOnMouseHeld(function(_,_,x,y)
    local point=ShapeUtil.ScreenToSurface(timeline,timelineSurface,UI.GetMousePos())
    local seconds=transport:Snap(math.clamp((point.x-12)/1660,0,1)*timelineDuration/1000000)
    if not draggedMarker then transport:Seek(seconds); return end
    draggedMarker.value=string.format("%.0f",seconds*1000000)
    draggedMarker.point=vec.New(12+1660*seconds*1000000/timelineDuration,draggedMarker.point.y)
end)
timeline:SetOnMouseRelease(function(_,_,button)
    if button~=0i or not draggedMarker then return end
    if draggedMarker.value then Asset.AnimationAssetEditField(entity,draggedMarker.field.path,draggedMarker.value) end
    draggedMarker=nil; Refresh()
end)
Commit=function()
    return not propertyInspector or propertyInspector:Commit()
end
Command=function(command,arg,index)
    if not entity or not document then return false end
    if command~="undo" and command~="redo" and command~="revert" and not Commit() then return false end
    local ok,error=Asset.AnimationAssetCommand(entity,command,arg or name:GetText(),integer.create(index or item))
    Refresh()
    if not ok then result:SetText(Short(error,86)) end
    if ok and command=="apply" then
        Scheduler.AfterFrames(2,function()
            local preview=Registry.Get("AnimationDebug")
            if preview then preview:RefreshParameterBindings() end
            Session.NotifyGraphChanged(); Refresh()
        end)
    end
    return ok
end
local modal=Modal.New(canvas,vec.New(0,0),vec.New(480,190),1200i,{name="AnimationAssetConfirmation",confirmText="Continue",cancelText="Cancel",dialogColor=vec.New(0.07,0.08,0.10),titleTemplate="DevToolsText",messageTemplate="DevToolsText",messageWidth=420})
ConfirmAction=function(title,action) modal:Open({title=title,message="This replaces or removes draft data. Save your source first if you want to keep it.",onConfirm=action}) end
local typeOptions={}; for i,label in types do table.insert(typeOptions,{label=label,value=i}) end
local typeButton=Anchor(Dropdown.New(root,canvas,vec.New(12,-38),vec.New(192,26),3i,{options=typeOptions,value=typeIndex,button={textTemplate="DevToolsText",textAlign="left",borderSize=0},menuLayer=1190i}))
typeButton:SetOnValueChanged(function(value)
    if not Commit() then return false end
    typeIndex=value; path:SetText(paths[typeIndex]); group=collections[typeIndex][1]; item=0; Refresh()
end)
Btn("Preview setup",212,-38,116,function() Registry.Get("AnimationAssets"):Hide(); Registry.Get("AnimationDebug"):Show() end)
Btn("State graph",334,-38,114,function() Registry.Get("AnimationAssets"):Hide(); Registry.Get("AnimationGraph"):Show() end)
Btn("Open asset",352,-74,96,function()
    local function Open()
        entity=Session.GetSnapshot().entity
        if not entity then result:SetText("Create a character in Preview setup first"); return end
        local nextDoc,error=Asset.AnimationAssetDocument(entity,path:GetText(),integer.create(typeIndex-1))
        if nextDoc then item=0; Refresh() else result:SetText(error or "Open failed") end
    end
    local current=Session.GetSnapshot().entity
    local previous=current and Asset.AnimationAssetDocument(current)
    if previous and previous.dirty then ConfirmAction("Replace the current draft?",Open) else Open() end
end)
Btn("Save source",262,-126,90,function() Command("save") end)
Btn("Load source",358,-126,90,function() ConfirmAction("Load saved source?",function() Command("load") end) end)
collectionPicker=Anchor(Dropdown.New(root,canvas,vec.New(12,-188),vec.New(274,26),3i,{button={textTemplate="DevToolsText",textAlign="left",borderSize=0},menuLayer=1190i}))
collectionPicker:SetOnValueChanged(function(value) if not Commit() then return false end; group=value; item=0; Refresh() end)
advancedButton=Btn("Advanced: off",292,-188,156,function()
    if not Commit() then return end
    advanced=not advanced; advancedButton:SetText(advanced and "Advanced: on" or "Advanced: off"); Refresh()
end)
itemPicker=Anchor(Dropdown.New(root,canvas,vec.New(12,-220),vec.New(352,26),3i,{placeholder="No entries",button={textTemplate="DevToolsText",textAlign="left",borderSize=0},menuLayer=1190i,menuWidth=352,itemHeight=22}))
itemPicker:SetOnValueChanged(function(value)
    if not Commit() then return false end
    item=value
    if group=="joints" or group=="jointWeights" then hierarchy:Select(item,true) else Refresh() end
end)
addButton=Btn("+",370,-220,36,function() Command("insert","/data/"..group) end)
removeButton=Btn("-",412,-220,36,function() ConfirmAction("Remove selected entry?",function() Command("remove","/data/"..group,item) end) end)
Tip(addButton,"Add an entry to the selected collection")
Tip(removeButton,"Remove the selected entry from the draft")
Btn("Undo",12,-695,64,function() Command("undo") end)
Btn("Redo",82,-695,64,function() Command("redo") end)
Btn("Revert",152,-695,76,function() ConfirmAction("Revert this draft?",function() Command("revert") end) end)
local apply=Btn("Apply to preview",290,-695,158,function() Command("apply") end)
apply.root:SetColor(vec.New(0.10,0.27,0.36))
propertyInspector=Properties.New(root,canvas,{
    edit=function(field,value) return Asset.AnimationAssetEditField(entity,field,value) end,
    changed=function() Refresh() end,
    error=function(error) result:SetText(Short(error,66)) end,
    choices=FieldChoices,
    paint=function(falloff)
        if not falloff or falloff<0 or falloff>1 then result:SetText("Falloff must be between 0 and 1"); return end
        local current=Asset.AnimationAssetDocument(entity)
        local weight=0
        for _,field in current.fields do if field.path=="/data/jointWeights/"..item then weight=(tonumber(field.value) or 0)/255 end end
        local ok,error=Asset.AnimationAssetCommand(entity,"paint",integer.create(item),weight,falloff,true)
        Refresh(); if not ok then result:SetText(Short(error,66)) end
    end,
    mirror=function(side) Command("mirror",side) end,
    goal=function(name,x,y,z)
        if not entity or not x or not y or not z then result:SetText("Enter a target parameter and three numbers"); return end
        local parameter=Asset.ResolveAnimationParameter(entity,name)
        if parameter and Asset.AnimationSetFloat3Parameter(entity,parameter,x,y,z) then result:SetText("Preview target updated") else result:SetText("Choose a Float3 parameter from the active graph") end
    end,
})
hierarchy=JointHierarchy.New(canvas,{
    select=function(index)
        if not Commit() then return false end
        if typeIndex==5 then group="jointWeights"; item=index
        elseif typeIndex==1 or typeIndex==2 then group="joints"; item=index end
        local concrete=-1
        for _,joint in entity and Asset.GetAnimationJointScreenPositions(entity) or {} do
            if tonumber(tostring(joint.index))==index then concrete=tonumber(tostring(joint.concreteIndex)); break end
        end
        SetCVar("animationDebugSkeletonSelectedJoint",integer.create(concrete))
        SetCVar("animationDebugSkeletonEnabled",1i)
        SetCVar("animationDebugSkeletonAxes",0i)
        Refresh()
        return true
    end,
    overlay=function()
        local p=Registry.Get("AnimationDebug"); if p then p:SetDebugView(true,typeIndex==5,false,typeIndex==6) end
    end,
    pick=function()
        picking=not picking; jointOverlay:SetInteractable(picking)
        result:SetText(picking and "Click a joint on the character. Right-click to cancel." or "Joint picking cancelled")
    end,
})
jointOverlay:SetOnMouseDown(function(_,_,button)
    if button==0i then
        local index=PickJoint(ShapeUtil.ScreenToSurface(jointOverlay,jointSurface,UI.GetMousePos()))
        if index==nil then result:SetText("No joint nearby; click a dot or right-click to cancel"); return end
        result:SetText("Joint selected in the skeleton tree")
    end
    picking=false; jointOverlay:SetInteractable(false)
end)
for i,text in {"Events","Sync markers","Action windows","Drag a marker to edit its time. Snap uses the 30 fps display grid.","Click or drag empty track space to scrub clip playback."} do
    local y=({-80,-110,-140,-188,-209})[i]
    local label=Anchor(timelineWindow.root:NewText(text,vec.New(18,y),3i,"DevToolsText"))
    label:SetFontSize(10); label:SetColor(vec.New(0.68,0.73,0.80)); label:SetInteractable(false); label:SetVisible(false)
    table.insert(timelineLabels,label)
end
for i=1,5 do
    local label=Anchor(timelineWindow.root:NewText("",vec.New(202+(i-1)*415,-166),3i,"DevToolsText"))
    if i==5 then label:SetRelativePoint(vec.New(1,1)) end
    label:SetFontSize(10); label:SetColor(vec.New(0.68,0.73,0.80)); label:SetInteractable(false); label:SetVisible(false)
    table.insert(timelineLabels,label); table.insert(rulerLabels,label)
end
cursorLabel=Anchor(timelineWindow.root:NewText("",vec.New(1460,-36),3i,"DevToolsText"))
cursorLabel:SetFontSize(10); cursorLabel:SetColor(vec.New(0.2,0.88,0.48)); cursorLabel:SetInteractable(false); cursorLabel:SetVisible(false)
table.insert(timelineLabels,cursorLabel)

local methods={}
function methods:Show()
    local preview=Registry.Get("AnimationDebug"); if preview then preview:Hide() end
    local graph=Registry.Get("AnimationGraph"); if graph then graph:Hide() end
    local author=Registry.Get("AnimationAuthoring"); if author then author:Hide() end
    root:SetVisible(true); jointOverlay:SetVisible(true); Refresh()
    timelineTick+=1; local generation=timelineTick
    local function Tick()
        if generation~=timelineTick or not root:IsVisible() then return end
        jointSurface:Clear()
        if entity and document and hierarchy.window.root:IsVisible() then
            local selected=hierarchy:GetSelected()
            for _,joint in Asset.GetAnimationJointScreenPositions(entity) do
                local index=tonumber(tostring(joint.index))
                local role=chainJoints[index]
                if picking or selected==index or role then
                    local point=vec.New(joint.x*1920,joint.y*1080)
                    local color=selected==index and vec.New(1,0.8,0.15) or role=="Root" and vec.New(0.2,0.8,1) or role=="Middle" and vec.New(1,0.5,0.2) or role=="Effector" and vec.New(0.3,1,0.45) or vec.New(0.5,0.75,0.9)
                    jointSurface:DrawCircle(point,selected==index and 7 or role and 5 or 3,color,1)
                end
            end
        end
        if typeIndex==6 and entity then
            local state=Asset.GetAnimationControllerState(entity)
            local ik=state and state.ikDiagnostics and state.ikDiagnostics[1]
            liveIK:SetText(ik and string.format("Goal %.2f, %.2f, %.2f | error %.4f | %s",ik.goalX,ik.goalY,ik.goalZ,ik.finalError,ik.clamped and "LIMITED" or "SOLVED") or "No active IK operation")
        end
        local playback=transport:Tick()
        if entity then
            timelineSurface:Clear()
            local metadata=typeIndex==3 and document~=nil
            if not metadata and playback then timelineDuration=math.max(1,playback.duration*1000000) end
            for _,y in metadata and {70,40,10} or {40} do ShapeUtil.DrawLine(timelineSurface,vec.New(12,y),vec.New(1672,y),{width=1,color=vec.New(0.2,0.25,0.32)}) end
            for _,entry in timelineEntries do timelineSurface:DrawCircle(entry.point,6,vec.New(0.3,0.7,1),1) end
            DrawWindowSpans()
            for i,label in rulerLabels do label:SetText(transport:Label((i-1)/4*timelineDuration/1000000)) end
            local state=playback
            if state then
                cursorLabel:SetText(string.format("%s  |  %s",state.direct and "Clip time" or "State time",transport:Label(state.time)))
                local x=12+1660*state.time*1000000/timelineDuration
                if not metadata or state.direct then ShapeUtil.DrawLine(timelineSurface,vec.New(x,3),vec.New(x,87),{width=2,color=vec.New(0.2,0.88,0.48)}) end
            end
        end
        Scheduler.AfterFrames(1,Tick)
    end
    Tick()
end
function methods:Hide() tooltip:Hide(); root:SetVisible(false); hierarchy:Hide(); timelineWindow.root:SetVisible(false); jointOverlay:SetVisible(false); picking=false; jointOverlay:SetInteractable(false); timelineTick+=1 end
function methods:IsVisible() return root:IsVisible() end
function methods:Open(nextPath,nextType)
    typeIndex=nextType+1; typeButton:SetValue(typeIndex,false); path:SetText(nextPath)
    group=collections[typeIndex][1]; entity=Session.GetSnapshot().entity
    local doc,error=Asset.AnimationAssetDocument(entity,nextPath,integer.create(nextType)); Refresh(); return doc~=nil,error
end
function methods:Select(nextGroup,nextItem) group=nextGroup; item=nextItem or 0; Refresh() end
function methods:Edit(field,value) local ok,error=Asset.AnimationAssetEditField(entity,field,tostring(value)); Refresh(); return ok,error end
function methods:Command(command,arg,index) return Command(command,arg,index) end
function methods:Refresh() Refresh() end
function methods:SelectJoint(index) return hierarchy:Select(index,true) end
function methods:SearchJoints(query) hierarchy:Search(query) end
function methods:VisibleJointCount() return hierarchy:VisibleCount() end
function methods:PickJointAt(x,y) return PickJoint(vec.New(x,y)) end
function methods:ExpandJoints(expand) if expand then hierarchy:ExpandAll() else hierarchy:CollapseAll() end end
function methods:SetHierarchyWidth(width) hierarchy:SetWidth(width) end
function methods:Seek(seconds) return transport:Seek(seconds) end
function methods:Pause() return transport:Pause() end
function methods:Play() return transport:Play() end
function methods:Step() return transport:Step(1) end
function methods:IsTimelineVisible() return timelineWindow.root:IsVisible() end
function methods:ShowAdvanced(value) if not Commit() then return false end; advanced=value; advancedButton:SetText(value and "Advanced: on" or "Advanced: off"); Refresh(); return true end
function methods:VisiblePropertyCount() return propertyInspector:VisibleFieldCount() end
function methods:EditProperty(path,text) return propertyInspector:EditProperty(path,text) end
window.closeButton:SetOnMouseUp(function() methods:Hide() end)
timelineWindow.closeButton:SetOnMouseUp(function() timelineWindow.root:SetVisible(false) end)
Registry.Register("AnimationAssets",methods)
