--[[
Animation Authoring - edits a graph draft without changing live playback.

Apply compiles and queues a publication. Invalid drafts remain editable while the
last accepted graph continues running. The preview actor is shared with Animation Debug.
]]

local Button = require("@src/API/UI/Button")
local InputBox = require("@src/API/UI/InputBox")
local Registry = require("@src/API/Editor/Registry")
local Session = require("@src/API/Editor/AnimationPreviewSession")
local Window = require("@src/API/Editor/UI/Window")

local canvas = UI.GetCanvas("AnimationAuthoring", vec.New(0,0), vec.New(1920,1080), false)
local window = Window.New(canvas, vec.New(16,-80), vec.New(500,570), 1120i, {
    title = "Animation Authoring", titleHeight = 22, padding = 10,
    windowColor = vec.New(0.055,0.063,0.078), titleColor = vec.New(0.035,0.04,0.055), alpha = 0.98,
})
local root = window.root
local muted = vec.New(0.58,0.64,0.72)
local accent = vec.New(0.30,0.70,1.0)
local function Anchor(widget)
    widget:SetAnchor(vec.New(0,1)); widget:SetRelativePoint(vec.New(0,1))
    return widget
end
local function Text(text,x,y,color)
    local widget = Anchor(root:NewText(text,vec.New(x,y),2i,"DevToolsText"))
    widget:SetColor(color or muted); widget:SetFontSize(10); widget:SetInteractable(false)
    return widget
end
local function Btn(text,x,y,w,callback)
    local widget = Anchor(Button.New(root,vec.New(x,y),vec.New(w,25),2i,{
        text=text,textTemplate="DevToolsText",borderColor=vec.New(0.22,0.27,0.34),borderSize=1,
    }))
    widget:SetOnMouseUp(callback)
    return widget
end
local function Input(x,y,w,text)
    local widget = Anchor(InputBox.New(root,vec.New(x,y),vec.New(w,25),2i,{
        panelTemplate="DebugDarkGrey",textTemplate="DevToolsText",defaultText=text or "",
    }))
    widget:SetTextVerticallyCentered()
    return widget
end

local heading = Text("Attach a controller in Animation Debug",12,-36,accent)
local status = Text("Draft and live playback are independent",12,-57)
local name = Input(12,-81,210,"locomotion")
local entity = nil
local document = nil
local section = "transitions"
local item = 0
local page = 0
local generation = 0
local pending = false
local rows = {}
local fields = {}
local labels = {
    comparison="Condition", curve="Blend curve", durationMicroseconds="Duration (seconds)",
    parameter="Parameter", sourceGUID="Source ID", targetGUID="Target state ID",
    threshold="Threshold", displayName="Name", clipSlotSemanticID="Clip slot ID",
    smoothingHalfLifeSeconds="Smoothing half-life (s)", wrapMinimum="Wrap minimum", wrapMaximum="Wrap maximum",
    stateVersion="State version", editorPosition="Canvas position [x,y]", defaultFloat="Default value",
    constantWeight="Weight", weightParameter="Weight parameter", executionOrder="Order",
}
local choices = {comparison={"Equal","Not equal","Less than","Less or equal","Greater than","Greater or equal"},curve={"Linear","Smooth step"},kind={"Clip","Blend space 1D","Blend space 2D"}}
local function DisplayValue(field)
    local value = tonumber(field.value)
    if value and field.name == "durationMicroseconds" then return string.format("%.6g",value/1000000) end
    if value and not string.find(field.name,"ID") and not string.find(field.name,"GUID") then return string.format("%.6g",value) end
    return field.value
end
local Refresh
local Commit
local Command
local sectionLabel = Text("TRANSITIONS",12,-155,accent)
local itemLabel = Text("",170,-155)
local diagnostics = {}
for i=1,3 do diagnostics[i] = Text("",12,-474-(i-1)*18,vec.New(1,0.62,0.30)) end
Text("Enter stores a field in the draft. Apply changes playback.",12,-542)

local function Short(value,length)
    return #value > length and string.sub(value,1,length-3).."..." or value
end

local function RenderFields()
    fields = {}
    local maxItem = 0
    if document ~= nil then
        for _,field in document.fields do
            if field.section == section then maxItem = math.max(maxItem,tonumber(tostring(field.index))) end
        end
        item = math.clamp(item,0,maxItem)
        for _,field in document.fields do
            if field.section == section and tonumber(tostring(field.index)) == item then table.insert(fields,field) end
        end
    end
    page = math.clamp(page,0,math.max(0,math.ceil(#fields/8)-1))
    sectionLabel:SetText(section == "" and "GRAPH" or string.upper(section))
    itemLabel:SetText(string.format("Item %d / %d   |   Page %d",item+1,maxItem+1,page+1))
    for i,row in rows do
        local field = fields[page*8+i]
        row.field = field
        row.options = field and (choices[field.name] or ((field.value == "true" or field.value == "false") and {"Off","On"})) or nil
        row.label:SetText(field and Short(labels[field.name] or field.name,25) or "")
        row.input:SetVisible(field ~= nil and row.options == nil)
        row.choice:SetVisible(field ~= nil and row.options ~= nil)
        row.displayValue = field and DisplayValue(field) or ""
        row.input:SetText(row.displayValue)
        if row.options then
            local index = tonumber(field.value) or (field.value == "true" and 1 or 0)
            row.choice:SetText(row.options[index+1] or "Unknown")
        end
    end
end

Refresh = function()
    local nextEntity = Session.GetSnapshot().entity
    if nextEntity ~= entity then entity=nextEntity; item=0; page=0 end
    document = entity and Asset.AnimationGraphDocument(entity) or nil
    if document == nil then
        heading:SetText("Attach a controller in Animation Debug")
        status:SetText("No graph document")
    else
        heading:SetText(document.dirty and "DRAFT  /  Unsaved changes" or "DRAFT  /  Saved")
        status:SetText(Short(document.status,74))
    end
    for i,label in diagnostics do
        local diagnostic = document and document.diagnostics[i]
        label:SetText(diagnostic and Short(diagnostic.field..": "..diagnostic.message,75) or "")
    end
    RenderFields()
end

Commit = function()
    if entity == nil then return true end
    for _,row in rows do
        if row.field and row.options == nil and row.input:GetText() ~= row.displayValue then
            local value=row.input:GetText()
            if row.field.name == "durationMicroseconds" then
                local seconds=tonumber(value)
                if not seconds or seconds<0 then status:SetText("Duration must be a non-negative number of seconds"); return false end
                value=string.format("%.0f",seconds*1000000)
            end
            local ok,error = Asset.AnimationGraphEditField(entity,row.field.section,row.field.index,row.field.name,value)
            if not ok then status:SetText(Short(error,74)); return false end
            row.field.value = value
            row.displayValue = row.input:GetText()
        end
    end
    return true
end

Command = function(command)
    if entity == nil then return false end
    if command ~= "undo" and command ~= "redo" and command ~= "revert" and not Commit() then return false end
    local collectionCommand=command == "duplicate" or command == "remove" or command == "add"
    local ok = Asset.AnimationGraphCommand(entity,command,collectionCommand and section or name:GetText(),integer.create(item))
    if command == "apply" and ok then
        pending=true
        local publishedEntity=entity
        local function WaitForPublication()
            if Session.GetSnapshot().entity ~= publishedEntity then return end
            local nextDocument=Asset.AnimationGraphDocument(publishedEntity)
            if not nextDocument then return end
            if nextDocument.pending then Scheduler.AfterFrames(1,WaitForPublication); return end
            pending=false
            local preview=Registry.Get("AnimationDebug")
            if preview then preview:RefreshParameterBindings() end
            Session.NotifyGraphChanged()
            Refresh()
        end
        Scheduler.AfterFrames(1,WaitForPublication)
    end
    Refresh()
    return ok
end

Btn("Save",230,-81,58,function() Command("save") end)
Btn("Load",294,-81,58,function() Command("load") end)
Btn("Revert",358,-81,62,function() Command("revert") end)
Btn("Apply",426,-81,62,function() Command("apply") end)
local sections = {"states","transitions","parameters","layers","actionSlots","ikOperations",""}
Btn("Category",12,-116,82,function()
    if not Commit() then return end
    local index = table.find(sections,section) or 1
    section=sections[index % #sections+1]; item=0; page=0; Refresh()
end)
Btn("Previous",100,-116,70,function() if Commit() then item=math.max(0,item-1); page=0; Refresh() end end)
Btn("Next",176,-116,54,function() if Commit() then item+=1; page=0; Refresh() end end)
Btn("Fields",236,-116,58,function() if Commit() then page=(page+1)%math.max(1,math.ceil(#fields/8)); Refresh() end end)
Btn("Undo",300,-116,58,function() Command("undo") end)
Btn("Redo",364,-116,58,function() Command("redo") end)
Btn("Graph",428,-116,60,function() Registry.Toggle("AnimationGraph") end)
Btn("Duplicate",346,-148,74,function() Command("duplicate") end)
Btn("Add",292,-148,48,function() Command("add") end)
Btn("Remove",426,-148,62,function() Command("remove") end)
for i=1,8 do
    local y=-187-(i-1)*34
    local row = {label=Text("",12,y-5), input=Input(204,y,284,"")}
    row.choice=Btn("",204,y,284,function()
        if not Commit() or not row.field or not row.options then return end
        local index=tonumber(row.field.value) or (row.field.value == "true" and 1 or 0)
        local nextIndex=(index+1)%#row.options
        local value=if row.field.value == "true" or row.field.value == "false" then (nextIndex==1 and "true" or "false") else tostring(nextIndex)
        Asset.AnimationGraphEditField(entity,row.field.section,row.field.index,row.field.name,value)
        Refresh()
    end)
    row.input:SetOnSubmit(function() if Commit() then Refresh() end end)
    table.insert(rows,row)
end

local methods = {}
function methods:Show()
    root:SetVisible(true); Refresh(); generation+=1
    local current=generation
    local function Tick()
        if generation ~= current or not root:IsVisible() then return end
        if Session.GetSnapshot().entity ~= entity then Refresh() end
        Scheduler.AfterFrames(3,Tick)
    end
    Tick()
end
function methods:Hide() root:SetVisible(false); generation+=1 end
function methods:IsVisible() return root:IsVisible() end
function methods:Select(nextSection,nextItem) Commit(); section=nextSection; item=nextItem or 0; page=0; Refresh() end
function methods:Edit(field,value)
    if entity == nil then return false end
    local ok=Asset.AnimationGraphEditField(entity,section,integer.create(item),field,tostring(value))
    Refresh(); return ok
end
function methods:Command(command) return Command(command) end
function methods:Refresh() Refresh() end
window.closeButton:SetOnMouseUp(function() methods:Hide() end)
Registry.Register("AnimationAuthoring",methods)
