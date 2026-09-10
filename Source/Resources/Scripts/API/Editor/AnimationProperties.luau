-- Contextual asset fields with shared editing rules and collapsible property groups.
local Button=require("@src/API/UI/Button")
local InputBox=require("@src/API/UI/InputBox")
local Dropdown=require("@src/API/UI/Dropdown")
local ScrollBox=require("@src/API/UI/ScrollBox")
local Slider=require("@src/API/UI/Slider")
local api={}
local function Anchor(w) w:SetAnchor(vec.New(0,1)); w:SetRelativePoint(vec.New(0,1)); return w end
local function Label(path,prefix)
    local label=string.sub(path,#prefix+1)
    label=string.gsub(label,"^/","")
    label=string.gsub(label,"Microseconds"," (seconds)")
    label=string.gsub(label,"(%l)(%u)","%1 %2")
    label=string.gsub(label,"/"," / ")
    if label=="" then return "Weight" end
    return string.upper(string.sub(label,1,1))..string.sub(label,2)
end
local function Section(path)
    if string.find(path,"Offset") or string.find(path,"num[A-Z]") or string.find(path,"ID$") or string.find(path,"flags$") or string.find(path,"hierarchyDepth") or string.find(path,"payloadSize") then return "Advanced data" end
    if string.find(path,"inverseBind") then return "Inverse bind transform" end
    if string.find(path,"restTransform") or string.find(path,"canonicalTransform") then return "Rest transform" end
    if string.find(path,"skeletonToFamily") or string.find(path,"familyToSkeleton") then return "Family calibration" end
    if string.find(path,"/rotation/") or string.find(path,"/translation/") or string.find(path,"/scale/") then return "Transform" end
    if string.find(path,"minimum") or string.find(path,"maximum") or string.find(path,"Limit") then return "Limits" end
    return "Settings"
end
function api.New(parent,canvas,callbacks)
    local scroll=Anchor(ScrollBox.New(parent,vec.New(12,-252),vec.New(436,380),3i,{verticalScrollBar=true,scrollBarWidth=6,contentSizeY=380,sensitivity=26,alpha=0}))
    local rows,headers,collapsed={},{},{}
    local fields,prefix,document={}, "", nil
    local advanced=false
    local refreshing=false
    local self={}
    local Refresh
    local function Text(s,x,y)
        local t=Anchor(scroll.content:NewText(s,vec.New(x,y),2i,"DevToolsText")); t:SetFontSize(11); t:SetColor(vec.New(0.73,0.78,0.84)); t:SetInteractable(false); return t
    end
    local extras={}
    local function ExtraInput(text,x,width)
        local input=Anchor(InputBox.New(scroll.content,vec.New(x,0),vec.New(width,25),2i,{defaultText=text,panelTemplate="DebugDarkGrey",textTemplate="DevToolsText"}))
        input:SetTextVerticallyCentered(); table.insert(extras,input); return input
    end
    local function ExtraButton(text,x,width,callback)
        local button=Anchor(Button.New(scroll.content,vec.New(x,0),vec.New(width,25),2i,{text=text,textTemplate="DevToolsText",borderSize=0}))
        button:SetOnMouseUp(callback); table.insert(extras,button); return button
    end
    local brushHeading=Text("Mask tools",8,0); table.insert(extras,brushHeading)
    local falloffLabel=Text("Subtree falloff",8,0); table.insert(extras,falloffLabel)
    local falloff=ExtraInput("0.8",204,216)
    local branch=ExtraButton("Paint subtree",8,196,function()
        if self:Commit() then callbacks.paint(tonumber(falloff:GetText())) end
    end)
    local mirrorLeft=ExtraButton("Whole mask: L > R",8,196,function() if self:Commit() then callbacks.mirror("left") end end)
    local mirrorRight=ExtraButton("Whole mask: R > L",210,210,function() if self:Commit() then callbacks.mirror("right") end end)
    local goalHeading=Text("Preview target / model space",8,0); table.insert(extras,goalHeading)
    local goalName=ExtraInput("rightHandGoal",8,412)
    local goalX=ExtraInput("-0.55",28,105)
    local goalY=ExtraInput("1.35",170,105)
    local goalZ=ExtraInput("0.3",312,108)
    local xyz={Text("X",8,0),Text("Y",150,0),Text("Z",292,0)}
    for _,label in xyz do table.insert(extras,label) end
    local goalApply=ExtraButton("Move preview target",8,196,function() callbacks.goal(goalName:GetText(),tonumber(goalX:GetText()),tonumber(goalY:GetText()),tonumber(goalZ:GetText())) end)
    function self:Commit()
        if refreshing then return true end
        for _,row in rows do
            if row.field and row.input:IsVisible() and row.input:GetText()~=row.display then
                local value=row.input:GetText()
                if row.seconds or row.weight then
                    local number=tonumber(value)
                    if not number or number~=number or number==math.huge or number==-math.huge or (row.weight and (number<0 or number>1)) then callbacks.error(row.weight and "Weight must be between 0 and 1" or "Enter a finite time in seconds"); return false end
                    value=string.format("%.0f",number*(row.weight and 255 or 1000000))
                end
                local ok,error=callbacks.edit(row.field.path,value)
                if not ok then callbacks.error(error); return false end
                row.display=row.input:GetText()
            end
        end
        return true
    end
    Refresh=function()
        refreshing=true
        for _,row in rows do row.field=nil; row.input:SetVisible(false); row.choice:SetVisible(false); row.label:SetVisible(false); row.axis:SetVisible(false); row.slider:SetVisible(false) end
        for _,header in headers do header:SetVisible(false) end
        for _,widget in extras do widget:SetVisible(false) end
        local grouped={}
        for _,field in fields do
            local section=Section(field.path)
            if advanced or (section~="Advanced data" and section~="Inverse bind transform") then
                grouped[section]=grouped[section] or {}; table.insert(grouped[section],field)
            end
        end
        local y,rowIndex,headerIndex=0,0,0
        for _,section in {"Settings","Rest transform","Transform","Family calibration","Limits","Inverse bind transform","Advanced data"} do
            if grouped[section] then
                headerIndex+=1
                local header=headers[headerIndex]
                if not header then
                    header=Anchor(Button.New(scroll.content,vec.New(0,0),vec.New(420,25),2i,{text="",textTemplate="DevToolsText",textAlign="left",borderSize=0,color=vec.New(0.08,0.095,0.115)})); headers[headerIndex]=header
                end
                header:SetText((collapsed[section] and "+  " or "-  ")..section); header:SetPos(vec.New(0,-y)); header:SetVisible(true)
                header:SetOnMouseUp(function() if self:Commit() then collapsed[section]=not collapsed[section]; Refresh() end end)
                y+=32
                if not collapsed[section] then
                    table.sort(grouped[section],function(a,b)
                        local function Key(path)
                            return string.gsub(string.gsub(string.gsub(path,"translation","0position"),"rotation","1rotation"),"scale","2scale")
                        end
                        return Key(a.path)<Key(b.path)
                    end)
                    for _,field in grouped[section] do
                        rowIndex+=1
                        local row=rows[rowIndex]
                        if not row then
                            row={label=Text("",8,0),axis=Text("",0,0)}
                            row.input=Anchor(InputBox.New(scroll.content,vec.New(204,0),vec.New(216,25),2i,{panelTemplate="DebugDarkGrey",textTemplate="DevToolsText",defaultText=""}))
                            row.input:SetTextVerticallyCentered()
                            row.slider=Anchor(Slider.New(scroll.content,vec.New(78,0),vec.New(116,14),3i,{Min=0,Max=1,Step=1/255,fillSizeY=6,fillSizeX=108,fillPosX=4}))
                            row.slider:SetOnValueChanged(function(_,value)
                                if refreshing or not row.field then return end
                                local ok,error=callbacks.edit(row.field.path,tostring(math.floor(value*255+0.5)))
                                if ok then row.display=string.format("%.4g",value); row.input:SetText(row.display) else callbacks.error(error) end
                            end)
                            row.slider:SetOnMouseUp(function() callbacks.changed() end)
                            row.choice=Anchor(Dropdown.New(scroll.content,canvas,vec.New(204,0),vec.New(216,25),4i,{button={textTemplate="DevToolsText",textAlign="left",borderSize=0},menuLayer=1190i,itemHeight=22,menuWidth=260}))
                            row.input:SetOnSubmit(function() if self:Commit() then callbacks.changed() end end)
                            row.choice:SetOnValueChanged(function(value)
                                if refreshing or not row.field then return end
                                if not self:Commit() then return end
                                local ok,error=callbacks.edit(row.field.path,tostring(value))
                                if ok then callbacks.changed() else callbacks.error(error) end
                            end)
                            rows[rowIndex]=row
                        end
                        row.field=field
                        row.seconds=string.find(field.path,"Microseconds")~=nil
                        row.weight=string.match(field.path,"^/data/jointWeights/%d+$")~=nil
                        local label=Label(field.path,prefix)
                        label=string.gsub(label,"rest Transform / ","")
                        label=string.gsub(label,"Rest Transform / ","")
                        local axis=string.match(field.path,"/(%d)$")
                        local vector=axis and (string.find(field.path,"translation") or string.find(field.path,"scale") or string.find(field.path,"rotation") or string.find(field.path,"Direction") or string.find(field.path,"Axis"))
                        local component=vector and tonumber(axis) or nil
                        local components=string.find(field.path,"rotation") and 4 or 3
                        if vector then
                            local names=string.find(field.path,"rotation") and {"W","X","Y","Z"} or {"X","Y","Z"}
                            label=string.gsub(label," / %d$","")
                            label=string.gsub(label,"translation","Position")
                            label=string.gsub(label,"rotation","Rotation (quaternion)")
                            label=string.gsub(label,"scale","Scale")
                            row.axis:SetText(names[component+1] or axis)
                            row.axis:SetPos(vec.New(8+component*412/components,-y-29)); row.axis:SetVisible(true)
                        end
                        if string.find(field.path,"familyJointIndex") then label="Family bone index" else label=string.gsub(label," Joint Index$"," bone") end
                        row.label:SetText(label); row.label:SetPos(vec.New(8,-y-5)); row.label:SetVisible(not vector or component==0)
                        while row.label:GetWidth()>188 and #label>4 do label=string.sub(label,1,-2); row.label:SetText(label.."...") end
                        local choices=callbacks.choices(field)
                        if not choices and string.match(field.path,"[Jj]ointIndex$") and not string.find(field.path,"familyJointIndex") then
                            choices={}
                            table.insert(choices,{label="None",value=string.find(field.path,"parentJointIndex") and "-1" or "4294967295"})
                            for _,joint in document and document.hierarchy or {} do table.insert(choices,{label=joint.name,value=tostring(joint.index)}) end
                        elseif choices then
                            local options={}; for i,name in choices do table.insert(options,{label=name,value=tostring(i-1)}) end; choices=options
                        end
                        if choices then row.choice:SetOptions(choices); row.choice:SetValue(field.value,false) end
                        row.display=field.value
                        local number=tonumber(field.value)
                        if number and not string.find(field.path,"ID$") then row.display=string.format("%.6g",number) end
                        if number and (row.seconds or row.weight) then row.display=string.format("%.6g",number/(row.weight and 255 or 1000000)) end
                        row.input:SetPos(vec.New(vector and 20+component*412/components or 204,-y-(vector and 23 or 0))); row.input:SetSize(vec.New(vector and 412/components-14 or 216,25)); row.choice:SetPos(vec.New(204,-y))
                        row.input:SetText(row.display); row.input:SetVisible(choices==nil); row.choice:SetVisible(choices~=nil)
                        row.slider:SetVisible(row.weight); row.slider:SetPos(vec.New(78,-y-5))
                        if row.weight then row.slider:SetValue(number and number/255 or 0) end
                        if not vector then y+=31 elseif component==components-1 then y+=58 end
                    end
                end
                y+=8
            end
        end
        local function Place(widget,offset) widget:SetVisible(true); widget:SetPosY(-y-offset) end
        if document and tonumber(tostring(document.type))==4 and string.find(prefix,"/data/jointWeights/") then
            Place(brushHeading,0); Place(falloffLabel,33); Place(falloff,28); Place(branch,61); Place(mirrorLeft,98); Place(mirrorRight,98)
            y+=135
        elseif document and tonumber(tostring(document.type))==5 then
            Place(goalHeading,0); Place(goalName,27)
            Place(goalX,59); Place(goalY,59); Place(goalZ,59)
            for _,label in xyz do Place(label,64) end
            Place(goalApply,91); y+=128
        end
        scroll:SetContentHeight(math.max(380,y)); refreshing=false
    end
    function self:SetFields(nextFields,nextPrefix,nextDocument,showAdvanced)
        if prefix~=nextPrefix then scroll:ScrollToStart() end
        fields=nextFields; prefix=nextPrefix; document=nextDocument; advanced=showAdvanced; Refresh()
    end
    function self:VisibleFieldCount() local count=0; for _,row in rows do if row.field then count+=1 end end; return count end
    function self:EditProperty(path,text)
        for _,row in rows do
            if row.field and row.field.path==path and row.input:IsVisible() then
                row.input:SetText(text)
                if not self:Commit() then return false end
                callbacks.changed(); return true
            end
        end
        return false
    end
    return self
end
return api
