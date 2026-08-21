export type Snapshot = { [string] : any };

export type Draft =
{
    menusByID : { [integer] : any },
    optionsByID : { [integer] : any },
    optionIDsByMenuID : { [integer] : { integer } },
    openMenuDestinations : { [integer] : integer? },
};

export type Session =
{
    nodePositions : { [integer] : vec },
    selectedMenuID : integer?,
    selectedOptionID : integer?,
    panOffset : vec,
    zoom : number,
    temporaryVisualState : { [string] : any },
};

export type GossipMenuModel =
{
    GetAuthoritativeSnapshot : (self : GossipMenuModel) -> Snapshot,
    GetDraft : (self : GossipMenuModel) -> Draft,
    GetSession : (self : GossipMenuModel) -> Session,
    Reset : (self : GossipMenuModel, snapshot : Snapshot) -> (),
    Reload : (self : GossipMenuModel, snapshot : Snapshot) -> (),
    IsDirty : (self : GossipMenuModel) -> boolean,
    IsMenuDirty : (self : GossipMenuModel, menuID : integer) -> boolean,
    IsMenuOrderDirty : (self : GossipMenuModel, menuID : integer) -> boolean,
    IsOptionDirty : (self : GossipMenuModel, optionID : integer) -> boolean,
    GetMenuIDs : (self : GossipMenuModel) -> { integer },
    GetMenuDraft : (self : GossipMenuModel, menuID : integer) -> any?,
    GetOptionDraft : (self : GossipMenuModel, optionID : integer) -> any?,
    SetMenuFields : (self : GossipMenuModel, menuID : integer, fields : { [string] : any }) -> (),
    SetOptionFields : (self : GossipMenuModel, optionID : integer, fields : { [string] : any }) -> (),
    GetOrderedOptionIDs : (self : GossipMenuModel, menuID : integer) -> { integer },
    ReorderOptions : (self : GossipMenuModel, menuID : integer, orderedOptionIDs : { integer }) -> (),
    RevertMenus : (self : GossipMenuModel, menuIDs : { integer }) -> (),
    SetOpenMenuDestination : (self : GossipMenuModel, optionID : integer, menuID : integer?) -> (),
    GetOpenMenuDestination : (self : GossipMenuModel, optionID : integer) -> integer?,
    ResetSession : (self : GossipMenuModel) -> (),
    ResetNodePositions : (self : GossipMenuModel) -> (),
    SetNodePosition : (self : GossipMenuModel, menuID : integer, position : vec) -> (),
    GetNodePosition : (self : GossipMenuModel, menuID : integer) -> vec?,
    SetSelectedMenu : (self : GossipMenuModel, menuID : integer?) -> (),
    SetSelectedOption : (self : GossipMenuModel, optionID : integer?) -> (),
    SetPanOffset : (self : GossipMenuModel, offset : vec) -> (),
    GetPanOffset : (self : GossipMenuModel) -> vec,
    SetZoom : (self : GossipMenuModel, zoom : number) -> (),
    GetZoom : (self : GossipMenuModel) -> number,
    SetTemporaryVisualState : (self : GossipMenuModel, key : string, value : any) -> (),
    GetTemporaryVisualState : (self : GossipMenuModel, key : string) -> any?,
};

export type GossipMenuModelAPI =
{
    New : (snapshot : Snapshot) -> GossipMenuModel,
};

local MENU_FIELDS = { internalName = true, greetingTextID = true, flags = true };
local OPTION_FIELDS =
{
    orderIndex = true,
    textID = true,
    icon = true,
    flags = true,
    visibilityConditionSetID = true,
    enabledConditionSetID = true,
    disabledReasonTextID = true,
    actionType = true,
    actionParameters = true,
};

local function Clone(value : any, seen : { [any] : any }?) : any
    if type(value) ~= "table" then return value; end
    local previous = if seen then seen[value] else nil;
    if previous ~= nil then return previous; end
    local result : { [any] : any } = { };
    local visited = seen or { };
    visited[value] = result;
    for key, child in value do result[Clone(key, visited)] = Clone(child, visited); end
    return result;
end

local function Equal(left : any, right : any) : boolean
    if left == right then return true; end
    if type(left) ~= "table" or type(right) ~= "table" then return false; end
    for key, value in left do
        if not Equal(value, right[key]) then return false; end
    end
    for key in right do
        if left[key] == nil then return false; end
    end
    return true;
end

local function ValidateSnapshot(snapshot : Snapshot)
    if type(snapshot) ~= "table" then error("GossipMenuModel: snapshot must be a table", 3); end
    for _, field in { "gossipMenus", "gossipMenuOptions", "texts", "conditionSets", "gossipActionDescriptors" } do
        if type(snapshot[field]) ~= "table" then error(`GossipMenuModel: snapshot.{field} must be a table`, 3); end
    end
end

local function FindMenu(draft : Draft, menuID : integer)
    return draft.menusByID[menuID];
end

local function FindOption(draft : Draft, optionID : integer)
    return draft.optionsByID[optionID];
end

local function BuildDraft(snapshot : Snapshot) : Draft
    local draft : Draft = { menusByID = { }, optionsByID = { }, optionIDsByMenuID = { }, openMenuDestinations = { } };
    for _, menu in snapshot.gossipMenus do
        if menu.id ~= nil then draft.menusByID[menu.id] = Clone(menu); end
    end
    for _, option in snapshot.gossipMenuOptions do
        if option.id ~= nil then
            draft.optionsByID[option.id] = Clone(option);
            local optionIDs = draft.optionIDsByMenuID[option.menuID];
            if optionIDs == nil then optionIDs = { }; draft.optionIDsByMenuID[option.menuID] = optionIDs; end
            table.insert(optionIDs, option.id);
        end
    end
    for _, optionIDs in draft.optionIDsByMenuID do
        table.sort(optionIDs, function(leftID, rightID)
            local left = draft.optionsByID[leftID];
            local right = draft.optionsByID[rightID];
            if left.orderIndex == right.orderIndex then return integer.lt(left.id, right.id); end
            return integer.lt(left.orderIndex, right.orderIndex);
        end);
    end
    return draft;
end

local function NewSession() : Session
    return { nodePositions = { }, selectedMenuID = nil, selectedOptionID = nil, panOffset = vec.New(0, 0), zoom = 1, temporaryVisualState = { } };
end

local function ApplyFields(target : any, fields : { [string] : any }, allowed : { [string] : boolean }, label : string)
    if type(fields) ~= "table" then error(`GossipMenuModel: {label} fields must be a table`, 3); end
    for key, value in fields do
        if not allowed[key] then error(`GossipMenuModel: {label} field '{key}' is not draft-editable`, 3); end
        target[key] = Clone(value);
    end
end

local function ReapplyChangedFields(previous : any, previousBaseline : any, target : any, allowed : { [string] : boolean })
    if previous == nil or previousBaseline == nil or target == nil then return; end
    for field in allowed do
        if not Equal(previous[field], previousBaseline[field]) then target[field] = Clone(previous[field]); end
    end
end

local function RebaseDraftChanges(previous : Draft, previousBaseline : Draft, target : Draft)
    for menuID, menu in previous.menusByID do ReapplyChangedFields(menu, previousBaseline.menusByID[menuID], target.menusByID[menuID], MENU_FIELDS); end
    for optionID, option in previous.optionsByID do ReapplyChangedFields(option, previousBaseline.optionsByID[optionID], target.optionsByID[optionID], OPTION_FIELDS); end

    for menuID, optionIDs in previous.optionIDsByMenuID do
        local baselineOptionIDs = previousBaseline.optionIDsByMenuID[menuID] or { };
        local targetOptionIDs = target.optionIDsByMenuID[menuID];
        if targetOptionIDs ~= nil and not Equal(optionIDs, baselineOptionIDs) then
            local available : { [integer] : boolean } = { };
            local rebased = { };
            for _, optionID in targetOptionIDs do available[optionID] = true; end
            for _, optionID in optionIDs do
                if available[optionID] then table.insert(rebased, optionID); available[optionID] = nil; end
            end
            for _, optionID in targetOptionIDs do
                if available[optionID] then table.insert(rebased, optionID); end
            end
            target.optionIDsByMenuID[menuID] = rebased;
        end
    end
end

local modelAPI : GossipMenuModelAPI = { };

function modelAPI.New(snapshot : Snapshot) : GossipMenuModel
    ValidateSnapshot(snapshot);
    local model : any = { };
    local authoritativeSnapshot = Clone(snapshot);
    local draft = BuildDraft(authoritativeSnapshot);
    local baselineDraft = Clone(draft);
    local session = NewSession();
    local dirty = false;

    local function RecomputeDirty()
        dirty = not Equal(draft, baselineDraft);
    end

    local function ReplaceDraft(nextSnapshot : Snapshot)
        authoritativeSnapshot = Clone(nextSnapshot);
        draft = BuildDraft(authoritativeSnapshot);
        baselineDraft = Clone(draft);
        dirty = false;
    end

    function model:GetAuthoritativeSnapshot() : Snapshot
        return Clone(authoritativeSnapshot);
    end

    function model:GetDraft() : Draft
        return Clone(draft);
    end

    function model:GetSession() : Session
        return Clone(session);
    end

    function model:Reset(nextSnapshot : Snapshot)
        ValidateSnapshot(nextSnapshot);
        ReplaceDraft(nextSnapshot);
        session = NewSession();
    end

    function model:Reload(nextSnapshot : Snapshot)
        ValidateSnapshot(nextSnapshot);
        local previousSession = session;
        local previousDraft = draft;
        local previousBaseline = baselineDraft;
        ReplaceDraft(nextSnapshot);
        RebaseDraftChanges(previousDraft, previousBaseline, draft);
        RecomputeDirty();
        session = NewSession();
        session.panOffset = previousSession.panOffset;
        session.zoom = previousSession.zoom;
        for menuID, position in previousSession.nodePositions do
            if FindMenu(draft, menuID) ~= nil then session.nodePositions[menuID] = position; end
        end
        if previousSession.selectedMenuID ~= nil and FindMenu(draft, previousSession.selectedMenuID) ~= nil then
            session.selectedMenuID = previousSession.selectedMenuID;
        end
        if previousSession.selectedOptionID ~= nil then
            local selectedOption = FindOption(draft, previousSession.selectedOptionID);
            if selectedOption ~= nil and FindMenu(draft, selectedOption.menuID) ~= nil then
                session.selectedOptionID = previousSession.selectedOptionID;
                session.selectedMenuID = selectedOption.menuID;
            end
        end
    end

    function model:IsDirty() : boolean
        return dirty;
    end

    function model:IsMenuDirty(menuID : integer) : boolean
        return not Equal(draft.menusByID[menuID], baselineDraft.menusByID[menuID]);
    end

    function model:IsMenuOrderDirty(menuID : integer) : boolean
        return not Equal(draft.optionIDsByMenuID[menuID] or { }, baselineDraft.optionIDsByMenuID[menuID] or { });
    end

    function model:IsOptionDirty(optionID : integer) : boolean
        return not Equal(draft.optionsByID[optionID], baselineDraft.optionsByID[optionID]);
    end

    function model:GetMenuIDs() : { integer }
        local result = { };
        for menuID in draft.menusByID do table.insert(result, menuID); end
        table.sort(result, function(left, right) return integer.lt(left, right); end);
        return result;
    end

    function model:GetMenuDraft(menuID : integer)
        local menu = FindMenu(draft, menuID);
        return if menu then Clone(menu) else nil;
    end

    function model:GetOptionDraft(optionID : integer)
        local option = FindOption(draft, optionID);
        return if option then Clone(option) else nil;
    end

    function model:SetMenuFields(menuID : integer, fields : { [string] : any })
        local menu = FindMenu(draft, menuID);
        if menu == nil then error(`GossipMenuModel: unknown menu ID {menuID}`, 2); end
        ApplyFields(menu, fields, MENU_FIELDS, "menu");
        RecomputeDirty();
    end

    function model:SetOptionFields(optionID : integer, fields : { [string] : any })
        local option = FindOption(draft, optionID);
        if option == nil then error(`GossipMenuModel: unknown option ID {optionID}`, 2); end
        ApplyFields(option, fields, OPTION_FIELDS, "option");
        RecomputeDirty();
    end

    function model:GetOrderedOptionIDs(menuID : integer) : { integer }
        return Clone(draft.optionIDsByMenuID[menuID] or { });
    end

    function model:ReorderOptions(menuID : integer, orderedOptionIDs : { integer })
        if FindMenu(draft, menuID) == nil then error(`GossipMenuModel: unknown menu ID {menuID}`, 2); end
        local current = draft.optionIDsByMenuID[menuID] or { };
        if #orderedOptionIDs ~= #current then error("GossipMenuModel: reordered options must contain every response exactly once", 2); end
        local expected : { [integer] : boolean } = { };
        local seen : { [integer] : boolean } = { };
        for _, optionID in current do expected[optionID] = true; end
        for _, optionID in orderedOptionIDs do
            if not expected[optionID] or seen[optionID] then error("GossipMenuModel: reordered options must contain every response exactly once", 2); end
            seen[optionID] = true;
        end
        if Equal(orderedOptionIDs, current) then return; end
        local result = { };
        for index = 1, #orderedOptionIDs do
            local optionID = orderedOptionIDs[index];
            result[index] = optionID;
            draft.optionsByID[optionID].orderIndex = integer.create(index - 1);
        end
        draft.optionIDsByMenuID[menuID] = result;
        RecomputeDirty();
    end

    function model:RevertMenus(menuIDs : { integer })
        if type(menuIDs) ~= "table" then error("GossipMenuModel: menu IDs must be a table", 2); end
        local seen : { [integer] : boolean } = { };
        for _, menuID in menuIDs do
            if seen[menuID] then continue; end
            local baselineMenu = baselineDraft.menusByID[menuID];
            if baselineMenu == nil then error(`GossipMenuModel: unknown menu ID {menuID}`, 2); end
            seen[menuID] = true;
            draft.menusByID[menuID] = Clone(baselineMenu);
            local currentOptionIDs = draft.optionIDsByMenuID[menuID] or { };
            local baselineOptionIDs = baselineDraft.optionIDsByMenuID[menuID] or { };
            local optionIDs : { [integer] : boolean } = { };
            for _, optionID in currentOptionIDs do optionIDs[optionID] = true; end
            for _, optionID in baselineOptionIDs do optionIDs[optionID] = true; end
            for optionID in optionIDs do
                draft.optionsByID[optionID] = Clone(baselineDraft.optionsByID[optionID]);
                draft.openMenuDestinations[optionID] = Clone(baselineDraft.openMenuDestinations[optionID]);
            end
            draft.optionIDsByMenuID[menuID] = Clone(baselineOptionIDs);
        end
        RecomputeDirty();
    end

    function model:SetOpenMenuDestination(optionID : integer, menuID : integer?)
        if FindOption(draft, optionID) == nil then error(`GossipMenuModel: unknown option ID {optionID}`, 2); end
        if menuID ~= nil and FindMenu(draft, menuID) == nil then error(`GossipMenuModel: unknown destination menu ID {menuID}`, 2); end
        draft.openMenuDestinations[optionID] = menuID;
        RecomputeDirty();
    end

    function model:GetOpenMenuDestination(optionID : integer) : integer?
        return draft.openMenuDestinations[optionID];
    end

    function model:ResetSession()
        session = NewSession();
    end

    function model:ResetNodePositions()
        session.nodePositions = { };
    end

    function model:SetNodePosition(menuID : integer, position : vec)
        if FindMenu(draft, menuID) == nil then error(`GossipMenuModel: unknown menu ID {menuID}`, 2); end
        session.nodePositions[menuID] = position;
    end

    function model:GetNodePosition(menuID : integer) : vec?
        return session.nodePositions[menuID];
    end

    function model:SetSelectedMenu(menuID : integer?)
        if menuID ~= nil and FindMenu(draft, menuID) == nil then error(`GossipMenuModel: unknown menu ID {menuID}`, 2); end
        session.selectedMenuID = menuID;
        local selectedOption = if session.selectedOptionID then FindOption(draft, session.selectedOptionID) else nil;
        if menuID == nil or selectedOption == nil or selectedOption.menuID ~= menuID then session.selectedOptionID = nil; end
    end

    function model:SetSelectedOption(optionID : integer?)
        if optionID ~= nil then
            local option = FindOption(draft, optionID);
            if option == nil then error(`GossipMenuModel: unknown option ID {optionID}`, 2); end
            session.selectedMenuID = option.menuID;
        end
        session.selectedOptionID = optionID;
    end

    function model:SetPanOffset(offset : vec)
        session.panOffset = offset;
    end

    function model:GetPanOffset() : vec
        return session.panOffset;
    end

    function model:SetZoom(nextZoom : number)
        if type(nextZoom) ~= "number" or nextZoom ~= nextZoom or nextZoom == math.huge or nextZoom == -math.huge or nextZoom <= 0 then error("GossipMenuModel: zoom must be a finite positive number", 2); end
        session.zoom = nextZoom;
    end

    function model:GetZoom() : number
        return session.zoom;
    end

    function model:SetTemporaryVisualState(key : string, value : any)
        if value == nil then session.temporaryVisualState[key] = nil; else session.temporaryVisualState[key] = Clone(value); end
    end

    function model:GetTemporaryVisualState(key : string) : any?
        return Clone(session.temporaryVisualState[key]);
    end

    return model;
end

return modelAPI;
