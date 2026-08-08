local UIButton = require("@src/API/UI/Button")
local UIScrollBox = require("@src/API/UI/ScrollBox")
local FrameManager = require("@src/API/Game/FrameManager")
local UIVerticalLayout = require("@src/API/UI/VerticalLayout")

type CharacterSelectScreen =
{
    RefreshCharacterList : () -> (),
    AddCharacter : (name : string, level : number, class : string, location : string) -> (),
    RemoveCharacter : (name : string) -> (),
    SelectCharacterByName : (name : string) -> (),
    GetCharacterNameByIndex : (index : number) -> string | nil,

    canvas : Canvas,
    panel : Panel,
    characters : { Character },
    selectedCharacterName : string | nil,
    selectedCharacterNameText : TextWidget,
    characterListScrollBox : UIScrollBox.ScrollBox,
    characterListLayout : any,  -- VerticalLayout instance
}

type Character =
{
    index : number,
    name : string,
    level : number,
    class : string,
    location : string,
    entryButton : UIButton.Button | nil,
    entryTextLayout : any,
    nameText : TextWidget | nil,
    levelAndClassText : TextWidget | nil,
    locationText : TextWidget | nil,
}

local sceneData = {
    characterSelectScreen = nil :: CharacterSelectScreen?,
}

local CHARACTER_ENTRY_HEIGHT = 88
local CHARACTER_LIST_TOP_PADDING = 8
local CHARACTER_ENTRY_HOVER_COLOR = vec.New(0.11, 0.15, 0.21)
local CHARACTER_ENTRY_SELECTED_COLOR = vec.New(0.13, 0.20, 0.29)

local classIDToName = {
    [1] = "Warrior",
    [2] = "Paladin",
    [3] = "Hunter",
    [4] = "Rogue",
    [5] = "Priest",
    [6] = "Unused",
    [7] = "Shaman",
    [8] = "Mage",
    [9] = "Warlock",
    [10] = "Unused",
    [11] = "Druid"
}

local function RegisterTemplates()
    UI.RegisterPanelTemplate("CharacterSelectBackgroundPanel",
    {
        background = "texture/ui/characterselect.png",
    });

    -- Restore Icon
    UI.RegisterPanelTemplate("RestoreIcon",
    {
        background = "texture/ui/icons/50x50/white/return.png",
        color = vec.New(255.0/255.0, 239.0/255.0, 0.0/255.0),
    });

    -- Rotate Icons
    UI.RegisterPanelTemplate("RotateLeftIcon",
    {
        background = "texture/ui/icons/50x50/white/arrow_left.png",
        color = vec.New(255.0/255.0, 239.0/255.0, 0.0/255.0),
    });
    UI.RegisterPanelTemplate("RotateRightIcon",
    {
        background = "texture/ui/icons/50x50/white/arrow_right.png",
        color = vec.New(255.0/255.0, 239.0/255.0, 0.0/255.0),
    });

    UI.RegisterTextTemplate("GreyText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 24.0,
        color = vec.New(110.0/255.0, 110.0/255.0, 110.0/255.0),
        borderSize = 2.5,
        borderColor = vec.New(0.1, 0.1, 0.1),
    });

    -- Dialog Box Header Text
    UI.RegisterTextTemplate("NameText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 22.0,
        color = vec.New(255.0/255.0, 210.0/255.0, 0.0/255.0),
        borderSize = 2.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("LevelText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf", -- Obviously we want a different font
        size = 20.0,
        color = vec.New(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 2.0,
        borderColor = vec.New(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("LocationText",
    {
        font = "Data/Fonts/OpenSans-Regular.ttf",
        size = 19.0,
        color = vec.New(110.0/255.0, 110.0/255.0, 110.0/255.0),
        borderSize = 2.0,
        borderColor = vec.New(0.1, 0.1, 0.1),
    });
end

local function CreateOrModifyCharacterEntry(characterSelectScreen : CharacterSelectScreen, character : Character)
    local entryButton = character.entryButton;
    if entryButton == nil then
        entryButton = UIButton.New(
            characterSelectScreen.characterListLayout,
            vec.New(0, 0),
            vec.New(math.max(characterSelectScreen.characterListLayout:GetWidth() - 10, 1), CHARACTER_ENTRY_HEIGHT),
            0i,
            {
                hoverColor = CHARACTER_ENTRY_HOVER_COLOR,
                highlightColor = CHARACTER_ENTRY_SELECTED_COLOR,
            });
        entryButton:SetOnMouseUp(function()
            characterSelectScreen:SelectCharacterByName(character.name);
        end);

        local entryTextLayout = UIVerticalLayout.New(entryButton.root, vec.New(0, 0), vec.New(entryButton.root:GetWidth(), CHARACTER_ENTRY_HEIGHT), 1i, {
            panelTemplate = "DebugDarkGrey",
            alpha = 0.0,
            alignV = "center",
            paddingLeft = 16,
            spacing = 0,
        });
        entryTextLayout:SetAnchor(vec.New(0.0, 1.0));
        entryTextLayout:SetRelativePoint(vec.New(0.0, 1.0));

        character.nameText = entryTextLayout:NewText(character.name, vec.New(0, 0), 1i, "NameText");
        character.levelAndClassText = entryTextLayout:NewText(`Level {character.level} {character.class}`, vec.New(0, 0), 1i, "LevelText");
        character.locationText = entryTextLayout:NewText(character.location, vec.New(0, 0), 1i, "LocationText");
        character.nameText:SetFontSize(17);
        character.levelAndClassText:SetFontSize(16);
        character.locationText:SetFontSize(14);

        character.entryButton = entryButton;
        character.entryTextLayout = entryTextLayout;
    else
        character.nameText:SetText(character.name);
        character.levelAndClassText:SetText(`Level {character.level} {character.class}`);
        character.locationText:SetText(character.location);
    end

    entryButton:SetHighlighted(characterSelectScreen.selectedCharacterName == character.name);
end

local function CreateLeftPanel(characterSelectScreen : CharacterSelectScreen)
    -- Menu button
    local menuButton = UIButton.New(characterSelectScreen.panel, vec.New(20, 40), vec.New(150, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Menu",
    });
    menuButton:SetAnchor(vec.New(0.0, 0.0));
    menuButton:SetRelativePoint(vec.New(0.0, 0.0));
end

local function CreateMiddlePanel(characterSelectScreen : CharacterSelectScreen)
    -- Selected character name
    characterSelectScreen.selectedCharacterNameText = characterSelectScreen.panel:NewText("Pursche", vec.New(0, 140), 0i, "NameText");
    characterSelectScreen.selectedCharacterNameText:SetAnchor(vec.New(0.5, 0.0));
    characterSelectScreen.selectedCharacterNameText:SetRelativePoint(vec.New(0.5, 0.0));

    -- Enter World button
    local enterWorldButton = UIButton.New(characterSelectScreen.panel, vec.New(0, 80), vec.New(200, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Enter World",
    });
    enterWorldButton:SetAnchor(vec.New(0.5, 0.0));
    enterWorldButton:SetRelativePoint(vec.New(0.5, 0.0));
    enterWorldButton:SetOnMouseUp(function(eventID, widget, ...)
        local characterIndex = nil

        -- TODO : Implement custom character sorting order
        if (characterSelectScreen.selectedCharacterName ~= nil) then
            for i, character in characterSelectScreen.characters do
                if (character.name == characterSelectScreen.selectedCharacterName) then
                    characterIndex = i
                    break;
                end
            end
        end

        if (characterIndex ~= nil) then
            SelectCharacter(integer.create(characterIndex));
        end
    end);

    -- Rotate character left button
    local rotateLeftButton = UIButton.New(characterSelectScreen.panel, vec.New(-22, 35), vec.New(40, 40), 0i, {
        panelTemplate = "DefaultButtonPanel"
    });
    rotateLeftButton:SetAnchor(vec.New(0.5, 0.0));
    rotateLeftButton:SetRelativePoint(vec.New(0.5, 0.0));

    local rotateLeftIcon = rotateLeftButton:NewPanel(vec.New(0, 0), vec.New(36, 36), 0i, "RotateLeftIcon");
    rotateLeftIcon:SetAnchor(vec.New(0.5, 0.5));
    rotateLeftIcon:SetRelativePoint(vec.New(0.5, 0.5));

    -- Rotate character right button
    local rotateRightButton = UIButton.New(characterSelectScreen.panel, vec.New(22, 35), vec.New(40, 40), 0i, {
        panelTemplate = "DefaultButtonPanel"
    });
    rotateRightButton:SetAnchor(vec.New(0.5, 0.0));
    rotateRightButton:SetRelativePoint(vec.New(0.5, 0.0));

    local rotateRightIcon = rotateRightButton:NewPanel(vec.New(0, 0), vec.New(36, 36), 0i, "RotateRightIcon");
    rotateRightIcon:SetAnchor(vec.New(0.5, 0.5));
    rotateRightIcon:SetRelativePoint(vec.New(0.5, 0.5));
end

local function CreateRightPanel(characterSelectScreen : CharacterSelectScreen)
    -- Right menu
    local characterSelectPanel = characterSelectScreen.panel:NewPanel(vec.New(-20, -80), vec.New(400, 850), 0i, "DialogBox");
    characterSelectPanel:SetAnchor(vec.New(1.0, 1.0));
    characterSelectPanel:SetRelativePoint(vec.New(1.0, 1.0));
    characterSelectPanel:SetBorderColor(vec.New(0.1, 0.1, 0.1));
    characterSelectPanel:SetBorderSize(2);

    local currentY = 0;

    -- Realm title
    local realmTitle = characterSelectPanel:NewText("Novus RPPVP", vec.New(0, currentY), 0i, "GreyText");
    realmTitle:SetAnchor(vec.New(0.5, 1.0));
    realmTitle:SetRelativePoint(vec.New(0.5, 1.0));
    currentY -= realmTitle:GetSize().y + 5;

    -- Change Realm button
    local changeRealmButton = UIButton.New(characterSelectPanel, vec.New(0, currentY), vec.New(180, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Change Realm",
    });
    changeRealmButton:SetAnchor(vec.New(0.5, 1.0));
    changeRealmButton:SetRelativePoint(vec.New(0.5, 1.0));
    currentY -= 40;

    -- Character selection scrollbox
    characterSelectScreen.characterListScrollBox = UIScrollBox.New(characterSelectPanel, vec.New(5, currentY), vec.New(390, 700), 0i, {
        panelTemplate = "DebugDarkGrey",
        alpha = 0.0,
        verticalScrollBar = true,
    });
    characterSelectScreen.characterListScrollBox:SetAnchor(vec.New(0.0, 1.0));
    characterSelectScreen.characterListScrollBox:SetRelativePoint(vec.New(0.0, 1.0));
    characterSelectScreen.characterListScrollBox.content:SetAlpha(0.0);
    characterSelectScreen.characterListScrollBox.content:SetBorderSize(0.0);

    -- Outer list layout: stacks character entries top-down inside the scrollbox content.
    -- heightMode="shrink" makes its measured height match the sum of entry heights, which we
    -- feed back to the scrollbox in RefreshCharacterList.
    local characterListViewportSize = characterSelectScreen.characterListScrollBox:GetViewportSize();
    characterSelectScreen.characterListLayout = UIVerticalLayout.New(characterSelectScreen.characterListScrollBox.content, vec.New(0, 0), vec.New(characterListViewportSize.x, 0), 0i, {
        panelTemplate = "DebugDarkGrey",
        alpha = 0.0,
        heightMode = "shrink",
        alignH = "center",
        paddingTop = CHARACTER_LIST_TOP_PADDING,
        spacing = 4,
    });
    characterSelectScreen.characterListLayout:SetAnchor(vec.New(0.0, 1.0));
    characterSelectScreen.characterListLayout:SetRelativePoint(vec.New(0.0, 1.0));

    -- Create New Character button
    local loginButton = UIButton.New(characterSelectPanel, vec.New(-40, 20), vec.New(250, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Create New Character",
    });
    loginButton:SetAnchor(vec.New(0.5, 0.0));
    loginButton:SetRelativePoint(vec.New(0.5, 0.0));
    loginButton:SetOnMouseUp(function(eventID, widget, ...)
        FrameManager.Open("CharacterCreation");
    end);

    -- Restore button
    local restoreButton = UIButton.New(characterSelectPanel, vec.New(130, 20), vec.New(50, 40), 0i, {
        panelTemplate = "DefaultButtonPanel"
    });
    restoreButton:SetAnchor(vec.New(0.5, 0.0));
    restoreButton:SetRelativePoint(vec.New(0.5, 0.0));

    local restoreIcon = restoreButton:NewPanel(vec.New(0, 0), vec.New(32, 32), 0i, "RestoreIcon");
    restoreIcon:SetAnchor(vec.New(0.5, 0.5));
    restoreIcon:SetRelativePoint(vec.New(0.5, 0.5));

    -- Back button
    local backButton = UIButton.New(characterSelectScreen.panel, vec.New(-20, 40), vec.New(150, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Back",
    });
    backButton:SetAnchor(vec.New(1.0, 0.0));
    backButton:SetRelativePoint(vec.New(1.0, 0.0));
    backButton:SetOnMouseUp(function(eventID, widget, ...)
        Disconnect();
    end);

    -- Delete Character button
    local deleteButton = UIButton.New(backButton, vec.New(-250, 0), vec.New(220, 40), 0i, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Delete Character",
    });
    deleteButton:SetAnchor(vec.New(0.0, 0.5));
    deleteButton:SetRelativePoint(vec.New(0.0, 0.5));
end

local function CreateCharacterSelectScreen()
    local characterSelectScreen : CharacterSelectScreen = {};

    -- Create Canvas
    characterSelectScreen.canvas = UI.GetCanvas("CharacterSelect", vec.New(0, 0), vec.New(1920, 1080)); -- TODO: We don't want to hardcode the reference resolution here
    
    -- Create Panel
    characterSelectScreen.panel = characterSelectScreen.canvas:NewPanel(vec.New(0, 0), vec.New(1920, 1080), 0i, "CharacterSelectBackgroundPanel");
    characterSelectScreen.panel:SetEnabled(false);
    characterSelectScreen.panel:SetAnchor(vec.New(0.5, 0.5));
    characterSelectScreen.panel:SetRelativePoint(vec.New(0.5, 0.5));

    -- Create widget covering whole canvas
    --characterSelectScreen.widget = characterSelectScreen.canvas:NewWidget(vec.New(0, 0), 0i);
    --characterSelectScreen.widget:SetEnabled(false);
    --characterSelectScreen.widget:SetAnchor(vec.New(0.5, 0.5));
    --characterSelectScreen.widget:SetRelativePoint(vec.New(0.5, 0.5));

    -- Variables
    characterSelectScreen.characters = setmetatable({}, {__index = table});

    -- Methods
    characterSelectScreen.RefreshCharacterList = function(self : CharacterSelectScreen)
        for i, character in self.characters do
            CreateOrModifyCharacterEntry(self, character);
        end

        local contentHeight = self.characterListLayout:GetMeasuredHeight();
        local scrollBoxHeight = self.characterListScrollBox.height;

        self.characterListScrollBox:SetVerticalScrollBarVisible(contentHeight > scrollBoxHeight);
        local viewportWidth = self.characterListScrollBox:GetViewportSize().x;
        self.characterListLayout:SetWidth(viewportWidth);
        for _, character in self.characters do
            if character.entryButton ~= nil then
                local entryWidth = math.max(viewportWidth - 10, 1);
                character.entryButton:SetSize(vec.New(entryWidth, CHARACTER_ENTRY_HEIGHT));
                character.entryTextLayout:SetWidth(entryWidth);
            end
        end
        self.characterListLayout:Refresh();
        contentHeight = self.characterListLayout:GetMeasuredHeight();
        self.characterListScrollBox:SetContentHeight(contentHeight);
    end

    characterSelectScreen.AddCharacter = function(self : CharacterSelectScreen, name : string, level : number, class : string, location : string)
        local character = {
            index = #self.characters + 1,
            name = name,
            level = level,
            class = class,
            location = location,
        }
        self.characters:insert(character);
        characterSelectScreen:RefreshCharacterList();
    end

    characterSelectScreen.RemoveCharacter = function(self : CharacterSelectScreen, name : string)
        for i, character in self.characters do
            if (character.name == name) then
                self.characters:remove(i);
                break;
            end
        end
    end

    characterSelectScreen.SelectCharacterByName = function(self : CharacterSelectScreen, name : string)
        if (self.selectedCharacterName ~= nil) then
            for i, character in self.characters do
                if (character.name == self.selectedCharacterName) then
                    if character.entryButton ~= nil then character.entryButton:SetHighlighted(false); end
                    break;
                end
            end
        end

        for i, character in self.characters do
            if (character.name == name) then
                if character.entryButton ~= nil then character.entryButton:SetHighlighted(true); end
                break;
            end
        end

        self.selectedCharacterName = name;
        self.selectedCharacterNameText:SetText(name);
    end

    characterSelectScreen.GetCharacterNameByIndex = function(self : CharacterSelectScreen, index : number) : string | nil
        if (self.characters[index] == nil) then
            return nil;
        end
        
        return self.characters[index].name;
    end

    -- Create panels
    CreateLeftPanel(characterSelectScreen);
    CreateMiddlePanel(characterSelectScreen);
    CreateRightPanel(characterSelectScreen);

    --characterSelectScreen:AddCharacter("Pursche", 60, "Paladin", "Stormwind");
    --characterSelectScreen:AddCharacter("Nix", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix1", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix2", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix3", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix4", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix5", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix6", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix7", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix8", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix9", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix10", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix11", 60, "Rogue", "Vietnam");
    --characterSelectScreen:AddCharacter("Nix12", 60, "Rogue", "Vietnam");

    --local firstCharacterName = characterSelectScreen:GetCharacterNameByIndex(1);
    --if (firstCharacterName ~= nil) then
    --    characterSelectScreen:SelectCharacterByName(firstCharacterName);
    --end

    FrameManager.Register("CharacterSelect", {
        root = characterSelectScreen.panel,
        role = "System",
        dismissible = false,
    });

    local authStage = GetAuthStage();
    local isInWorld = IsInWorld();
    if not isInWorld and authStage == 4i then -- Authenticated, no character selected
        FrameManager.Open("CharacterSelect");
    end

    return characterSelectScreen;
end

local function OnGameLoaded(eventID : integer, data : any)
    RegisterTemplates();

    local characterSelectScreen : CharacterSelectScreen = CreateCharacterSelectScreen();
    sceneData.characterSelectScreen = characterSelectScreen;
end

local function OnCharacterListChanged(eventID : integer, data : any)
    if sceneData.characterSelectScreen == nil then
        return;
    end

    FrameManager.Open("CharacterSelect");

    local characterSelectScreen : CharacterSelectScreen = sceneData.characterSelectScreen;

    -- Remove all characters
    for i = #characterSelectScreen.characters, 1, -1 do
        characterSelectScreen.characters:Remove(i);
    end

    -- Add all characters
    local characterList = GetCharacterList();
    for k, v in characterList do
        local level = if v.level == 0i then 1i else v.level;
        local className = if v.class == 0i then "Unknown" else classIDToName[integer.tonumber(v.class)];
        local mapName = GetMapName(v.mapID);
        characterSelectScreen:AddCharacter(v.name, level, className, mapName);
    end

    -- Refresh character list
    characterSelectScreen:RefreshCharacterList();

    local firstCharacterName = characterSelectScreen:GetCharacterNameByIndex(1);
    if (firstCharacterName ~= nil) then
        characterSelectScreen:SelectCharacterByName(firstCharacterName);
    end
end

RegisterEvent(GameEvent.Loaded, OnGameLoaded);
RegisterEvent(GameEvent.CharacterListChanged, OnCharacterListChanged);
