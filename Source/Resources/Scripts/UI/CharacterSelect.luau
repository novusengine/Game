local UIButton = require("@src/API/UI/Button")
local UIScrollBox : ScrollBoxAPI = require("@src/API/UI/ScrollBox")
local UIStack : StackAPI = require("@src/API/UI/UIStack")

type CharacterSelectScreen =
{
    RefreshCharacterList : () -> (),
    AddCharacter : (name : string, level : number, class : string, location : string) -> (),
    RemoveCharacter : (name : string) -> (),
    SelectCharacterByName : (name : string) -> (),
    GetCharacterNameByIndex : (index : number) -> string | nil,

    canvas : Canvas,
    widget : Widget,
    currentY : number,
    characters : { Character },
    selectedCharacterName : string | nil,
    selectedCharacterNameText : Text,
    characterListScrollBox : ScrollBox,
}

type Character = 
{
    index : number,
    name : string,
    level : number,
    class : string,
    location : string,
    panel : Panel | nil,
    nameText : Text | nil,
    levelAndClassText : Text | nil,
    locationText : Text | nil,
}

local function RegisterTemplates()
    -- Restore Icon
    local restoreIconMinX, restoreIconMinY = UI.PixelsToTexCoord(8, 8, 32, 32);
    local restoreIconMaxX, restoreIconMaxY = UI.PixelsToTexCoord(23, 23, 32, 32);
    UI.RegisterPanelTemplate("RestoreIcon",
    {
        background = "Data/Texture/interface/glues/characterselect/characterundelete.dds",
        cornerRadius = 0.0,

        texCoords = Box.new(restoreIconMinX, restoreIconMinY, restoreIconMaxX, restoreIconMaxY),
    });

    -- Rotate Icon
    local rotateIconMinX, rotateIconMinY = UI.PixelsToTexCoord(15, 15, 64, 64);
    local rotateIconMaxX, rotateIconMaxY = UI.PixelsToTexCoord(47, 47, 64, 64);
    UI.RegisterPanelTemplate("RotateIcon",
    {
        background = "Data/Texture/interface/glues/charactercreate/ui-rotationright-big-up.dds",
        cornerRadius = 0.0,

        texCoords = Box.new(rotateIconMinX, rotateIconMinY, rotateIconMaxX, rotateIconMaxY),

        -- These get applied additively on this template
        onClickTemplate = "RotateIconDown",
    });

    UI.RegisterPanelTemplate("RotateIconDown",
    {
        background = "Data/Texture/interface/glues/charactercreate/ui-rotationright-big-down.dds",
    });

    UI.RegisterTextTemplate("GreyText",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf",
        size = 24.0,
        color = vector.create(110.0/255.0, 110.0/255.0, 110.0/255.0),
        borderSize = 2.5,
        borderColor = vector.create(0.1, 0.1, 0.1),
    });

    -- Dialog Box Header Text
    UI.RegisterTextTemplate("NameText",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 22.0,
        color = vector.create(255.0/255.0, 210.0/255.0, 0.0/255.0),
        borderSize = 2.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("LevelText",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf", -- Obviously we want a different font
        size = 20.0,
        color = vector.create(255.0/255.0, 255.0/255.0, 255.0/255.0),
        borderSize = 2.0,
        borderColor = vector.create(0.0, 0.0, 0.0),
    });

    UI.RegisterTextTemplate("LocationText",
    {
        font = "Data/Fonts/Ubuntu-Regular.ttf",
        size = 19.0,
        color = vector.create(110.0/255.0, 110.0/255.0, 110.0/255.0),
        borderSize = 2.0,
        borderColor = vector.create(0.1, 0.1, 0.1),
    });

    -- Character Entry
    local entryMinX, entryMinY = UI.PixelsToTexCoord(6, 2, 256, 64);
    local entryMaxX, entryMaxY = UI.PixelsToTexCoord(249, 59, 256, 64);

    local entryNineSliceMinX, entryNineSliceMinY = UI.PixelsToTexCoord(16, 12, 256, 64);
    local entryNineSliceMaxX, entryNineSliceMaxY = UI.PixelsToTexCoord(239, 49, 256, 64);

    UI.RegisterPanelTemplate("CharacterEntryButtonPanel",
    {
        cornerRadius = 0.0,
        color = vector.create(0.0, 0.0, 0.0),
        alpha = 0.0,

        texCoords = Box.new(entryMinX, entryMinY, entryMaxX, entryMaxY),
        --nineSliceCoords = Box.new(entryNineSliceMinX, entryNineSliceMinY, entryNineSliceMaxX, entryNineSliceMaxY), -- TODO: Investigate 9 slicing issues?
    });
end

local function CreateOrModifyCharacterEntry(characterSelectScreen : CharacterSelectScreen, character : Character)
    local localY = -15;
    if (character.panel == nil) then
        -- Background
        character.panel = characterSelectScreen.characterListScrollBox.content:NewPanel(0, characterSelectScreen.currentY, 390, 100, 0, "CharacterEntryButtonPanel");
        character.panel:SetAnchor(0.5, 1.0);
        character.panel:SetRelativePoint(0.5, 1.0);
        character.panel:SetOnMouseUp(function(eventID, widget, ...)
            characterSelectScreen:SelectCharacterByName(character.name);
        end);

        -- Name
        character.nameText = character.panel:NewText(character.name, 25, localY, 0, "NameText");
        character.nameText:SetAnchor(0.0, 1.0);
        character.nameText:SetRelativePoint(0.0, 1.0);
        localY -= select(2, character.nameText:GetSize());

        -- Level and class
        character.levelAndClassText = character.panel:NewText(`Level {character.level} {character.class}`, 25, localY, 0, "LevelText");
        character.levelAndClassText:SetAnchor(0.0, 1.0);
        character.levelAndClassText:SetRelativePoint(0.0, 1.0);
        localY -= select(2, character.levelAndClassText:GetSize());

        -- Location
        character.locationText = character.panel:NewText(character.location, 25, localY, 0, "LocationText");
        character.locationText:SetAnchor(0.0, 1.0);
        character.locationText:SetRelativePoint(0.0, 1.0);
        localY -= select(2, character.locationText:GetSize());

        local padding = 25;
        localY -= padding;
        character.panel:SetHeight(math.abs(localY));
    else
        character.panel:SetPos(0, characterSelectScreen.currentY);

        character.nameText:SetText(character.name);
        character.nameText:SetPos(25, localY);
        localY -= select(2, character.nameText:GetSize());

        character.levelAndClassText:SetText(`Level {character.level} {character.class}`);
        character.levelAndClassText:SetPos(25, localY);
        localY -= select(2, character.levelAndClassText:GetSize());

        character.locationText:SetText(character.location);
        character.locationText:SetPos(25, localY);
        localY -= select(2, character.locationText:GetSize());
    end

    characterSelectScreen.currentY += localY;
end

local function CreateLeftPanel(characterSelectScreen : CharacterSelectScreen)
    -- Menu button
    local menuButton = UIButton.NewButton(characterSelectScreen.widget, 20, 40, 150, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Menu",
    });
    menuButton:SetAnchor(0.0, 0.0);
    menuButton:SetRelativePoint(0.0, 0.0);
end

local function CreateMiddlePanel(characterSelectScreen : CharacterSelectScreen)
    -- Selected character name
    characterSelectScreen.selectedCharacterNameText = characterSelectScreen.widget:NewText("Pursche", 0, 140, 0, "NameText");
    characterSelectScreen.selectedCharacterNameText:SetAnchor(0.5, 0.0);
    characterSelectScreen.selectedCharacterNameText:SetRelativePoint(0.5, 0.0);

    -- Enter World button
    local enterWorldButton = UIButton.NewButton(characterSelectScreen.widget, 0, 80, 200, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Enter World",
    });
    enterWorldButton:SetAnchor(0.5, 0.0);
    enterWorldButton:SetRelativePoint(0.5, 0.0);

    -- Rotate character left button
    local rotateLeftButton = UIButton.NewButton(characterSelectScreen.widget, -22, 35, 40, 40, 0, {
        panelTemplate = "RotateIcon",
    });
    rotateLeftButton:SetAnchor(0.5, 0.0);
    rotateLeftButton:SetRelativePoint(0.5, 0.0);

    -- Rotate character right button
    local rotateRightButton = UIButton.NewButton(characterSelectScreen.widget, 22, 35, 40, 40, 0, {
        panelTemplate = "RotateIcon",
    });
    rotateRightButton:SetAnchor(0.5, 0.0);
    rotateRightButton:SetRelativePoint(0.5, 0.0);
end

local function CreateRightPanel(characterSelectScreen : CharacterSelectScreen)
    -- Right menu
    local characterSelectPanel = characterSelectScreen.widget:NewPanel(-20, -40, 400, 850, 0, "DialogBox");
    characterSelectPanel:SetAnchor(1.0, 1.0);
    characterSelectPanel:SetRelativePoint(1.0, 1.0);

    local currentY = 0;

    -- Realm title
    local realmTitle = characterSelectPanel:NewText("Novus RPPVP", 0, currentY, 0, "GreyText");
    realmTitle:SetAnchor(0.5, 1.0);
    realmTitle:SetRelativePoint(0.5, 1.0);
    currentY -= select(2, realmTitle:GetSize()) + 5;

    -- Change Realm button
    local changeRealmButton = UIButton.NewButton(characterSelectPanel, 0, currentY, 180, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Change Realm",
    });
    changeRealmButton:SetAnchor(0.5, 1.0);
    changeRealmButton:SetRelativePoint(0.5, 1.0);
    currentY -= 40;

    -- Character selection scrollbox
    characterSelectScreen.characterListScrollBox = UIScrollBox.NewScrollBox(characterSelectPanel, 0, currentY, 390, 700, 0, {});
    characterSelectScreen.characterListScrollBox:SetAnchor(0.0, 1.0);
    characterSelectScreen.characterListScrollBox:SetRelativePoint(0.0, 1.0);

    -- Create New Character button
    local loginButton = UIButton.NewButton(characterSelectPanel, -40, 20, 250, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Create New Character",
    });
    loginButton:SetAnchor(0.5, 0.0);
    loginButton:SetRelativePoint(0.5, 0.0);
    loginButton:SetOnMouseUp(function(eventID, widget, ...)
        local stack = UIStack.Get("Menu");
        stack:Push("CharacterCreation");
    end);

    -- Restore button
    local restoreButton = UIButton.NewButton(characterSelectPanel, 130, 20, 50, 40, 0, {
        panelTemplate = "DefaultButtonPanel"
    });
    restoreButton:SetAnchor(0.5, 0.0);
    restoreButton:SetRelativePoint(0.5, 0.0);

    local restoreIcon = restoreButton:NewPanel(0, 0, 24, 24, 0, "RestoreIcon");
    restoreIcon:SetAnchor(0.5, 0.5);
    restoreIcon:SetRelativePoint(0.5, 0.5);

    -- Back button
    local backButton = UIButton.NewButton(characterSelectScreen.widget, -20, 40, 150, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Back",
    });
    backButton:SetAnchor(1.0, 0.0);
    backButton:SetRelativePoint(1.0, 0.0);
    backButton:SetOnMouseUp(function(eventID, widget, ...)
        local stack = UIStack.Get("Menu");
        stack:Pop();
    end);

    -- Delete Character button
    local deleteButton = UIButton.NewButton(backButton, -250, 0, 220, 40, 0, {
        panelTemplate = "DefaultButtonPanel",
        textTemplate = "SmallYellowButtonText",
        text = "Delete Character",
    });
    deleteButton:SetAnchor(0.0, 0.5);
    deleteButton:SetRelativePoint(0.0, 0.5);
end

local function CreateCharacterSelectScreen(stack : Stack)
    local characterSelectScreen : CharacterSelectScreen = {};

    -- Create Canvas
    characterSelectScreen.canvas = UI.GetCanvas("CharacterSelect", 0, 0, 1920, 1080); -- TODO: We don't want to hardcode the reference resolution here
    
    -- Create widget covering whole canvas
    characterSelectScreen.widget = characterSelectScreen.canvas:NewWidget(0, 0, 0);
    characterSelectScreen.widget:SetEnabled(false);
    characterSelectScreen.widget:SetAnchor(0.5, 0.5);
    characterSelectScreen.widget:SetRelativePoint(0.5, 0.5);

    -- Variables
    characterSelectScreen.currentY = 0;
    characterSelectScreen.characters = setmetatable({}, {__index = table});

    -- Methods
    characterSelectScreen.RefreshCharacterList = function(self : CharacterSelectScreen)
        self.currentY = 0;

        for i, character in self.characters do
            CreateOrModifyCharacterEntry(self, character);
        end

        local scrollBoxHeight = self.characterListScrollBox.height;
        local visibleScrollBar = math.abs(self.currentY) > scrollBoxHeight;

        self.characterListScrollBox:SetVerticalScrollBarVisible(visibleScrollBar);
        self.characterListScrollBox:SetContentHeight(math.abs(self.currentY));
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
                    self.characters[i].panel:SetForeground(nil);
                    break;
                end
            end
        end

        for i, character in self.characters do
            if (character.name == name) then
                self.characters[i].panel:SetForeground("Data/Texture/interface/glues/characterselect/glue-characterselect-highlight.dds");
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

    characterSelectScreen:AddCharacter("Pursche", 60, "Paladin", "Stormwind");
    characterSelectScreen:AddCharacter("Nix", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix1", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix2", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix3", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix4", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix5", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix6", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix7", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix8", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix9", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix10", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix11", 60, "Rogue", "Vietnam");
    characterSelectScreen:AddCharacter("Nix12", 60, "Rogue", "Vietnam");

    local firstCharacterName = characterSelectScreen:GetCharacterNameByIndex(1);
    if (firstCharacterName ~= nil) then
        characterSelectScreen:SelectCharacterByName(firstCharacterName);
    end

    stack:Register("CharacterSelect", characterSelectScreen.widget, 0); -- Name, widget, slot, onPush, onPop
    --stack:Push("CharacterSelect");

    return characterSelectScreen;
end

local function OnGameLoaded(eventID : number, motd : string)
    local stack = UIStack.Get("Menu");
    RegisterTemplates();

    local characterSelectScreen : CharacterSelectScreen = CreateCharacterSelectScreen(stack);
end

RegisterGameEvent(GameEvent.Loaded, OnGameLoaded);