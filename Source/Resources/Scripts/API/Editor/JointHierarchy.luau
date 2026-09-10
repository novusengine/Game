-- Skeleton navigation and selection, independent of the asset field editor.
local Button=require("@src/API/UI/Button")
local InputBox=require("@src/API/UI/InputBox")
local ScrollBox=require("@src/API/UI/ScrollBox")
local Window=require("@src/API/Editor/UI/Window")
local api={}
local ROW_HEIGHT=20
local function Anchor(w) w:SetAnchor(vec.New(0,1)); w:SetRelativePoint(vec.New(0,1)); return w end
local function Number(v) return tonumber(tostring(v)) end
function api.New(canvas,callbacks)
    local window=Window.New(canvas,vec.New(1510,-80),vec.New(390,730),1131i,{title="Skeleton",titleHeight=26,padding=12,windowColor=vec.New(0.055,0.063,0.078),titleColor=vec.New(0.035,0.04,0.055),alpha=0.98})
    local root=window.root
    local function Text(s,x,y,size)
        local t=Anchor(root:NewText(s,vec.New(x,y),2i,"DevToolsText")); t:SetFontSize(size or 11); t:SetColor(vec.New(0.72,0.78,0.84)); t:SetInteractable(false); return t
    end
    local function Btn(s,x,y,w,fn)
        local b=Anchor(Button.New(root,vec.New(x,y),vec.New(w,25),2i,{text=s,textTemplate="DevToolsText",borderSize=0,color=vec.New(0.10,0.12,0.15)})); b:SetOnMouseUp(fn); return b
    end
    local function Input(s,x,y,w)
        local b=Anchor(InputBox.New(root,vec.New(x,y),vec.New(w,26),2i,{panelTemplate="DebugDarkGrey",textTemplate="DevToolsText",defaultText=s})); b:SetTextVerticallyCentered(); return b
    end
    Text("Search joints or semantics",12,-37)
    local search=Input("",12,-57,366)
    local scroll=Anchor(ScrollBox.New(root,vec.New(10,-123),vec.New(370,500),2i,{verticalScrollBar=true,scrollBarWidth=6,autoScrollToStart=false,contentSizeY=500,sensitivity=ROW_HEIGHT,alpha=0}))
    local count=Text("No skeleton loaded",12,-636,10)
    local heading=Text("Select a joint",12,-660,15)
    local parentLabel=Text("",12,-687)
    local nodes,byIndex,children,expanded,rows,visible={},{},{},{},{},{}
    local fields,chain={},{}
    local selected=nil
    local documentPath=nil
    local kind=0
    local width=390
    local Refresh
    local self={window=window}
    function self:ExpandAll() for _,node in nodes do expanded[Number(node.index)]=true end; Refresh() end
    function self:CollapseAll() expanded={}; Refresh() end
    local function Depth(node)
        local depth,seen=0,{}
        while node and Number(node.parent)>=0 and not seen[Number(node.index)] do
            seen[Number(node.index)]=true; depth+=1; node=byIndex[Number(node.parent)]
        end
        return depth
    end
    function self:Select(index,reveal)
        if not byIndex[index] then return false end
        if callbacks.select(index)==false then return false end
        selected=index
        if reveal then
            search:SetText("")
            local node=byIndex[index]; local seen={}
            while node and Number(node.parent)>=0 and not seen[Number(node.index)] do
                seen[Number(node.index)]=true; expanded[Number(node.parent)]=true; node=byIndex[Number(node.parent)]
            end
        end
        Refresh()
        if reveal then
            for i,node in visible do if Number(node.index)==index then scroll:SetOffset(vec.New(0,math.max(0,(i-3)*ROW_HEIGHT))); break end end
        end
        return true
    end
    Refresh=function()
        local query=string.lower(search:GetText())
        local matches={}
        if query~="" then
            for _,node in nodes do
                if string.find(string.lower(node.name),query,1,true) or string.find(tostring(node.semanticID),query,1,true) then
                    local current=node; local seen={}
                    while current and not seen[Number(current.index)] do
                        local index=Number(current.index); matches[index]=true; seen[index]=true; current=byIndex[Number(current.parent)]
                    end
                end
            end
        end
        visible={}
        local visited={}
        local function Visit(node)
            local index=Number(node.index)
            if visited[index] or (query~="" and not matches[index]) then return end
            visited[index]=true; table.insert(visible,node)
            if expanded[index] or query~="" then for _,child in children[index] or {} do Visit(child) end end
        end
        for _,node in nodes do if not byIndex[Number(node.parent)] then Visit(node) end end
        scroll:SetContentHeight(math.max(500,#visible*ROW_HEIGHT))
        for i,node in visible do
            local row=rows[i]
            if not row then
                row={}; rows[i]=row
                row.button=Anchor(Button.New(scroll.content,vec.New(0,0),vec.New(width-34,ROW_HEIGHT-1),2i,{text="",textTemplate="DevToolsText",textAlign="left",textPadX=0,borderSize=0,color=vec.New(0.055,0.063,0.078),highlightColor=vec.New(0.10,0.24,0.34)}))
                row.button:SetOnMouseUp(function() self:Select(Number(row.node.index),false) end)
                row.toggle=Anchor(Button.New(row.button.root,vec.New(0,0),vec.New(18,ROW_HEIGHT-1),2i,{text="",textTemplate="DevToolsText",textPadX=0,borderSize=0,alpha=0,hoverAlpha=0.5}))
                row.toggle:SetOnMouseUp(function() local index=Number(row.node.index); expanded[index]=not expanded[index]; Refresh() end)
                row.bar=Anchor(row.button.root:NewPanel(vec.New(0,-19),vec.New(0,2),2i,"DebugDarkGrey")); row.bar:SetColor(vec.New(0.20,0.72,0.9)); row.bar:SetInteractable(false)
                row.value=Anchor(row.button.root:NewText("",vec.New(0,-5),2i,"DevToolsText")); row.value:SetFontSize(10); row.value:SetColor(vec.New(0.40,0.78,0.90)); row.value:SetInteractable(false)
                row.guides={}
                for d=1,16 do local guide=Anchor(row.button.root:NewPanel(vec.New(0,0),vec.New(1,ROW_HEIGHT),1i,"DebugDarkGrey")); guide:SetColor(vec.New(0.17,0.21,0.25)); guide:SetInteractable(false); table.insert(row.guides,guide) end
            end
            row.node=node
            local index=Number(node.index)
            local depth=math.min(Depth(node),12)
            row.button:SetVisible(true); row.button:SetPos(vec.New(0,-(i-1)*ROW_HEIGHT)); row.button:SetSize(vec.New(width-34,ROW_HEIGHT-1))
            local label=node.name
            row.button:SetText(label)
            local available=width-125-depth*12
            while row.button.text:GetWidth()>available and #label>3 do label=string.sub(label,1,-2); row.button:SetText(label.."...") end
            row.button.text:SetPos(vec.New(depth*12+24,0)); row.button:SetHighlighted(index==selected)
            row.toggle:SetPos(vec.New(depth*12,0)); row.toggle:SetVisible(children[index]~=nil); row.toggle:SetText((expanded[index] or query~="") and "-" or "+")
            local amount=tonumber(fields["/data/jointWeights/"..tostring(index)]) or 0
            row.bar:SetVisible(kind==4); row.bar:SetPos(vec.New(width-91,-ROW_HEIGHT+4)); row.bar:SetWidth(48*amount/255)
            row.value:SetPos(vec.New(width-91,-1)); row.value:SetText(kind==4 and string.format("%.0f%%",amount*100/255) or chain[index] or (Number(node.familyIndex)>=0 and "mapped" or ""))
            row.value:SetColor(chain[index]=="Root" and vec.New(0.2,0.8,1) or chain[index]=="Middle" and vec.New(1,0.5,0.2) or chain[index]=="Effector" and vec.New(0.3,1,0.45) or vec.New(0.40,0.78,0.90))
            for d,guide in row.guides do guide:SetVisible(d<=depth); guide:SetPos(vec.New((d-1)*12+8,0)) end
        end
        for i=#visible+1,#rows do rows[i].button:SetVisible(false) end
        count:SetText(string.format("%d visible / %d joints%s",#visible,#nodes,query~="" and "  |  filtered" or ""))
        local node=selected and byIndex[selected]
        heading:SetText(node and node.name or "Select a joint")
        local parent=node and byIndex[Number(node.parent)]
        parentLabel:SetText(node and "Parent: "..(parent and parent.name or "Root") or "")

    end
    search:SetOnTextChanged(function() scroll:ScrollToStart(); Refresh() end)
    Btn("Expand",12,-91,76,function() self:ExpandAll() end)
    Btn("Collapse",94,-91,82,function() self:CollapseAll() end)
    Btn("Pick joint",182,-91,92,function() callbacks.pick() end)
    Btn("Skeleton",280,-91,98,function() callbacks.overlay() end)
    function self:SetDocument(doc,nextKind)
        nodes=doc and doc.hierarchy or {}; kind=nextKind; fields={}; byIndex={}; children={}
        for _,field in doc and doc.fields or {} do fields[field.path]=field.value end
        local changed=documentPath~=(doc and doc.path)
        documentPath=doc and doc.path
        if changed then expanded={}; selected=nil; search:SetText(""); scroll:ScrollToStart() end
        for _,node in nodes do byIndex[Number(node.index)]=node end
        for _,node in nodes do
            local parent=Number(node.parent)
            if parent>=0 then children[parent]=children[parent] or {}; table.insert(children[parent],node) end
            if changed and Depth(node)<4 then expanded[Number(node.index)]=true end
        end
        Refresh()
    end
    function self:SetChain(indices)
        chain=indices or {}
        for index in chain do
            local node=byIndex[index]; local seen={}
            while node and not seen[Number(node.index)] do
                seen[Number(node.index)]=true; expanded[Number(node.parent)]=true; node=byIndex[Number(node.parent)]
            end
        end
        Refresh()
    end
    function self:SetWidth(nextWidth)
        local previousWidth=width
        width=math.clamp(nextWidth,390,600)
        root:SetPosX(root:GetPosX()+previousWidth-width)
        root:SetWidth(width); window.titleBar:SetWidth(width); search:SetWidth(width-24)
        scroll.width=width-20; scroll.widthWithScrollBar=width-26
        scroll.root:SetWidth(width-20); scroll._viewport:SetWidth(width-26); scroll.content:SetWidth(width-26)
        scroll:RefreshScrollbar(); Refresh()
    end
    local resize=root:NewPanel(vec.New(0,0),vec.New(6,680),4i,"DebugDarkGrey")
    resize:SetAnchor(vec.New(0,0)); resize:SetRelativePoint(vec.New(0,0)); resize:SetAlpha(0.25)
    local dragX,dragWidth=0,390
    resize:SetOnMouseDown(function() dragX=UI.GetMousePos().x; dragWidth=width end)
    resize:SetOnMouseHeld(function() self:SetWidth(dragWidth+dragX-UI.GetMousePos().x) end)
    function self:GetSelected() return selected end
    function self:Search(query) search:SetText(query) end
    function self:VisibleCount() return #visible end
    function self:Show() root:SetVisible(true) end
    function self:Hide() root:SetVisible(false) end
    window.closeButton:SetOnMouseUp(function() self:Hide() end)
    return self
end
return api
